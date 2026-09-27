#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;

    // Direzione di un angolo misurato in senso orario dalle ore 12 (convenzione JUCE)
    juce::Point<float> directionFor (float angle) noexcept
    {
        return { std::sin (angle), -std::cos (angle) };
    }

    const juce::Colour inkColour   { 0xff262626 };   // serigrafia del pannello
    const juce::Colour pointerTint { 0xfff4f1e8 };   // bianco avorio degli indici
}

//==============================================================================
//  VintageLookAndFeel
//==============================================================================
VintageLookAndFeel::VintageLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId, juce::Colours::black);
    setColour (juce::Slider::thumbColourId, pointerTint);

    // Menu dei preset: nero come la targhetta del logo, freccia blu ALAP
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff090b0f));
    setColour (juce::ComboBox::textColourId, pointerTint);
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a3d42));
    setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff2f7fd6));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff111419));
    setColour (juce::PopupMenu::textColourId, pointerTint);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff1f5fae));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    // Pulsanti dei preset
    setColour (juce::TextButton::textColourOffId, pointerTint);
    setColour (juce::TextButton::textColourOnId, pointerTint);
}

void VintageLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour&, bool isHighlighted, bool isDown)
{
    const auto bounds = button.getLocalBounds().toFloat();

    auto base = juce::Colour (0xff090b0f);
    if (isDown)             base = base.brighter (0.25f);
    else if (isHighlighted) base = base.brighter (0.12f);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Smussatura (invertita quando il pulsante e' premuto)
    const auto light = juce::Colours::white.withAlpha (0.35f);
    const auto dark  = juce::Colours::black.withAlpha (0.60f);
    g.setGradientFill (juce::ColourGradient (isDown ? dark : light, 0.0f, 0.0f,
                                             isDown ? light : dark, 0.0f, bounds.getBottom(), false));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
}

juce::Font VintageLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (juce::FontOptions (12.0f, juce::Font::bold));
}

void VintageLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                       int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    // Corpo della targhetta
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 4.0f);

    // Smussatura: bordo chiaro in alto, scuro in basso
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), 0.0f, 0.0f,
                                             juce::Colours::black.withAlpha (0.60f), 0.0f, (float) height,
                                             false));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    // Freccia verso il basso
    const auto arrowArea = bounds.removeFromRight ((float) height).reduced ((float) height * 0.34f);
    juce::Path arrow;
    arrow.addTriangle (arrowArea.getX(), arrowArea.getY(),
                       arrowArea.getRight(), arrowArea.getY(),
                       arrowArea.getCentreX(), arrowArea.getBottom());
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font VintageLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (13.0f, juce::Font::bold));
}

juce::Font VintageLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (14.0f));
}

void VintageLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle,
                                           float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    if ((bool) slider.getProperties().getWithDefault (selectorProperty, false))
        drawSelectorKnob (g, bounds, angle);
    else
        drawLargeKnob (g, bounds, angle, rotaryStartAngle, rotaryEndAngle);
}

