#pragma once

// Con Projucer si usa <JuceHeader.h>.
// Con CMake sostituire con:
//   #include <juce_audio_utils/juce_audio_utils.h>
//   #include <juce_dsp/juce_dsp.h>
#include <JuceHeader.h>
#include "PresetManager.h"
#include <atomic>
#include <vector>

//==============================================================================
// Identificativi dei parametri, condivisi tra processore ed editor
namespace ParamIDs
{
    inline constexpr const char* peakReduction = "peakReduction";
    inline constexpr const char* gain          = "gain";
    inline constexpr const char* meterMode     = "meterMode";
}

//==============================================================================
/**
    Modello a due stadi della cella ottica T4B (pannello elettroluminescente
    + fotoresistenze al solfuro di cadmio).

    - Attacco fisso di 10 ms per entrambi gli stadi.
    - Rilascio veloce (~60 ms) per la parte "rapida" della fotoresistenza.
    - Rilascio lento (da 0,5 a 5 s) la cui durata dipende dalla "memoria"
      della cella: piu' a lungo si comprime, piu' lento diventa il rilascio
      e maggiore diventa il peso della parte lenta.
*/
class T4BEnvelope
{
public:
    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    /** Da chiamare una volta per blocco: aggiorna il tempo di rilascio lento
        in funzione della storia del segnale. */
    void updateProgramDependency() noexcept;

    /** Riceve la riduzione di guadagno desiderata (dB, >= 0) e restituisce
        quella effettiva prodotta dalla cella ottica. */
    float process (float targetGrDb) noexcept;

private:
    static float timeToCoef (double seconds, double sr) noexcept
    {
        return (float) std::exp (-1.0 / (seconds * sr));
    }

    // Costanti temporali del modello
    static constexpr double attackSec           = 0.010;
    static constexpr double fastReleaseSec      = 0.060;
    static constexpr double slowReleaseMinSec   = 0.5;
    static constexpr double slowReleaseMaxSec   = 3.5;
    static constexpr double historyChargeSec    = 2.0;  // la memoria si "carica" in ~2 s
    static constexpr double historyDischargeSec = 8.0;  // e si "scarica" in ~8 s

    double sampleRate = 44100.0;

    float attackCoef = 0.0f, fastReleaseCoef = 0.0f, slowReleaseCoef = 0.0f;
    float historyChargeCoef = 0.0f, historyDischargeCoef = 0.0f;

    float fastState = 0.0f;   // stadio veloce (dB di riduzione)
    float slowState = 0.0f;   // stadio lento (dB di riduzione)
    float history   = 0.0f;   // memoria della cella, 0..1
};

//==============================================================================
class LA2AAudioProcessor : public juce::AudioProcessor
{
public:
    LA2AAudioProcessor();
    ~LA2AAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

   #ifdef JucePlugin_Name
    const juce::String getName() const override            { return JucePlugin_Name; }
   #else
    // Usato quando il processore e' compilato nel programma di prova
    const juce::String getName() const override            { return "ALAP Opto Leveler"; }
   #endif
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    // Preset di fabbrica, esposti alla DAW come "programmi"
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Valori letti dall'interfaccia grafica (thread-safe)
    float getGainReductionDb() const noexcept { return gainReductionDb.load (std::memory_order_relaxed); }
    float getOutputLevelDb() const noexcept   { return outputLevelDb.load (std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    PresetManager& getPresetManager() noexcept { return presetManager; }

private:
    //==========================================================================
    // Curva di compressione statica a ginocchio morbido (dB -> dB di riduzione)
    static float computeGainReduction (float levelDb) noexcept;

    // Funzione di trasferimento asimmetrica dello stadio valvolare
    static float tubeTransfer (float x) noexcept;

    // Parametri fissi della curva (modalita' Compress)
    static constexpr float thresholdDb = -10.0f;
    static constexpr float ratio       = 4.0f;
    static constexpr float kneeDb      = 10.0f;

    //==========================================================================
    std::atomic<float>* peakReductionParam = nullptr;
    std::atomic<float>* gainParam          = nullptr;

    T4BEnvelope t4b;

    // Rivelatore di picco (catena laterale)
    float detectorLevel       = 0.0f;
    float detectorReleaseCoef = 0.0f;

    // Parametri addolciti per evitare scatti udibili
    juce::SmoothedValue<float> sidechainDriveDb;
    juce::SmoothedValue<float> outputGain;

    // Sovracampionamento per limitare l'aliasing della saturazione
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // Filtro anti-componente continua (l'asimmetria genera DC)
    struct DCBlocker
    {
        float x1 = 0.0f, y1 = 0.0f;
        float process (float x, float r) noexcept
        {
            const float y = x - x1 + r * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };
    std::vector<DCBlocker> dcBlockers;
    float dcCoef = 0.999f;

    // Misure per il VU-meter
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> outputLevelDb   { -100.0f };

    // Preset di fabbrica e utente (dichiarato dopo apvts, da cui dipende)
    PresetManager presetManager { apvts };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LA2AAudioProcessor)
};
