// Unit tests for the TightBottom DSP (no JUCE needed). Run: ctest, or the
// TightBottomTests executable directly. Exit code 0 = all passed.

#include "DSP/TightBottomEngine.h"
#include <cstdio>
#include <random>
#include <vector>

using namespace tb;

static int failures = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (! (cond)) { ++failures; std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); } \
    } while (0)

/** Steady-state gain (dB) of a sine through `proc`, measured by correlation over whole cycles. */
template <typename Proc>
static double sineGainDb (double sr, double freq, Proc&& proc)
{
    const int settle = (int) (sr * 0.5) + (int) (20.0 * sr / freq);   // >= 20 cycles + 0.5 s
    const int cycles = std::max (10, (int) (freq * 0.25));
    const int measure = (int) std::round (cycles * sr / freq);
    double re = 0.0, im = 0.0;
    for (int n = 0; n < settle + measure; ++n)
    {
        const double ph = 2.0 * pi * freq * n / sr;
        const double y = proc (std::sin (ph));
        if (n >= settle) { re += y * std::sin (ph); im += y * std::cos (ph); }
    }
    return gainToDb (2.0 * std::sqrt (re * re + im * im) / measure);
}

static std::vector<double> logFrequencies (int count)
{
    std::vector<double> f;
    for (int i = 0; i < count; ++i)
        f.push_back (20.0 * std::pow (1000.0, (double) i / (count - 1)));   // 20 Hz .. 20 kHz
    return f;
}

//==============================================================================
static void testCrossoverSum()
{
    std::printf ("Crossover: low + high is flat (+/-0.1 dB, 20 Hz - 20 kHz)\n");
    double worst = 0.0;

    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double fc : { 60.0, 120.0, 250.0 })
            for (double f : logFrequencies (31))
            {
                if (f > sr * 0.45) continue;
                LinkwitzRiley4 xo;
                xo.setCutoff (sr, fc);
                const double g = sineGainDb (sr, f, [&] (double x) { double lo, hi; xo.process (x, lo, hi); return lo + hi; });
                worst = std::max (worst, std::abs (g));
                CHECK (std::abs (g) <= 0.1, "sr %.0f fc %.0f f %.1f Hz: %.3f dB", sr, fc, f, g);
            }

    // The whole engine with the compressor idle (S off, Low End 0, gains 0).
    for (double sr : { 44100.0, 96000.0 })
        for (double f : logFrequencies (21))
        {
            TightBottomEngine e;
            e.prepare (sr);
            e.setParameters ({});
            e.setCompressorEnabledForTesting (false);
            const double g = sineGainDb (sr, f, [&] (double x)
            {
                float s = (float) x; float* p = &s;
                e.process (&p, 1, 1);
                return (double) s;
            });
            worst = std::max (worst, std::abs (g));
            CHECK (std::abs (g) <= 0.1, "engine sr %.0f f %.1f Hz: %.3f dB", sr, f, g);
        }
    std::printf ("  worst deviation %.4f dB\n", worst);
}

//==============================================================================
static void testCompressorStaticCurve()
{
    std::printf ("Compressor: static curve\n");
    const double T = -20.0, R = 4.0, W = 6.0;
    struct Case { double in, expected; };
    const Case cases[] =
    {
        { -40.0, 0.0 },                                          // well below the knee
        { -23.0, 0.0 },                                          // knee start
        { -20.0, (1.0 / R - 1.0) * 9.0 / 12.0 },                 // at threshold: -0.5625 dB
        { -17.0, (1.0 / R - 1.0) * 3.0 },                        // knee end: -2.25 dB
        { -10.0, (1.0 / R - 1.0) * 10.0 },                       // above: -7.5 dB
        {   0.0, (1.0 / R - 1.0) * 20.0 },                       // -15 dB
    };
    for (auto& c : cases)
    {
        const double g = LowBandCompressor::staticGainDb (c.in, T, R, W);
        CHECK (std::abs (g - c.expected) < 1.0e-9, "in %.1f dB -> %.4f dB, expected %.4f", c.in, g, c.expected);
    }

    // Through the real detector: a constant-level signal settles to the static curve
    // evaluated at threshold = level - offset, and the result is the same at any input
    // level (auto threshold).
    const double sr = 48000.0;
    for (double tightness : { 0.0, 0.5, 1.0 })
    {
        const auto m = macroSettings (tightness);
        const double expected = LowBandCompressor::staticGainDb (0.0, -m.thresholdOffsetDb, m.ratio, voicing::kneeDb);
        for (double levelDb : { -36.0, -18.0, -6.0, 0.0 })
        {
            LowBandCompressor comp;
            comp.prepare (sr);
            const double a = dbToGain (levelDb);
            double in[2];
            for (int n = 0; n < (int) (sr * 12.0); ++n)
            {
                const double x = (n & 1) ? a : -a;   // square wave at Nyquist: RMS = a exactly
                in[0] = in[1] = x;
                comp.process (in, 2, tightness, 1.0);
            }
            const double gr = comp.getGainReductionDb();
            CHECK (std::abs (gr - expected) < 0.05, "tight %.1f level %.0f dB: GR %.3f dB, expected %.3f", tightness, levelDb, gr, expected);
            CHECK (std::abs (comp.getAutoPunchDb() + gr) < 0.05, "Auto Punch %.3f should cancel GR %.3f", comp.getAutoPunchDb(), gr);
        }
        std::printf ("  tightness %.1f: ratio %.2f, offset %.1f dB -> steady GR %.3f dB (attack %.1f ms, release %.1f ms)\n",
                     tightness, m.ratio, m.thresholdOffsetDb, expected, m.attackMs, m.releaseMs);
    }
}