void VintageLookAndFeel::drawLargeKnob (juce::Graphics& g, juce::Rectangle<float> bounds,
                                        float angle, float startAngle, float endAngle) const
{
    const auto centre = bounds.getCentre();
    const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float knobR = outer * 0.68f;

    // --- Scala serigrafata 0-100 sul pannello ------------------------------
    g.setFont (juce::FontOptions (outer * 0.11f, juce::Font::bold));

    for (int i = 0; i <= 10; ++i)
    {
        const float a   = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const auto  dir = directionFor (a);

        g.setColour (inkColour);
        g.drawLine (juce::Line<float> (centre + dir * (outer * 0.73f),
                                       centre + dir * (outer * 0.80f)),
                    i % 5 == 0 ? 2.0f : 1.2f);

        const auto textArea = juce::Rectangle<float> (outer * 0.32f, outer * 0.14f)
                                  .withCentre (centre + dir * (outer * 0.91f));
        g.drawText (juce::String (i * 10), textArea, juce::Justification::centred, false);
    }

    // --- Ombra portata ------------------------------------------------------
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (knobR * 2.08f, knobR * 2.08f)
                       .withCentre (centre.translated (3.0f, 6.0f)));

    // --- Gonna esterna ------------------------------------------------------
    const auto skirt = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3c3c3c), skirt.getX(), skirt.getY(),
                                             juce::Colour (0xff050505), skirt.getRight(), skirt.getBottom(),
                                             false));
    g.fillEllipse (skirt);

    // --- Zigrinatura del bordo ----------------------------------------------
    constexpr int numRidges = 72;
    for (int i = 0; i < numRidges; ++i)
    {
        const auto dir = directionFor (juce::MathConstants<float>::twoPi * (float) i / (float) numRidges);
        g.setColour (i % 2 == 0 ? juce::Colour (0xff343434) : juce::Colour (0xff0e0e0e));
        g.drawLine (juce::Line<float> (centre + dir * (knobR * 0.86f),
                                       centre + dir * (knobR * 0.99f)), 1.4f);
    }

    // --- Calotta superiore con gradiente radiale ----------------------------
    const float capR = knobR * 0.80f;
    const auto cap   = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a),
                                             centre.x - capR * 0.35f, centre.y - capR * 0.45f,
                                             juce::Colour (0xff080808),
                                             centre.x + capR * 0.60f, centre.y + capR * 0.70f,
                                             true));
    g.fillEllipse (cap);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (cap, 1.2f);

    // Riflesso morbido della luce ambiente
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillEllipse (juce::Rectangle<float> (capR * 1.1f, capR * 0.6f)
                       .withCentre (centre.translated (-capR * 0.15f, -capR * 0.45f)));

    // --- Indice bianco -----------------------------------------------------
    const float w = juce::jmax (3.0f, capR * 0.09f);
    juce::Path pointer;
    pointer.addRoundedRectangle (-w * 0.5f, -capR * 0.93f, w, capR * 0.55f, w * 0.5f);

    g.setColour (pointerTint);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void VintageLookAndFeel::drawSelectorKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float angle) const
{
    const auto  centre = bounds.getCentre();
    const float r      = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f * 0.85f;

    // Sagoma a "becco di pollo" disegnata verso l'alto, poi ruotata
    juce::Path head;
    head.startNewSubPath (0.0f, -r);
    head.quadraticTo ( r * 0.42f, -r * 0.15f,  r * 0.45f, r * 0.35f);
    head.quadraticTo ( 0.0f,       r * 0.80f, -r * 0.45f, r * 0.35f);
    head.quadraticTo (-r * 0.42f, -r * 0.15f,  0.0f,     -r);
    head.closeSubPath();

    const auto t = juce::AffineTransform::rotation (angle).translated (centre.x, centre.y);

    // Ombra
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillPath (head, t.translated (2.0f, 4.0f));

    // Corpo
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3e3e3e), centre.x - r, centre.y - r,
                                             juce::Colour (0xff050505), centre.x + r, centre.y + r, false));
    g.fillPath (head, t);

    // Mozzo centrale
    const auto hub = juce::Rectangle<float> (r * 0.8f, r * 0.8f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2e2e), hub.getX(), hub.getY(),
                                             juce::Colour (0xff0a0a0a), hub.getRight(), hub.getBottom(), false));
    g.fillEllipse (hub);

    // Linea bianca di riferimento
    juce::Path line;
    line.addRoundedRectangle (-1.5f, -r * 0.92f, 3.0f, r * 0.70f, 1.5f);
    g.setColour (pointerTint);
    g.fillPath (line, t);
}

//==============================================================================
//  AnalogVUMeter
//==============================================================================
float AnalogVUMeter::dbToNeedlePosition (float vuDb) noexcept
{
    // La scala di un VU e' (circa) lineare in tensione: 0 VU cade a ~70%
    const float minV = juce::Decibels::decibelsToGain (-20.0f);
    const float maxV = juce::Decibels::decibelsToGain (3.0f);
    const float v    = juce::Decibels::decibelsToGain (vuDb, -100.0f);

    // Piccolo margine oltre i fondo scala per simulare i fermi meccanici
    return juce::jlimit (-0.04f, 1.05f, (v - minV) / (maxV - minV));
}

float AnalogVUMeter::needleAngle (float position) noexcept
{
    return juce::jmap (position, -maxAngle, maxAngle);
}

void AnalogVUMeter::setReading (float vuDb) noexcept
{
    targetPosition = dbToNeedlePosition (vuDb);
}

void AnalogVUMeter::advanceBallistics (float deltaSeconds)
{
    const float previous = needlePosition;

    // Modello massa-molla-smorzatore integrato in piccoli passi per stabilita'
    constexpr int subSteps = 4;
    const float h = deltaSeconds / (float) subSteps;

    for (int i = 0; i < subSteps; ++i)
    {
        const float accel = naturalOmega * naturalOmega * (targetPosition - needlePosition)
                            - 2.0f * damping * naturalOmega * needleVelocity;
        needleVelocity += accel * h;
        needlePosition += needleVelocity * h;
    }

    // Fermi meccanici
    if (needlePosition < -0.04f) { needlePosition = -0.04f; needleVelocity = 0.0f; }
    if (needlePosition >  1.05f) { needlePosition =  1.05f; needleVelocity = 0.0f; }

    if (std::abs (needlePosition - previous) > 1.0e-4f)
        repaint();
}

