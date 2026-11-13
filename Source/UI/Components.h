#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Slider with Virus-style interaction (shared by knobs and the horizontal sliders):
      - drag and the scroll wheel move in whole steps (1.0); hold Shift for 0.1 steps
      - double-click resets to the default value
      - the tooltip shows the value (plus extra detail from describeValue)
*/
class StepSlider : public juce::Slider
{
public:
    StepSlider (juce::Slider::SliderStyle, const juce::String& labelText);

    void setDefaultValue (double v) noexcept    { defaultValue = v; }
    const juce::String& getLabel() const        { return label; }
    juce::String valueText (double v) const;

    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<juce::String (double)> displayValue;   // value text, e.g. "+3 dB"
    std::function<juce::String (double)> describeValue;  // extra tooltip line(s)

    juce::String getTooltip() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

protected:
    bool isHovering() const { return hovering; }
    bool isDragging() const { return dragging; }

private:
    void setValueWithGesture (double v);

    juce::String label;
    double defaultValue = 0.0;
    float lastDragPos = 0.0f;
    double dragValue = 0.0;
    bool dragShift = false, dragging = false, hovering = false;
};

//==============================================================================
/** Rotary knob: large chrome dial with a blue arc, or small rubber knob with a label. */
class VirusKnob : public StepSlider
{
public:
    enum class Size { Large, Small };
    VirusKnob (Size, const juce::String& labelText, bool bipolar = false);

    Size getKnobSize() const noexcept   { return knobSize; }
    bool isBipolar() const noexcept     { return bipolar; }
    void paint (juce::Graphics&) override;

private:
    Size knobSize;
    bool bipolar;
};

/** Horizontal slider (CROSSOVER, RELEASE). */
class VirusSlider : public StepSlider
{
public:
    explicit VirusSlider (const juce::String& labelText);
};

//==============================================================================
/** Connects a StepSlider to a parameter with proper host gesture handling. */
class SliderAttachment
{
public:
    SliderAttachment (juce::RangedAudioParameter&, StepSlider&);
    ~SliderAttachment();

private:
    StepSlider& slider;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Round, glowing "S" button. Each click cycles Off -> 1 -> 2 -> Off. */
class SModeButton : public juce::Component,
                    public juce::SettableTooltipClient
{
public:
    explicit SModeButton (juce::RangedAudioParameter&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    juce::String getTooltip() override;
    int getMode() const { return mode; }

private:
    juce::ParameterAttachment attachment;
    int mode = 0;
    bool hover = false;
};

//==============================================================================
/** Blue LCD readout ("NAME      value"), as in Virus Delay. */
class LcdDisplay : public juce::Component
{
public:
    void setContent (const juce::String& name, const juce::String& value);
    void paint (juce::Graphics&) override;

private:
    juce::String nameText, valueText;
};

//==============================================================================
/** Two-row (L / R) horizontal peak meter, -48 .. +6 dBFS, with peak-hold dots. */
class PeakMeter : public juce::Component
{
public:
    void setLevels (float peakL, float peakR, float deltaSeconds);
    void paint (juce::Graphics&) override;

private:
    float shown[2] { -100.0f, -100.0f }, hold[2] { -100.0f, -100.0f }, holdTime[2] {};
};

/** Vertical low-band gain-reduction meter, 0 .. -12 dB. */
class GrMeter : public juce::Component
{
public:
    void setGainReduction (float grDb, float deltaSeconds);
    void paint (juce::Graphics&) override;

private:
    float shown = 0.0f, hold = 0.0f, holdTime = 0.0f;
};

/** Centre meter, -10 .. +10 dB scale.
    Row 1 (LOW): blue = gain reduction (left of 0), pale = Auto Punch + Low End (right), dot = net.
    Row 2 (OUT): output loudness change vs input (what Gain Match removes). */
class GainChangeMeter : public juce::Component
{
public:
    void setValues (float grDb, float boostDb, float outDeltaDb, float deltaSeconds);
    void paint (juce::Graphics&) override;

private:
    float gr = 0.0f, boost = 0.0f, delta = 0.0f;
};

//==============================================================================
/** Round "?" button in the bottom bar. */
class HelpButton : public juce::Button
{
public:
    HelpButton() : juce::Button ("help") {}
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

/** Quick reference shown over the editor; click anywhere to close. */
class HelpOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
