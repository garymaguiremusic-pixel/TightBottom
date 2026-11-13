#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "UI/Components.h"

//==============================================================================
/** The whole user interface, laid out at its base size. The editor below scales
    it to the window size. */
class TightBottomPanel : public juce::Component,
                         private juce::Timer
{
public:
    static constexpr int kBaseWidth = 600, kBaseHeight = 364;

    explicit TightBottomPanel (TightBottomAudioProcessor&);
    ~TightBottomPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void createBackground();
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);
    float param (const char* id) const;

    TightBottomAudioProcessor& processor;
    CustomLookAndFeel lnf;
    juce::Image background;

    VirusKnob tightKnob  { VirusKnob::Size::Large, "Loose / Tight" };
    VirusKnob lowEndKnob { VirusKnob::Size::Large, "Punch", true };
    VirusKnob inKnob     { VirusKnob::Size::Small, "In" };
    VirusKnob outKnob    { VirusKnob::Size::Small, "Out" };
    VirusSlider crossoverSlider { "Crossover" };
    VirusSlider releaseSlider   { "Release" };
    SModeButton sButton;

    juce::TextButton x2Button { "x2" }, autoPunchButton { "Auto Punch" }, gainMatchButton { "Gain Match" },
                     bypassButton { "Bypass" };

    LcdDisplay tightLcd, lowEndLcd, macroLcd, statusLcd;
    GrMeter grMeter;
    GainChangeMeter gainMeter;
    PeakMeter inMeter, outMeter;
    HelpButton helpButton;
    HelpOverlay helpOverlay;
    juce::TooltipWindow tooltipWindow { this, 400 };

    // Attachments (declared after the controls so they are destroyed first)
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    // Layout
    juce::Rectangle<float> leftPanel, centrePanel, rightPanel, inPanel, xoverPanel, releasePanel, outPanel;
    double lastTimerMs = 0.0;
    int lastLabelState = -1;
    juce::String statusMessage;          // shown in the bottom display for a moment
    double statusUntilMs = 0.0;

    void mouseDoubleClick (const juce::MouseEvent&) override;
    void showStatus (const juce::String&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TightBottomPanel)
};

//==============================================================================
/** Resizable editor (fixed aspect ratio, 75 % .. 200 %); remembers its size. */
class TightBottomAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit TightBottomAudioProcessorEditor (TightBottomAudioProcessor&);
    void resized() override;

private:
    TightBottomAudioProcessor& processor;
    TightBottomPanel panel;
    bool sizeRestored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TightBottomAudioProcessorEditor)
};
