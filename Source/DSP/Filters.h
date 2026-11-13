#pragma once

#include <cmath>
#include <initializer_list>

// Small, dependency-free filter building blocks. All state is double precision so
// low-frequency filters stay accurate (and quiet) at 192 kHz.
namespace tb
{
    constexpr double pi = 3.14159265358979323846;

    inline double dbToGain (double db)   { return std::pow (10.0, db * 0.05); }
    inline double gainToDb (double g)    { return 20.0 * std::log10 (g > 1.0e-15 ? g : 1.0e-15); }
    inline double powerToDb (double p)   { return 10.0 * std::log10 (p > 1.0e-30 ? p : 1.0e-30); }

    /** One-pole smoothing coefficient for a time constant in ms. */
    inline double onePoleCoef (double timeMs, double sampleRate)
    {
        if (timeMs <= 0.0) return 1.0;
        return 1.0 - std::exp (-1.0 / (timeMs * 0.001 * sampleRate));
    }

    //==========================================================================
    /** One-pole parameter smoother (ramps toward a target). */
    struct Smoother
    {
        void setTime (double ms, double sr)   { coef = onePoleCoef (ms, sr); }
        void snap (double v)                  { current = target = v; }
        void setTarget (double v)             { target = v; }
        double next()                         { current += coef * (target - current); return current; }
        bool isSettled() const                { return std::abs (target - current) < 1.0e-7; }

        double current = 0.0, target = 0.0, coef = 1.0;
    };

    //==========================================================================
    /** Transposed direct form II biquad (RBJ cookbook designs). */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        void reset() { z1 = z2 = 0.0; }

        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void setNormalised (double nb0, double nb1, double nb2, double na0, double na1, double na2)
        {
            b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
            a1 = na1 / na0; a2 = na2 / na0;
        }

        void makeHighPass (double sr, double freq, double q)
        {
            const double w = 2.0 * pi * freq / sr, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            setNormalised ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
        }

        void makePeak (double sr, double freq, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * pi * freq / sr, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            setNormalised (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
        }
    };

    //==========================================================================
    /** Topology-preserving-transform state variable filter (Zavalishin / Simper).
        Safe to retune every sample, which is why the crossover uses it. */
    struct Svf
    {
        double g = 0, k = 1.41421356237, a1 = 0, a2 = 0, a3 = 0;
        double ic1 = 0, ic2 = 0;

        void reset() { ic1 = ic2 = 0.0; }

        void setCutoff (double sr, double freq, double q)
        {
            g = std::tan (pi * freq / sr);
            k = 1.0 / q;
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        // Returns low-pass, writes high-pass.
        double process (double x, double& hp)
        {
            const double v3 = x - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            hp = x - k * v1 - v2;
            return v2;
        }

        double lowPass (double x)  { double hp; return process (x, hp); }
        double highPass (double x) { double hp; process (x, hp); return hp; }
    };

    //==========================================================================
    /** 4th-order Linkwitz-Riley crossover for one channel: two cascaded
        Butterworth low-passes and two cascaded Butterworth high-passes.
        low + high is an all-pass (flat magnitude), and both bands share the same
        phase, so the untouched high band stays phase-aligned with the low band. */
    struct LinkwitzRiley4
    {
        Svf lp1, lp2, hp1, hp2;

        void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }

        void setCutoff (double sr, double freq)
        {
            constexpr double butterQ = 0.70710678118654752;
            for (auto* f : { &lp1, &lp2, &hp1, &hp2 })
                f->setCutoff (sr, freq, butterQ);
        }

        void process (double x, double& low, double& high)
        {
            low  = lp2.lowPass (lp1.lowPass (x));
            high = hp2.highPass (hp1.highPass (x));
        }
    };

    //==========================================================================
    /** ITU-R BS.1770 K-weighting (pre-filter shelf + RLB high-pass), with the
        coefficients derived for any sample rate (same method as libebur128). */
    struct KWeighting
    {
        Biquad shelf, rlb;

        void prepare (double sr)
        {
            {
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (pi * f0 / sr);
                const double Vh = std::pow (10.0, G / 20.0);
                const double Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                shelf.setNormalised (Vh + Vb * K / Q + K * K, 2.0 * (K * K - Vh), Vh - Vb * K / Q + K * K,
                                     a0, 2.0 * (K * K - 1.0), 1.0 - K / Q + K * K);
            }
            {
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan (pi * f0 / sr);
                rlb.setNormalised (1.0, -2.0, 1.0, 1.0 + K / Q + K * K, 2.0 * (K * K - 1.0), 1.0 - K / Q + K * K);
            }
            reset();
        }

        void reset() { shelf.reset(); rlb.reset(); }
        double process (double x) { return rlb.process (shelf.process (x)); }
    };
}
