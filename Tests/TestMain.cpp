// =============================================================================
//  ALAP Opto Leveler - programma di prova
//
//  Istanzia direttamente il processore audio (senza DAW), genera segnali di
//  prova e misura:
//    1. curva statica di compressione
//    2. tempo di attacco (a 44,1 / 48 / 96 kHz)
//    3. rilascio dipendente dalla storia del segnale (impulso breve vs lungo)
//    4. distorsione armonica dello stadio valvolare
//    5. aliasing (residui del sovracampionamento)
//    6. robustezza (segnali estremi, silenzio, valori denormali)
//    7. prestazioni (quante volte piu' veloce del tempo reale)
//    8. preset di fabbrica (valori, riduzione, salvataggio dello stato)
//    9. preset utente (salva, carica, rinomina, importa, esporta, elimina)
//
//  I dati grezzi vengono salvati in file CSV (separatore ";" e virgola
//  decimale, pronti per Excel in italiano) nella cartella "risultati_test".
//  Il codice di uscita e' 0 se tutte le verifiche sono superate, 1 altrimenti.
// =============================================================================

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;

    // Costanti del modello: devono coincidere con quelle di PluginProcessor
    constexpr float modelThresholdDb = -10.0f;
    constexpr float modelRatio       = 4.0f;
    constexpr float modelKneeDb      = 10.0f;
    constexpr float modelMinDriveDb  = -30.0f;
    constexpr float modelMaxDriveDb  = 30.0f;

    //==========================================================================
    //  Limiti delle verifiche
    //  Tarati sui valori misurati con il modello attuale (settembre 2026),
    //  con un margine ragionevole: servono a segnalare subito una modifica
    //  che peggiora il comportamento. Se cambi volutamente il carattere del
    //  plugin, aggiorna questi valori.
    //==========================================================================
    namespace Limits
    {
        // 1. Curva statica (misurato: 0,02 dB)
        constexpr float  curveMaxErrorDb        = 0.2f;

        // 2. Attacco (misurato: 10,2-10,3 ms, scarto 0,17 ms)
        constexpr double attackT63MinMs         = 9.0;
        constexpr double attackT63MaxMs         = 11.5;
        constexpr double attackMaxSpreadMs      = 0.5;

        // 3. Rilascio (misurato: breve 83 ms / 0,94 s, lungo 4,30 s)
        constexpr double shortT50MinMs          = 50.0;
        constexpr double shortT50MaxMs          = 100.0;
        constexpr double shortT90MinMs          = 500.0;
        constexpr double shortT90MaxMs          = 1500.0;
        constexpr double longT90MinMs           = 3000.0;
        constexpr double longT90MaxMs           = 6000.0;
        constexpr double memoryMinRatio         = 3.0;   // lungo / breve (misurato: 4,6)

        // 4. Armoniche (misurato: 0,26% a -18 dBFS, 4,85% a 0 dBFS)
        constexpr double thdNominalMaxPercent   = 0.5;   // a -18 dBFS
        constexpr double thdFullScaleMaxPercent = 8.0;   // a 0 dBFS
        constexpr double slopeH2Min = 0.8, slopeH2Max = 1.2;
        constexpr double slopeH3Min = 1.8, slopeH3Max = 2.2;

        // 5. Aliasing (misurato: -109 dBc)
        constexpr double aliasingMaxDbc         = -100.0;

        // 6. Robustezza (misurato: picco 1,88 con +12 dBFS e GAIN al massimo)
        constexpr float  robustnessMaxPeak      = 2.5f;
        constexpr double silenceTailMaxDb       = -90.0;

        // 7. Prestazioni in Release (misurato: 237 volte il tempo reale)
        constexpr double minRealtimeFactor      = 100.0;

        // 8. Preset: scarto tra riduzione misurata e teorica
        constexpr float  presetMaxErrorDb       = 0.2f;
    }

    //==========================================================================
    //  Raccolta dei risultati
    //==========================================================================
    struct Report
    {
        int passed = 0, failed = 0, skipped = 0;

        void check (bool ok, const juce::String& text)
        {
            std::printf ("  [%s] %s\n", ok ? " OK " : " NO ", text.toRawUTF8());
            if (ok) ++passed; else ++failed;
        }

        void skip (const juce::String& text)
        {
            std::printf ("  [ -- ] %s\n", text.toRawUTF8());
            ++skipped;
        }
    };

    void printSection (const char* title)
    {
        std::printf ("\n==================================================================\n");
        std::printf (" %s\n", title);
        std::printf ("==================================================================\n");
    }

    //==========================================================================
    //  Funzioni di supporto
    //==========================================================================
    float dbToGain (float db)       { return juce::Decibels::decibelsToGain (db); }
    double toDb (double linear)     { return 20.0 * std::log10 (std::max (linear, 1.0e-12)); }
    juce::String csvNum (double v)  { return juce::String (v, 3).replaceCharacter ('.', ','); }

    float driveForKnob (float knob)
    {
        return juce::jmap (knob, 0.0f, 100.0f, modelMinDriveDb, modelMaxDriveDb);
    }

    // Stessa curva a ginocchio morbido del processore: riferimento teorico
    float expectedGainReduction (float levelDb)
    {
        const float over = levelDb - modelThresholdDb;
        float outDb;

        if (2.0f * over < -modelKneeDb)
            outDb = levelDb;
        else if (2.0f * std::abs (over) <= modelKneeDb)
        {
            const float t = over + modelKneeDb * 0.5f;
            outDb = levelDb + (1.0f / modelRatio - 1.0f) * t * t / (2.0f * modelKneeDb);
        }
        else
            outDb = modelThresholdDb + over / modelRatio;

        return std::max (0.0f, levelDb - outDb);
    }

    // Algoritmo di Goertzel: ampiezza di picco di una singola frequenza.
    // Esatto (senza dispersione spettrale) se la finestra contiene un numero
    // intero di periodi, come in tutte le prove seguenti.
    double toneAmplitude (const std::vector<float>& x, size_t start, size_t count,
                          double freq, double sampleRate)
    {
        const double w     = 2.0 * pi * freq / sampleRate;
        const double coeff = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;

        for (size_t i = 0; i < count; ++i)
        {
            const double s0 = (double) x[start + i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        const double re = s1 - s2 * std::cos (w);
        const double im = s2 * std::sin (w);
        return 2.0 * std::sqrt (re * re + im * im) / (double) count;
    }

    double rmsOf (const std::vector<float>& x, size_t start, size_t count)
    {
        double sum = 0.0;
        for (size_t i = start; i < start + count; ++i)
            sum += (double) x[i] * (double) x[i];
        return std::sqrt (sum / (double) count);
    }

    //==========================================================================
    //  Generatori di segnale (ricevono il tempo in secondi)
    //==========================================================================
    using Generator = std::function<float (double)>;

    Generator sine (double freq, float amplitude)
    {
        return [=] (double t) { return amplitude * (float) std::sin (2.0 * pi * freq * t); };
    }

    Generator silence()
    {
        return [] (double) { return 0.0f; };
    }

    //==========================================================================
    //  Banco di prova: fa girare il processore a blocchi come farebbe una DAW
    //==========================================================================
    class Rig
    {
    public:
        Rig (double sr, int block) : sampleRate (sr), blockSize (block) {}

        void set (const char* paramId, float value)
        {
            if (auto* param = processor.apvts.getParameter (paramId))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        }

        // Azzera completamente lo stato (inviluppo, memoria T4B, filtri)
        void restart()
        {
            processor.prepareToPlay (sampleRate, blockSize);
            sampleCounter = 0;
        }

        // Elabora "seconds" secondi di segnale; facoltativamente registra
        // l'uscita del canale sinistro e la riduzione di guadagno per blocco
        void run (double seconds, const Generator& gen,
                  std::vector<float>* out = nullptr, std::vector<float>* grTrace = nullptr)
        {
            const auto total = (juce::int64) std::llround (seconds * sampleRate);

            for (juce::int64 done = 0; done < total;)
            {
                const int n = (int) std::min<juce::int64> (blockSize, total - done);
                buffer.setSize (2, n, false, false, true);

                for (int i = 0; i < n; ++i)
                {
                    const float v = gen ((double) (sampleCounter + i) / sampleRate);
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }

                const auto t0 = juce::Time::getHighResolutionTicks();
                processor.processBlock (buffer, midi);
                processingTicks += juce::Time::getHighResolutionTicks() - t0;

                if (out != nullptr)
                    out->insert (out->end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + n);

                if (grTrace != nullptr)
                    grTrace->push_back (processor.getGainReductionDb());

                sampleCounter += n;
                done += n;
            }
        }

        double blockSeconds() const       { return (double) blockSize / sampleRate; }
        double processingSeconds() const  { return juce::Time::highResolutionTicksToSeconds (processingTicks); }

        const double sampleRate;
        const int blockSize;
        LA2AAudioProcessor processor;

    private:
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        juce::int64 sampleCounter = 0;
        juce::int64 processingTicks = 0;
    };

    //==========================================================================
    //  1. Curva statica
    //==========================================================================
    void testStaticCurve (Report& report, const juce::File& outDir)
    {
        printSection ("1. Curva statica (seno 1 kHz, PEAK REDUCTION 50, GAIN 50)");

        Rig rig (48000.0, 256);
        rig.set (ParamIDs::peakReduction, 50.0f);
        rig.set (ParamIDs::gain, 50.0f);
        const float drive = driveForKnob (50.0f);

        std::printf ("  Ingresso   Rid. attesa   Rid. misurata   Errore   Uscita RMS\n");
        std::printf ("  (dBFS)     (dB)          (dB)            (dB)     (dBFS)\n");

        juce::String csv = "ingresso_dbfs;riduzione_attesa_db;riduzione_misurata_db;uscita_rms_dbfs\n";
        float maxError = 0.0f;

        for (int level = -40; level <= 0; level += 2)
        {
            rig.restart();
            std::vector<float> out, gr;
            rig.run (2.0, sine (1000.0, dbToGain ((float) level)), &out, &gr);

            // Media della riduzione negli ultimi ~200 ms (regime stazionario)
            const size_t avgBlocks = 40;
            float measured = 0.0f;
            for (size_t i = gr.size() - avgBlocks; i < gr.size(); ++i)
                measured += gr[i];
            measured /= (float) avgBlocks;

            const float expected = expectedGainReduction ((float) level + drive);
            const float error    = measured - expected;
            maxError = std::max (maxError, std::abs (error));

            const size_t tail = 9600;   // 200 ms
            const double outRmsDb = toDb (rmsOf (out, out.size() - tail, tail));

            std::printf ("  %6d     %7.2f       %7.2f         %+6.2f   %7.2f\n",
                         level, expected, measured, error, outRmsDb);

            csv << level << ";" << csvNum (expected) << ";" << csvNum (measured) << ";" << csvNum (outRmsDb) << "\n";
        }

        outDir.getChildFile ("curva_statica.csv").replaceWithText (csv);

        report.check (maxError < Limits::curveMaxErrorDb,
                      "errore massimo rispetto alla curva teorica: " + juce::String (maxError, 2)
                      + " dB (limite " + juce::String (Limits::curveMaxErrorDb, 2) + " dB)");
    }

    //==========================================================================
    //  2. Attacco
    //==========================================================================
    struct AttackResult { double t63ms = -1.0, t90ms = -1.0; float finalGr = 0.0f; };

    AttackResult measureAttack (double sampleRate)
    {
        Rig rig (sampleRate, 16);
        rig.set (ParamIDs::peakReduction, 70.0f);
        rig.set (ParamIDs::gain, 50.0f);
        rig.restart();

        // Assestamento a basso livello (nessuna compressione)
        rig.run (0.5, sine (1000.0, dbToGain (-40.0f)));

        // Gradino a -6 dBFS
        std::vector<float> gr;
        rig.run (0.3, sine (1000.0, dbToGain (-6.0f)), nullptr, &gr);

        AttackResult r;
        r.finalGr = gr.back();

        for (size_t i = 0; i < gr.size(); ++i)
        {
            const double tMs = (double) (i + 1) * rig.blockSeconds() * 1000.0;
            if (r.t63ms < 0.0 && gr[i] >= 0.632f * r.finalGr) r.t63ms = tMs;
            if (r.t90ms < 0.0 && gr[i] >= 0.900f * r.finalGr) r.t90ms = tMs;
        }

        return r;
    }

    void testAttack (Report& report)
    {
        printSection ("2. Attacco (gradino da -40 a -6 dBFS, PEAK REDUCTION 70)");
        std::printf ("  Attesi: 63%% della riduzione in ~10 ms, 90%% in ~23 ms\n\n");
        std::printf ("  Frequenza    Rid. finale   Tempo 63%%   Tempo 90%%\n");

        const double rates[] = { 44100.0, 48000.0, 96000.0 };
        double minT63 = 1.0e9, maxT63 = 0.0;
        bool allInRange = true;

        for (auto sr : rates)
        {
            const auto r = measureAttack (sr);
            std::printf ("  %6.1f kHz   %6.2f dB     %6.2f ms   %6.2f ms\n",
                         sr / 1000.0, r.finalGr, r.t63ms, r.t90ms);

            minT63 = std::min (minT63, r.t63ms);
            maxT63 = std::max (maxT63, r.t63ms);
            allInRange = allInRange && r.t63ms >= Limits::attackT63MinMs && r.t63ms <= Limits::attackT63MaxMs;
        }

        report.check (allInRange, "tempo di attacco al 63% tra " + juce::String (Limits::attackT63MinMs, 1)
                                  + " e " + juce::String (Limits::attackT63MaxMs, 1) + " ms a tutte le frequenze");
        report.check (maxT63 - minT63 < Limits::attackMaxSpreadMs,
                      "attacco indipendente dalla frequenza di campionamento (scarto "
                      + juce::String (maxT63 - minT63, 2) + " ms, limite "
                      + juce::String (Limits::attackMaxSpreadMs, 2) + " ms)");
    }

    //==========================================================================
    //  3. Rilascio dipendente dalla storia
    //==========================================================================
    struct ReleaseResult { float startGr = 0.0f; double t50ms = -1.0, t90ms = -1.0; std::vector<float> trace; };

    ReleaseResult measureRelease (double burstSeconds)
    {
        Rig rig (48000.0, 16);
        rig.set (ParamIDs::peakReduction, 70.0f);
        rig.set (ParamIDs::gain, 50.0f);
        rig.restart();

        // Impulso a -6 dBFS della durata richiesta, poi silenzio
        rig.run (burstSeconds, sine (1000.0, dbToGain (-6.0f)));

        ReleaseResult r;
        r.startGr = rig.processor.getGainReductionDb();
        rig.run (15.0, silence(), nullptr, &r.trace);

        for (size_t i = 0; i < r.trace.size(); ++i)
        {
            const double tMs = (double) (i + 1) * rig.blockSeconds() * 1000.0;
            if (r.t50ms < 0.0 && r.trace[i] <= 0.5f * r.startGr) r.t50ms = tMs;
            if (r.t90ms < 0.0 && r.trace[i] <= 0.1f * r.startGr) r.t90ms = tMs;
        }

        return r;
    }

    juce::String formatTime (double ms)
    {
        if (ms < 0.0)      return "> 15 s";
        if (ms < 1000.0)   return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms / 1000.0, 2) + " s";
    }

    void testRelease (Report& report, const juce::File& outDir)
    {
        printSection ("3. Rilascio (impulso breve 0,2 s contro impulso lungo 10 s)");
        std::printf ("  Atteso: rilascio rapido dopo l'impulso breve, molto piu' lento\n");
        std::printf ("  dopo una compressione prolungata (memoria della cella T4B)\n\n");

        const auto shortBurst = measureRelease (0.2);
        const auto longBurst  = measureRelease (10.0);

        std::printf ("  Impulso    Rid. iniziale   Recupero 50%%   Recupero 90%%\n");
        std::printf ("  breve      %6.2f dB       %-12s   %s\n", shortBurst.startGr,
                     formatTime (shortBurst.t50ms).toRawUTF8(), formatTime (shortBurst.t90ms).toRawUTF8());
        std::printf ("  lungo      %6.2f dB       %-12s   %s\n", longBurst.startGr,
                     formatTime (longBurst.t50ms).toRawUTF8(), formatTime (longBurst.t90ms).toRawUTF8());

        // Andamento ogni 10 ms (30 blocchi da 16 campioni a 48 kHz)
        juce::String csv = "tempo_ms;riduzione_impulso_breve_db;riduzione_impulso_lungo_db\n";
        for (size_t i = 0; i < shortBurst.trace.size() && i < longBurst.trace.size(); i += 30)
            csv << csvNum ((double) (i + 1) * 16.0 / 48.0) << ";"
                << csvNum (shortBurst.trace[i]) << ";" << csvNum (longBurst.trace[i]) << "\n";
        outDir.getChildFile ("rilascio.csv").replaceWithText (csv);

        std::printf ("\n");

        const auto inRange = [] (double v, double lo, double hi) { return v >= lo && v <= hi; };
        const double memoryRatio = (shortBurst.t90ms > 0.0 && longBurst.t90ms > 0.0)
                                     ? longBurst.t90ms / shortBurst.t90ms : 0.0;

        std::printf ("  Rapporto di memoria (recupero 90%% lungo / breve): %.2f\n\n", memoryRatio);

        report.check (inRange (shortBurst.t50ms, Limits::shortT50MinMs, Limits::shortT50MaxMs),
                      "impulso breve: recupero 50% tra " + formatTime (Limits::shortT50MinMs)
                      + " e " + formatTime (Limits::shortT50MaxMs));
        report.check (inRange (shortBurst.t90ms, Limits::shortT90MinMs, Limits::shortT90MaxMs),
                      "impulso breve: recupero 90% tra " + formatTime (Limits::shortT90MinMs)
                      + " e " + formatTime (Limits::shortT90MaxMs));
        report.check (inRange (longBurst.t90ms, Limits::longT90MinMs, Limits::longT90MaxMs),
                      "compressione lunga: recupero 90% tra " + formatTime (Limits::longT90MinMs)
                      + " e " + formatTime (Limits::longT90MaxMs));
        report.check (memoryRatio >= Limits::memoryMinRatio,
                      "effetto memoria: recupero dopo compressione lunga almeno "
                      + juce::String (Limits::memoryMinRatio, 1) + " volte piu' lento");
    }

    //==========================================================================
    //  4. Distorsione armonica
    //==========================================================================
    void testHarmonics (Report& report, const juce::File& outDir)
    {
        printSection ("4. Distorsione armonica (seno 1 kHz, PEAK REDUCTION 0)");
        std::printf ("  Compressione esclusa: si misura solo lo stadio valvolare\n\n");
        std::printf ("  Ingresso  GAIN    H2 (dBc)  H3 (dBc)  H4 (dBc)  H5 (dBc)  THD (%%)\n");

        struct Row { float inputDb; float gainKnob; double hDbc[8]; double thd; };
        std::vector<Row> rows = { { -24.0f, 50.0f, {}, 0.0 }, { -18.0f, 50.0f, {}, 0.0 },
                                  { -12.0f, 50.0f, {}, 0.0 }, {  -6.0f, 50.0f, {}, 0.0 },
                                  {   0.0f, 50.0f, {}, 0.0 }, { -18.0f, 80.0f, {}, 0.0 } };

        Rig rig (48000.0, 512);
        rig.set (ParamIDs::peakReduction, 0.0f);

        juce::String csv = "ingresso_dbfs;gain;h2_dbc;h3_dbc;h4_dbc;h5_dbc;thd_percento\n";

        for (auto& row : rows)
        {
            rig.set (ParamIDs::gain, row.gainKnob);
            rig.restart();

            std::vector<float> out;
            rig.run (1.5, sine (1000.0, dbToGain (row.inputDb)), &out);

            // Finestra di 1 s = 1000 periodi esatti
            const size_t count = 48000, start = out.size() - count;
            double h[8] = {};
            for (int k = 1; k <= 7; ++k)
                h[k] = toneAmplitude (out, start, count, 1000.0 * k, 48000.0);

            double harmonicPower = 0.0;
            for (int k = 2; k <= 7; ++k)
            {
                row.hDbc[k] = toDb (h[k] / h[1]);
                harmonicPower += h[k] * h[k];
            }
            row.thd = 100.0 * std::sqrt (harmonicPower) / h[1];

            std::printf ("  %6.1f    %4.0f    %7.1f   %7.1f   %7.1f   %7.1f   %7.3f\n",
                         row.inputDb, row.gainKnob, row.hDbc[2], row.hDbc[3], row.hDbc[4], row.hDbc[5], row.thd);

            csv << csvNum (row.inputDb) << ";" << csvNum (row.gainKnob) << ";" << csvNum (row.hDbc[2]) << ";"
                << csvNum (row.hDbc[3]) << ";" << csvNum (row.hDbc[4]) << ";" << csvNum (row.hDbc[5]) << ";"
                << csvNum (row.thd) << "\n";
        }

        outDir.getChildFile ("armoniche.csv").replaceWithText (csv);

        // Pendenze tra -24 e -12 dBFS (righe 0 e 2): in regime polinomiale
        // la 2a armonica cresce di 1 dB/dB, la 3a di 2 dB/dB (relative alla fondamentale)
        const double slopeH2 = (rows[2].hDbc[2] - rows[0].hDbc[2]) / 12.0;
        const double slopeH3 = (rows[2].hDbc[3] - rows[0].hDbc[3]) / 12.0;

        // Livello a cui la 3a armonica supera la 2a
        juce::String crossover = "mai nell'intervallo provato";
        for (size_t i = 0; i < 5; ++i)
            if (rows[i].hDbc[3] > rows[i].hDbc[2]) { crossover = juce::String (rows[i].inputDb, 0) + " dBFS"; break; }

        bool thdRises = true;
        for (size_t i = 1; i < 5; ++i)
            thdRises = thdRises && rows[i].thd > rows[i - 1].thd;

        std::printf ("\n  Pendenza H2: %.2f dB/dB   Pendenza H3: %.2f dB/dB\n", slopeH2, slopeH3);
        std::printf ("  La 3a armonica supera la 2a a partire da: %s\n\n", crossover.toRawUTF8());

        report.check (thdRises, "la distorsione cresce con il livello d'ingresso");
        report.check (rows[1].thd < Limits::thdNominalMaxPercent,
                      "THD al livello nominale (-18 dBFS) sotto " + juce::String (Limits::thdNominalMaxPercent, 2) + "%");
        report.check (rows[4].thd < Limits::thdFullScaleMaxPercent,
                      "THD a 0 dBFS sotto " + juce::String (Limits::thdFullScaleMaxPercent, 1) + "%");
        report.check (rows[1].hDbc[2] > rows[1].hDbc[3] && rows[4].hDbc[3] > rows[4].hDbc[2],
                      "2a armonica dominante a -18 dBFS, 3a dominante a 0 dBFS");
        report.check (slopeH3 > Limits::slopeH3Min && slopeH3 < Limits::slopeH3Max,
                      "la 3a armonica cresce di circa 2 dB per dB (comportamento cubico)");
        report.check (slopeH2 > Limits::slopeH2Min && slopeH2 < Limits::slopeH2Max,
                      "la 2a armonica cresce di circa 1 dB per dB (asimmetria)");
        report.check (rows[5].thd > rows[1].thd, "alzando GAIN la valvola satura di piu'");
    }

    //==========================================================================
    //  5. Aliasing
    //==========================================================================
    void testAliasing (Report& report)
    {
        printSection ("5. Aliasing (seno 15 kHz a -3 dBFS, GAIN +4 dB)");
        std::printf ("  Le armoniche sopra 24 kHz non devono ripiegarsi nella banda udibile\n\n");

        Rig rig (48000.0, 512);
        rig.set (ParamIDs::peakReduction, 0.0f);
        rig.set (ParamIDs::gain, 60.0f);
        rig.restart();

        std::vector<float> out;
        rig.run (1.5, sine (15000.0, dbToGain (-3.0f)), &out);

        const size_t count = 48000, start = out.size() - count;
        const double fundamental = toneAmplitude (out, start, count, 15000.0, 48000.0);

        double worstDbc = -300.0, worstFreq = 0.0;
        int worstHarmonic = 0;

        for (int k = 2; k <= 15; ++k)
        {
            const double f = 15000.0 * k;
            if (f <= 24000.0)
                continue;

            double folded = std::fmod (f, 48000.0);
            if (folded > 24000.0)
                folded = 48000.0 - folded;
            if (folded < 20.0 || std::abs (folded - 15000.0) < 1.0)
                continue;

            const double dbc = toDb (toneAmplitude (out, start, count, folded, 48000.0) / fundamental);
            if (k <= 7)
                std::printf ("  Armonica %2d (%6.1f kHz) ripiegata a %5.1f kHz: %7.1f dBc\n",
                             k, f / 1000.0, folded / 1000.0, dbc);

            if (dbc > worstDbc) { worstDbc = dbc; worstFreq = folded; worstHarmonic = k; }
        }

        std::printf ("\n  Componente di aliasing peggiore: %.1f dBc a %.1f kHz (armonica %d)\n\n",
                     worstDbc, worstFreq / 1000.0, worstHarmonic);

        report.check (worstDbc < Limits::aliasingMaxDbc,
                      "aliasing sotto " + juce::String (Limits::aliasingMaxDbc, 0) + " dBc");
    }

    //==========================================================================
    //  6. Robustezza
    //==========================================================================
    void testRobustness (Report& report)
    {
        printSection ("6. Robustezza (onda quadra +12 dBFS, silenzio, valori denormali)");

        Rig rig (48000.0, 512);
        rig.set (ParamIDs::peakReduction, 100.0f);
        rig.set (ParamIDs::gain, 100.0f);
        rig.restart();

        std::vector<float> out;
        juce::Random rng (7);

        rig.run (1.0, [] (double t) { return std::fmod (t * 100.0, 1.0) < 0.5 ? 4.0f : -4.0f; }, &out);
        rig.run (2.0, silence(), &out);
        rig.run (1.0, [&rng] (double) { return (rng.nextFloat() - 0.5f) * 1.0e-39f; }, &out);

        bool allFinite = true;
        float peak = 0.0f;
        for (auto v : out)
        {
            allFinite = allFinite && std::isfinite (v);
            peak = std::max (peak, std::abs (v));
        }

        // Ultimi 100 ms del silenzio
        const size_t silenceEnd = 48000 + 96000;
        const double tailDb = toDb (rmsOf (out, silenceEnd - 4800, 4800));

        std::printf ("  Picco d'uscita: %.3f (%.1f dBFS)   Coda del silenzio: %.1f dBFS\n\n",
                     peak, toDb (peak), tailDb);

        report.check (allFinite, "nessun valore non numerico o infinito in uscita");
        report.check (peak < Limits::robustnessMaxPeak,
                      "uscita limitata anche con ingresso a +12 dBFS (limite "
                      + juce::String (juce::Decibels::gainToDecibels (Limits::robustnessMaxPeak), 1) + " dBFS)");
        report.check (tailDb < Limits::silenceTailMaxDb, "con ingresso nullo l'uscita torna al silenzio");
    }

    //==========================================================================
    //  7. Prestazioni
    //==========================================================================
    void testPerformance (Report& report)
    {
        printSection ("7. Prestazioni (20 s stereo a 48 kHz, blocchi da 512)");

        Rig rig (48000.0, 512);
        rig.set (ParamIDs::peakReduction, 60.0f);
        rig.set (ParamIDs::gain, 60.0f);
        rig.restart();

        juce::Random rng (1);
        rig.run (20.0, [&rng] (double t)
        {
            return 0.3f * (float) std::sin (2.0 * pi * 220.0 * t) + 0.1f * (rng.nextFloat() - 0.5f);
        });

        const double factor = 20.0 / rig.processingSeconds();
        std::printf ("  Tempo di elaborazione: %.3f s   ->   %.0f volte il tempo reale\n",
                     rig.processingSeconds(), factor);
        std::printf ("  Carico stimato per istanza: %.2f%% di un nucleo\n", 100.0 / factor);
        std::printf ("  Latenza dichiarata alla DAW: %d campioni\n\n", rig.processor.getLatencySamples());

       #if JUCE_DEBUG
        report.skip ("verifica prestazioni saltata: compilazione Debug (usare Release)");
       #else
        report.check (factor > Limits::minRealtimeFactor,
                      "almeno " + juce::String (Limits::minRealtimeFactor, 0) + " volte piu' veloce del tempo reale");
       #endif
    }

    //==========================================================================
    //  8. Preset di fabbrica
    //==========================================================================
    void testPresets (Report& report)
    {
        printSection ("8. Preset di fabbrica (seno 1 kHz con picchi a -10 dBFS)");

        Rig rig (48000.0, 256);
        auto& proc = rig.processor;
        const int numPresets = proc.getNumPrograms();

        std::printf ("  N.  Nome                               PEAK RED.  GAIN   Rid. attesa  Rid. misurata\n");

        bool namesOk = numPresets > 1;
        float maxError = 0.0f;

        for (int i = 0; i < numPresets; ++i)
        {
            proc.setCurrentProgram (i);
            const auto name = proc.getProgramName (i);
            namesOk = namesOk && name.isNotEmpty() && proc.getCurrentProgram() == i;

            const float pr   = proc.apvts.getRawParameterValue (ParamIDs::peakReduction)->load();
            const float gain = proc.apvts.getRawParameterValue (ParamIDs::gain)->load();

            rig.restart();
            std::vector<float> gr;
            rig.run (2.0, sine (1000.0, dbToGain (-10.0f)), nullptr, &gr);

            const size_t avgBlocks = 40;
            float measured = 0.0f;
            for (size_t k = gr.size() - avgBlocks; k < gr.size(); ++k)
                measured += gr[k];
            measured /= (float) avgBlocks;

            const float expected = expectedGainReduction (-10.0f + driveForKnob (pr));
            maxError = std::max (maxError, std::abs (measured - expected));

            std::printf ("  %2d  %-33s  %6.1f    %5.1f    %6.2f dB     %6.2f dB\n",
                         i + 1, name.toRawUTF8(), pr, gain, expected, measured);
        }

        // Salvataggio e ripristino: il preset e i valori devono sopravvivere
        // alla chiusura e riapertura del progetto
        proc.setCurrentProgram (3);
        proc.apvts.getParameter (ParamIDs::gain)->setValueNotifyingHost (0.55f);   // ritocco dopo il preset

        juce::MemoryBlock state;
        proc.getStateInformation (state);

        LA2AAudioProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());

        const bool roundTrip =
            restored.getCurrentProgram() == 3
            && std::abs (restored.apvts.getRawParameterValue (ParamIDs::peakReduction)->load()
                         - proc.apvts.getRawParameterValue (ParamIDs::peakReduction)->load()) < 0.01f
            && std::abs (restored.apvts.getRawParameterValue (ParamIDs::gain)->load()
                         - proc.apvts.getRawParameterValue (ParamIDs::gain)->load()) < 0.01f;

        std::printf ("\n");
        report.check (namesOk, juce::String (numPresets) + " preset con nome, selezionabili dalla DAW");
        report.check (maxError < Limits::presetMaxErrorDb,
                      "riduzione dei preset conforme al modello (scarto massimo "
                      + juce::String (maxError, 2) + " dB)");
        report.check (roundTrip, "preset e ritocchi conservati nel salvataggio del progetto");
    }

    //==========================================================================
    //  9. Preset utente
    //==========================================================================
    void testUserPresets (Report& report)
    {
        printSection ("9. Preset utente (cartella temporanea)");

        // Cartella temporanea: i preset reali in Documenti non vengono toccati
        const auto tempDir  = juce::File::getSpecialLocation (juce::File::tempDirectory);
        const auto folder   = tempDir.getChildFile ("ALAP_prova_preset");
        const auto exported = tempDir.getChildFile ("ALAP_prova_esportato.alappreset");
        folder.deleteRecursively();
        exported.deleteFile();

        LA2AAudioProcessor proc;
        auto& pm = proc.getPresetManager();
        pm.setUserPresetFolder (folder);

        auto setParam = [&proc] (const char* id, float value)
        {
            if (auto* p = proc.apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        auto paramIs = [&proc] (const char* id, float value)
        {
            return std::abs (proc.apvts.getRawParameterValue (id)->load() - value) < 0.05f;
        };

        // Salvataggio
        setParam (ParamIDs::peakReduction, 62.0f);
        setParam (ParamIDs::gain, 58.0f);
        const bool saved = pm.saveUserPreset ("Mia voce")
                           && folder.getChildFile ("Mia voce.alappreset").existsAsFile()
                           && pm.getCurrentPresetName() == "Mia voce"
                           && ! pm.isModified();

        // Ritocco e rilevamento della modifica
        setParam (ParamIDs::peakReduction, 10.0f);
        setParam (ParamIDs::gain, 90.0f);
        const bool modifiedDetected = pm.isModified();

        // Ricarica dal file
        const bool loaded = pm.loadUserPreset ("Mia voce")
                            && paramIs (ParamIDs::peakReduction, 62.0f)
                            && paramIs (ParamIDs::gain, 58.0f)
                            && ! pm.isModified();

        // Ricarica senza file (annulla ritocchi)
        setParam (ParamIDs::gain, 75.0f);
        pm.reloadCurrentPreset();
        const bool reloaded = paramIs (ParamIDs::gain, 58.0f);

        // Rinomina
        const bool renamed = pm.renameUserPreset ("Mia voce", "Voce principale")
                             && pm.userPresetExists ("Voce principale")
                             && ! pm.userPresetExists ("Mia voce")
                             && pm.getCurrentPresetName() == "Voce principale";

        // Esportazione e importazione (con nome gia' esistente -> numerazione)
        const bool exportOk = pm.exportCurrentSettings (exported) && exported.existsAsFile();
        const int importedFirst  = pm.importPresetFiles ({ exported });
        const int importedSecond = pm.importPresetFiles ({ exported });
        const bool importOk = importedFirst == 1 && importedSecond == 1
                              && pm.userPresetExists ("ALAP_prova_esportato")
                              && pm.userPresetExists ("ALAP_prova_esportato (2)");

        // File non valido: deve essere ignorato
        const auto invalid = tempDir.getChildFile ("ALAP_prova_non_valido.alappreset");
        invalid.replaceWithText ("non sono un preset");
        const bool invalidIgnored = pm.importPresetFiles ({ invalid }) == 0;
        invalid.deleteFile();

        // Nomi non validi
        const bool badNamesRejected = ! pm.saveUserPreset ("   ") && ! pm.saveUserPreset ("...");

        // Stato del progetto: preset utente + ritocco sopravvivono alla riapertura
        setParam (ParamIDs::gain, 70.0f);
        juce::MemoryBlock state;
        proc.getStateInformation (state);

        LA2AAudioProcessor restored;
        restored.getPresetManager().setUserPresetFolder (folder);
        restored.setStateInformation (state.getData(), (int) state.getSize());
        const auto& rpm = restored.getPresetManager();
        const bool stateOk = rpm.getCurrentPresetName() == "Voce principale"
                             && rpm.getSelection().isUser
                             && rpm.isModified()
                             && std::abs (restored.apvts.getRawParameterValue (ParamIDs::gain)->load() - 70.0f) < 0.05f;

        // Elenco e eliminazione
        const auto names = pm.getUserPresetNames();
        std::printf ("  Preset nella cartella: %s\n\n", names.joinIntoString (", ").toRawUTF8());

        const bool deleted = pm.deleteUserPreset ("Voce principale") && ! pm.userPresetExists ("Voce principale");

        folder.deleteRecursively();
        exported.deleteFile();

        report.check (saved,            "salvataggio di un preset utente");
        report.check (modifiedDetected, "rilevamento dei ritocchi non salvati");
        report.check (loaded,           "caricamento dal file con i valori corretti");
        report.check (reloaded,         "ricarica del preset corrente (annulla i ritocchi)");
        report.check (renamed,          "rinomina del preset corrente");
        report.check (exportOk && importOk, "esportazione e importazione, con numerazione degli omonimi");
        report.check (invalidIgnored,   "file non validi ignorati all'importazione");
        report.check (badNamesRejected, "nomi non validi rifiutati");
        report.check (stateOk,          "preset utente e ritocchi conservati nel progetto");
        report.check (deleted,          "eliminazione del preset");
    }
}

//==============================================================================
int main()
{
    // Serve un gestore dei messaggi JUCE per creare il processore e i parametri
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("risultati_test");
    outDir.createDirectory();

    std::printf ("ALAP Opto Leveler - programma di prova\n");
    std::printf ("File CSV in: %s\n", outDir.getFullPathName().toRawUTF8());

    Report report;

    testStaticCurve (report, outDir);
    testAttack (report);
    testRelease (report, outDir);
    testHarmonics (report, outDir);
    testAliasing (report);
    testRobustness (report);
    testPerformance (report);
    testPresets (report);
    testUserPresets (report);

    std::printf ("\n==================================================================\n");
    std::printf (" Riepilogo: %d superate, %d non superate, %d saltate\n",
                 report.passed, report.failed, report.skipped);
    std::printf ("==================================================================\n");

    return report.failed == 0 ? 0 : 1;
}
