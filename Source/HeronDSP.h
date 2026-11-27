// HeronDSP.h — framework-independent DSP core for Heron EQ.
//
// Topology (modelled on a classic "boost/cut amplifier" parallel equaliser,
// the way many discrete Class-A program EQs are built):
//
//   * Four band filters sit around one boost/cut amplifier.
//   * A band set to BOOST feeds its filtered signal forward and adds it to the
//     input:                              y_ff = x + sum_k k_i * F_i(x)
//   * A band set to CUT sits in the amp's feedback path, which gives a
//     response that mirrors the boost:     y + sum_c m_i * F_i(y) = y_ff
//     The loop has no delay, so it is solved exactly each sample. That works
//     because every TPT state-variable filter output is affine in its input.
//   * So the overall transfer function is
//         H = (1 + sum_k k_i F_i) / (1 + sum_c m_i F_i)
//     with k = A - 1 for a boost and m = 1/A - 1 for a cut (A = linear gain).
//     Boost and cut curves are exact mirror images, and bands that overlap
//     interact the way a real parallel EQ does, not like a cascade of biquads.
//   * F_i is a unity-peak band-pass for a peaking band, a low-pass for the
//     band-1 shelf and a high-pass for the band-4 shelf.
//
// "Colour" adds 2nd and 3rd harmonics. They come either from one band's
// filter signal (scaled by that band's boost or cut, so a cut subtracts
// harmonics) or from the full programme.
//
// The core is meant to run at 2x oversampling. The plugin wrapper does that.

#pragma once

#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

namespace heron
{

constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// Stepped frequency tables (note-based, 12 positions per band, +1 whole step)
//==============================================================================
namespace freqs
{
    // MIDI note of the lowest position of each band. Positions are 4 semitones
    // (a major third) apart; the "+1 step" switch adds 2 semitones.
    //   Band 1: C1  (32.7 Hz)  ... G#4 (415 Hz)
    //   Band 2: C#3 (138.6 Hz) ... A6  (1760 Hz)
    //   Band 3: A#4 (466.2 Hz) ... F#8 (5920 Hz)
    //   Band 4: G6  (1568 Hz)  ... D#10 (19.9 kHz)
    constexpr int bandBaseNote[4] = { 24, 49, 70, 91 };
    constexpr int numPositions = 12;
    constexpr int semitonesPerPosition = 4;
    constexpr int semitonesPerStep = 2;

    inline int noteFor (int band, int position, bool plusStep) noexcept
    {
        return bandBaseNote[band] + position * semitonesPerPosition + (plusStep ? semitonesPerStep : 0);
    }

    inline double noteToHz (int midiNote) noexcept
    {
        return 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
    }

    inline double hzFor (int band, int position, bool plusStep) noexcept
    {
        return noteToHz (noteFor (band, position, plusStep));
    }

    inline const char* noteName (int midiNote) noexcept
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((midiNote % 12) + 12) % 12];
    }

    // Low-cut positions: index 0 is OUT.
    constexpr int numLowCut = 12;
    constexpr double lowCutHz[numLowCut] = { 0.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 120.0, 150.0 };
}

//==============================================================================
// Control-law helpers
//==============================================================================

// Boost/cut knob: +/-12 dB, with a flat dead zone of +/-0.3 dB around the
// centre so a band is easy to set exactly flat.
inline double applyDeadZone (double knobDb) noexcept
{
    constexpr double dz = 0.3, range = 12.0;
    const double a = std::abs (knobDb);
    if (a <= dz) return 0.0;
    return std::copysign ((a - dz) * range / (range - dz), knobDb);
}

// Bandwidth is specified in octaves at full (12 dB) boost, measured 3 dB
// below the peak. For H = 1 + k*BP with k = 3 (12 dB), |H| = peak/sqrt2 when
// Q*(r - 1/r) = sqrt(8/7), where r = 2^(N/2).
inline double bandwidthToQ (double octaves) noexcept
{
    octaves = std::clamp (octaves, 0.1, 6.0);
    const double r = std::pow (2.0, octaves * 0.5);
    return std::sqrt (8.0 / 7.0) / (r - 1.0 / r);
}

