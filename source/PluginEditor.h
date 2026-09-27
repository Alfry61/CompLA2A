#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    Aspetto grafico "vintage":
    - manopole grandi nere in stile bachelite, zigrinate, con indice bianco;
    - selettore a becco di pollo per il VU-meter (attivato impostando la
      proprieta' "selettore" = true sullo Slider).
*/
class VintageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VintageLookAndFeel();

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;

    // Menu dei preset in stile targhetta nera (come il logo)
    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;
    juce::Font getComboBoxFont (juce::ComboBox& box) override;
    juce::Font getPopupMenuFont() override;

    // Pulsanti SALVA e "..." nello stesso stile
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override;

    static constexpr const char* selectorProperty = "selettore";

private:
    void drawLargeKnob (juce::Graphics& g, juce::Rectangle<float> bounds,
                        float angle, float startAngle, float endAngle) const;
    void drawSelectorKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float angle) const;
};

//==============================================================================
/**
    VU-meter analogico simulato.
    La scala e' lineare in tensione (come uno strumento reale) e la lancetta
    segue una balistica del secondo ordine: ~300 ms di salita e una lieve
    sovraelongazione.
*/
class AnalogVUMeter : public juce::Component
{
public:
    AnalogVUMeter() { setInterceptsMouseClicks (false, false); }

    /** Imposta la lettura desiderata in dB VU (0 = riferimento). */
    void setReading (float vuDb) noexcept;

    /** Fa avanzare la simulazione meccanica della lancetta. */
    void advanceBallistics (float deltaSeconds);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    static float dbToNeedlePosition (float vuDb) noexcept;
    static float needleAngle (float position) noexcept;
    static float dbToAngle (float vuDb) noexcept { return needleAngle (dbToNeedlePosition (vuDb)); }

    void renderFace();   // il quadrante viene disegnato una sola volta in un'immagine

    juce::Image faceImage;
    juce::Rectangle<float> faceArea;
    juce::Point<float> pivot;
    float needleRadius = 100.0f;

    float targetPosition = 0.0f;
    float needlePosition = 0.0f;
    float needleVelocity = 0.0f;

    static constexpr float maxAngle     = 0.80f;   // escursione +/- in radianti
    static constexpr float naturalOmega = 13.0f;   // pulsazione propria (rad/s)
    static constexpr float damping      = 0.81f;   // smorzamento (<1 = lieve rimbalzo)
};

//==============================================================================
/**
    Menu dei preset: prima di aprirsi rilegge la cartella dei preset utente,
    cosi' compaiono anche quelli salvati da un'altra istanza del plugin.
*/
class PresetComboBox : public juce::ComboBox
{
public:
    std::function<void()> onBeforePopup;

    void showPopup() override
    {
        if (onBeforePopup != nullptr)
            onBeforePopup();

        juce::ComboBox::showPopup();
    }
};

//==============================================================================
class LA2AAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit LA2AAudioProcessorEditor (LA2AAudioProcessor&);
    ~LA2AAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // --- Gestione dei preset ---------------------------------------------
    void refreshPresetList();       // ricostruisce il menu (fabbrica + utente)
    void updatePresetDisplay();     // nome corrente, con "*" se ritoccato
    void onPresetChosen();
    void savePreset();
    void showPresetMenu();
    void renameCurrentPreset();
    void deleteCurrentPreset();
    void importPresets();
    void exportPreset();

    // Finestra di dialogo interna al plugin (nome e/o conferma)
    void showDialog (const juce::String& title, const juce::String& message,
                     bool withNameField, const juce::String& initialText,
                     const juce::String& confirmText, const juce::String& cancelText,
                     std::function<void (juce::String)> onConfirmed);
    void showMessage (const juce::String& title, const juce::String& message);

    // Identificativi delle voci del menu
    static constexpr int factoryIdBase = 1;
    static constexpr int emptyUserId   = 999;
    static constexpr int userIdBase    = 1001;

    void createPanelTexture();
    void setupLargeKnob (juce::Slider& knob);
    void drawScrew (juce::Graphics& g, juce::Point<float> centre) const;
    void drawLogoBadge (juce::Graphics& g) const;
    void drawEngravedText (juce::Graphics& g, const juce::String& text,
                           juce::Rectangle<float> area, float fontSize,
                           juce::Justification justification) const;

    // 0 VU corrisponde a -18 dBFS RMS (equivalente digitale di +4 dBu)
    static constexpr float referenceOffsetDb = 18.0f;

    LA2AAudioProcessor& processorRef;

    // Il LookAndFeel deve essere dichiarato PRIMA degli slider
    // perche' venga distrutto dopo di essi
    VintageLookAndFeel vintageLnf;

    juce::Slider gainKnob;
    juce::Slider peakReductionKnob;
    juce::Slider meterSelector;
    PresetComboBox presetBox;                       // preset di fabbrica e utente
    juce::TextButton savePresetButton { "SALVA" };
    juce::TextButton presetMenuButton { "..." };    // rinomina, elimina, importa...
    juce::StringArray userPresetNames;              // nomi nell'ordine del menu
    std::unique_ptr<juce::AlertWindow> presetDialog;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::TooltipWindow tooltipWindow { this, 600 };
    AnalogVUMeter vuMeter;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment, peakAttachment, meterAttachment;

    juce::Image panelTexture;   // alluminio satinato generato proceduralmente
    juce::Image logoImage;      // logo ALAP incorporato (BinaryData)
    juce::Rectangle<float> logoArea;
    double lastTimerMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LA2AAudioProcessorEditor)
};
