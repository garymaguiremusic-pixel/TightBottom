#include "Components.h"
#include "../CustomLookAndFeel.h"
#include "../Parameters.h"

using namespace juce;

//==============================================================================
// StepSlider
//==============================================================================
StepSlider::StepSlider (Slider::SliderStyle style, const String& labelText)
    : Slider (style, Slider::NoTextBox), label (labelText)
{
    setName (labelText);
    setRange (0.0, 100.0, 0.0);
}

String StepSlider::valueText (double v) const
{
    return displayValue ? displayValue (v) : tbp::formatNumber (v);
}

String StepSlider::getTooltip()
{
    const double v = getValue();
    String text = label.toUpperCase() + "  " + valueText (v);
    if (describeValue)
        text << "\n" << describeValue (v);
    return text;
}

void StepSlider::setValueWithGesture (double v)
{
    if (onGestureBegin) onGestureBegin();
    setValue (jlimit (getMinimum(), getMaximum(), v), sendNotificationSync);
    if (onGestureEnd) onGestureEnd();
}

void StepSlider::mouseDown (const MouseEvent& e)
{
    if (! isEnabled())
        return;

    dragging = true;
    const bool horizontal = getSliderStyle() == LinearHorizontal;
    lastDragPos = horizontal ? e.position.x : e.position.y;
    dragValue = getValue();
    dragShift = e.mods.isShiftDown();
    if (onGestureBegin) onGestureBegin();
    repaint();
}

void StepSlider::mouseDrag (const MouseEvent& e)
{
    if (! isEnabled() || ! dragging)
        return;

    // Incremental drag: whole steps normally, 0.1 steps with Shift. Pressing or
    // releasing Shift mid-drag continues from the current value.
    const bool shift = e.mods.isShiftDown();
    if (shift != dragShift)
    {
        dragShift = shift;
        dragValue = getValue();
    }

    const bool horizontal = getSliderStyle() == LinearHorizontal;
    const float pos = horizontal ? e.position.x : e.position.y;
    const double pixels = horizontal ? (double) (pos - lastDragPos) : (double) (lastDragPos - pos);
    lastDragPos = pos;

    const double fullRangePixels = horizontal ? jmax (60.0, (double) getWidth() - 14.0) : 200.0;
    dragValue += shift ? pixels * 0.05                                                    // 0.1 every 2 pixels
                       : pixels * (getMaximum() - getMinimum()) / fullRangePixels;
    dragValue = jlimit (getMinimum(), getMaximum(), dragValue);

    const double snapped = shift ? std::round (dragValue * 10.0) / 10.0 : std::round (dragValue);
    if (std::abs (snapped - getValue()) > 1.0e-9)
        setValue (snapped, sendNotificationSync);
}

void StepSlider::mouseUp (const MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    if (onGestureEnd) onGestureEnd();
    repaint();
}

void StepSlider::mouseDoubleClick (const MouseEvent&)
{
    if (isEnabled())
        setValueWithGesture (defaultValue);
}

void StepSlider::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (! isEnabled())
        return;

    const float dir = (wheel.isReversed ? -1.0f : 1.0f) * (std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : -wheel.deltaX);
    if (std::abs (dir) < 1.0e-6f)
        return;

    // One notch = 1.0, or 0.1 with Shift.
    const bool shift = e.mods.isShiftDown();
    const double step = shift ? 0.1 : 1.0;
    const double target = getValue() + (dir > 0.0f ? step : -step);
    setValueWithGesture (shift ? std::round (target * 10.0) / 10.0 : std::round (target));
}

void StepSlider::mouseEnter (const MouseEvent&) { hovering = true;  repaint(); }
void StepSlider::mouseExit (const MouseEvent&)  { hovering = false; repaint(); }

//==============================================================================
// VirusKnob / VirusSlider
//==============================================================================
VirusKnob::VirusKnob (Size s, const String& labelText, bool isBipolarKnob)
    : StepSlider (Slider::RotaryVerticalDrag, labelText), knobSize (s), bipolar (isBipolarKnob)
{
    setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
}