//==============================================================================
// TPT state-variable filter (Simper) exposing its outputs as affine functions
// of the current input, so a delay-free feedback loop can be solved exactly.
//==============================================================================
struct SVF
{
    // LowPass1 / HighPass1: first-order (6 dB/oct) responses for the SHELVES. The
    // original's manual (BAND 1 / BAND 4 EQ CURVES) shows first-order shelves that
    // BANDWIDTH does not change: 1st-order fits its measured +/-12 dB shelves within
    // 0.3 dB rms with the corner at the selected frequency.
    enum class Mode { BandPass, LowPass, HighPass, LowPass1, HighPass1 };

    double g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    double ic1 = 0, ic2 = 0;
    Mode mode = Mode::BandPass;

    void setup (double fc, double q, double sampleRate, Mode m) noexcept
    {
        fc = std::clamp (fc, 5.0, sampleRate * 0.49);
        g = std::tan (kPi * fc / sampleRate);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        mode = m;
        G1 = g / (1.0 + g);                     // first-order TPT one-pole
    }

    void reset() noexcept { ic1 = ic2 = 0; }

    double G1 = 0;                              // first-order modes: state in ic1

    // Output = alpha * v0 + beta for the current state.
    // Band-pass output is normalised to unity gain at the centre (k * v1).
    void affine (double& alpha, double& beta) const noexcept
    {
        // v1 = a2*v0 + (a1*ic1 - a2*ic2)
        // v2 = a3*v0 + (ic2 + a2*ic1 - a3*ic2)
        const double v1a = a2, v1b = a1 * ic1 - a2 * ic2;
        const double v2a = a3, v2b = ic2 + a2 * ic1 - a3 * ic2;
        switch (mode)
        {
            case Mode::BandPass: alpha = k * v1a; beta = k * v1b; break;
            case Mode::LowPass:  alpha = v2a;     beta = v2b;     break;
            case Mode::HighPass: alpha = 1.0 - k * v1a - v2a; beta = -k * v1b - v2b; break;
            case Mode::LowPass1:  alpha = G1;       beta = (1.0 - G1) * ic1;  break;   // lp = G x + (1-G) s
            case Mode::HighPass1: alpha = 1.0 - G1; beta = -(1.0 - G1) * ic1; break;   // hp = x - lp
        }
    }

    // Advance the state with input v0.
    void tick (double v0) noexcept
    {
        if (mode == Mode::LowPass1 || mode == Mode::HighPass1)
        {
            const double v = (v0 - ic1) * G1;
            ic1 = ic1 + 2.0 * v;                // s = lp + v
            return;
        }
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
    }

    double process (double v0) noexcept
    {
        double a = 0, b = 0; affine (a, b);
        const double out = a * v0 + b;
        tick (v0);
        return out;
    }
};

//==============================================================================
// Low cut: Butterworth high-pass, 12 dB/oct, or 24 dB/oct when STEEP is on.
//==============================================================================
struct LowCut
{
    static constexpr double kMain = 0.9, kMainQ = 0.45, kSteep = 0.355, kSteepQ = 0.65;
    SVF s1, s2;
    bool enabled = false, steep = false;

    void setup (double fc, bool isSteep, double sr) noexcept
    {
        enabled = fc > 0.0;
        steep = isSteep;
        if (! enabled) return;
        // As the original (manual: LOW CUT EQ CURVES at 40 / 80 / 150 Hz, fitted):
        // 12 dB/oct is a soft-knee 2nd-order high-pass (Q 0.45, corner at 0.9 x the
        // setting; Butterworth fits 3-5x worse); STEEP keeps that knee and adds a
        // second 2nd-order section lower down (Q 0.65 at 0.355 x the setting).
        s1.setup (fc * kMain, kMainQ, sr, SVF::Mode::HighPass);
        if (steep) s2.setup (fc * kSteep, kSteepQ, sr, SVF::Mode::HighPass);
    }

    void reset() noexcept { s1.reset(); s2.reset(); }

    double process (double x) noexcept
    {
        if (! enabled) return x;
        x = s1.process (x);
        return steep ? s2.process (x) : x;
    }
};

//==============================================================================
// Discrete class-A output stage, to the original's spec sheet: 0.001 % THD at
// +20 dBm (EQ flat), clipping at +25 dBm. Units: v = 1.0 RMS <-> 0 VU = +4 dBu
// = -18 dBFS; +25 dBm is a sine peak of ~15.9 v (about +6 dBFS peak). A biased
// saturating curve (2nd harmonic first, then 3rd near the rail), hard knee.
// Tabulated (cubic Hermite) so it costs no pow() per sample.
//==============================================================================
struct ClassAOutput
{
    static constexpr double kDigitalToVolts = 7.943282347242815;   // 10^(18/20)
    static constexpr int tableN = 16384;
    static constexpr double tableW = 4.0;           // the curve for w in [-4, 4] (x4 the rail); exact outside
    double vc = 16.0, b = 0.002, p = 14.0, s0 = 0.0, d0 = 1.0, invStep = 1.0;
    std::vector<double> tab, dtab;

