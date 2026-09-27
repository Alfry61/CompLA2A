#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Escursione della catena laterale pilotata da PEAK REDUCTION (dB)
    constexpr float minDriveDb = -30.0f;
    constexpr float maxDriveDb =  30.0f;

    // Escursione della manopola GAIN (dB)
    constexpr float minGainDb = -20.0f;
    constexpr float maxGainDb =  20.0f;

    // Taratura dello stadio valvolare
    constexpr float tubeDrive = 0.8f;    // quanto "spinge" la valvola (taratura validata)
    constexpr float tubeBias  = 0.05f;   // punto di lavoro: genera l'asimmetria (taratura validata)
    const float tubeBiasTanh  = std::tanh (tubeBias);
    // Normalizzazione: guadagno unitario per piccoli segnali
    const float tubeNorm      = 1.0f / (tubeDrive * (1.0f - tubeBiasTanh * tubeBiasTanh));

    float peakReductionToDriveDb (float knob) noexcept { return juce::jmap (knob, 0.0f, 100.0f, minDriveDb, maxDriveDb); }
    float gainKnobToDb (float knob) noexcept           { return juce::jmap (knob, 0.0f, 100.0f, minGainDb, maxGainDb); }

}

//==============================================================================
//  T4BEnvelope
//==============================================================================
void T4BEnvelope::prepare (double newSampleRate) noexcept
{
    sampleRate           = newSampleRate;
    attackCoef           = timeToCoef (attackSec, sampleRate);
    fastReleaseCoef      = timeToCoef (fastReleaseSec, sampleRate);
    historyChargeCoef    = timeToCoef (historyChargeSec, sampleRate);
    historyDischargeCoef = timeToCoef (historyDischargeSec, sampleRate);
    reset();
    updateProgramDependency();
}

void T4BEnvelope::reset() noexcept
{
    fastState = slowState = history = 0.0f;
}

void T4BEnvelope::updateProgramDependency() noexcept
{
    // Piu' la cella e' stata illuminata, piu' lento diventa il rilascio
    const double slowSec = slowReleaseMinSec + (slowReleaseMaxSec - slowReleaseMinSec) * (double) history;
    slowReleaseCoef = timeToCoef (slowSec, sampleRate);
}

float T4BEnvelope::process (float targetGrDb) noexcept
{
    // 1) Stadio veloce: attacco 10 ms, rilascio 60 ms
    const float fastCoef = targetGrDb > fastState ? attackCoef : fastReleaseCoef;
    fastState = targetGrDb + fastCoef * (fastState - targetGrDb);

    // 2) Stadio lento: attacco 10 ms, rilascio 0,5..5 s (dipendente dalla storia)
    const float slowCoef = targetGrDb > slowState ? attackCoef : slowReleaseCoef;
    slowState = targetGrDb + slowCoef * (slowState - targetGrDb);

    // 3) Memoria della cella: si carica con la compressione (10 dB = carica piena)
    const float historyTarget = juce::jlimit (0.0f, 1.0f, targetGrDb / 10.0f);
    const float historyCoef   = historyTarget > history ? historyChargeCoef : historyDischargeCoef;
    history = historyTarget + historyCoef * (history - historyTarget);

    // 4) Combinazione: con una storia lunga pesa di piu' la parte lenta.
    //    La parte lenta pesa dal 30% (storia breve) al 60% (compressione prolungata).
    const float slowWeight = 0.3f + 0.3f * history;
    return (1.0f - slowWeight) * fastState + slowWeight * slowState;
}

//==============================================================================
//  LA2AAudioProcessor
//==============================================================================
LA2AAudioProcessor::LA2AAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETRI", createParameterLayout())
{
    peakReductionParam = apvts.getRawParameterValue (ParamIDs::peakReduction);
    gainParam          = apvts.getRawParameterValue (ParamIDs::gain);

    // Dichiaro la latenza gia' alla creazione: alcuni programmi ospiti la
    // leggono prima di prepareToPlay. La latenza dei filtri di
    // sovracampionamento dipende solo dal tipo di filtro, non dalla
    // frequenza di campionamento, quindi basta un oggetto di prova.
    juce::dsp::Oversampling<float> latencyProbe (2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    setLatencySamples (juce::roundToInt (latencyProbe.getLatencyInSamples()));
}

juce::AudioProcessorValueTreeState::ParameterLayout LA2AAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::peakReduction, 1 }, "Peak Reduction",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::gain, 1 }, "Gain",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::meterMode, 1 }, "VU Meter",
        juce::StringArray { "Output +10", "Output +4", "Gain Reduction" }, 2));

    return { params.begin(), params.end() };
}

//==============================================================================
void LA2AAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = juce::jmax (1, getTotalNumOutputChannels());

    // Sovracampionamento 4x (ordine 2) con filtri IIR a mezza banda
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples (juce::roundToInt (oversampler->getLatencyInSamples()));

    t4b.prepare (sampleRate);

    // Rivelatore: salita istantanea, discesa 10 ms
    detectorLevel       = 0.0f;
    detectorReleaseCoef = (float) std::exp (-1.0 / (0.010 * sampleRate));

    sidechainDriveDb.reset (sampleRate, 0.05);
    sidechainDriveDb.setCurrentAndTargetValue (peakReductionToDriveDb (peakReductionParam->load()));

    outputGain.reset (sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainKnobToDb (gainParam->load())));

    // Filtro anti-DC a circa 5 Hz
    dcCoef = 1.0f - (juce::MathConstants<float>::twoPi * 5.0f / (float) sampleRate);
    dcBlockers.assign ((size_t) numChannels, DCBlocker {});
}