//==============================================================================
static double kLevelDb (const std::vector<float>& x, double sr, int start)
{
    KWeighting k; k.prepare (sr);
    double sum = 0.0; int count = 0;
    for (int n = 0; n < (int) x.size(); ++n)
    {
        const double y = k.process (x[(size_t) n]);
        if (n >= start) { sum += y * y; ++count; }
    }
    return powerToDb (sum / count);
}

static void testGainMatch()
{
    std::printf ("Gain Match: converges to unity\n");
    const double sr = 48000.0;
    const int total = (int) (sr * 25.0);

    // Test signal: 55 Hz "bass" + filtered noise, with a kick-like pulse every 0.5 s.
    std::vector<float> input ((size_t) total);
    std::mt19937 rng (1234);
    std::normal_distribution<double> noise (0.0, 1.0);
    double lp = 0.0;
    for (int n = 0; n < total; ++n)
    {
        lp += 0.05 * (noise (rng) - lp);
        const double t = n / sr, tk = std::fmod (t, 0.5);
        const double kick = std::exp (-tk * 18.0) * std::sin (2.0 * pi * (50.0 + 80.0 * std::exp (-tk * 30.0)) * tk);
        input[(size_t) n] = (float) (0.25 * std::sin (2.0 * pi * 55.0 * t) + 0.3 * lp + 0.4 * kick);
    }

    struct Setup { const char* name; EngineParams p; };
    EngineParams a; a.gainMatch = true; a.lowEndDb = 6.0;
    EngineParams b; b.gainMatch = true; b.lowEndDb = -10.0; b.tightness = 1.0; b.sMode = 2;
    EngineParams c; c.gainMatch = true; c.inGainDb = 8.0; c.outGainDb = 0.0; c.sMode = 1;
    const Setup setups[] = { { "Low End +6 dB", a }, { "Low End -10, Tight, S2", b }, { "IN +8 dB, S1", c } };

    const double inLevel = kLevelDb (input, sr, total - (int) (sr * 3.0));
    for (auto& s : setups)
    {
        std::vector<float> l = input, r = input;
        TightBottomEngine e;
        e.prepare (sr);
        e.setParameters (s.p);
        float* chans[2] = { l.data(), r.data() };
        for (int pos = 0; pos < total; pos += 512)
        {
            float* blk[2] = { chans[0] + pos, chans[1] + pos };
            e.process (blk, 2, std::min (512, total - pos));
        }
        const double outLevel = kLevelDb (l, sr, total - (int) (sr * 3.0));
        std::printf ("  %-24s in %.2f dB  out %.2f dB  (correction %.2f dB)\n", s.name, inLevel, outLevel, e.meters.gainMatchDb.load());
        CHECK (std::abs (outLevel - inLevel) < 0.5, "%s: output %.2f dB vs input %.2f dB", s.name, outLevel, inLevel);
    }
}

//==============================================================================
static void testSafety()
{
    std::printf ("Safety: silence, mono, denormal-range input\n");
    for (double sr : { 44100.0, 192000.0 })
    {
        TightBottomEngine e;
        e.prepare (sr);
        EngineParams p; p.sMode = 2; p.gainMatch = true; p.lowEndDb = 10.0;
        e.setParameters (p);
        std::vector<float> buf (4096, 0.0f);
        buf[0] = 1.0f;
        for (int i = 1; i < 100; ++i) buf[(size_t) i] = 1.0e-38f;
        float* ch = buf.data();
        for (int rep = 0; rep < 50; ++rep)
        {
            e.process (&ch, 1, (int) buf.size());
            for (float v : buf)
                CHECK (std::isfinite (v) && std::abs (v) < 4.0f, "non-finite or huge output at sr %.0f", sr);
            std::fill (buf.begin(), buf.end(), 0.0f);
        }
    }
}

int main()
{
    testCrossoverSum();
    testCompressorStaticCurve();
    testGainMatch();
    testSafety();
    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