    void set (double clip, double bias, double hardness)
    {
        vc = clip; b = bias; p = hardness;
        s0 = sat (b);
        d0 = std::pow (1.0 + std::pow (std::abs (b), p), -1.0 - 1.0 / p);
        tab.resize (tableN + 1); dtab.resize (tableN + 1);
        const double h = 2.0 * tableW / tableN;
        invStep = 1.0 / h;
        for (int i = 0; i <= tableN; ++i)
        {
            const double w = -tableW + h * i;
            tab[(size_t) i]  = sat (w);
            dtab[(size_t) i] = std::pow (1.0 + std::pow (std::abs (w), p), -1.0 - 1.0 / p) * h;
        }
    }
    double sat (double u) const { return u / std::pow (1.0 + std::pow (std::abs (u), p), 1.0 / p); }
    double satFast (double w) const
    {
        const double x = (w + tableW) * invStep;
        if (! (x >= 0.0 && x < (double) tableN)) return sat (w);
        const int i = (int) x;
        const double t = x - i, t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * tab[(size_t) i] + (t3 - 2 * t2 + t) * dtab[(size_t) i]
             + (-2 * t3 + 3 * t2) * tab[(size_t) i + 1] + (t3 - t2) * dtab[(size_t) i + 1];
    }
    /** digital sample in, digital sample out */
    double process (double x) const
    {
        const double u = x * kDigitalToVolts / vc;
        return vc * (satFast (u + b) - s0) / d0 / kDigitalToVolts;
    }
};

//==============================================================================
// Colour: additive 2nd/3rd-harmonic generator. Returns only the harmonic part.
//==============================================================================
inline double harmonics (double s, double amount01) noexcept
{
    // Gentle clamp keeps the polynomial well behaved on hot signals.
    s = std::clamp (s, -2.0, 2.0);
    const double a2 = 0.22 * amount01;   // even (2nd): warmth
    const double a3 = 0.12 * amount01;   // odd (3rd): presence and edge
    return a2 * s * s - a3 * s * s * s;
}

//==============================================================================
// Parameters for one processing block
//==============================================================================
struct BandSettings
{
    int position = 0;         // 0..11
    bool plusStep = false;
    double gainDb = 0.0;      // knob value; the dead zone is applied inside
    double bandwidthOct = 1.5;
    bool shelf = false;       // bands 0 and 3 only
};

enum class ColourSource { Off = 0, Band1, Band2, Band3, Band4, Full };

struct Settings
{
    std::array<BandSettings, 4> bands;
    int lowCutIndex = 0;      // 0 = OUT
    bool lowCutSteep = false;
    ColourSource colourSource = ColourSource::Off;
    double colour = 0.0;      // 0..10
};