void LA2AAudioProcessor::releaseResources()
{
    if (oversampler != nullptr)
        oversampler->reset();
}

bool LA2AAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return out == layouts.getMainInputChannelSet();
}

//==============================================================================
float LA2AAudioProcessor::computeGainReduction (float levelDb) noexcept
{
    // Curva classica a ginocchio morbido (Giannoulis, Massberg, Reiss)
    const float over = levelDb - thresholdDb;
    float outDb;

    if (2.0f * over < -kneeDb)
    {
        outDb = levelDb;                                    // sotto la soglia
    }
    else if (2.0f * std::abs (over) <= kneeDb)
    {
        const float t = over + kneeDb * 0.5f;               // dentro il ginocchio
        outDb = levelDb + (1.0f / ratio - 1.0f) * t * t / (2.0f * kneeDb);
    }
    else
    {
        outDb = thresholdDb + over / ratio;                 // sopra la soglia, 4:1
    }

    return juce::jmax (0.0f, levelDb - outDb);
}

float LA2AAudioProcessor::tubeTransfer (float x) noexcept
{
    // tanh con punto di lavoro spostato (bias):
    // sviluppando in serie, il termine cubico e' dominante (3a armonica)
    // mentre il piccolo termine quadratico dovuto al bias aggiunge una
    // leggera 2a armonica, come in uno stadio valvolare reale.
    // A basso livello la funzione e' praticamente lineare: la distorsione
    // cresce in modo naturale con l'ampiezza del segnale.
    return (std::tanh (tubeDrive * x + tubeBias) - tubeBiasTanh) * tubeNorm;
}

//==============================================================================
void LA2AAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const int numCh = juce::jmin (numIn, buffer.getNumChannels());
    if (numCh == 0 || numSamples == 0 || oversampler == nullptr)
        return;

    // --- 1. Lettura parametri ---------------------------------------------
    sidechainDriveDb.setTargetValue (peakReductionToDriveDb (peakReductionParam->load()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (gainKnobToDb (gainParam->load())));

    t4b.updateProgramDependency();

    auto* const* channels = buffer.getArrayOfWritePointers();
    float currentGr = 0.0f;

    // --- 2. Compressione ottica (canali collegati) -------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        // a) Rivelatore di picco sul massimo tra i canali
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            peak = juce::jmax (peak, std::abs (channels[ch][i]));

        detectorLevel = peak > detectorLevel
                          ? peak
                          : peak + detectorReleaseCoef * (detectorLevel - peak);

        // b) PEAK REDUCTION amplifica il segnale che illumina la cella
        const float levelDb = juce::Decibels::gainToDecibels (detectorLevel, -120.0f)
                              + sidechainDriveDb.getNextValue();

        // c) Curva statica 4:1 a ginocchio morbido
        const float targetGr = computeGainReduction (levelDb);

        // d) Dinamica della cella T4B
        currentGr = t4b.process (targetGr);

        // e) Applicazione della riduzione e del guadagno d'uscita (GAIN)
        const float g = juce::Decibels::decibelsToGain (-currentGr) * outputGain.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
            channels[ch][i] *= g;
    }

    // --- 3. Stadio valvolare sovracampionato -------------------------------
    // GAIN precede la valvola: alzandolo si spinge di piu' in saturazione
    juce::dsp::AudioBlock<float> block (buffer);
    auto activeBlock = block.getSubsetChannelBlock (0, (size_t) numCh);
    auto upBlock     = oversampler->processSamplesUp (activeBlock);

    for (size_t ch = 0; ch < upBlock.getNumChannels(); ++ch)
    {
        auto* data = upBlock.getChannelPointer (ch);
        for (size_t i = 0; i < upBlock.getNumSamples(); ++i)
            data[i] = tubeTransfer (data[i]);
    }

    oversampler->processSamplesDown (activeBlock);

    // --- 4. Rimozione DC e misura del livello d'uscita ---------------------
    float rms = 0.0f;

    for (int ch = 0; ch < numCh && ch < (int) dcBlockers.size(); ++ch)
    {
        auto& dc   = dcBlockers[(size_t) ch];
        auto* data = channels[ch];

        for (int i = 0; i < numSamples; ++i)
            data[i] = dc.process (data[i], dcCoef);

        rms = juce::jmax (rms, buffer.getRMSLevel (ch, 0, numSamples));
    }

    gainReductionDb.store (currentGr, std::memory_order_relaxed);
    outputLevelDb.store (juce::Decibels::gainToDecibels (rms, -100.0f), std::memory_order_relaxed);
}

//==============================================================================
//  Preset di fabbrica, esposti alla DAW come programmi
//  (la tabella e i preset utente sono in PresetManager)
//==============================================================================
int LA2AAudioProcessor::getNumPrograms()
{
    return PresetManager::getNumFactoryPresets();
}

int LA2AAudioProcessor::getCurrentProgram()
{
    return presetManager.getSelection().factoryIndex;
}

const juce::String LA2AAudioProcessor::getProgramName (int index)
{
    return PresetManager::getFactoryPresetName (index);
}

void LA2AAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, PresetManager::getNumFactoryPresets()))
        return;

    presetManager.loadFactoryPreset (index);
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}

//==============================================================================
juce::AudioProcessorEditor* LA2AAudioProcessor::createEditor()
{
    return new LA2AAudioProcessorEditor (*this);
}

void LA2AAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presetManager.writeToState (state);   // preset corrente e valori di riferimento

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LA2AAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);

            // Solo il nome del preset e i riferimenti: i valori dei parametri
            // salvati hanno la precedenza, quindi i ritocchi vengono conservati
            presetManager.readFromState (tree);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LA2AAudioProcessor();
}
