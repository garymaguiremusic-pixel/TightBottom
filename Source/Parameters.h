#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Voicing.h"
#include "DSP/LowBandCompressor.h"

namespace tbp
{
    namespace ParamIDs
    {
        inline constexpr const char* tightLoose = "tightLoose";  // 0 = LOOSE .. 100 = TIGHT (turn up = tighter)
        inline constexpr const char* lowEnd     = "lowEnd";      // dB
        inline constexpr const char* inGain     = "inGain";      // dB
        inline constexpr const char* outGain    = "outGain";     // dB
        inline constexpr const char* crossover  = "crossover";   // Hz (slider; x2 doubles it)
        inline constexpr const char* xoverX2    = "xoverX2";
        inline constexpr const char* release    = "release";     // 0 = FAST .. 100 = SLOW
        inline constexpr const char* sMode      = "sMode";       // Off / 1 / 2
        inline constexpr const char* autoPunch  = "autoPunch";
        inline constexpr const char* gainMatch  = "gainMatch";
        inline constexpr const char* bypass     = "bypass";
    }

    inline juce::StringArray getSModeNames() { return { "Off", "1 Dull", "2 Bright" }; }

    //==========================================================================
    // Value <-> text
    inline juce::String formatNumber (double v)
    {
        const double r = std::round (v * 10.0) / 10.0;
        return std::abs (r - std::round (r)) < 0.05 ? juce::String (juce::roundToInt (r)) : juce::String (r, 1);
    }

    inline juce::String formatDb (double v)
    {
        const double r = std::round (v * 10.0) / 10.0;
        return (r > 0.04 ? "+" : "") + formatNumber (std::abs (r) < 0.05 ? 0.0 : r) + " dB";
    }

    inline juce::String formatMs (double ms)
    {
        return ms < 10.0 ? juce::String (ms, 1) + " ms" : juce::String (juce::roundToInt (ms)) + " ms";
    }

    inline juce::String formatHz (double hz)  { return juce::String (juce::roundToInt (hz)) + " Hz"; }

    /** Knob (0 = Loose .. 100 = Tight) -> compressor tightness (0 = Loose .. 1 = Tight). */
    inline double knobToTightness (double knob)   { return juce::jlimit (0.0, 100.0, knob) / 100.0; }
    inline double sliderToReleaseScale (double v) { return tb::releaseScaleFromSlider (v / 100.0); }

    inline juce::String describeTightLoose (double knob)
    {
        const double tight = juce::jlimit (0.0, 100.0, knob);
        if (tight < 0.5)  return "Loose";
        if (tight > 99.5) return "Tight";
        return tight >= 50.0 ? "Tight " + formatNumber (tight) + " %" : "Loose " + formatNumber (100.0 - tight) + " %";
    }

    inline juce::String describeMacro (double knob, double releaseSlider)
    {
        const auto m = tb::macroSettings (knobToTightness (knob), sliderToReleaseScale (releaseSlider));
        return "Attack " + formatMs (m.attackMs) + ", Release " + formatMs (m.releaseMs)
             + ", Ratio " + juce::String (m.ratio, 1) + ":1, Threshold -" + juce::String (m.thresholdOffsetDb, 1) + " dB";
    }

    inline juce::String describeRelease (double v)
    {
        return "Release x" + juce::String (sliderToReleaseScale (v), 2);
    }

    inline double crossoverHz (double slider, bool x2)
    {
        return juce::jlimit (voicing::crossoverMinHz, voicing::crossoverMaxHz, slider * (x2 ? voicing::crossoverX2Factor : 1.0));
    }

    //==========================================================================
    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        using namespace voicing;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto dbAttrs = AudioParameterFloatAttributes()
                           .withLabel ("dB")
                           .withStringFromValueFunction ([] (float v, int) { return formatDb (v); })
                           .withValueFromStringFunction ([] (const String& s) { return s.retainCharacters ("-+.0123456789").getFloatValue(); });

        // Every knob/slider moves in whole steps by default (interval 0.1 allows Shift fine steps).
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::tightLoose, 1 }, "Tight / Loose",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return describeTightLoose (v); })));

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::lowEnd, 1 }, "Punch",
            NormalisableRange<float> ((float) lowEndMinDb, (float) lowEndMaxDb, 0.1f), (float) lowEndDefaultDb, dbAttrs));

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::inGain, 1 }, "Input",
            NormalisableRange<float> ((float) -ioGainRangeDb, (float) ioGainRangeDb, 0.1f), 0.0f, dbAttrs));

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::outGain, 1 }, "Output",
            NormalisableRange<float> ((float) -ioGainRangeDb, (float) ioGainRangeDb, 0.1f), 0.0f, dbAttrs));

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::crossover, 1 }, "Crossover",
            NormalisableRange<float> ((float) crossoverSliderMinHz, (float) crossoverSliderMaxHz, 0.1f),
            (float) juce::jlimit (crossoverSliderMinHz, crossoverSliderMaxHz, crossoverDefaultHz),
            AudioParameterFloatAttributes().withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int) { return formatNumber (v) + " Hz"; })));

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::xoverX2, 1 }, "Crossover x2", false));

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::release, 1 }, "Release",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return describeRelease (v); })));

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::sMode, 1 }, "S Enhancer", getSModeNames(), 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::autoPunch, 1 }, "Auto Punch", true));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::gainMatch, 1 }, "Gain Match", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

        return layout;
    }
}