//==============================================================================
// One channel of the equaliser
//==============================================================================
class ChannelEQ
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        { const double g = std::tan (kPi * kCouplingHz / sr); cplG = g / (1.0 + g); }
        if (outStage.tab.empty()) outStage.set (16.0, 0.002, 14.0);
        // DC blocker for the even-harmonic path (about 5 Hz).
        dcR = std::exp (-2.0 * kPi * 5.0 / sr);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : ff) b.reset();
        for (auto& b : fb) b.reset();
        for (int i = 0; i < 4; ++i) cplFF[i] = cplFB[i] = 0.0;
        lowCut.reset();
        dcX = dcY = 0; apZ = 0;
    }

    // Per-band smoothed values, so automation is zipper-free. The caller
    // supplies already-smoothed values through `update`.
    struct BandCoeffs { double fc = 1000, q = 1, k = 0, m = 0; SVF::Mode mode = SVF::Mode::BandPass; };

    /** analog component tolerances of this channel (two hand-built channels never match):
        frequency, Q and gain multipliers, and a first-order all-pass corner (phase only) */
    void setTolerance (double freqMul, double qMul, double gainMul, double allpassHz) noexcept
    {
        tolF = freqMul; tolQ = qMul; tolG = gainMul; apHz = allpassHz;
        if (apHz > 0.0) { const double t = std::tan (kPi * std::min (apHz, 0.45 * sr) / sr); apA = (t - 1.0) / (t + 1.0); }
        outStage.set (16.0 * clipMulFor (gainMul), 0.002, 14.0);
    }
    static double clipMulFor (double gainMul) noexcept { return gainMul > 1.0 ? 0.99 : 1.0; }   // R rail 1 % lower

    void update (const std::array<BandCoeffs, 4>& bc, double lowCutHz, bool steep,
                 ColourSource src, double colour01) noexcept
    {
        auto tolGain = [this] (double km) { return km > 0.0 ? std::pow (1.0 + km, tolG) - 1.0 : 0.0; };   // gain in dB * tolG
        for (size_t i = 0; i < 4; ++i)
        {
            ff[i].setup (bc[i].fc * tolF, bc[i].q * tolQ, sr, bc[i].mode);
            fb[i].setup (bc[i].fc * tolF, bc[i].q * tolQ, sr, bc[i].mode);
            kBoost[i] = tolGain (bc[i].k);
            mCut[i]   = tolGain (bc[i].m);
            cplOn[i]  = couplingFor ((int) i, bc[i].mode);
        }
        if (lowCutHz != lastLowCutHz || steep != lastSteep)
        {
            lowCut.setup (lowCutHz, steep, sr);
            if (steep != lastSteep) lowCut.reset();
            lastLowCutHz = lowCutHz; lastSteep = steep;
        }
        colourSrc = src;
        colourAmt = colour01;
    }

    double process (double xIn) noexcept
    {
        const double x = lowCut.process (xIn);
        const int colourBand = (colourSrc >= ColourSource::Band1 && colourSrc <= ColourSource::Band4)
                                 ? int (colourSrc) - 1 : -1;
        double harm = 0.0;

        // --- Feed-forward (boost) section ---
        double yff = x;
        for (int i = 0; i < 4; ++i)
        {
            double f = ff[i].process (x);
            if (cplOn[i]) f = couplingHP (f, cplFF[i]);
            yff += kBoost[i] * f;
            if (i == colourBand && kBoost[i] > 0.0)
                harm += kBoost[i] * harmonics (f, colourAmt);
        }

        // --- Feedback (cut) section, solved without delay ---
        double num = yff, den = 1.0;
        double alpha[4], beta[4];
        double uA[4], uB[4];                            // the band filter's own output (before coupling)
        for (int i = 0; i < 4; ++i)
        {
            fb[i].affine (alpha[i], beta[i]);
            uA[i] = alpha[i]; uB[i] = beta[i];
            if (cplOn[i]) { alpha[i] *= 1.0 - cplG; beta[i] = (1.0 - cplG) * (beta[i] - cplFB[i]); }   // hp = (1-G)(u - s)
            num -= mCut[i] * beta[i];
            den += mCut[i] * alpha[i];
        }
        const double y = num / den;
        for (int i = 0; i < 4; ++i)
        {
            if (i == colourBand && mCut[i] > 0.0)
            {
                const double f = alpha[i] * y + beta[i];
                // A cut removes harmonic content (it is in the feedback path).
                harm -= mCut[i] * harmonics (f, colourAmt) / den;
            }
            fb[i].tick (y);
            if (cplOn[i]) couplingHP (uA[i] * y + uB[i], cplFB[i]);
        }

        if (colourSrc == ColourSource::Full)
            harm += harmonics (y, colourAmt);

        // DC-block only the harmonic signal, so the clean path stays untouched.
        const double h = harm - dcX + dcR * dcY;
        dcX = harm; dcY = h;

        double out = y + h;
        if (apHz > 0.0)                                                // channel tolerance: phase only
        {
            const double a = apA * out + apZ;
            apZ = out - apA * a;
            out = a;
        }
        return outStage.process (out);                                 // discrete class-A output stage
    }

private:
    double sr = 96000.0;
    SVF ff[4], fb[4];
    double kBoost[4] {}, mCut[4] {};
    LowCut lowCut;
    double lastLowCutHz = -1; bool lastSteep = false;
    ColourSource colourSrc = ColourSource::Off;
    double colourAmt = 0.0;
    double dcR = 0.999, dcX = 0, dcY = 0;
    double tolF = 1.0, tolQ = 1.0, tolG = 1.0, apHz = 0.0, apA = 0.0, apZ = 0.0;
    ClassAOutput outStage;

