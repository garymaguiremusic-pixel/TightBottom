#pragma once

#include "Filters.h"
#include "LowBandCompressor.h"
#include "../Voicing.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace tb
{
    /** Plain values the engine needs, read from the parameters once per block. */
    struct EngineParams
    {
        double tightness   = 0.5;    // 0 = Loose, 1 = Tight
        double lowEndDb    = 0.0;
        double crossoverHz = voicing::crossoverDefaultHz;
        double releaseScale = 1.0;
        double inGainDb    = 0.0;
        double outGainDb   = 0.0;
        int    sMode       = 0;      // 0 Off, 1 Clean, 2 Shape
        bool   autoPunch   = true;
        bool   gainMatch   = false;
        bool   bypass      = false;
    };

    /** Values for the meters, written by the audio thread. */
    struct MeterData
    {
        std::atomic<float> inPeak[2]  { 0.0f, 0.0f };
        std::atomic<float> outPeak[2] { 0.0f, 0.0f };
        std::atomic<float> grDb { 0.0f };          // low-band gain reduction (<= 0), deepest in the block
        std::atomic<float> lowBoostDb { 0.0f };    // Auto Punch makeup + Low End (can be negative)
        std::atomic<float> outDeltaDb { 0.0f };    // output loudness minus input loudness
        std::atomic<float> gainMatchDb { 0.0f };   // correction currently applied
    };

    /**
        The complete TightBottom signal flow (zero latency):

          in -> IN gain -> S filter -> LR4 crossover -+- low  -> compressor -> Auto Punch -> Low End -+- sum
                                                      +- high -> untouched ---------------------------+
             -> Gain Match -> OUT gain -> (bypass crossfade against the dry input) -> out
    */
    class TightBottomEngine
    {
    public:
        static constexpr int maxChannels = 2;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            using namespace voicing;

            for (int c = 0; c < maxChannels; ++c)
            {
                ch[c].hp1.makeHighPass (sr, sHighPassHz, sHighPassQ1);
                ch[c].hp2.makeHighPass (sr, sHighPassHz, sHighPassQ2);
                ch[c].bell1.makePeak (sr, sBell1Hz, sBell1Q, sBell1GainDb);
                ch[c].bell2.makePeak (sr, sBell2Hz, sBell2Q, sBell2GainDb);
                ch[c].kIn.prepare (sr);
                ch[c].kOut.prepare (sr);
            }

            for (auto* s : { &inGain, &outGain, &lowEnd, &tight, &relScale, &punchMix })
                s->setTime (paramSmoothMs, sr);
            crossover.setTime (crossoverSmoothMs, sr);
            for (auto& w : sWeight) w.setTime (sModeFadeMs, sr);
            wet.setTime (bypassFadeMs, sr);
            gmSmooth.setTime (gainMatchSmoothMs, sr);
            gmCoef = onePoleCoef (gainMatchWindowMs, sr);

            comp.prepare (sr);
            if (crossover.target <= 0.0)
                crossover.snap (voicing::crossoverDefaultHz);
            setCrossover (crossover.current);   // all filters are recalculated for the new rate
            reset();
        }

        void reset()
        {
            for (auto& c : ch)
            {
                c.hp1.reset(); c.hp2.reset(); c.bell1.reset(); c.bell2.reset();
                c.xover.reset(); c.kIn.reset(); c.kOut.reset();
            }
            comp.reset();
            gmIn = gmOut = 0.0;
            gmSmooth.snap (0.0);
            first = true;
        }

        void setParameters (const EngineParams& p)
        {
            using namespace voicing;
            inGain.setTarget (p.inGainDb);
            outGain.setTarget (p.outGainDb);
            lowEnd.setTarget (p.lowEndDb);
            tight.setTarget (std::clamp (p.tightness, 0.0, 1.0));
            relScale.setTarget (p.releaseScale);
            punchMix.setTarget (p.autoPunch ? 1.0 : 0.0);
            crossover.setTarget (std::clamp (p.crossoverHz, crossoverMinHz, crossoverMaxHz));
            for (int i = 0; i < 3; ++i)
                sWeight[i].setTarget (i == std::clamp (p.sMode, 0, 2) ? 1.0 : 0.0);
            wet.setTarget (p.bypass ? 0.0 : 1.0);
            gainMatchOn = p.gainMatch;

            if (first)
            {
                // Start settled: no fades when the plugin is first loaded.
                for (auto* s : { &inGain, &outGain, &lowEnd, &tight, &relScale, &punchMix, &crossover, &wet })
                    s->snap (s->target);
                for (auto& w : sWeight) w.snap (w.target);
                setCrossover (crossover.current);
                first = false;
            }
        }

        /** For the unit tests: run the band split with the compressor held idle. */
        void setCompressorEnabledForTesting (bool enabled) { compressorEnabled = enabled; }

        template <typename SampleType>
        void process (SampleType* const* channels, int numChannels, int numSamples)
        {
            numChannels = std::clamp (numChannels, 0, maxChannels);
            if (numChannels == 0) return;

            float inPk[2] {}, outPk[2] {};
            double deepestGr = 0.0;

            for (int n = 0; n < numSamples; ++n)
            {
                const double gIn  = dbToGain (inGain.next());
                const double gOut = dbToGain (outGain.next());
                const double tNow = tight.next();
                const double rNow = relScale.next();
                const double pMix = punchMix.next();
                const double lowEndDb = lowEnd.next();
                const double w0 = sWeight[0].next(), w1 = sWeight[1].next(), w2 = sWeight[2].next();
                const double wetNow = wet.next();

                if (! crossover.isSettled())
                {
                    crossover.next();
                    if ((n & 7) == 0) setCrossover (crossover.current);
                }

                double dry[2], low[2], high[2];
                double kInPow = 0.0;

                for (int c = 0; c < numChannels; ++c)
                {
                    auto& s = ch[c];
                    const double x = (double) channels[c][n];
                    dry[c] = x;
                    inPk[c] = std::max (inPk[c], (float) std::abs (x));

                    const double kx = s.kIn.process (x);
                    kInPow += kx * kx;

                    const double xi = x * gIn;
                    const double hp = s.hp2.process (s.hp1.process (xi));
                    const double shaped = s.bell2.process (s.bell1.process (hp));
                    const double sOut = w0 * xi + w1 * hp + w2 * shaped;

                    s.xover.process (sOut, low[c], high[c]);
                }

                double lowGainDb = lowEndDb;
                if (compressorEnabled)
                {
                    comp.process (low, numChannels, tNow, rNow);
                    const double gr = comp.getGainReductionDb();
                    deepestGr = std::min (deepestGr, gr);
                    lowGainDb += gr + pMix * comp.getAutoPunchDb();
                }
                const double lowGain = dbToGain (lowGainDb);

                // Gain Match: K-weighted loudness of the input vs the processed signal
                // (measured before the correction, so the loop cannot run away).
                double y[2], kOutPow = 0.0;
                for (int c = 0; c < numChannels; ++c)
                {
                    y[c] = low[c] * lowGain + high[c];
                    const double ky = ch[c].kOut.process (y[c]);
                    kOutPow += ky * ky;
                }
                gmIn  += gmCoef * (kInPow - gmIn);
                gmOut += gmCoef * (kOutPow - gmOut);

                const double inDb = powerToDb (gmIn), procDb = powerToDb (gmOut);
                if (inDb > voicing::gainMatchGateDb && procDb > voicing::gainMatchGateDb - 24.0)
                    gmTargetDb = std::clamp (inDb - procDb, -voicing::gainMatchMaxDb, voicing::gainMatchMaxDb);
                gmSmooth.setTarget (gainMatchOn ? gmTargetDb : 0.0);
                const double gmDb = gmSmooth.next();
                const double gFinal = dbToGain (gmDb) * gOut;

                for (int c = 0; c < numChannels; ++c)
                {
                    const double wetSample = y[c] * gFinal;
                    const double out = dry[c] + wetNow * (wetSample - dry[c]);
                    channels[c][n] = (SampleType) out;
                    outPk[c] = std::max (outPk[c], (float) std::abs (out));
                }

                lastLowBoostDb = lowEndDb + (compressorEnabled ? pMix * comp.getAutoPunchDb() : 0.0);
                lastOutDeltaDb = (procDb - inDb) + gmDb + gainToDb (gOut);
                lastGainMatchDb = gmDb;
            }

            // Meters (peaks are max'd here and reset by the GUI when it reads them).
            for (int c = 0; c < maxChannels; ++c)
            {
                const int src = std::min (c, numChannels - 1);
                atomicMax (meters.inPeak[c], inPk[src]);
                atomicMax (meters.outPeak[c], outPk[src]);
            }
            meters.grDb.store ((float) deepestGr);
            meters.lowBoostDb.store ((float) lastLowBoostDb);
            meters.outDeltaDb.store (gmIn > 1.0e-9 ? (float) lastOutDeltaDb : 0.0f);
            meters.gainMatchDb.store ((float) lastGainMatchDb);
        }

        MeterData meters;
        const LowBandCompressor& getCompressor() const { return comp; }

    private:
        static void atomicMax (std::atomic<float>& a, float v)
        {
            float prev = a.load();
            while (v > prev && ! a.compare_exchange_weak (prev, v)) {}
        }

        void setCrossover (double hz)
        {
            for (auto& c : ch) c.xover.setCutoff (sr, hz);
        }

        struct Channel
        {
            Biquad hp1, hp2, bell1, bell2;
            LinkwitzRiley4 xover;
            KWeighting kIn, kOut;
        };

        double sr = 44100.0;
        Channel ch[maxChannels];
        LowBandCompressor comp;
        Smoother inGain, outGain, lowEnd, tight, relScale, punchMix, crossover, wet, gmSmooth;
        Smoother sWeight[3];
        double gmIn = 0, gmOut = 0, gmCoef = 0, gmTargetDb = 0;
        double lastLowBoostDb = 0, lastOutDeltaDb = 0, lastGainMatchDb = 0;
        bool gainMatchOn = false, compressorEnabled = true, first = true;
    };
}
