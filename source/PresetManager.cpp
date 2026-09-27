#include "PresetManager.h"
#include "PluginProcessor.h"   // ParamIDs

namespace
{
    //==========================================================================
    //  Preset di fabbrica
    //  Tarati per tracce con picchi intorno a -10 dBFS. Con PEAK REDUCTION P
    //  e picchi a -10 dBFS la riduzione vale circa 0,75 * (0,6 * P - 30) dB
    //  sopra il ginocchio; GAIN G corrisponde a (0,4 * G - 20) dB.
    //==========================================================================
    struct FactoryPreset
    {
        const char* name;
        float peakReduction;
        float gain;
    };

    const FactoryPreset factoryPresets[] =
    {
        //  Nome                                PEAK RED.  GAIN    rid. ~   guad. ~
        { "Iniziale",                            40.0f,    50.0f },  //  0 dB    0 dB
        { "Voce - livellamento dolce",           59.0f,    57.0f },  //  4 dB  +2,8 dB
        { "Voce - in primo piano",               66.0f,    62.0f },  //  7 dB  +4,8 dB
        { "Voce parlata / podcast",              63.0f,    60.0f },  //  6 dB  +4 dB
        { "Basso - controllo",                   61.0f,    60.0f },  //  5 dB  +4 dB
        { "Chitarra acustica",                   57.0f,    56.0f },  //  3 dB  +2,4 dB
        { "Pianoforte",                          55.0f,    54.0f },  //  2,5 dB +1,6 dB
        { "Bus batteria - coesione",             57.0f,    60.0f },  //  3 dB  +4 dB (piu' valvola)
        { "Batteria - compressione estrema",     77.0f,    70.0f },  // 12 dB  +8 dB
        { "Bus mix - tocco leggero",             52.0f,    53.0f },  //  1,5 dB +1,2 dB
        { "Solo colore valvolare",                0.0f,    62.0f },  //  0 dB  +4,8 dB nella valvola
    };

    constexpr int numFactoryPresets = (int) (sizeof (factoryPresets) / sizeof (factoryPresets[0]));

    // Differenza oltre la quale un parametro si considera ritoccato
    constexpr float modifiedTolerance = 0.05f;

    // Nomi usati nei file e nello stato del progetto
    constexpr const char* presetTag   = "ALAPPreset";
    constexpr const char* paramTag    = "Param";
    constexpr const char* propFactory = "preset";            // indice del preset di fabbrica
    constexpr const char* propUser    = "presetUtente";      // nome del preset utente
    constexpr const char* propRefPeak = "rifPeakReduction";  // valori di riferimento
    constexpr const char* propRefGain = "rifGain";
}

//==============================================================================
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    userFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                     .getChildFile ("Al Learned Audio Productions")
                     .getChildFile ("ALAP Opto Leveler")
                     .getChildFile ("Preset");

    reference = factoryValues (0);
}

//==============================================================================
//  Preset di fabbrica
//==============================================================================
int PresetManager::getNumFactoryPresets() noexcept
{
    return numFactoryPresets;
}

juce::String PresetManager::getFactoryPresetName (int index)
{
    return juce::isPositiveAndBelow (index, numFactoryPresets) ? juce::String (factoryPresets[index].name)
                                                               : juce::String();
}

PresetManager::Values PresetManager::factoryValues (int index)
{
    Values v;
    if (juce::isPositiveAndBelow (index, numFactoryPresets))
    {
        v.peakReduction = factoryPresets[index].peakReduction;
        v.gain          = factoryPresets[index].gain;
    }
    return v;
}

void PresetManager::loadFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, numFactoryPresets))
        return;

    const auto values = factoryValues (index);
    {
        const juce::ScopedLock sl (lock);
        selection.isUser       = false;
        selection.factoryIndex = index;
        selection.userName.clear();
        reference = values;
    }
    applyValues (values);
}

//==============================================================================
//  Preset utente
//==============================================================================
juce::File PresetManager::getUserPresetFolder() const
{
    const juce::ScopedLock sl (lock);
    return userFolder;
}

void PresetManager::setUserPresetFolder (const juce::File& folder)
{
    const juce::ScopedLock sl (lock);
    userFolder = folder;
}

juce::File PresetManager::fileFor (const juce::String& cleanName) const
{
    return getUserPresetFolder().getChildFile (cleanName + fileExtension);
}