void VirusKnob::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float pos = (float) valueToProportionOfLength (getValue());
    const auto rotary = getRotaryParameters();

    Rectangle<float> knobArea;
    if (knobSize == Size::Large)
    {
        const float side = std::min (bounds.getWidth(), bounds.getHeight());
        knobArea = bounds.withSizeKeepingCentre (side, side);
    }
    else
    {
        // Small knobs: label underneath, swapped for the value while hovering/dragging.
        auto labelArea = bounds.removeFromBottom (14.0f);
        const float side = std::min (bounds.getWidth(), bounds.getHeight());
        knobArea = bounds.withSizeKeepingCentre (side, side);

        if ((isHovering() || isDragging()) && isEnabled())
        {
            g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
            g.setColour (VirusColours::blue);
            g.drawFittedText (valueText (getValue()), labelArea.toNearestInt(), Justification::centred, 1, 0.7f);
        }
        else
        {
            CustomLookAndFeel::drawSmallLabel (g, getLabel(), labelArea);
        }
    }

    getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                       (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                       pos, rotary.startAngleRadians, rotary.endAngleRadians, *this);
}

VirusSlider::VirusSlider (const String& labelText)
    : StepSlider (Slider::LinearHorizontal, labelText)
{
}

//==============================================================================
// SliderAttachment
//==============================================================================
SliderAttachment::SliderAttachment (RangedAudioParameter& p, StepSlider& s)
    : slider (s),
      attachment (p, [this] (float v) { slider.setValue (v, dontSendNotification); slider.repaint(); })
{
    const auto range = p.getNormalisableRange();
    slider.setRange (range.start, range.end, 0.0);
    slider.setDefaultValue (p.convertFrom0to1 (p.getDefaultValue()));

    slider.onGestureBegin = [this] { attachment.beginGesture(); };
    slider.onGestureEnd   = [this] { attachment.endGesture(); };
    slider.onValueChange  = [this] { attachment.setValueAsPartOfGesture ((float) slider.getValue()); };

    attachment.sendInitialUpdate();
}

SliderAttachment::~SliderAttachment()
{
    slider.onGestureBegin = nullptr;
    slider.onGestureEnd = nullptr;
    slider.onValueChange = nullptr;
}

//==============================================================================
// SModeButton
//==============================================================================
SModeButton::SModeButton (RangedAudioParameter& p)
    : attachment (p, [this] (float v) { mode = jlimit (0, 2, roundToInt (v)); repaint(); })
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

void SModeButton::mouseDown (const MouseEvent&)
{
    attachment.setValueAsCompleteGesture ((float) ((mode + 1) % 3));
}

String SModeButton::getTooltip()
{
    String t = "S FILTER  " + tbp::getSModeNames()[mode].toUpperCase() + "\n";
    t << "Off: no filtering\n"
      << "1 Clean: 24 dB/oct high-pass at " << roundToInt (voicing::sHighPassHz) << " Hz\n"
      << "2 Shape: high-pass + " << tbp::formatDb (voicing::sBell1GainDb) << " at " << roundToInt (voicing::sBell1Hz)
      << " Hz + " << tbp::formatDb (voicing::sBell2GainDb) << " at " << roundToInt (voicing::sBell2Hz) << " Hz\n"
      << "Click to cycle";
    return t;
}

void SModeButton::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min (b.getWidth(), b.getHeight()) - 2.0f;
    const auto c = b.getCentre();
    const auto outer = Rectangle<float> (d, d).withCentre (c);
    const bool on = mode > 0;

    // recessed bezel
    g.setColour (Colours::black.withAlpha (0.65f));
    g.fillEllipse (outer.translated (0.0f, 1.5f));
    g.setGradientFill (ColourGradient (Colour (0xff15171a), c.x, outer.getY(), Colour (0xff3a3e43), c.x, outer.getBottom(), false));
    g.fillEllipse (outer);

    // glow ring
    const auto ring = outer.reduced (d * 0.07f);
    if (on)
    {
        for (int i = 4; i > 0; --i)
        {
            g.setColour (VirusColours::blueGlow.withAlpha (0.10f * (float) i));
            g.drawEllipse (ring.expanded ((float) i * 1.2f), 1.6f);
        }
        g.setColour (VirusColours::blue);
        g.drawEllipse (ring, 2.4f);
    }
    else
    {
        g.setColour (Colour (0xff1a2633));
        g.drawEllipse (ring, 2.4f);
    }

    // cap
    const auto cap = ring.reduced (d * 0.07f);
    auto top = on ? Colour (0xff2f86e0) : Colour (0xff4a4f55);
    auto bottom = on ? Colour (0xff123f73) : Colour (0xff1f2225);
    if (hover) { top = top.brighter (0.15f); bottom = bottom.brighter (0.15f); }
    g.setGradientFill (ColourGradient (top, c.x, cap.getY(), bottom, c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.18f), c.x, cap.getY(),
                                       Colours::white.withAlpha (0.0f), c.x, cap.getCentreY(), false));
    g.fillEllipse (cap.reduced (1.0f));
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawEllipse (cap, 0.8f);

    // "S" glyph (+ mode digit)
    g.setFont (Font (FontOptions (d * 0.46f, Font::bold | Font::italic)));
    g.setColour (Colours::black.withAlpha (0.45f));
    g.drawText ("S", cap.translated (0.0f, 1.0f), Justification::centred);
    g.setColour (on ? Colours::white : Colour (0xff9aa1a8));
    g.drawText ("S", cap, Justification::centred);

    if (on)
    {
        const auto badge = Rectangle<float> (d * 0.28f, d * 0.28f).withCentre ({ cap.getRight() - d * 0.08f, cap.getBottom() - d * 0.08f });
        g.setColour (Colour (0xff0b0c0e));
        g.fillEllipse (badge);
        g.setColour (VirusColours::blue);
        g.drawEllipse (badge, 1.0f);
        g.setColour (Colours::white);
        g.setFont (CustomLookAndFeel::uiFont (d * 0.2f, true));
        g.drawText (String (mode), badge, Justification::centred);
    }
}

