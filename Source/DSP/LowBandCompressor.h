#pragma once

#include "Filters.h"
#include "../Voicing.h"
#include <algorithm>
#include <cmath>

namespace tb
{
    /** What the Tight/Loose macro maps to (also shown in the GUI). */
    struct MacroSettings
    {
        double attackMs, releaseMs, ratio, thresholdOffsetDb;
    };

    inline double logInterp (double a, double b, double t) { return a * std::pow (b / a, t); }
    inline double linInterp (double a, double b, double t) { return a + (b - a) * t; }

    /** tightness: 0 = Loose, 1 = Tight. releaseScale multiplies the release time. */
    inline MacroSettings macroSettings (double tightness, double releaseScale = 1.0)
    {
        using namespace voicing;
        const double t = std::clamp (tightness, 0.0, 1.0);
        return { logInterp (looseAttackMs, tightAttackMs, t),
                 logInterp (looseReleaseMs, tightReleaseMs, t) * releaseScale,
                 linInterp (looseRatio, tightRatio, t),
                 linInterp (looseThreshOffsetDb, tightThreshOffsetDb, t) };
    }

    /** RELEASE slider 0..1 (Fast..Slow) -> release multiplier, log-scaled, centre = x1. */
    inline double releaseScaleFromSlider (double s01)
    {
        const double s = std::clamp (s01, 0.0, 1.0);
        return s < 0.5 ? logInterp (voicing::releaseScaleFast, 1.0, s * 2.0)
                       : logInterp (1.0, voicing::releaseScaleSlow, (s - 0.5) * 2.0);
    }

    //==========================================================================
    /** Feed-forward, stereo-linked low-band compressor with auto threshold and
        Auto Punch makeup. It only computes gains; the engine applies them. */
    class LowBandCompressor
    {
    public:
        /** Static soft-knee curve: returns the gain change in dB (<= 0). */
        static double staticGainDb (double inputDb, double thresholdDb, double ratio, double kneeDb)
        {
            const double over = inputDb - thresholdDb;
            double outDb;
            if (2.0 * over < -kneeDb)
                outDb = inputDb;
            else if (kneeDb > 0.0 && 2.0 * std::abs (over) <= kneeDb)
                outDb = inputDb + (1.0 / ratio - 1.0) * (over + kneeDb * 0.5) * (over + kneeDb * 0.5) / (2.0 * kneeDb);
            else
                outDb = thresholdDb + over / ratio;
            return outDb - inputDb;
        }

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            detCoef      = onePoleCoef (voicing::detectorRmsMs, sr);
            longCoef     = onePoleCoef (voicing::longTermRmsMs, sr);
            catchUpCoef  = onePoleCoef (voicing::longTermCatchUpMs, sr);
            punchCoef    = onePoleCoef (voicing::autoPunchAverageMs, sr);
            lastTightness = lastReleaseScale = -1.0;
            reset();
        }

        void reset()
        {
            ms[0] = ms[1] = 0.0;
            longMs = 0.0;
            grDb = avgGrDb = 0.0;
            thresholdDb = voicing::thresholdFloorDb;
        }

        /** Per sample: the low-band samples for each channel (1 or 2). */
        void process (const double* low, int numChannels, double tightness, double releaseScale)
        {
            if (std::abs (tightness - lastTightness) > 1.0e-12 || std::abs (releaseScale - lastReleaseScale) > 1.0e-12)
                updateMacro (tightness, releaseScale);

            // Short RMS per channel, linked with max(L, R).
            double linked = 0.0;
            for (int c = 0; c < numChannels; ++c)
            {
                ms[c] += detCoef * (low[c] * low[c] - ms[c]);
                linked = std::max (linked, ms[c]);
            }
            levelDb = powerToDb (linked);

            // Long-term level (auto threshold reference). Frozen in silence/tails;
            // rises faster when the level jumps well above it (e.g. playback starts).
            if (levelDb > voicing::detectorGateDb)
            {
                const double longDb = powerToDb (longMs);
                const double c = (levelDb - longDb > voicing::longTermCatchUpDb) ? catchUpCoef : longCoef;
                longMs += c * (linked - longMs);
            }
            thresholdDb = std::max (powerToDb (longMs) - macro.thresholdOffsetDb, voicing::thresholdFloorDb);

            double target = staticGainDb (levelDb, thresholdDb, macro.ratio, voicing::kneeDb);
            target = std::max (target, -voicing::maxGainReductionDb);

            grDb += (target < grDb ? attackCoef : releaseCoef) * (target - grDb);
            avgGrDb += punchCoef * (grDb - avgGrDb);
        }

        double getGainReductionDb() const   { return grDb; }                                         // <= 0
        double getAutoPunchDb() const       { return -avgGrDb * voicing::autoPunchAmount; }          // >= 0
        double getThresholdDb() const       { return thresholdDb; }
        double getLevelDb() const           { return levelDb; }
        const MacroSettings& getMacro() const { return macro; }

    private:
        void updateMacro (double tightness, double releaseScale)
        {
            lastTightness = tightness;
            lastReleaseScale = releaseScale;
            macro = macroSettings (tightness, releaseScale);
            attackCoef  = onePoleCoef (macro.attackMs, sr);
            releaseCoef = onePoleCoef (macro.releaseMs, sr);
        }

        double sr = 44100.0;
        double detCoef = 0, longCoef = 0, catchUpCoef = 0, punchCoef = 0, attackCoef = 0, releaseCoef = 0;
        double ms[2] {}, longMs = 0, grDb = 0, avgGrDb = 0, thresholdDb = 0, levelDb = -300.0;
        double lastTightness = -1, lastReleaseScale = -1;
        MacroSettings macro { 10, 100, 2, 3 };
    };
}
