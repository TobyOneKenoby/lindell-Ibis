// Standalone tests for the Heron EQ DSP core (no JUCE needed).
// Build: c++ -std=c++17 -O2 -I../Source DSPTests.cpp -o dsp_tests && ./dsp_tests

#include "HeronDSP.h"
#include <cstdio>
#include <vector>
#include <random>
#include <complex>

using namespace heron;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

constexpr double SR = 96000.0; // the core runs at 2x of 48 kHz

static void configure (ChannelEQ& eq, const Settings& s)
{
    std::array<ChannelEQ::BandCoeffs, 4> bc;
    for (int i = 0; i < 4; ++i) bc[(size_t) i] = makeBandCoeffs (i, s.bands[(size_t) i]);
    eq.update (bc, freqs::lowCutHz[s.lowCutIndex], s.lowCutSteep, s.colourSource, s.colour / 10.0);
}

// Measured steady-state gain (dB) of a sine through the EQ.
static double measureDb (const Settings& s, double hz)
{
    ChannelEQ eq; eq.prepare (SR); configure (eq, s);
    const int settle = (int) (SR * 0.5), len = (int) (SR * 0.25);
    std::complex<double> acc { 0, 0 };
    for (int n = 0; n < settle + len; ++n)
    {
        const double ph = 2.0 * kPi * hz * n / SR;
        const double y = eq.process (std::sin (ph));
        if (n >= settle) acc += y * std::polar (1.0, -ph);
    }
    return 20.0 * std::log10 (2.0 * std::abs (acc) / len);
}

static double analyticDb (const Settings& s, double hz) { return 20.0 * std::log10 (magnitudeAt (s, hz)); }