void AnalogVUMeter::resized()
{
    const auto bounds = getLocalBounds().toFloat();

    faceArea     = bounds.reduced (10.0f);
    // Il perno sta sotto il quadrante, nascosto dall'alloggiamento
    pivot        = { faceArea.getCentreX(), faceArea.getBottom() + faceArea.getHeight() * 0.30f };
    needleRadius = pivot.y - faceArea.getY() - 12.0f;

    renderFace();
}

void AnalogVUMeter::renderFace()
{
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    faceImage = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (faceImage);

    // --- Cornice nera -------------------------------------------------------
    g.setColour (juce::Colour (0xff1b1b1b));
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4a), 0.0f, bounds.getY(),
                                             juce::Colour (0xff0e0e0e), 0.0f, bounds.getBottom(), false));
    g.drawRoundedRectangle (bounds.reduced (2.0f), 7.0f, 3.0f);

    // --- Quadrante color crema ----------------------------------------------
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7eed0), faceArea.getCentreX(), faceArea.getY(),
                                             juce::Colour (0xffe6d3a0), faceArea.getCentreX(), faceArea.getBottom(),
                                             false));
    g.fillRoundedRectangle (faceArea, 4.0f);

    const float scaleR = needleRadius * 0.84f;

    // Arco nero (-20 .. 0 VU)
    juce::Path blackArc;
    blackArc.addCentredArc (pivot.x, pivot.y, scaleR, scaleR, 0.0f, dbToAngle (-20.0f), dbToAngle (0.0f), true);
    g.setColour (juce::Colour (0xff1d1d1d));
    g.strokePath (blackArc, juce::PathStrokeType (1.6f));

    // Zona rossa (0 .. +3 VU)
    juce::Path redArc;
    redArc.addCentredArc (pivot.x, pivot.y, scaleR + 3.0f, scaleR + 3.0f, 0.0f, dbToAngle (0.0f), dbToAngle (3.0f), true);
    g.setColour (juce::Colour (0xffb3261e));
    g.strokePath (redArc, juce::PathStrokeType (6.0f));

    // --- Tacche e numeri ----------------------------------------------------
    struct Tick { float db; const char* text; bool major; };
    static const Tick ticks[] = {
        { -20.0f, "20", true  }, { -10.0f, "10", true  }, { -7.0f, "7", true  },
        {  -5.0f, "5",  true  }, {  -3.0f, "3",  true  }, { -2.0f, "2", false },
        {  -1.0f, "1",  false }, {   0.0f, "0",  true  }, {  1.0f, "1", false },
        {   2.0f, "2",  false }, {   3.0f, "3",  true  }
    };

    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));

    for (const auto& t : ticks)
    {
        const auto dir = directionFor (dbToAngle (t.db));
        g.setColour (t.db > 0.0f ? juce::Colour (0xffb3261e) : juce::Colour (0xff1d1d1d));

        g.drawLine (juce::Line<float> (pivot + dir * scaleR,
                                       pivot + dir * (scaleR + (t.major ? 11.0f : 7.0f))),
                    t.major ? 1.8f : 1.2f);

        g.drawText (t.text,
                    juce::Rectangle<float> (28.0f, 14.0f).withCentre (pivot + dir * (scaleR + 22.0f)),
                    juce::Justification::centred, false);
    }

    // Segni "-" e "+" alle estremita'
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.setColour (juce::Colour (0xff1d1d1d));
    g.drawText ("-", juce::Rectangle<float> (16.0f, 16.0f)
                         .withCentre (pivot + directionFor (dbToAngle (-20.0f) - 0.09f) * (scaleR + 22.0f)),
                juce::Justification::centred, false);
    g.setColour (juce::Colour (0xffb3261e));
    g.drawText ("+", juce::Rectangle<float> (16.0f, 16.0f)
                         .withCentre (pivot + directionFor (dbToAngle (3.0f) + 0.09f) * (scaleR + 22.0f)),
                juce::Justification::centred, false);

    // Scritta "VU"
    g.setColour (juce::Colour (0xff1d1d1d));
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("VU",
                juce::Rectangle<float> (80.0f, 28.0f)
                    .withCentre ({ faceArea.getCentreX(), faceArea.getY() + faceArea.getHeight() * 0.62f }),
                juce::Justification::centred, false);
}

