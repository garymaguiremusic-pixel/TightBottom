#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "DSP/TightBottomEngine.h"

class TightBottomAudioProcessor : public juce::AudioProcessor
{
public:
    TightBottomAudioProcessor();
    ~TightBottomAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;
    tb::TightBottomEngine engine;
    std::atomic<int> editorWidth { 0 };   // remembered window size (0 = default)

private:
    template <typename T> void process (juce::AudioBuffer<T>&);
    void updateEngineParameters();

    juce::AudioParameterBool* bypassParam = nullptr;
    std::atomic<float>* pTight = nullptr, * pLowEnd = nullptr, * pIn = nullptr, * pOut = nullptr,
                      * pXover = nullptr, * pX2 = nullptr, * pRelease = nullptr, * pSMode = nullptr,
                      * pPunch = nullptr, * pMatch = nullptr, * pBypass = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TightBottomAudioProcessor)
};