public:
    // Coupling capacitor in the band-filter path of the bells on bands 3 and 4: the
    // manual's band-3 peaking curves show the widest +12 dB bell 0.8..1 dB down below
    // ~50 Hz (and the widest cut as much up); a first-order 22 Hz high-pass inside the
    // filter path gives exactly that. Band 1's curves are flat to 20 Hz: none there.
    static constexpr double kCouplingHz = 22.0;
    static bool couplingFor (int band, SVF::Mode mode) noexcept { return band >= 2 && mode == SVF::Mode::BandPass; }
private:
    double cplG = 0.0, cplFF[4] {}, cplFB[4] {};
    bool cplOn[4] {};
    double couplingHP (double u, double& s) const noexcept
    {
        const double v = (u - s) * cplG, lp = v + s;
        s = lp + v;
        return u - lp;
    }
};

//==============================================================================
// Turn user settings into per-band coefficient targets
//==============================================================================
inline ChannelEQ::BandCoeffs makeBandCoeffs (int band, const BandSettings& s)
{
    ChannelEQ::BandCoeffs c;
    c.fc = freqs::hzFor (band, std::clamp (s.position, 0, freqs::numPositions - 1), s.plusStep);
    const bool shelf = s.shelf && (band == 0 || band == 3);
    if (shelf)
    {
        c.mode = band == 0 ? SVF::Mode::LowPass1 : SVF::Mode::HighPass1;   // 6 dB/oct, as the original
        c.q = 0.70710678;                                                  // (unused: BANDWIDTH has no effect)
    }
    else
    {
        c.mode = SVF::Mode::BandPass;
        c.q = bandwidthToQ (s.bandwidthOct);
    }
    const double g = applyDeadZone (s.gainDb);
    const double A = std::pow (10.0, g / 20.0);
    c.k = g > 0 ? A - 1.0 : 0.0;
    c.m = g < 0 ? 1.0 / A - 1.0 : 0.0;
    return c;
}

//==============================================================================
// Analytic magnitude response, used for the on-screen curve
//==============================================================================
inline double magnitudeAt (const Settings& s, double hz)
{
    // Analog prototypes: BP(s) = (s/Q)/(s^2 + s/Q + 1), LP = 1/(...), HP = s^2/(...).
    struct C { double re, im; };
    auto mul = [] (C a, C b) { return C { a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re }; };
    auto div = [] (C a, C b) { const double d = b.re * b.re + b.im * b.im; return C { (a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d }; };
    auto absC = [] (C a) { return std::sqrt (a.re * a.re + a.im * a.im); };

    C num { 1, 0 }, den { 1, 0 };
    for (int i = 0; i < 4; ++i)
    {
        const auto c = makeBandCoeffs (i, s.bands[(size_t) i]);
        const double w = hz / c.fc;
        const C sj { 0, w };
        const C d { 1 - w * w, w / c.q };
        C F { 0, 0 };
        switch (c.mode)
        {
            case SVF::Mode::BandPass: F = div (C { 0, w / c.q }, d); break;
            case SVF::Mode::LowPass:  F = div (C { 1, 0 }, d); break;
            case SVF::Mode::HighPass: F = div (mul (sj, sj), d); break;
            case SVF::Mode::LowPass1:  F = div (C { 1, 0 }, C { 1, w }); break;
            case SVF::Mode::HighPass1: F = div (C { 0, w }, C { 1, w }); break;
        }
        if (ChannelEQ::couplingFor (i, c.mode))
        {
            const double wc = hz / ChannelEQ::kCouplingHz;
            F = mul (F, div (C { 0, wc }, C { 1, wc }));
        }
        num.re += c.k * F.re; num.im += c.k * F.im;
        den.re += c.m * F.re; den.im += c.m * F.im;
    }
    double mag = absC (num) / absC (den);

    const double lc = freqs::lowCutHz[std::clamp (s.lowCutIndex, 0, freqs::numLowCut - 1)];
    if (lc > 0)
    {
        auto hp2 = [hz] (double fc, double q)
        {
            const double w = hz / fc;
            return w * w / std::sqrt ((1.0 - w * w) * (1.0 - w * w) + (w / q) * (w / q));
        };
        mag *= hp2 (lc * LowCut::kMain, LowCut::kMainQ);
        if (s.lowCutSteep) mag *= hp2 (lc * LowCut::kSteep, LowCut::kSteepQ);
    }
    return mag;
}

} // namespace heron