juce::String PresetManager::sanitiseName (const juce::String& name)
{
    auto clean = juce::File::createLegalFileName (name.trim()).trim();

    // Niente nomi che iniziano con un punto (file nascosti) o troppo lunghi
    while (clean.startsWithChar ('.'))
        clean = clean.substring (1).trim();

    if (clean.length() > 64)
        clean = clean.substring (0, 64).trim();

    return clean;
}

juce::StringArray PresetManager::getUserPresetNames() const
{
    juce::StringArray names;

    for (const auto& file : getUserPresetFolder().findChildFiles (juce::File::findFiles, false,
                                                                  juce::String ("*") + fileExtension))
        names.add (file.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

bool PresetManager::userPresetExists (const juce::String& name) const
{
    const auto clean = sanitiseName (name);
    return clean.isNotEmpty() && fileFor (clean).existsAsFile();
}

bool PresetManager::saveUserPreset (const juce::String& name)
{
    const auto clean = sanitiseName (name);
    if (clean.isEmpty())
        return false;

    if (getUserPresetFolder().createDirectory().failed())
        return false;

    const auto values = currentValues();
    if (! writePresetFile (fileFor (clean), values))
        return false;

    const juce::ScopedLock sl (lock);
    selection.isUser   = true;
    selection.userName = clean;
    reference = values;
    return true;
}

bool PresetManager::loadUserPreset (const juce::String& name)
{
    const auto clean = sanitiseName (name);
    Values values;

    if (clean.isEmpty() || ! readPresetFile (fileFor (clean), values))
        return false;

    {
        const juce::ScopedLock sl (lock);
        selection.isUser   = true;
        selection.userName = clean;
        reference = values;
    }
    applyValues (values);
    return true;
}

bool PresetManager::deleteUserPreset (const juce::String& name)
{
    const auto clean = sanitiseName (name);
    const auto file  = fileFor (clean);

    // Se era il preset corrente, i valori restano e il nome resta visibile:
    // l'utente puo' sempre risalvarlo
    return clean.isNotEmpty() && file.existsAsFile() && file.deleteFile();
}

bool PresetManager::renameUserPreset (const juce::String& oldName, const juce::String& newName)
{
    const auto cleanOld = sanitiseName (oldName);
    const auto cleanNew = sanitiseName (newName);

    if (cleanOld.isEmpty() || cleanNew.isEmpty())
        return false;

    if (cleanOld == cleanNew)
        return true;

    const auto source      = fileFor (cleanOld);
    const auto destination = fileFor (cleanNew);

    if (! source.existsAsFile())
        return false;

    // Su Windows i nomi non distinguono maiuscole e minuscole:
    // "voce" -> "Voce" e' un cambio lecito, non un conflitto
    if (destination.existsAsFile() && ! cleanOld.equalsIgnoreCase (cleanNew))
        return false;

    if (! source.moveFileTo (destination))
        return false;

    const juce::ScopedLock sl (lock);
    if (selection.isUser && selection.userName == cleanOld)
        selection.userName = cleanNew;

    return true;
}

int PresetManager::importPresetFiles (const juce::Array<juce::File>& files)
{
    if (getUserPresetFolder().createDirectory().failed())
        return 0;

    int imported = 0;

    for (const auto& file : files)
    {
        Values values;
        if (! readPresetFile (file, values))
            continue;   // non e' un preset valido: lo salto

        auto baseName = sanitiseName (file.getFileNameWithoutExtension());
        if (baseName.isEmpty())
            baseName = "Preset importato";

        // In caso di omonimia aggiungo un numero: "Voce (2)", "Voce (3)"...
        auto destination = fileFor (baseName);
        for (int n = 2; destination.existsAsFile(); ++n)
            destination = fileFor (baseName + " (" + juce::String (n) + ")");

        // Riscrivo il file invece di copiarlo: formato sempre pulito e valori limitati
        if (writePresetFile (destination, values))
            ++imported;
    }

    return imported;
}

bool PresetManager::exportCurrentSettings (const juce::File& destination) const
{
    const auto file = destination.hasFileExtension (fileExtension) ? destination
                                                                   : destination.withFileExtension (fileExtension);
    return writePresetFile (file, currentValues());
}

//==============================================================================
//  Preset corrente
//==============================================================================
PresetManager::Selection PresetManager::getSelection() const
{
    const juce::ScopedLock sl (lock);
    return selection;
}

juce::String PresetManager::getCurrentPresetName() const
{
    const juce::ScopedLock sl (lock);
    return selection.isUser ? selection.userName : getFactoryPresetName (selection.factoryIndex);
}

bool PresetManager::isModified() const
{
    Values ref;
    {
        const juce::ScopedLock sl (lock);
        ref = reference;
    }

    const auto current = currentValues();
    return std::abs (current.peakReduction - ref.peakReduction) > modifiedTolerance
        || std::abs (current.gain - ref.gain) > modifiedTolerance;
}

void PresetManager::reloadCurrentPreset()
{
    Values ref;
    {
        const juce::ScopedLock sl (lock);
        ref = reference;
    }
    applyValues (ref);
}

//==============================================================================
//  Stato del progetto
//==============================================================================
void PresetManager::writeToState (juce::ValueTree& state) const
{
    const juce::ScopedLock sl (lock);
    state.setProperty (propFactory, selection.factoryIndex, nullptr);
    state.setProperty (propUser, selection.isUser ? selection.userName : juce::String(), nullptr);
    state.setProperty (propRefPeak, reference.peakReduction, nullptr);
    state.setProperty (propRefGain, reference.gain, nullptr);
}

void PresetManager::readFromState (const juce::ValueTree& state)
{
    Selection restored;
    const int index = (int) state.getProperty (propFactory, 0);
    restored.factoryIndex = juce::isPositiveAndBelow (index, numFactoryPresets) ? index : 0;
    restored.userName     = state.getProperty (propUser, juce::String()).toString();
    restored.isUser       = restored.userName.isNotEmpty();

    // Riferimento: salvato nel progetto; per i progetti creati con la versione
    // precedente (senza riferimento) uso i valori del preset di fabbrica
    Values ref = restored.isUser ? currentValues() : factoryValues (restored.factoryIndex);
    if (state.hasProperty (propRefPeak) && state.hasProperty (propRefGain))
    {
        ref.peakReduction = (float) state.getProperty (propRefPeak);
        ref.gain          = (float) state.getProperty (propRefGain);
    }

    const juce::ScopedLock sl (lock);
    selection = restored;
    reference = ref;
}

//==============================================================================
//  Parametri e file
//==============================================================================
PresetManager::Values PresetManager::currentValues() const
{
    Values v;
    v.peakReduction = apvts.getRawParameterValue (ParamIDs::peakReduction)->load();
    v.gain          = apvts.getRawParameterValue (ParamIDs::gain)->load();
    return v;
}

void PresetManager::applyValues (const Values& values)
{
    // Ogni valore e' un gesto completo: la DAW lo registra per l'annullamento
    auto apply = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (value));
            param->endChangeGesture();
        }
    };

    apply (ParamIDs::peakReduction, values.peakReduction);
    apply (ParamIDs::gain, values.gain);
}

bool PresetManager::readPresetFile (const juce::File& file, Values& values)
{
    if (! file.existsAsFile())
        return false;

    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (presetTag))
        return false;

    bool foundAny = false;

    for (auto* param : xml->getChildWithTagNameIterator (paramTag))
    {
        const auto id    = param->getStringAttribute ("id");
        const auto value = juce::jlimit (0.0f, 100.0f, (float) param->getDoubleAttribute ("value"));

        if (id == ParamIDs::peakReduction) { values.peakReduction = value; foundAny = true; }
        else if (id == ParamIDs::gain)     { values.gain = value;          foundAny = true; }
    }

    return foundAny;
}

bool PresetManager::writePresetFile (const juce::File& file, const Values& values)
{
    juce::XmlElement root (presetTag);
    root.setAttribute ("formato", 1);
    root.setAttribute ("plugin", "ALAP Opto Leveler");

    auto addParam = [&root] (const char* id, float value)
    {
        auto* param = root.createNewChildElement (paramTag);
        param->setAttribute ("id", id);
        param->setAttribute ("value", (double) value);
    };

    addParam (ParamIDs::peakReduction, values.peakReduction);
    addParam (ParamIDs::gain, values.gain);

    return root.writeTo (file);
}