int main()
{
    // 1. Frequency tables match the published chart.
    CHECK (std::abs (freqs::hzFor (0, 0, false) - 32.70) < 0.01, "band1 lowest");
    CHECK (std::abs (freqs::hzFor (0, 0, true)  - 36.71) < 0.01, "band1 +1 lowest");
    CHECK (std::abs (freqs::hzFor (1, 0, true)  - 155.56) < 0.01, "band2 +1 lowest");
    CHECK (std::abs (freqs::hzFor (2, 0, true)  - 523.25) < 0.01, "band3 +1 lowest");
    CHECK (std::abs (freqs::hzFor (3, 0, true)  - 1760.0) < 0.01, "band4 +1 lowest");
    CHECK (std::abs (freqs::hzFor (3, 11, false) - 19912.1) < 1.0, "band4 highest");
    CHECK (std::abs (freqs::hzFor (1, 5, false) - 440.0) < 0.01, "band2 A440");

    // 2. Dead zone.
    CHECK (applyDeadZone (0.29) == 0.0 && applyDeadZone (-0.29) == 0.0, "dead zone");
    CHECK (std::abs (applyDeadZone (12.0) - 12.0) < 1e-12 && std::abs (applyDeadZone (-12.0) + 12.0) < 1e-12, "dead zone endpoints");

    // 3. Flat settings are bit-transparent (apart from double rounding).
    {
        Settings s; ChannelEQ eq; eq.prepare (SR); configure (eq, s);
        std::mt19937 rng (1); std::uniform_real_distribution<double> d (-1, 1);
        double maxErr = 0;
        for (int n = 0; n < 48000; ++n) { const double x = d (rng); maxErr = std::max (maxErr, std::abs (eq.process (x) - x)); }
        CHECK (maxErr < 1e-12, "flat EQ not transparent: %g", maxErr);
    }

    // 4. Peak boost and cut hit +/-12 dB at the centre and mirror each other.
    for (int band = 0; band < 4; ++band)
    {
        Settings s; s.bands[(size_t) band].position = 6; s.bands[(size_t) band].gainDb = 12.0;
        const double fc = freqs::hzFor (band, 6, false);
        const double boost = measureDb (s, fc);
        s.bands[(size_t) band].gainDb = -12.0;
        const double cut = measureDb (s, fc);
        std::printf ("band %d @ %8.1f Hz: boost %+6.2f dB, cut %+6.2f dB\n", band + 1, fc, boost, cut);
        CHECK (std::abs (boost - 12.0) < 0.15, "band %d boost %.2f", band + 1, boost);
        CHECK (std::abs (cut + 12.0) < 0.15, "band %d cut %.2f", band + 1, cut);
        // Symmetry off-centre
        const double off = fc * 1.6;
        if (off < 18000)
        {
            s.bands[(size_t) band].gainDb = 6.0;  const double b = measureDb (s, off);
            s.bands[(size_t) band].gainDb = -6.0; const double c = measureDb (s, off);
            CHECK (std::abs (b + c) < 0.05, "band %d asymmetric off-centre: %.3f vs %.3f", band + 1, b, c);
        }
    }

    // 5. Bandwidth: 1.5 oct at +12 dB, -3 dB points about 1.5 octaves apart.
    {
        Settings s; auto& b = s.bands[2]; b.position = 4; b.gainDb = 12.0; b.bandwidthOct = 1.5;
        const double fc = freqs::hzFor (2, 4, false);
        const double lo = fc / std::pow (2.0, 0.75), hi = fc * std::pow (2.0, 0.75);
        const double dlo = analyticDb (s, lo), dhi = analyticDb (s, hi);
        std::printf ("bandwidth 1.5 oct: %.2f dB / %.2f dB at the edges (target 9.0)\n", dlo, dhi);
        CHECK (std::abs (dlo - 9.0) < 0.05 && std::abs (dhi - 9.0) < 0.05, "bandwidth edges");
    }

    // 6. Measured response matches the analytic display curve for a busy setting.
    {
        Settings s;
        s.bands[0] = { 3, false, 6.0, 2.0, true };
        s.bands[1] = { 4, true, -5.0, 0.8, false };
        s.bands[2] = { 6, false, 8.0, 1.0, false };
        s.bands[3] = { 6, false, -4.0, 3.0, true };
        s.lowCutIndex = 3;
        double worst = 0;
        for (double hz : { 25.0, 50.0, 100.0, 200.0, 400.0, 700.0, 1000.0, 1500.0, 2200.0, 3300.0, 5000.0, 8000.0 })
        {
            const double m = measureDb (s, hz), a = analyticDb (s, hz);
            worst = std::max (worst, std::abs (m - a));
        }
        std::printf ("analytic vs measured, worst deviation below 8 kHz: %.3f dB\n", worst);
        CHECK (worst < 0.35, "analytic curve deviates %.3f dB", worst);
    }

    // 7. Low cut: -3 dB at the corner; 12 vs 24 dB/oct an octave below.
    {
        Settings s; s.lowCutIndex = 7; // 80 Hz
        const double c12 = measureDb (s, 80.0), o12 = measureDb (s, 40.0);
        s.lowCutSteep = true;
        const double c24 = measureDb (s, 80.0), o24 = measureDb (s, 40.0);
        std::printf ("low cut 80 Hz: corner %.2f / %.2f dB, one octave down %.2f / %.2f dB\n", c12, c24, o12, o24);
        CHECK (std::abs (c12 + 3.01) < 0.1 && std::abs (c24 + 3.01) < 0.1, "low cut corner");
        CHECK (std::abs (o12 + 12.3) < 0.5 && std::abs (o24 + 24.1) < 0.5, "low cut slope");
    }

    // 8. Colour adds 2nd and 3rd harmonics; with colour off there are none.
    auto harmonicLevels = [] (const Settings& s, double hz, double amp, double& h2, double& h3)
    {
        ChannelEQ eq; eq.prepare (SR); configure (eq, s);
        const int settle = (int) SR, len = (int) SR;
        std::complex<double> a1 {}, a2 {}, a3 {};
        for (int n = 0; n < settle + len; ++n)
        {
            const double ph = 2.0 * kPi * hz * n / SR;
            const double y = eq.process (amp * std::sin (ph));
            if (n >= settle) { a1 += y * std::polar (1.0, -ph); a2 += y * std::polar (1.0, -2 * ph); a3 += y * std::polar (1.0, -3 * ph); }
        }
        h2 = 20 * std::log10 (std::abs (a2) / std::abs (a1) + 1e-20);
        h3 = 20 * std::log10 (std::abs (a3) / std::abs (a1) + 1e-20);
    };
    {
        Settings s; double h2, h3;
        harmonicLevels (s, 1000.0, 0.5, h2, h3);
        CHECK (h2 < -200 && h3 < -200, "harmonics with colour off: %.1f %.1f", h2, h3);
        s.colourSource = ColourSource::Full; s.colour = 10.0;
        harmonicLevels (s, 1000.0, 0.5, h2, h3);
        std::printf ("full colour 10 @ -6 dBFS: H2 %.1f dB, H3 %.1f dB\n", h2, h3);
        CHECK (h2 > -40 && h2 < -15 && h3 > -60 && h3 < -25, "full colour harmonic levels");
        s.colour = 3.0;
        double h2b, h3b; harmonicLevels (s, 1000.0, 0.5, h2b, h3b);
        CHECK (h2b < h2 - 8, "colour knob does not scale harmonics");

        // Colour on a boosted band adds; on a cut band it subtracts (opposite polarity).
        Settings b; b.bands[1] = { 5, false, 9.0, 1.5, false }; b.colourSource = ColourSource::Band2; b.colour = 10.0;
        harmonicLevels (b, 440.0, 0.3, h2, h3);
        std::printf ("band-2 colour on a +9 dB boost: H2 %.1f dB, H3 %.1f dB\n", h2, h3);
        CHECK (h2 > -60, "band colour has no effect");
        b.colourSource = ColourSource::Band1; // band 1 is flat, so nothing happens
        harmonicLevels (b, 440.0, 0.3, h2, h3);
        CHECK (h2 < -200, "colour on a flat band should do nothing (%.1f)", h2);
    }

    // 9. Stability: random settings, random noise, hot input, no NaN/Inf or blow-up.
    {
        std::mt19937 rng (42);
        std::uniform_real_distribution<double> u (0, 1);
        for (int trial = 0; trial < 200; ++trial)
        {
            Settings s;
            for (int i = 0; i < 4; ++i)
                s.bands[(size_t) i] = { int (u (rng) * 12), u (rng) > 0.5, u (rng) * 24 - 12, 0.2 * std::pow (20.0, u (rng)), u (rng) > 0.5 };
            s.lowCutIndex = int (u (rng) * 12); s.lowCutSteep = u (rng) > 0.5;
            s.colourSource = (ColourSource) int (u (rng) * 6); s.colour = u (rng) * 10;
            ChannelEQ eq; eq.prepare (SR); configure (eq, s);
            double peak = 0;
            for (int n = 0; n < 20000; ++n)
            {
                const double y = eq.process ((u (rng) * 2 - 1) * 2.0);
                if (! std::isfinite (y)) { peak = 1e9; break; }
                peak = std::max (peak, std::abs (y));
            }
            CHECK (peak < 100.0, "trial %d unstable (peak %g)", trial, peak);
        }
    }

    std::printf (failures == 0 ? "\nAll DSP tests passed.\n" : "\n%d DSP test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
