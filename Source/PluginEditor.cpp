#include "PluginEditor.h"

using namespace juce;
using namespace tbp;

namespace
{
    constexpr int kWidth = TightBottomPanel::kBaseWidth, kHeight = TightBottomPanel::kBaseHeight;
    constexpr int kTopStripH = 34, kBottomBarY = 328;

    // centre panel pieces
    const Rectangle<float> grilleLeft  { 216.0f, 122.0f, 60.0f, 58.0f };
    const Rectangle<float> grilleRight { 324.0f, 122.0f, 60.0f, 58.0f };
    const Point<float> sCentre { 300.0f, 150.0f };
    const Rectangle<float> statusBox   { 70.0f, 335.0f, 460.0f, 22.0f };
}

//==============================================================================
TightBottomPanel::TightBottomPanel (TightBottomAudioProcessor& p)
    : processor (p),
      sButton (*p.apvts.getParameter (ParamIDs::sMode))
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    setSize (kWidth, kHeight);
    createBackground();

    auto& apvts = processor.apvts;

    // Knobs and sliders -------------------------------------------------------------
    const std::pair<StepSlider*, const char*> sliders[] =
    {
        { &tightKnob, ParamIDs::tightLoose }, { &lowEndKnob, ParamIDs::lowEnd },
        { &inKnob, ParamIDs::inGain },        { &outKnob, ParamIDs::outGain },
        { &crossoverSlider, ParamIDs::crossover }, { &releaseSlider, ParamIDs::release },
    };
    for (auto& [s, id] : sliders)
    {
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (*apvts.getParameter (id), *s));
        addAndMakeVisible (*s);
    }

    tightKnob.displayValue  = [] (double v) { return describeTightLoose (v); };
    tightKnob.describeValue = [this] (double v) { return describeMacro (v, param (ParamIDs::release)).replace (", ", "\n"); };
    for (VirusKnob* k : { &lowEndKnob, &inKnob, &outKnob })
        k->displayValue = [] (double v) { return formatDb (v); };
    lowEndKnob.describeValue = [] (double) { return String ("Adds (or removes) low-end punch after the compressor.\nWith Auto Punch on, this adds even more on top."); };
    inKnob.describeValue     = [] (double) { return String ("Input trim"); };
    outKnob.describeValue    = [] (double) { return String ("Output trim (after Gain Match)"); };

    crossoverSlider.displayValue = [this] (double v) { return formatHz (crossoverHz (v, param (ParamIDs::xoverX2) > 0.5f)); };
    crossoverSlider.describeValue = [] (double) { return String ("Low/high split (Linkwitz-Riley 24 dB/oct). x2 doubles the range"); };
    releaseSlider.displayValue  = [] (double v) { return "x" + String (sliderToReleaseScale (v), 2); };
    releaseSlider.describeValue = [this] (double v)
    {
        const auto m = tb::macroSettings (knobToTightness (param (ParamIDs::tightLoose)), sliderToReleaseScale (v));
        return "Release now " + formatMs (m.releaseMs) + " (FAST x" + String (voicing::releaseScaleFast, 1)
             + " .. SLOW x" + String (voicing::releaseScaleSlow, 1) + ")";
    };

    // Buttons ------------------------------------------------------------------------
    const std::tuple<TextButton*, const char*, const char*> buttons[] =
    {
        { &x2Button,        ParamIDs::xoverX2,   "x2: doubles the crossover range (120 - 240 Hz)" },
        { &autoPunchButton, ParamIDs::autoPunch, "A (Auto Punch): puts the punch back automatically, so Loose/Tight changes the feel, not the level. PUNCH adds more on top" },
        { &gainMatchButton, ParamIDs::gainMatch, "Gain Match: output follows the input loudness (K-weighted, about 3 s) for fair A/B" },
        { &bypassButton,    ParamIDs::bypass,    "Bypass (click-free)" },
    };
    for (auto& [button, id, tip] : buttons)
    {
        button->setClickingTogglesState (true);
        button->setTooltip (tip);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (apvts, id, *button));
        addAndMakeVisible (*button);
    }
    x2Button.onClick = [this] { repaint(); };

    addAndMakeVisible (sButton);

    for (Component* c : { (Component*) &tightLcd, (Component*) &lowEndLcd, (Component*) &macroLcd,
                          (Component*) &grMeter, (Component*) &gainMeter, (Component*) &inMeter, (Component*) &outMeter })
    {
        c->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*c);
    }

    helpButton.setTooltip ("Help");
    helpButton.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    addAndMakeVisible (helpButton);
    addChildComponent (helpOverlay);

    lastTimerMs = Time::getMillisecondCounterHiRes();
    timerCallback();
    startTimerHz (30);
}