void AnalogVUMeter::paint (juce::Graphics& g)
{
    if (faceImage.isValid())
        g.drawImageAt (faceImage, 0, 0);

    // --- Lancetta -----------------------------------------------------------
    const auto dir = directionFor (needleAngle (needlePosition));
    const auto tip = pivot + dir * needleRadius;

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (faceArea.toNearestInt());

        // Ombra della lancetta sul quadrante
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.drawLine (juce::Line<float> (pivot.translated (3.0f, 2.0f), tip.translated (3.0f, 2.0f)), 2.0f);

        g.setColour (juce::Colour (0xff141414));
        g.drawLine (juce::Line<float> (pivot, tip), 1.6f);
    }

    // --- Alloggiamento del perno (copre la base della lancetta) -------------
    const auto cover = faceArea.withTop (faceArea.getBottom() - 24.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), 0.0f, cover.getY(),
                                             juce::Colour (0xff0c0c0c), 0.0f, cover.getBottom(), false));
    g.fillRect (cover);

    // --- Riflesso del vetro -------------------------------------------------
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), 0.0f, faceArea.getY(),
                                             juce::Colours::white.withAlpha (0.0f),  0.0f, faceArea.getCentreY(),
                                             false));
    g.fillRoundedRectangle (faceArea.withHeight (faceArea.getHeight() * 0.5f), 4.0f);
}

//==============================================================================
//  LA2AAudioProcessorEditor
//==============================================================================
LA2AAudioProcessorEditor::LA2AAudioProcessorEditor (LA2AAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // Manopole grandi
    setupLargeKnob (gainKnob);
    setupLargeKnob (peakReductionKnob);

    // Selettore a 3 posizioni: -45 gradi, ore 12, +45 gradi
    meterSelector.getProperties().set (VintageLookAndFeel::selectorProperty, true);
    meterSelector.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    meterSelector.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    meterSelector.setRotaryParameters (pi * 1.75f, pi * 2.25f, true);
    meterSelector.setMouseDragSensitivity (80);   // bastano piccoli movimenti
    meterSelector.setLookAndFeel (&vintageLnf);
    addAndMakeVisible (meterSelector);

    addAndMakeVisible (vuMeter);

    // --- Preset: menu, pulsante SALVA e menu delle azioni --------------------
    presetBox.setLookAndFeel (&vintageLnf);
    presetBox.setTooltip ("Preset di fabbrica e preset utente");
    presetBox.onChange      = [this] { onPresetChosen(); };
    presetBox.onBeforePopup = [this] { refreshPresetList(); };
    addAndMakeVisible (presetBox);

    savePresetButton.setLookAndFeel (&vintageLnf);
    savePresetButton.setTooltip ("Salva le impostazioni correnti come preset utente");
    savePresetButton.onClick = [this] { savePreset(); };
    addAndMakeVisible (savePresetButton);

    presetMenuButton.setLookAndFeel (&vintageLnf);
    presetMenuButton.setTooltip ("Ricarica, rinomina, elimina, importa, esporta");
    presetMenuButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetMenuButton);

    refreshPresetList();

    // Logo incorporato nel plugin (ImageCache evita di decodificarlo piu' volte)
    logoImage = juce::ImageCache::getFromMemory (BinaryData::ALAP_Logo_png, BinaryData::ALAP_Logo_pngSize);

    // Collegamenti ai parametri (creati dopo la configurazione degli slider)
    gainAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, ParamIDs::gain, gainKnob);
    peakAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, ParamIDs::peakReduction, peakReductionKnob);
    meterAttachment = std::make_unique<SliderAttachment> (processorRef.apvts, ParamIDs::meterMode, meterSelector);

    setSize (920, 400);

    lastTimerMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

LA2AAudioProcessorEditor::~LA2AAudioProcessorEditor()
{
    stopTimer();
    gainKnob.setLookAndFeel (nullptr);
    peakReductionKnob.setLookAndFeel (nullptr);
    meterSelector.setLookAndFeel (nullptr);
    presetBox.setLookAndFeel (nullptr);
    savePresetButton.setLookAndFeel (nullptr);
    presetMenuButton.setLookAndFeel (nullptr);
    presetDialog.reset();
    fileChooser.reset();
}

void LA2AAudioProcessorEditor::setupLargeKnob (juce::Slider& knob)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters (pi * 1.25f, pi * 2.75f, true);   // 270 gradi di escursione
    knob.setMouseDragSensitivity (300);
    knob.setLookAndFeel (&vintageLnf);
    addAndMakeVisible (knob);
}

//==============================================================================
void LA2AAudioProcessorEditor::createPanelTexture()
{
    // Alluminio satinato: striature orizzontali + rumore fine + lieve
    // variazione di luminosita' verticale. Seme fisso = texture sempre uguale.
    const int w = juce::jmax (1, getWidth());
    const int h = juce::jmax (1, getHeight());

    panelTexture = juce::Image (juce::Image::RGB, w, h, false);
    juce::Random rng (0x2A2A);
    juce::Image::BitmapData pixels (panelTexture, juce::Image::BitmapData::writeOnly);

    for (int py = 0; py < h; ++py)
    {
        const float rowOffset = (rng.nextFloat() - 0.5f) * 9.0f;
        const float light     = 196.0f + 16.0f * std::sin ((float) py / (float) h * pi);

        for (int px = 0; px < w; ++px)
        {
            const float v    = light + rowOffset + (rng.nextFloat() - 0.5f) * 5.0f;
            const auto  grey = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (v));
            const auto  blue = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (v + 4.0f));
            pixels.setPixelColour (px, py, juce::Colour (grey, grey, blue));
        }
    }
}