//==============================================================================
// LcdDisplay
//==============================================================================
void LcdDisplay::setContent (const String& name, const String& value)
{
    if (name != nameText || value != valueText)
    {
        nameText = name;
        valueText = value;
        repaint();
    }
}

void LcdDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    CustomLookAndFeel::drawLcd (g, b, false);

    auto textArea = b.reduced (6.0f, 0.0f);
    const auto font = CustomLookAndFeel::uiFont (11.5f, true);
    g.setFont (font);

    const float valueWidth = GlyphArrangement::getStringWidth (font, valueText) + 4.0f;
    auto nameArea = textArea.withTrimmedRight (valueWidth).toNearestInt();
    const auto valueJust = nameText.isEmpty() ? Justification::centred : Justification::centredRight;

    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawFittedText (nameText, nameArea.translated (0, 1), Justification::centredLeft, 1, 0.75f);
    g.drawFittedText (valueText, textArea.translated (0.0f, 1.0f).toNearestInt(), valueJust, 1, 0.75f);
    g.setColour (Colours::white);
    g.drawFittedText (nameText, nameArea, Justification::centredLeft, 1, 0.75f);
    g.drawFittedText (valueText, textArea.toNearestInt(), valueJust, 1, 0.75f);
}

//==============================================================================
// Meters
//==============================================================================
namespace
{
    float gainToDbF (float g) { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }

    void fillBar (Graphics& g, Rectangle<float> bar, Colour colour)
    {
        if (bar.getWidth() <= 0.0f || bar.getHeight() <= 0.0f)
            return;
        g.setColour (colour.withAlpha (0.25f));
        g.fillRect (bar.expanded (0.0f, 1.5f));
        g.setGradientFill (ColourGradient (colour.brighter (0.35f), 0.0f, bar.getY(), colour.darker (0.25f), 0.0f, bar.getBottom(), false));
        g.fillRect (bar);
    }

    void drawDot (Graphics& g, Point<float> p)
    {
        g.setColour (VirusColours::blue.withAlpha (0.45f));
        g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (p));
        g.setColour (Colours::white);
        g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (p));
    }
}

void PeakMeter::setLevels (float peakL, float peakR, float dt)
{
    const float in[2] = { gainToDbF (peakL), gainToDbF (peakR) };
    for (int c = 0; c < 2; ++c)
    {
        // instant attack, ~20 dB/s fall; peak dot holds 1.5 s
        shown[c] = std::max (in[c], shown[c] - 20.0f * dt);
        holdTime[c] += dt;
        if (in[c] >= hold[c])          { hold[c] = in[c]; holdTime[c] = 0.0f; }
        else if (holdTime[c] > 1.5f)   hold[c] = shown[c];
    }
    repaint();
}