TightBottomPanel::~TightBottomPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

float TightBottomPanel::param (const char* id) const
{
    return processor.apvts.getRawParameterValue (id)->load();
}

//==============================================================================
void TightBottomPanel::resized()
{
    leftPanel    = { 8.0f,   40.0f, 184.0f, 212.0f };
    centrePanel  = { 198.0f, 40.0f, 204.0f, 212.0f };
    rightPanel   = { 408.0f, 40.0f, 184.0f, 212.0f };
    inPanel      = { 8.0f,   258.0f, 140.0f, 64.0f };
    xoverPanel   = { 154.0f, 258.0f, 154.0f, 64.0f };
    releasePanel = { 314.0f, 258.0f, 132.0f, 64.0f };
    outPanel     = { 452.0f, 258.0f, 140.0f, 64.0f };

    // top corner: A (Auto Punch), Gain Match, Bypass
    autoPunchButton.setBounds (352, 8, 70, 19);
    gainMatchButton.setBounds (428, 8, 86, 19);
    bypassButton.setBounds (520, 8, 72, 19);

    // TIGHT / LOOSE
    grMeter.setBounds (16, 66, 40, 146);
    tightKnob.setBounds (62, 72, 120, 120);
    tightLcd.setBounds (56, 218, 130, 22);

    // LOW BAND
    gainMeter.setBounds (206, 62, 190, 52);
    sButton.setBounds (268, 118, 64, 64);
    macroLcd.setBounds (206, 218, 188, 22);

    // LOW END
    lowEndKnob.setBounds (440, 72, 120, 120);
    lowEndLcd.setBounds (422, 218, 156, 22);

    // bottom row
    inKnob.setBounds (12, 276, 44, 44);
    inMeter.setBounds (60, 286, 82, 20);
    crossoverSlider.setBounds (160, 279, 110, 18);
    x2Button.setBounds (276, 279, 26, 18);
    releaseSlider.setBounds (320, 279, 120, 18);
    outMeter.setBounds (458, 286, 82, 20);
    outKnob.setBounds (544, 276, 44, 44);

    // bottom bar
    helpButton.setBounds (kWidth - 28, 336, 20, 20);
    helpOverlay.setBounds (getLocalBounds());
}

//==============================================================================
void TightBottomPanel::createBackground()
{
    // Rendered at 2x so it stays sharp when the window is enlarged (same brushed
    // metal as Virus Delay).
    background = Image (Image::RGB, kWidth * 2, kHeight * 2, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (2.0f));

    g.setGradientFill (ColourGradient (Colour (0xff3b3e42), 0.0f, 0.0f, Colour (0xff1e2023), 0.0f, (float) kHeight, false));
    g.fillAll();

    Random rng (0x7a11);
    for (int y = 0; y < kHeight; ++y)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.035f));
        g.fillRect (0, y, kWidth, 1);
    }
    for (int i = 0; i < 1700; ++i)
    {
        const int x = rng.nextInt (kWidth), y = rng.nextInt (kHeight), len = 20 + rng.nextInt (140);
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.015f + rng.nextFloat() * 0.03f));
        g.fillRect (x, y, len, 1);
    }

    ColourGradient vignette (Colours::transparentBlack, kWidth * 0.5f, kHeight * 0.45f,
                             Colours::black.withAlpha (0.35f), 0.0f, 0.0f, true);
    g.setGradientFill (vignette);
    g.fillAll();
}