void LA2AAudioProcessorEditor::drawScrew (juce::Graphics& g, juce::Point<float> centre) const
{
    constexpr float r = 7.0f;
    const auto b = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8e8e8), b.getX(), b.getY(),
                                             juce::Colour (0xff7d7d7d), b.getRight(), b.getBottom(), false));
    g.fillEllipse (b);
    g.setColour (juce::Colour (0xff555555));
    g.drawEllipse (b, 1.0f);

    juce::Path slot;
    slot.addRectangle (-r * 0.8f, -1.0f, r * 1.6f, 2.0f);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillPath (slot, juce::AffineTransform::rotation (0.6f).translated (centre.x, centre.y));
}

void LA2AAudioProcessorEditor::drawLogoBadge (juce::Graphics& g) const
{
    if (! logoImage.isValid() || logoArea.isEmpty())
        return;

    // Targhetta nera applicata sul pannello, dello stesso colore dello sfondo
    // del logo, cosi' i bordi dell'immagine non si vedono
    const auto badge = logoArea.expanded (7.0f, 5.0f);

    // Ombra portata
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillRoundedRectangle (badge.translated (1.5f, 2.5f), 4.0f);

    // Corpo della targhetta
    g.setColour (juce::Colour (0xff090b0f));
    g.fillRoundedRectangle (badge, 4.0f);

    // Smussatura: bordo chiaro in alto, scuro in basso
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), 0.0f, badge.getY(),
                                             juce::Colours::black.withAlpha (0.60f), 0.0f, badge.getBottom(),
                                             false));
    g.drawRoundedRectangle (badge.reduced (0.5f), 4.0f, 1.0f);

    // Logo, ridimensionato con alta qualita'
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (logoImage, logoArea, juce::RectanglePlacement::centred);
}

void LA2AAudioProcessorEditor::drawEngravedText (juce::Graphics& g, const juce::String& text,
                                                 juce::Rectangle<float> area, float fontSize,
                                                 juce::Justification justification) const
{
    // Effetto inciso: luce sotto, inchiostro sopra
    g.setFont (juce::FontOptions (fontSize, juce::Font::bold));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawText (text, area.translated (0.0f, 1.0f), justification, false);
    g.setColour (inkColour);
    g.drawText (text, area, justification, false);
}

//==============================================================================
void LA2AAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // --- Pannello in alluminio ---------------------------------------------
    if (panelTexture.isValid())
        g.drawImageAt (panelTexture, 0, 0);

    // Velatura satinata
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), 0.0f, 0.0f,
                                             juce::Colours::black.withAlpha (0.10f), 0.0f, bounds.getBottom(),
                                             false));
    g.fillAll();

    g.setColour (juce::Colour (0xff8a8d90));
    g.drawRect (getLocalBounds(), 2);

    // Viti agli angoli
    constexpr float m = 18.0f;
    drawScrew (g, { m, m });
    drawScrew (g, { bounds.getRight() - m, m });
    drawScrew (g, { m, bounds.getBottom() - m });
    drawScrew (g, { bounds.getRight() - m, bounds.getBottom() - m });

    // --- Intestazione ------------------------------------------------------
    // Logo in alto a sinistra; il nome del marchio e' gia' nel logo,
    // quindi al centro resta solo il titolo del modello
    drawLogoBadge (g);

    // Titolo centrato nello spazio tra il logo e i comandi dei preset
    const float titleLeft  = logoArea.isEmpty() ? 40.0f : logoArea.getRight() + 16.0f;
    const float titleRight = (float) presetBox.getX() - 10.0f;
    drawEngravedText (g, "LEVELING AMPLIFIER", { titleLeft, 22.0f, titleRight - titleLeft, 26.0f },
                      22.0f, juce::Justification::centred);

    // Linea incisa di separazione
    g.setColour (juce::Colour (0xff6e7174));
    g.drawHorizontalLine (66, 40.0f, bounds.getRight() - 40.0f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawHorizontalLine (67, 40.0f, bounds.getRight() - 40.0f);

    // --- Etichette delle manopole -------------------------------------------
    auto labelUnder = [] (const juce::Slider& s)
    {
        return juce::Rectangle<float> ((float) s.getX(), (float) s.getBottom() + 4.0f, (float) s.getWidth(), 24.0f);
    };

    drawEngravedText (g, "GAIN", labelUnder (gainKnob), 16.0f, juce::Justification::centred);
    drawEngravedText (g, "PEAK REDUCTION", labelUnder (peakReductionKnob), 16.0f, juce::Justification::centred);

    // --- Etichette del selettore VU ----------------------------------------
    const auto c = meterSelector.getBounds().toFloat().getCentre();
    const juce::StringArray labels { "+10", "+4", "GAIN RED." };

    for (int i = 0; i < labels.size(); ++i)
    {
        const float a   = pi * 1.75f + pi * 0.25f * (float) i;
        const auto  dir = directionFor (a);

        // Puntino di riferimento
        g.setColour (inkColour);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (c + dir * 34.0f));

        // Testo spostato verso l'esterno per non toccare la manopola
        const auto pos = c + dir * 44.0f + juce::Point<float> (dir.x * 18.0f, 0.0f);
        drawEngravedText (g, labels[i], juce::Rectangle<float> (70.0f, 14.0f).withCentre (pos),
                          11.0f, juce::Justification::centred);
    }

    drawEngravedText (g, "METER", juce::Rectangle<float> (120.0f, 14.0f).withCentre (c.translated (0.0f, 42.0f)),
                      11.0f, juce::Justification::centred);
}