void PeakMeter::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    CustomLookAndFeel::drawInsetBox (g, b, 2.5f);

    constexpr float minDb = -48.0f, maxDb = 6.0f;
    auto inner = b.reduced (4.0f, 3.0f);
    const auto xOf = [&] (float db) { return inner.getX() + inner.getWidth() * (jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb); };

    // 0 dB mark
    g.setColour (Colours::white.withAlpha (0.25f));
    g.drawVerticalLine ((int) xOf (0.0f), inner.getY(), inner.getBottom());

    const float rowH = (inner.getHeight() - 2.0f) * 0.5f;
    for (int c = 0; c < 2; ++c)
    {
        const auto row = Rectangle<float> (inner.getX(), inner.getY() + (float) c * (rowH + 2.0f), inner.getWidth(), rowH);
        g.setColour (Colour (0xff1a2633));
        g.fillRect (row);

        const float x = xOf (shown[c]);
        fillBar (g, row.withRight (std::min (x, xOf (-6.0f))), VirusColours::blueGlow);
        if (x > xOf (-6.0f)) fillBar (g, row.withLeft (xOf (-6.0f)).withRight (std::min (x, xOf (0.0f))), VirusColours::blue.brighter (0.5f));
        if (x > xOf (0.0f))  fillBar (g, row.withLeft (xOf (0.0f)).withRight (x), VirusColours::red);

        if (hold[c] > minDb)
        {
            g.setColour (hold[c] > 0.0f ? VirusColours::red : Colours::white);
            g.fillRect (Rectangle<float> (xOf (hold[c]) - 1.0f, row.getY(), 2.0f, row.getHeight()));
        }
    }
}

void GrMeter::setGainReduction (float grDb, float dt)
{
    const float v = jlimit (-12.0f, 0.0f, grDb);
    shown = std::min (v, shown + 24.0f * dt);   // falls back at 24 dB/s
    holdTime += dt;
    if (v <= hold)              { hold = v; holdTime = 0.0f; }
    else if (holdTime > 1.5f)   hold = shown;
    repaint();
}

void GrMeter::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto barArea = b.removeFromLeft (12.0f);
    CustomLookAndFeel::drawInsetBox (g, barArea, 2.0f);
    auto inner = barArea.reduced (2.5f, 3.0f);
    const auto yOf = [&] (float db) { return inner.getY() + inner.getHeight() * (-db / 12.0f); };

    g.setColour (Colour (0xff1a2633));
    g.fillRect (inner);

    if (shown < -0.05f)
    {
        auto bar = inner.withBottom (yOf (shown));
        g.setColour (VirusColours::blueGlow.withAlpha (0.3f));
        g.fillRect (bar.expanded (1.0f, 0.0f));
        g.setGradientFill (ColourGradient (VirusColours::blue.brighter (0.3f), bar.getX(), 0.0f, VirusColours::blueGlow, bar.getRight(), 0.0f, false));
        g.fillRect (bar);
    }
    if (hold < -0.05f)
    {
        g.setColour (Colours::white);
        g.fillRect (inner.getX(), yOf (hold) - 1.0f, inner.getWidth(), 2.0f);
    }

    // scale
    g.setFont (CustomLookAndFeel::uiFont (11.0f, false));
    for (int db = 0; db >= -12; db -= 3)
    {
        const float y = yOf ((float) db);
        g.setColour (VirusColours::labelText);
        g.drawText (String (db), Rectangle<float> (b.getX() + 6.0f, y - 6.0f, b.getWidth() - 6.0f, 12.0f), Justification::centredLeft);
    }
}

void GainChangeMeter::setValues (float grDb, float boostDb, float outDeltaDb, float dt)
{
    const float k = 1.0f - std::exp (-dt / 0.06f);   // light visual smoothing
    gr    += k * (jlimit (-10.0f, 0.0f, grDb) - gr);
    boost += k * (jlimit (-10.0f, 10.0f, boostDb) - boost);
    delta += k * (jlimit (-10.0f, 10.0f, outDeltaDb) - delta);
    repaint();
}