void TightBottomPanel::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Original stylised lowercase wordmark: chrome "tight", blue "bottom", and a swoosh.
    const float baseline = area.getBottom() - 7.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (27.0f, Font::bold | Font::italic)), "tight", area.getX(), baseline);
    Path mainPath;
    main.createPath (mainPath);
    const auto mainBounds = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mainBounds.getY(), Colour (0xff8c949c), 0.0f, mainBounds.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement second;
    second.addLineOfText (Font (FontOptions (19.0f, Font::plain)), "bottom", mainBounds.getRight() + 6.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (VirusColours::blue);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (VirusColours::blue.withAlpha (0.0f), sx, sy, VirusColours::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void TightBottomPanel::paint (Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    // --- top strip ----------------------------------------------------------------
    auto top = Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kTopStripH);
    g.setGradientFill (ColourGradient (Colour (0xff4a4e53), 0.0f, 0.0f, Colour (0xff2b2e32), 0.0f, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 0.0f, (float) kWidth);
    g.setColour (VirusColours::bevelDark);
    g.drawHorizontalLine (kTopStripH - 1, 0.0f, (float) kWidth);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kTopStripH, 0.0f, (float) kWidth);

    drawWordmark (g, { 14.0f, 0.0f, 260.0f, (float) kTopStripH });

    // --- main panels ------------------------------------------------------------------
    CustomLookAndFeel::drawPanel (g, leftPanel,   "Loose / Tight");
    CustomLookAndFeel::drawPanel (g, centrePanel, "Low Band");
    CustomLookAndFeel::drawPanel (g, rightPanel,  "Punch");

    // Tight / Loose: end labels under the knob, GR label
    CustomLookAndFeel::drawSmallLabel (g, "Loose", { 52.0f, 188.0f, 64.0f, 14.0f });
    CustomLookAndFeel::drawSmallLabel (g, "Tight", { 128.0f, 188.0f, 64.0f, 14.0f });
    CustomLookAndFeel::drawSmallLabel (g, "GR", { 12.0f, 222.0f, 28.0f, 14.0f }, Justification::centredLeft);

    // Low End: -10 / 0 / +10
    CustomLookAndFeel::drawSmallLabel (g, "0", { 485.0f, 60.0f, 30.0f, 13.0f });
    CustomLookAndFeel::drawSmallLabel (g, String (roundToInt (voicing::lowEndMinDb)), { 430.0f, 188.0f, 64.0f, 14.0f });
    CustomLookAndFeel::drawSmallLabel (g, "+" + String (roundToInt (voicing::lowEndMaxDb)), { 506.0f, 188.0f, 64.0f, 14.0f });

    // Low band: separator under the meter, speaker grilles round the S button, S mode text
    g.setColour (VirusColours::bevelDark);
    g.drawHorizontalLine (116, 210.0f, 390.0f);
    g.setColour (Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine (117, 210.0f, 390.0f);
    CustomLookAndFeel::drawGrille (g, grilleLeft, sCentre, 40.0f);
    CustomLookAndFeel::drawGrille (g, grilleRight, sCentre, 40.0f);

    const int sMode = jlimit (0, 2, roundToInt (param (ParamIDs::sMode)));
    const String sText[] = { "S Enhancer  Off", "S1  Dull", "S2  Bright" };
    g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
    g.setColour (sMode > 0 ? VirusColours::blue : VirusColours::labelText);
    g.drawText (sText[sMode].toUpperCase(), Rectangle<float> (206.0f, 188.0f, 188.0f, 14.0f), Justification::centred);

    // --- bottom row -------------------------------------------------------------------
    CustomLookAndFeel::drawPanel (g, inPanel,      "Input");
    CustomLookAndFeel::drawPanel (g, xoverPanel,   "Crossover");
    CustomLookAndFeel::drawPanel (g, releasePanel, "Release");
    CustomLookAndFeel::drawPanel (g, outPanel,     "Output");

    const bool x2 = param (ParamIDs::xoverX2) > 0.5f;
    const double f = x2 ? voicing::crossoverX2Factor : 1.0;
    CustomLookAndFeel::drawSmallLabel (g, String (roundToInt (voicing::crossoverSliderMinHz * f)), { 160.0f, 300.0f, 40.0f, 14.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, String (roundToInt (voicing::crossoverSliderMaxHz * f)), { 230.0f, 300.0f, 40.0f, 14.0f }, Justification::centredRight);
    {
        // live values between the end labels
        g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
        g.setColour (VirusColours::blue);
        g.drawText (formatHz (crossoverHz (param (ParamIDs::crossover), x2)), Rectangle<float> (185.0f, 300.0f, 60.0f, 14.0f), Justification::centred);
        g.drawText ("x" + String (sliderToReleaseScale (param (ParamIDs::release)), 2), Rectangle<float> (350.0f, 300.0f, 60.0f, 14.0f), Justification::centred);
    }
    CustomLookAndFeel::drawSmallLabel (g, "Fast", { 320.0f, 300.0f, 50.0f, 14.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Slow", { 390.0f, 300.0f, 50.0f, 14.0f }, Justification::centredRight);
    CustomLookAndFeel::drawSmallLabel (g, "L / R", { 60.0f, 306.0f, 82.0f, 14.0f });
    CustomLookAndFeel::drawSmallLabel (g, "L / R", { 458.0f, 306.0f, 82.0f, 14.0f });

    // --- bottom bar -------------------------------------------------------------------
    auto bar = Rectangle<float> (0.0f, (float) kBottomBarY, (float) kWidth, (float) (kHeight - kBottomBarY));
    g.setGradientFill (ColourGradient (Colour (0xff17191b), 0.0f, bar.getY(), Colour (0xff0c0d0e), 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine (kBottomBarY, 0.0f, (float) kWidth);

    CustomLookAndFeel::drawInsetBox (g, statusBox, 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
    g.setColour (Colour (0xffc8cdd2));
    const bool showMessage = Time::getMillisecondCounterHiRes() < statusUntilMs;
    if (showMessage) g.setColour (VirusColours::blue);
    g.drawText (showMessage ? statusMessage : String ("TIGHT BOTTOM"), statusBox, Justification::centred);
}

//==============================================================================
void TightBottomPanel::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = (float) jlimit (0.0, 0.1, (now - lastTimerMs) * 0.001);
    lastTimerMs = now;

    auto& m = processor.engine.meters;
    inMeter.setLevels  (m.inPeak[0].exchange (0.0f),  m.inPeak[1].exchange (0.0f),  dt);
    outMeter.setLevels (m.outPeak[0].exchange (0.0f), m.outPeak[1].exchange (0.0f), dt);

    const bool bypassed = param (ParamIDs::bypass) > 0.5f;
    const float gr = bypassed ? 0.0f : m.grDb.load();
    grMeter.setGainReduction (gr, dt);
    gainMeter.setValues (gr, bypassed ? 0.0f : m.lowBoostDb.load(), bypassed ? 0.0f : m.outDeltaDb.load(), dt);

    const float tl = param (ParamIDs::tightLoose);
    tightLcd.setContent ("FEEL", describeTightLoose (tl));
    lowEndLcd.setContent ("PUNCH", formatDb (param (ParamIDs::lowEnd)));

    const auto macro = tb::macroSettings (knobToTightness (tl), sliderToReleaseScale (param (ParamIDs::release)));
    macroLcd.setContent ({}, "ATT " + formatMs (macro.attackMs) + "  REL " + formatMs (macro.releaseMs)
                             + "  " + String (macro.ratio, 1) + ":1");

    // repaint the static labels when S mode or x2 change (also from automation)
    const int labelState = roundToInt (param (ParamIDs::sMode)) * 2 + (param (ParamIDs::xoverX2) > 0.5f ? 1 : 0)
                         + 8 * roundToInt (param (ParamIDs::crossover) * 10.0f) + 8 * 4096 * roundToInt (param (ParamIDs::release) * 10.0f);
    if (statusUntilMs > 0.0 && now >= statusUntilMs)
    {
        statusUntilMs = 0.0;
        repaint (statusBox.toNearestInt());
    }
    if (labelState != lastLabelState)
    {
        lastLabelState = labelState;
        repaint();
    }
}

//==============================================================================
void TightBottomPanel::showStatus (const String& text)
{
    statusMessage = text;
    statusUntilMs = Time::getMillisecondCounterHiRes() + 2500.0;
    repaint (statusBox.toNearestInt());
}

void TightBottomPanel::mouseDoubleClick (const MouseEvent& e)
{
    // Double-click a section name to see exactly where it is set.
    const auto p = e.position;
    const auto header = [] (Rectangle<float> r) { return r.withHeight (18.0f); };
    const bool x2 = param (ParamIDs::xoverX2) > 0.5f;

    if (header (xoverPanel).contains (p))
        showStatus ("CROSSOVER  " + formatHz (crossoverHz (param (ParamIDs::crossover), x2))
                    + "  -  everything below is tightened");
    else if (header (releasePanel).contains (p))
    {
        const auto m = tb::macroSettings (knobToTightness (param (ParamIDs::tightLoose)), sliderToReleaseScale (param (ParamIDs::release)));
        showStatus ("RELEASE  " + formatMs (m.releaseMs));
    }
    else if (header (leftPanel).contains (p))
        showStatus (describeMacro (param (ParamIDs::tightLoose), param (ParamIDs::release)).toUpperCase());
    else if (header (rightPanel).contains (p))
        showStatus ("PUNCH  " + formatDb (param (ParamIDs::lowEnd)));
}

//==============================================================================
// Resizable editor: the panel is laid out at its base size and scaled as a whole.
//==============================================================================
TightBottomAudioProcessorEditor::TightBottomAudioProcessorEditor (TightBottomAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    // Read the remembered size first: setting up the resize limits below briefly
    // resizes the window, and that must not overwrite the saved size.
    const int savedWidth = processor.editorWidth.load();

    addAndMakeVisible (panel);

    const int minW = kWidth * 3 / 4, maxW = kWidth * 2;
    setResizable (true, true);
    setResizeLimits (minW, kHeight * 3 / 4, maxW, kHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);

    const int width = savedWidth > 0 ? jlimit (minW, maxW, savedWidth) : kWidth;
    setSize (width, roundToInt (width * (double) kHeight / (double) kWidth));
    sizeRestored = true;
}

void TightBottomAudioProcessorEditor::resized()
{
    panel.setBounds (0, 0, kWidth, kHeight);
    panel.setTransform (AffineTransform::scale ((float) getWidth() / (float) kWidth));

    if (sizeRestored)
        processor.editorWidth.store (getWidth());
}