void LA2AAudioProcessorEditor::resized()
{
    if (panelTexture.getWidth() != getWidth() || panelTexture.getHeight() != getHeight())
        createPanelTexture();

    // Logo in alto a sinistra, a destra della vite d'angolo e sopra la linea
    // di separazione (y = 66). Altezza 42 px, larghezza dalle proporzioni.
    if (logoImage.isValid())
    {
        constexpr float logoHeight = 42.0f;
        const float aspect = (float) logoImage.getWidth() / (float) logoImage.getHeight();
        logoArea = { 44.0f, 14.0f, logoHeight * aspect, logoHeight };
    }

    // Comandi dei preset in alto a destra: [ menu ] [SALVA] [...]
    {
        constexpr int rowY = 23, rowH = 26, gap = 6;
        int right = getWidth() - 44;

        presetMenuButton.setBounds (right - 30, rowY, 30, rowH);
        right -= 30 + gap;
        savePresetButton.setBounds (right - 60, rowY, 60, rowH);
        right -= 60 + gap;
        presetBox.setBounds (right - 190, rowY, 190, rowH);
    }

    auto area = getLocalBounds().reduced (30, 16);
    area.removeFromTop (56);      // intestazione
    area.removeFromBottom (34);   // etichette sotto le manopole

    // Disposizione orizzontale: GAIN | VU + selettore | PEAK REDUCTION
    auto leftArea  = area.removeFromLeft (230);
    auto rightArea = area.removeFromRight (230);

    gainKnob.setBounds (leftArea.withSizeKeepingCentre (210, 210));
    peakReductionKnob.setBounds (rightArea.withSizeKeepingCentre (210, 210));

    auto centreArea = area;
    const int vuWidth = juce::jmin (320, centreArea.getWidth());
    vuMeter.setBounds (centreArea.removeFromTop (170).withSizeKeepingCentre (vuWidth, 170));

    meterSelector.setBounds (centreArea.withSizeKeepingCentre (58, 58).translated (0, 4));
}

//==============================================================================
void LA2AAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float  dt  = juce::jlimit (0.0f, 0.1f, (float) ((now - lastTimerMs) * 0.001));
    lastTimerMs = now;

    const int mode = juce::roundToInt (processorRef.apvts.getRawParameterValue (ParamIDs::meterMode)->load());
    float reading = 0.0f;

    switch (mode)
    {
        case 0:  // Uscita con riferimento +10: strumento 10 dB meno sensibile
            reading = processorRef.getOutputLevelDb() + referenceOffsetDb - 10.0f;
            break;

        case 1:  // Uscita con riferimento +4
            reading = processorRef.getOutputLevelDb() + referenceOffsetDb;
            break;

        default: // Riduzione di guadagno: la lancetta parte da 0 VU e scende
            reading = -processorRef.getGainReductionDb();
            break;
    }

    vuMeter.setReading (reading);
    vuMeter.advanceBallistics (dt);

    // Nome del preset (anche se cambiato dalla DAW) e segnalazione dei ritocchi
    updatePresetDisplay();
}

//==============================================================================
//  Gestione dei preset
//==============================================================================
void LA2AAudioProcessorEditor::refreshPresetList()
{
    auto& pm = processorRef.getPresetManager();
    userPresetNames = pm.getUserPresetNames();

    presetBox.clear (juce::dontSendNotification);

    presetBox.addSectionHeading ("Fabbrica");
    for (int i = 0; i < PresetManager::getNumFactoryPresets(); ++i)
        presetBox.addItem (PresetManager::getFactoryPresetName (i), factoryIdBase + i);

    presetBox.addSeparator();
    presetBox.addSectionHeading ("Utente");

    if (userPresetNames.isEmpty())
    {
        presetBox.addItem ("(nessun preset salvato)", emptyUserId);
        presetBox.setItemEnabled (emptyUserId, false);
    }
    else
    {
        for (int i = 0; i < userPresetNames.size(); ++i)
            presetBox.addItem (userPresetNames[i], userIdBase + i);
    }

    updatePresetDisplay();
}

