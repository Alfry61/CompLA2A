#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Gestore dei preset di ALAP Opto Leveler.

    - Preset di fabbrica: tabella fissa, esposta alla DAW come "programmi".
    - Preset utente: file XML con estensione .alappreset nella cartella
        Documenti\Al Learned Audio Productions\ALAP Opto Leveler\Preset
      condivisi tra tutte le DAW e l'applicazione autonoma.

    Tiene traccia del preset caricato e dei suoi valori di riferimento,
    cosi' l'interfaccia puo' segnalare i ritocchi non salvati (asterisco).
    I preset memorizzano PEAK REDUCTION e GAIN; il selettore del VU no.

    Tutti i metodi sono sicuri se chiamati dal thread dei messaggi o da
    quello della DAW (salvataggio dello stato); nessuno va chiamato dal
    thread audio.
*/
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".alappreset";

    struct Selection
    {
        bool isUser = false;       // true = preset utente, false = di fabbrica
        int factoryIndex = 0;      // ultimo preset di fabbrica (quello che vede la DAW)
        juce::String userName;     // nome del preset utente, se isUser
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    //==========================================================================
    // Preset di fabbrica
    static int getNumFactoryPresets() noexcept;
    static juce::String getFactoryPresetName (int index);
    void loadFactoryPreset (int index);

    //==========================================================================
    // Preset utente
    juce::File getUserPresetFolder() const;
    void setUserPresetFolder (const juce::File& folder);   // usato dal programma di prova

    juce::StringArray getUserPresetNames() const;
    bool userPresetExists (const juce::String& name) const;
    bool saveUserPreset (const juce::String& name);          // salva i valori correnti e lo seleziona
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    bool renameUserPreset (const juce::String& oldName, const juce::String& newName);
    int  importPresetFiles (const juce::Array<juce::File>& files);   // restituisce quanti ne ha importati
    bool exportCurrentSettings (const juce::File& destination) const;

    //==========================================================================
    // Preset corrente
    Selection getSelection() const;
    juce::String getCurrentPresetName() const;
    bool isModified() const;          // i parametri differiscono dal preset caricato?
    void reloadCurrentPreset();       // annulla i ritocchi

    //==========================================================================
    // Persistenza nello stato del plugin (progetto della DAW)
    void writeToState (juce::ValueTree& state) const;
    void readFromState (const juce::ValueTree& state);

    /** Rende un nome utilizzabile come nome di file (vuoto se non valido). */
    static juce::String sanitiseName (const juce::String& name);

private:
    struct Values
    {
        float peakReduction = 40.0f;
        float gain          = 50.0f;
    };

    Values currentValues() const;
    void applyValues (const Values& values);
    juce::File fileFor (const juce::String& cleanName) const;

    static Values factoryValues (int index);
    static bool readPresetFile (const juce::File& file, Values& values);
    static bool writePresetFile (const juce::File& file, const Values& values);

    juce::AudioProcessorValueTreeState& apvts;

    mutable juce::CriticalSection lock;
    juce::File userFolder;
    Selection selection;
    Values reference;   // valori del preset caricato, per rilevare i ritocchi

    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};
