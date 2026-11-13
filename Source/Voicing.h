#pragma once

//==============================================================================
//  TIGHT BOTTOM - VOICING CONSTANTS
//
//  Every number that defines the sound lives in this file. Edit, rebuild, A/B.
//  Nothing else in the code needs to change when you re-voice the plugin.
//
//  (Changing a *range* below - crossover, Low End, IN/OUT, release scale - also
//   changes what saved automation maps to, so prefer tweaking the sound-only
//   constants once you have sessions that use the plugin.)
//==============================================================================
namespace voicing
{
    //--------------------------------------------------------------------------
    // S FILTER STAGE (button cycles Off -> 1 -> 2)
    //--------------------------------------------------------------------------
    // Mode 1 "Clean" and Mode 2 "Shape": high-pass built from two cascaded
    // 2nd-order sections = 24 dB/oct.
    constexpr double sHighPassHz       = 65.0;      // HPF corner frequency
    constexpr double sHighPassQ1       = 0.7071;    // Q of section 1 (0.7071 = Butterworth section)
    constexpr double sHighPassQ2       = 0.7071;    // Q of section 2 (use 0.5412 / 1.3066 for a true 4th-order Butterworth)

    // Mode 2 "Shape" adds two bells after the high-pass.
    constexpr double sBell1Hz          = 160.0;     // low-mid dip centre
    constexpr double sBell1GainDb      = -2.0;      // dip depth
    constexpr double sBell1Q           = 1.0;       // dip width
    constexpr double sBell2Hz          = 280.0;     // upper "body" lift centre
    constexpr double sBell2GainDb      = 1.5;       // lift amount
    constexpr double sBell2Q           = 0.9;       // lift width

    constexpr double sModeFadeMs       = 30.0;      // crossfade time when switching S modes (click-free)

    //--------------------------------------------------------------------------
    // CROSSOVER (Linkwitz-Riley 4th order, low band + high band sum flat)
    //--------------------------------------------------------------------------
    constexpr double crossoverDefaultHz   = 120.0;  // default split frequency
    constexpr double crossoverSliderMinHz = 60.0;   // CROSSOVER slider, left end
    constexpr double crossoverSliderMaxHz = 120.0;  // CROSSOVER slider, right end
    constexpr double crossoverX2Factor    = 2.0;    // "x2" button multiplies the slider (60-120 -> 120-240 Hz)
    constexpr double crossoverMinHz       = 60.0;   // hard safety limits for the split
    constexpr double crossoverMaxHz       = 250.0;
    constexpr double crossoverSmoothMs    = 60.0;   // glide time when the crossover moves

    //--------------------------------------------------------------------------
    // LOW-BAND COMPRESSOR
    //--------------------------------------------------------------------------
    // Tight/Loose macro. The knob runs TIGHT (left, 0) -> LOOSE (right, 100);
    // every row below is interpolated between its two ends (times log-interpolated).
    constexpr double looseAttackMs        = 30.0;   // attack at full LOOSE
    constexpr double tightAttackMs        = 6.0;    // attack at full TIGHT
    constexpr double looseReleaseMs       = 250.0;  // release at full LOOSE
    constexpr double tightReleaseMs       = 45.0;   // release at full TIGHT
    constexpr double looseRatio           = 1.5;    // ratio at full LOOSE (x:1)
    constexpr double tightRatio           = 4.0;    // ratio at full TIGHT (x:1)
    constexpr double looseThreshOffsetDb  = 1.0;    // threshold this far BELOW the long-term level at LOOSE
    constexpr double tightThreshOffsetDb  = 6.0;    // threshold this far BELOW the long-term level at TIGHT

    constexpr double kneeDb               = 6.0;    // soft-knee width (total, centred on the threshold)

    // RELEASE slider (FAST <-> SLOW) scales the macro's release time.
    constexpr double releaseScaleFast     = 0.5;    // slider fully FAST = release x 0.5
    constexpr double releaseScaleSlow     = 2.0;    // slider fully SLOW = release x 2 (centre = x 1)

    // Detector
    constexpr double detectorRmsMs        = 5.0;    // short RMS window (follows kick/bass envelope)
    constexpr double longTermRmsMs        = 1500.0; // slow RMS that the auto threshold sits under
    constexpr double longTermCatchUpMs    = 150.0;  // faster long-term rise when the level jumps up a lot
    constexpr double longTermCatchUpDb    = 10.0;   // ...i.e. when short-term is this far above long-term
    constexpr double detectorGateDb       = -70.0;  // below this the long-term level is frozen (tails/silence)
    constexpr double thresholdFloorDb     = -60.0;  // the auto threshold never goes below this
    constexpr double maxGainReductionDb   = 24.0;   // safety limit on gain reduction

    //--------------------------------------------------------------------------
    // AUTO PUNCH (low-band makeup gain)
    //--------------------------------------------------------------------------
    constexpr double autoPunchAverageMs   = 400.0;  // average-GR smoothing; makeup = inverse of this average
    constexpr double autoPunchAmount      = 1.0;    // 1.0 = full inverse of average GR (0.8 = a bit less)

    //--------------------------------------------------------------------------
    // LOW END (punch-back gain on the low band, after makeup)
    //--------------------------------------------------------------------------
    constexpr double lowEndMinDb          = -10.0;  // knob left end
    constexpr double lowEndMaxDb          = 10.0;   // knob right end (spec's original idea was 0 .. +6)
    constexpr double lowEndDefaultDb      = 0.0;

    //--------------------------------------------------------------------------
    // IN / OUT trims
    //--------------------------------------------------------------------------
    constexpr double ioGainRangeDb        = 12.0;   // IN and OUT knobs run -12 .. +12 dB

    //--------------------------------------------------------------------------
    // GAIN MATCH (output level follows input level, for fair A/B)
    //--------------------------------------------------------------------------
    constexpr double gainMatchWindowMs    = 3000.0; // K-weighted RMS window for in and out loudness
    constexpr double gainMatchSmoothMs    = 800.0;  // how slowly the correction itself moves
    constexpr double gainMatchMaxDb       = 12.0;   // correction limit (+/-)
    constexpr double gainMatchGateDb      = -60.0;  // correction is frozen when the input is quieter than this

    //--------------------------------------------------------------------------
    // GENERAL
    //--------------------------------------------------------------------------
    constexpr double paramSmoothMs        = 30.0;   // knob/slider smoothing (no zipper noise)
    constexpr double bypassFadeMs         = 20.0;   // bypass crossfade
}