void LA2AAudioProcessorEditor::updatePresetDisplay()
{
    auto& pm = processorRef.getPresetManager();
    const auto selection = pm.getSelection();
    const bool modified  = pm.isModified();

    // Voce del menu che corrisponde al preset corrente (0 = nessuna,
    // per esempio un preset utente eliminato dalla cartella)
    int id = 0;
    if (selection.isUser)
    {
        const int index = userPresetNames.indexOf (selection.userName);
        if (index >= 0)
            id = userIdBase + index;
    }
    else
    {
        id = factoryIdBase + selection.factoryIndex;
    }

    if (! modified && id != 0)
    {
        if (presetBox.getSelectedId() != id)
            presetBox.setSelectedId (id, juce::dontSendNotification);
    }
    else
    {
        // Testo libero: nessuna voce selezionata, cosi' riscegliere lo
        // stesso preset dal menu lo ricarica
        const auto text = pm.getCurrentPresetName() + (modified ? " *" : "");
        if (presetBox.getText() != text || presetBox.getSelectedId() != 0)
            presetBox.setText (text, juce::dontSendNotification);
    }
}

void LA2AAudioProcessorEditor::onPresetChosen()
{
    const int id = presetBox.getSelectedId();
    auto& pm = processorRef.getPresetManager();

    if (id >= userIdBase)
    {
        const int index = id - userIdBase;
        if (juce::isPositiveAndBelow (index, userPresetNames.size())
             && ! pm.loadUserPreset (userPresetNames[index]))
        {
            showMessage ("Preset non caricato",
                         "Il file del preset \"" + userPresetNames[index] + "\" non esiste piu' o non e' valido.");
            refreshPresetList();
        }
    }
    else if (id >= factoryIdBase && id < factoryIdBase + PresetManager::getNumFactoryPresets())
    {
        // Passa dal processore, cosi' la DAW viene avvisata del cambio di programma
        processorRef.setCurrentProgram (id - factoryIdBase);
    }

    updatePresetDisplay();
}

void LA2AAudioProcessorEditor::savePreset()
{
    const auto selection = processorRef.getPresetManager().getSelection();
    const auto suggestion = selection.isUser ? selection.userName : juce::String();

    showDialog ("Salva preset", "Nome del preset utente:", true, suggestion, "Salva", "Annulla",
                [this] (juce::String name)
    {
        auto& pm = processorRef.getPresetManager();
        const auto clean = PresetManager::sanitiseName (name);

        if (clean.isEmpty())
        {
            showMessage ("Nome non valido", "Inserisci un nome per il preset.");
            return;
        }

        auto doSave = [this, clean]
        {
            if (processorRef.getPresetManager().saveUserPreset (clean))
                refreshPresetList();
            else
                showMessage ("Salvataggio non riuscito",
                             "Impossibile scrivere nella cartella dei preset:\n"
                             + processorRef.getPresetManager().getUserPresetFolder().getFullPathName());
        };

        if (pm.userPresetExists (clean))
            showDialog ("Sovrascrivere il preset?",
                        "Esiste gia' un preset chiamato \"" + clean + "\".",
                        false, {}, "Sovrascrivi", "Annulla",
                        [doSave] (juce::String) { doSave(); });
        else
            doSave();
    });
}

void LA2AAudioProcessorEditor::showPresetMenu()
{
    auto& pm = processorRef.getPresetManager();
    const auto selection = pm.getSelection();
    const bool isUserPreset = selection.isUser && pm.userPresetExists (selection.userName);

    juce::PopupMenu menu;
    menu.setLookAndFeel (&vintageLnf);
    menu.addItem (1, "Ricarica \"" + pm.getCurrentPresetName() + "\"", pm.isModified());
    menu.addSeparator();
    menu.addItem (2, "Rinomina...", isUserPreset);
    menu.addItem (3, "Elimina", isUserPreset);
    menu.addSeparator();
    menu.addItem (4, "Importa preset...");
    menu.addItem (5, "Esporta impostazioni correnti...");
    menu.addSeparator();
    menu.addItem (6, "Apri la cartella dei preset");

    juce::Component::SafePointer<LA2AAudioProcessorEditor> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetMenuButton),
                        [safeThis] (int result)
    {
        if (safeThis == nullptr)
            return;

        auto& editor = *safeThis;
        auto& manager = editor.processorRef.getPresetManager();

        switch (result)
        {
            case 1: manager.reloadCurrentPreset(); break;
            case 2: editor.renameCurrentPreset();  break;
            case 3: editor.deleteCurrentPreset();  break;
            case 4: editor.importPresets();        break;
            case 5: editor.exportPreset();         break;
            case 6:
            {
                const auto folder = manager.getUserPresetFolder();
                folder.createDirectory();
                folder.startAsProcess();   // apre la cartella in Esplora file
                break;
            }
            default: break;   // menu chiuso senza scelta
        }
    });
}

