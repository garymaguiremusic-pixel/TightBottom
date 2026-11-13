#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace tbp;

TightBottomAudioProcessor::TightBottomAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TightBottomState", createParameterLayout())
{
    pTight   = apvts.getRawParameterValue (ParamIDs::tightLoose);
    pLowEnd  = apvts.getRawParameterValue (ParamIDs::lowEnd);
    pIn      = apvts.getRawParameterValue (ParamIDs::inGain);
    pOut     = apvts.getRawParameterValue (ParamIDs::outGain);
    pXover   = apvts.getRawParameterValue (ParamIDs::crossover);
    pX2      = apvts.getRawParameterValue (ParamIDs::xoverX2);
    pRelease = apvts.getRawParameterValue (ParamIDs::release);
    pSMode   = apvts.getRawParameterValue (ParamIDs::sMode);
    pPunch   = apvts.getRawParameterValue (ParamIDs::autoPunch);
    pMatch   = apvts.getRawParameterValue (ParamIDs::gainMatch);
    pBypass  = apvts.getRawParameterValue (ParamIDs::bypass);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::bypass));
}

bool TightBottomAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void TightBottomAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);   // recalculates every filter for this sample rate
    updateEngineParameters();
}

void TightBottomAudioProcessor::updateEngineParameters()
{
    tb::EngineParams p;
    p.tightness    = knobToTightness (pTight->load());
    p.lowEndDb     = pLowEnd->load();
    p.inGainDb     = pIn->load();
    p.outGainDb    = pOut->load();
    p.crossoverHz  = crossoverHz (pXover->load(), pX2->load() > 0.5f);
    p.releaseScale = sliderToReleaseScale (pRelease->load());
    p.sMode        = juce::roundToInt (pSMode->load());
    p.autoPunch    = pPunch->load() > 0.5f;
    p.gainMatch    = pMatch->load() > 0.5f;
    p.bypass       = pBypass->load() > 0.5f;
    engine.setParameters (p);
}

template <typename T>
void TightBottomAudioProcessor::process (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    updateEngineParameters();
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (numOut, buffer.getNumChannels()), buffer.getNumSamples());
}

void TightBottomAudioProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)  { process (b); }
void TightBottomAudioProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { process (b); }

//==============================================================================
void TightBottomAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("editorWidth", editorWidth.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TightBottomAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            editorWidth.store ((int) state.getProperty ("editorWidth", 0));
            apvts.replaceState (state);
        }
    }
}

juce::AudioProcessorEditor* TightBottomAudioProcessor::createEditor()
{
    return new TightBottomAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TightBottomAudioProcessor();
}