void GainChangeMeter::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto scale = b.removeFromTop (20.0f);
    constexpr float labelW = 30.0f;
    const auto barsX = b.getX() + labelW, barsW = b.getWidth() - labelW - 12.0f;
    const auto xOf = [&] (float db) { return barsX + barsW * (jlimit (-10.0f, 10.0f, db) + 10.0f) / 20.0f; };

    // ticks + numbers (every 2 dB ticks, numbers every 5 dB)
    g.setFont (CustomLookAndFeel::uiFont (11.0f, false));
    for (int db = -10; db <= 10; db += 1)
    {
        const float x = xOf ((float) db);
        const bool major = db % 5 == 0;
        g.setColour (Colours::white.withAlpha (major ? 0.55f : 0.25f));
        g.drawVerticalLine ((int) x, scale.getBottom() - (major ? 6.0f : 3.0f), scale.getBottom());
        if (major)
        {
            g.setColour (VirusColours::labelText);
            g.drawText (db > 0 ? "+" + String (db) : String (db), Rectangle<float> (x - 15.0f, scale.getY(), 30.0f, 12.0f), Justification::centred);
        }
    }

    const float rowH = 9.0f, gap = 6.0f;
    const char* labels[] = { "LOW", "OUT" };
    for (int r = 0; r < 2; ++r)
    {
        const auto row = Rectangle<float> (barsX, b.getY() + 3.0f + (float) r * (rowH + gap), barsW, rowH);
        CustomLookAndFeel::drawSmallLabel (g, labels[r], Rectangle<float> (b.getX(), row.getY() - 3.0f, labelW - 4.0f, rowH + 6.0f), Justification::centredLeft);
        CustomLookAndFeel::drawInsetBox (g, row.expanded (2.0f, 2.0f), 2.0f);
        g.setColour (Colour (0xff1a2633));
        g.fillRect (row);
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawVerticalLine ((int) xOf (0.0f), row.getY() - 1.0f, row.getBottom() + 1.0f);

        const float x0 = xOf (0.0f);
        if (r == 0)
        {
            fillBar (g, row.withLeft (xOf (gr)).withRight (x0), VirusColours::blue);
            if (boost > 0.0f) fillBar (g, row.withLeft (x0).withRight (xOf (boost)), Colour (0xffc9e6ff));
            else              fillBar (g, row.withLeft (xOf (boost)).withRight (x0).reduced (0.0f, 2.5f), Colour (0xffc9e6ff));
            drawDot (g, { xOf (gr + boost), row.getCentreY() });
        }
        else
        {
            if (delta >= 0.0f) fillBar (g, row.withLeft (x0).withRight (xOf (delta)), Colour (0xffc9e6ff));
            else               fillBar (g, row.withLeft (xOf (delta)).withRight (x0), VirusColours::blue);
            drawDot (g, { xOf (delta), row.getCentreY() });
        }
    }
}

//==============================================================================
// Help
//==============================================================================
void HelpButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (b.translated (0.0f, 1.0f));
    g.setGradientFill (ColourGradient (isHighlighted ? Colour (0xff8a9096) : Colour (0xff6f757b), 0.0f, b.getY(),
                                       Colour (0xff35393d), 0.0f, b.getBottom(), false));
    g.fillEllipse (isDown ? b.reduced (0.5f) : b);
    g.setColour (VirusColours::bevelDark);
    g.drawEllipse (b, 1.0f);
    g.setColour (Colours::white);
    g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
    g.drawText ("?", b, Justification::centred);
}

void HelpOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));

    auto box = getLocalBounds().toFloat().reduced (16.0f, 18.0f);
    CustomLookAndFeel::drawPanel (g, box, "Tight Bottom - Quick Help");

    const char* lines[] =
    {
        "Splits off the low end (kick + bass), compresses it as one band, then lets you add punch back.",
        "TIGHT / LOOSE:  one macro for attack, release, ratio and threshold depth (hover for values)",
        "LOW END:  -10 .. +10 dB on the low band after the compressor (the punch-back control)",
        "S:  Off / 1 Clean (65 Hz 24 dB/oct high-pass) / 2 Shape (high-pass + low-mid dip + body lift)",
        "CROSSOVER:  where the low band ends (60 - 120 Hz, or 120 - 240 Hz with x2)",
        "RELEASE:  scales the macro's release time from x0.5 (FAST) to x2 (SLOW)",
        "AUTO PUNCH:  automatic makeup on the low band, so Tight/Loose changes feel, not level",
        "GAIN MATCH:  output follows the input loudness (K-weighted, 3 s) for fair A/B with Bypass",
        "Meters:  GR (left), LOW = reduction / punch, OUT = level change vs input, IN / OUT peaks",
        "Knobs and sliders: drag or scroll wheel in steps of 1, Shift = 0.1, double-click = default",
        "Drag the bottom-right corner to resize.",
        "",
        "Click anywhere to close."
    };

    auto text = box.reduced (14.0f, 0.0f).withTrimmedTop (30.0f);
    g.setFont (CustomLookAndFeel::uiFont (12.0f));
    for (auto* line : lines)
    {
        g.setColour (String (line).startsWith ("Click") ? VirusColours::blue : Colour (0xffdfe3e6));
        g.drawFittedText (line, text.removeFromTop (21.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}