void LA2AAudioProcessorEditor::renameCurrentPreset()
{
    const auto oldName = processorRef.getPresetManager().getSelection().userName;

    showDialog ("Rinomina preset", "Nuovo nome:", true, oldName, "Rinomina", "Annulla",
                [this, oldName] (juce::String newName)
    {
        if (! processorRef.getPresetManager().renameUserPreset (oldName, newName))
            showMessage ("Impossibile rinominare",
                         "Il nome non e' valido oppure esiste gia' un preset con questo nome.");
        refreshPresetList();
    });
}

void LA2AAudioProcessorEditor::deleteCurrentPreset()
{
    const auto name = processorRef.getPresetManager().getSelection().userName;

    showDialog ("Eliminare il preset?",
                "Il preset \"" + name + "\" verra' eliminato definitivamente.\n"
                "Le impostazioni correnti del plugin non cambiano.",
                false, {}, "Elimina", "Annulla",
                [this, name] (juce::String)
    {
        if (! processorRef.getPresetManager().deleteUserPreset (name))
            showMessage ("Eliminazione non riuscita", "Impossibile eliminare il file del preset.");
        refreshPresetList();
    });
}

void LA2AAudioProcessorEditor::importPresets()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Importa preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        juce::String ("*") + PresetManager::fileExtension);

    juce::Component::SafePointer<LA2AAudioProcessorEditor> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto files = chooser.getResults();
        if (files.isEmpty())
            return;   // annullato

        const int imported = safeThis->processorRef.getPresetManager().importPresetFiles (files);
        safeThis->refreshPresetList();

        safeThis->showMessage ("Importazione completata",
                               juce::String (imported) + " preset importati su " + juce::String (files.size())
                               + (imported < files.size() ? ".\nI file non validi sono stati ignorati." : "."));
    });
}

void LA2AAudioProcessorEditor::exportPreset()
{
    auto& pm = processorRef.getPresetManager();
    const auto defaultFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                 .getChildFile (PresetManager::sanitiseName (pm.getCurrentPresetName())
                                                + PresetManager::fileExtension);

    fileChooser = std::make_unique<juce::FileChooser> ("Esporta preset", defaultFile,
                                                       juce::String ("*") + PresetManager::fileExtension);

    juce::Component::SafePointer<LA2AAudioProcessorEditor> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();
        if (file == juce::File())
            return;   // annullato

        if (! safeThis->processorRef.getPresetManager().exportCurrentSettings (file))
            safeThis->showMessage ("Esportazione non riuscita", "Impossibile scrivere il file:\n" + file.getFullPathName());
    });
}

//==============================================================================
void LA2AAudioProcessorEditor::showDialog (const juce::String& title, const juce::String& message,
                                           bool withNameField, const juce::String& initialText,
                                           const juce::String& confirmText, const juce::String& cancelText,
                                           std::function<void (juce::String)> onConfirmed)
{
    presetDialog = std::make_unique<juce::AlertWindow> (title, message, juce::MessageBoxIconType::NoIcon);

    if (withNameField)
        presetDialog->addTextEditor ("nome", initialText);

    presetDialog->addButton (confirmText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    if (cancelText.isNotEmpty())
        presetDialog->addButton (cancelText, 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // La finestra resta dentro il plugin: evita che finisca dietro la DAW
    addAndMakeVisible (*presetDialog);
    presetDialog->setCentrePosition (getLocalBounds().getCentre());

    juce::Component::SafePointer<LA2AAudioProcessorEditor> safeThis (this);

    presetDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis, withNameField, onConfirmed] (int result)
    {
        if (safeThis == nullptr || safeThis->presetDialog == nullptr)
            return;

        const auto text = withNameField ? safeThis->presetDialog->getTextEditorContents ("nome")
                                        : juce::String();

        // La finestra viene distrutta dopo la chiamata, non durante:
        // cosi' onConfirmed puo' aprire subito un'altra finestra
        std::shared_ptr<juce::AlertWindow> finished (safeThis->presetDialog.release());
        finished->setVisible (false);
        juce::MessageManager::callAsync ([finished] {});

        if (result == 1 && onConfirmed != nullptr)
            onConfirmed (text);
    }), false);
}

void LA2AAudioProcessorEditor::showMessage (const juce::String& title, const juce::String& message)
{
    showDialog (title, message, false, {}, "OK", {}, nullptr);
}
