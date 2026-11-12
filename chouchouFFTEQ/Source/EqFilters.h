#pragma once

#include <JuceHeader.h>
#include <complex>
#include <cmath>

namespace chouchoueq
{
static constexpr double kAbsMaxHz = 96000.0;

inline double ceilingHz (double sampleRate) noexcept
{
    return juce::jmin (kAbsMaxHz, sampleRate > 0.0 ? sampleRate * 0.49 : kAbsMaxHz);
}

// Transposed direct-form II biquad, RBJ cookbook.
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setUnity() noexcept
    {
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
    }

    void copyCoeffs (const Biquad& o) noexcept
    {
        b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2;
    }

    float responseDb (double hz, double sampleRate) const noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * hz / sampleRate;
        const std::complex<double> z = std::exp (std::complex<double> (0.0, -w));
        const std::complex<double> z2 = z * z;
        const auto num = (double) b0 + (double) b1 * z + (double) b2 * z2;
        const auto den = 1.0 + (double) a1 * z + (double) a2 * z2;
        return (float) juce::Decibels::gainToDecibels ((float) std::abs (num / den), -100.0f);
    }

    static Biquad peak (double sr, double hz, double q, double gainDb) noexcept
    {
        if (std::abs (gainDb) < 0.01)
        {
            Biquad u;
            u.setUnity();
            return u;
        }

        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * hz / sr;
        const double c = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha / A;
        Biquad f;
        f.b0 = (float) ((1.0 + alpha * A) / a0);
        f.b1 = (float) ((-2.0 * c) / a0);
        f.b2 = (float) ((1.0 - alpha * A) / a0);
        f.a1 = f.b1;
        f.a2 = (float) ((1.0 - alpha / A) / a0);
        return f;
    }

    static Biquad lowShelf (double sr, double hz, double q, double gainDb) noexcept
    {
        if (std::abs (gainDb) < 0.01)
        {
            Biquad u;
            u.setUnity();
            return u;
        }

        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * hz / sr;
        const double c = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double two = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1.0) + (A - 1.0) * c + two;
        Biquad f;
        f.b0 = (float) (A * ((A + 1.0) - (A - 1.0) * c + two) / a0);
        f.b1 = (float) (2.0 * A * ((A - 1.0) - (A + 1.0) * c) / a0);
        f.b2 = (float) (A * ((A + 1.0) - (A - 1.0) * c - two) / a0);
        f.a1 = (float) (-2.0 * ((A - 1.0) + (A + 1.0) * c) / a0);
        f.a2 = (float) (((A + 1.0) + (A - 1.0) * c - two) / a0);
        return f;
    }

    static Biquad highShelf (double sr, double hz, double q, double gainDb) noexcept
    {
        if (std::abs (gainDb) < 0.01)
        {
            Biquad u;
            u.setUnity();
            return u;
        }

        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * hz / sr;
        const double c = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double two = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1.0) - (A - 1.0) * c + two;
        Biquad f;
        f.b0 = (float) (A * ((A + 1.0) + (A - 1.0) * c + two) / a0);
        f.b1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0);
        f.b2 = (float) (A * ((A + 1.0) + (A - 1.0) * c - two) / a0);
        f.a1 = (float) (2.0 * ((A - 1.0) - (A + 1.0) * c) / a0);
        f.a2 = (float) (((A + 1.0) - (A - 1.0) * c - two) / a0);
        return f;
    }

    static Biquad highPass (double sr, double hz, double q) noexcept
    {
        const double w0 = juce::MathConstants<double>::twoPi * hz / sr;
        const double c = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad f;
        f.b0 = (float) ((1.0 + c) * 0.5 / a0);
        f.b1 = (float) (-(1.0 + c) / a0);
        f.b2 = f.b0;
        f.a1 = (float) (-2.0 * c / a0);
        f.a2 = (float) ((1.0 - alpha) / a0);
        return f;
    }

    static Biquad lowPass (double sr, double hz, double q) noexcept
    {
        const double w0 = juce::MathConstants<double>::twoPi * hz / sr;
        const double c = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad f;
        f.b0 = (float) ((1.0 - c) * 0.5 / a0);
        f.b1 = (float) ((1.0 - c) / a0);
        f.b2 = f.b0;
        f.a1 = (float) (-2.0 * c / a0);
        f.a2 = (float) ((1.0 - alpha) / a0);
        return f;
    }

    // One pole, bilinear, -3 dB at hz. 6 dB/oct.
    static Biquad lowPass1 (double sr, double hz) noexcept
    {
        const double k = std::tan (juce::MathConstants<double>::pi * hz / sr);
        const double a0 = 1.0 + k;
        Biquad f;
        f.b0 = (float) (k / a0);
        f.b1 = f.b0;
        f.a1 = (float) ((k - 1.0) / a0);
        return f;
    }

    static Biquad highPass1 (double sr, double hz) noexcept
    {
        const double k = std::tan (juce::MathConstants<double>::pi * hz / sr);
        const double a0 = 1.0 + k;
        Biquad f;
        f.b0 = (float) (1.0 / a0);
        f.b1 = (float) (-1.0 / a0);
        f.a1 = (float) ((k - 1.0) / a0);
        return f;
    }
};

inline constexpr int kMaxBands = 32;
inline constexpr int kMaxCutSections = 8;

inline int cutOrderFromSlope (float slopeDb) noexcept
{
    return juce::jlimit (1, 16, (int) std::lround ((double) slopeDb / 6.0));
}

struct CutDesign
{
    int n = 1;
    Biquad coeff[kMaxCutSections];
    float q[kMaxCutSections] {};
    bool quadratic[kMaxCutSections] {};
};

inline CutDesign designBand (int type, double sr, double hz, double q, double gainDb, float slope, int cutCalc) noexcept
{
    CutDesign d;
    d.quadratic[0] = true;
    d.q[0] = (float) q;

    if (type != 3 && type != 4)
    {
        if (type == 1)
            d.coeff[0] = Biquad::lowShelf (sr, hz, q, gainDb);
        else if (type == 2)
            d.coeff[0] = Biquad::highShelf (sr, hz, q, gainDb);
        else
            d.coeff[0] = Biquad::peak (sr, hz, q, gainDb);
        return d;
    }

    const bool highpass = type == 3;
    const int order = cutOrderFromSlope (slope);
    const bool rbj = cutCalc != 0;
    const double qUser = juce::jlimit (0.1, 18.0, q);
    d.n = 0;

    if (order % 2 == 1)
    {
        d.coeff[d.n] = highpass ? Biquad::highPass1 (sr, hz) : Biquad::lowPass1 (sr, hz);
        d.q[d.n] = 0.707f;
        d.quadratic[d.n] = false;
        ++d.n;
    }

    const int pairs = order / 2;
    int sharp = -1;
    double sharpQ = -1.0;
    for (int i = 0; i < pairs; ++i)
    {
        const double sectionQ = rbj
            ? qUser
            : 1.0 / (2.0 * std::sin ((2.0 * (i + 1) - 1.0) * juce::MathConstants<double>::pi / (2.0 * (double) order)));
        if (! rbj && sectionQ > sharpQ)
        {
            sharpQ = sectionQ;
            sharp = d.n;
        }
        d.q[d.n] = (float) sectionQ;
        d.quadratic[d.n] = true;
        ++d.n;
    }

    if (! rbj && sharp >= 0)
        d.q[sharp] = (float) juce::jlimit (0.1, 18.0, sharpQ * (qUser / 0.7071067811865476));

    for (int s = 0; s < d.n; ++s)
        if (d.quadratic[s])
            d.coeff[s] = highpass ? Biquad::highPass (sr, hz, d.q[s]) : Biquad::lowPass (sr, hz, d.q[s]);

    return d;
}
inline constexpr int kFirTaps = 1023;
inline constexpr int kFirDelay = (kFirTaps - 1) / 2;
inline constexpr int kBandDelayCap = 8192;

struct BandSettings
{
    bool on = false;
    int type = 0; // 0 peak, 1 low shelf, 2 high shelf, 3 low cut, 4 high cut
    float freq = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.707f;
    float slope = 12.0f; // dB/oct, used by low cut and high cut
    int cutCalc = 0;     // 0 Butterworth, 1 repeated Q
    float delayMs = 0.0f;
    float pan = 0.0f; // -1 left, 0 centre, +1 right. Centre leaves both channels untouched.
};

// 0 = the biquad cascade written for this plugin (minimum phase, no extra delay).
// 1 = trapezoidal state-variable IIR on the same band settings.
// 2 = linear-phase FIR matching the biquad magnitude, delayed by kFirDelay.
class GraphicEq
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        firWork.assign (2048, 0.0f);
        firKernel.assign ((size_t) kFirTaps, 0.0f);
        firKernel[(size_t) kFirDelay] = 1.0f;
        for (auto& h : firHist)
            h.assign ((size_t) kFirTaps, 0.0f);
        firPos[0] = firPos[1] = 0;
        firReady = false;
        for (auto& b : bands)
            for (auto& mem : b.delayMem)
                mem.assign ((size_t) kBandDelayCap, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : bands)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < kMaxCutSections; ++s)
                {
                    b.filt[ch][s].reset();
                    b.svf[ch][s].reset();
                }
        }
        for (auto& h : firHist)
            std::fill (h.begin(), h.end(), 0.0f);
        firPos[0] = firPos[1] = 0;
        for (auto& b : bands)
        {
            b.delayPos[0] = b.delayPos[1] = 0;
            for (auto& mem : b.delayMem)
                std::fill (mem.begin(), mem.end(), 0.0f);
        }
    }

    void setEngine (int next) noexcept
    {
        engine = juce::jlimit (0, 2, next);
    }

    int latencySamples() const noexcept
    {
        if (anyPan)
            return 0;
        return (active && engine == 2) ? kFirDelay : 0;
    }

    void update (const BandSettings* settings, int count, bool enabled) noexcept
    {
        active = enabled;
        used = juce::jlimit (0, kMaxBands, count);
        bool changed = engine != builtEngine;
        int audible = 0;
        anyDelay = false;
        anyPan = false;

        for (int i = 0; i < kMaxBands; ++i)
        {
            auto& b = bands[(size_t) i];
            if (i >= used || ! enabled || ! settings[i].on)
            {
                if (b.audible || b.delaySamples != 0)
                    changed = true;
                b.audible = false;
                b.delaySamples = 0;
                continue;
            }

            const auto& s = settings[i];
            if (! b.audible || s.type != b.settings.type || s.freq != b.settings.freq
                || s.gainDb != b.settings.gainDb || s.q != b.settings.q
                || s.slope != b.settings.slope || s.cutCalc != b.settings.cutCalc
                || s.delayMs != b.settings.delayMs)
                changed = true;

            const double hz = juce::jlimit (1.0, ceilingHz (sr), (double) s.freq);
            const double q = juce::jlimit (0.1, 18.0, (double) s.q);
            const auto designed = designBand (s.type, sr, hz, q, s.gainDb, s.slope, s.cutCalc);
            const int oldN = b.nSections;
            b.nSections = designed.n;
            b.audible = true;
            for (int sec = 0; sec < designed.n; ++sec)
            {
                b.coeff[sec] = designed.coeff[sec];
                b.quadratic[sec] = designed.quadratic[sec];
                for (int ch = 0; ch < 2; ++ch)
                {
                    if (sec >= oldN)
                    {
                        b.filt[ch][sec].reset();
                        b.svf[ch][sec].reset();
                    }
                    b.filt[ch][sec].copyCoeffs (designed.coeff[sec]);
                    if (designed.quadratic[sec])
                        b.svf[ch][sec].set (s.type, (float) sr, (float) hz, designed.q[sec], s.gainDb);
                }
            }
            const int samples = juce::jlimit (0, kBandDelayCap - 1,
                                              (int) std::lround ((double) s.delayMs * 0.001 * sr));
            if (samples != b.delaySamples)
            {
                b.delaySamples = samples;
                b.delayPos[0] = b.delayPos[1] = 0;
                for (auto& mem : b.delayMem)
                    std::fill (mem.begin(), mem.end(), 0.0f);
            }
            if (b.delaySamples > 0)
                anyDelay = true;
            if (std::abs (s.pan) > 0.001f)
                anyPan = true;
            b.settings = s;
            ++audible;
        }

        if (engine == 2 && (changed || ! firReady))
            designFir (audible);
        else if (engine == 1 && changed)
            bakeCurveFromSvf();
        else if (engine == 0)
            curveReady = false;

        builtEngine = engine;
    }

    float process (int channel, float x) noexcept
    {
        if (! active)
            return x;

        const int ch = juce::jlimit (0, 1, channel);
        if (anyPan)
            return processPanned (ch, x);
        if (anyDelay)
            return processDelayed (ch, x);

        if (engine == 2)
            return processFir (ch, x);

        for (int i = 0; i < used; ++i)
            if (bands[(size_t) i].audible)
                x = filterBand (i, ch, x);
        return x;
    }

    float responseDb (double hz) const noexcept
    {
        if (! active)
            return 0.0f;
        const double maxHz = ceilingHz (sr);
        if (hz <= 0.0 || hz > maxHz)
            return 0.0f;
        if (engine == 0 || ! curveReady)
            return biquadResponseDb (hz);

        const double minHz = 1.0;
        const double t = std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
        const float idx = juce::jlimit (0.0f, 255.0f, (float) t * 255.0f);
        const int i0 = (int) idx;
        const int i1 = juce::jmin (255, i0 + 1);
        const float f = idx - (float) i0;
        return curve[(size_t) i0] * (1.0f - f) + curve[(size_t) i1] * f;
    }

private:
    struct Svf
    {
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;

        void reset() noexcept { ic1 = ic2 = 0.0f; }

        void set (int type, float sampleRate, float hz, float q, float gainDb) noexcept
        {
            const float nyquist = sampleRate * 0.49f;
            hz = juce::jlimit (1.0f, nyquist, hz);
            q = juce::jlimit (0.1f, 18.0f, q);
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float g0 = std::tan (juce::MathConstants<float>::pi * hz / sampleRate);
            float g = g0;
            float k = 1.0f / q;
            m0 = 1.0f;
            m1 = 0.0f;
            m2 = 0.0f;

            if (type == 1)
            {
                g = g0 / std::sqrt (A);
                m0 = 1.0f;
                m1 = k * (A - 1.0f);
                m2 = A * A - 1.0f;
            }
            else if (type == 2)
            {
                g = g0 * std::sqrt (A);
                m0 = A * A;
                m1 = k * (1.0f - A) * A;
                m2 = 1.0f - A * A;
            }
            else if (type == 3)
            {
                m0 = 1.0f;
                m1 = -k;
                m2 = -1.0f;
            }
            else if (type == 4)
            {
                m0 = 0.0f;
                m1 = 0.0f;
                m2 = 1.0f;
            }
            else
            {
                k = 1.0f / (q * A);
                m0 = 1.0f;
                m1 = k * (A * A - 1.0f);
                m2 = 0.0f;
            }

            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        float process (float x) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return m0 * x + m1 * v1 + m2 * v2;
        }
    };

    struct Band
    {
        bool audible = false;
        BandSettings settings;
        int nSections = 1;
        Biquad coeff[kMaxCutSections];
        bool quadratic[kMaxCutSections] {};
        Biquad filt[2][kMaxCutSections];
        Svf svf[2][kMaxCutSections];
        int delaySamples = 0;
        int delayPos[2] { 0, 0 };
        std::vector<float> delayMem[2];
    };

    float filterBand (int index, int channel, float x) noexcept
    {
        auto& b = bands[(size_t) index];
        float y = x;
        for (int s = 0; s < b.nSections; ++s)
        {
            if (engine == 1 && b.quadratic[s])
                y = b.svf[channel][s].process (y);
            else
                y = b.filt[channel][s].process (y);
        }
        return y;
    }

    float takeDelay (Band& b, int channel, float x) noexcept
    {
        const int n = b.delaySamples;
        auto& mem = b.delayMem[(size_t) channel];
        if (n <= 0 || (int) mem.size() < n)
            return x;

        int& pos = b.delayPos[(size_t) channel];
        if (pos >= n)
            pos = 0;
        const float y = mem[(size_t) pos];
        mem[(size_t) pos] = x;
        pos = (pos + 1) % n;
        return y;
    }

    // Bands at 0 ms stay in series. A band with a delay contributes only its
    // difference from the dry input, shifted in time, so that band's phase moves
    // without delaying the rest of the spectrum.
    float processDelayed (int channel, float x) noexcept
    {
        float y = x;
        for (int i = 0; i < used; ++i)
        {
            auto& b = bands[(size_t) i];
            if (b.audible && b.delaySamples <= 0)
                y = filterBand (i, channel, y);
        }

        for (int i = 0; i < used; ++i)
        {
            auto& b = bands[(size_t) i];
            if (! b.audible || b.delaySamples <= 0)
                continue;
            const float filtered = filterBand (i, channel, x);
            y += takeDelay (b, channel, filtered - x);
        }
        return y;
    }

    static float panGain (float pan, int channel) noexcept
    {
        pan = juce::jlimit (-1.0f, 1.0f, pan);
        return channel == 0 ? juce::jmin (1.0f, 1.0f - pan) : juce::jmin (1.0f, 1.0f + pan);
    }

    // Centre pan (gain 1) keeps the serial cascade. A hard side keeps the band
    // on that channel only. Delayed bands still read the original sample, as before.
    float processPanned (int channel, float x) noexcept
    {
        float y = x;
        for (int i = 0; i < used; ++i)
        {
            auto& b = bands[(size_t) i];
            if (! b.audible || b.delaySamples > 0)
                continue;
            const float filtered = filterBand (i, channel, y);
            const float g = panGain (b.settings.pan, channel);
            y = g >= 0.999f ? filtered : y + (filtered - y) * g;
        }
        for (int i = 0; i < used; ++i)
        {
            auto& b = bands[(size_t) i];
            if (! b.audible || b.delaySamples <= 0)
                continue;
            const float filtered = filterBand (i, channel, x);
            y += takeDelay (b, channel, filtered - x) * panGain (b.settings.pan, channel);
        }
        return y;
    }

    float biquadResponseDb (double hz) const noexcept
    {
        // Above Nyquist the z-plane response wraps and draws "ghost" peaks.
        const double maxHz = ceilingHz (sr);
        if (hz <= 0.0 || hz > maxHz)
            return 0.0f;
        float db = 0.0f;
        for (int i = 0; i < used; ++i)
            if (bands[(size_t) i].audible)
                for (int s = 0; s < bands[(size_t) i].nSections; ++s)
                    db += bands[(size_t) i].coeff[s].responseDb (hz, sr);
        return db;
    }

    void designFir (int audible) noexcept
    {
        if ((int) firWork.size() < 2048 || (int) firKernel.size() < kFirTaps)
            return;

        std::fill (firWork.begin(), firWork.end(), 0.0f);
        constexpr int n = 1024;
        if (audible == 0)
        {
            firWork[0] = 1.0f;
        }
        else
        {
            for (int k = 0; k <= n / 2; ++k)
            {
                const double hz = (double) k * sr / (double) n;
                const float mag = std::pow (10.0f, biquadResponseDb (hz) / 20.0f);
                if (k == 0)
                    firWork[0] = mag;
                else if (k == n / 2)
                    firWork[1] = mag;
                else
                {
                    firWork[(size_t) (2 * k)] = mag;
                    firWork[(size_t) (2 * k + 1)] = 0.0f;
                }
            }
            firFft.performRealOnlyInverseTransform (firWork.data());
        }

        for (int i = 0; i < kFirTaps; ++i)
            firKernel[(size_t) i] = firWork[(size_t) ((i + n - kFirDelay) % n)];

        firReady = true;
        bakeCurveFromKernel (firKernel.data(), kFirTaps);
    }

    void bakeCurveFromSvf() noexcept
    {
        float ir[1024];
        struct Stage
        {
            bool quadratic = false;
            Svf svf;
            Biquad bq;
        };
        Stage stages[(size_t) kMaxBands * kMaxCutSections];
        int nStage = 0;
        for (int i = 0; i < used; ++i)
        {
            auto& b = bands[(size_t) i];
            if (! b.audible)
                continue;
            for (int s = 0; s < b.nSections; ++s)
            {
                auto& stage = stages[(size_t) nStage++];
                stage.quadratic = b.quadratic[s];
                if (stage.quadratic)
                    stage.svf = b.svf[0][s];
                else
                    stage.bq = b.coeff[s];
                stage.svf.reset();
                stage.bq.reset();
            }
        }

        float x = 1.0f;
        for (int n = 0; n < 1024; ++n)
        {
            float y = x;
            for (int i = 0; i < nStage; ++i)
                y = stages[(size_t) i].quadratic ? stages[(size_t) i].svf.process (y)
                                                 : stages[(size_t) i].bq.process (y);
            ir[n] = y;
            x = 0.0f;
        }
        bakeCurveFromKernel (ir, 1024);
    }

    void bakeCurveFromKernel (const float* h, int taps) noexcept
    {
        const double maxHz = ceilingHz (sr);
        const double minHz = 1.0;
        for (int i = 0; i < 256; ++i)
        {
            const double hz = minHz * std::pow (maxHz / minHz, (double) i / 255.0);
            const double w = juce::MathConstants<double>::twoPi * hz / sr;
            double re = 0.0, im = 0.0;
            for (int n = 0; n < taps; ++n)
            {
                const double a = w * (double) n;
                re += (double) h[n] * std::cos (a);
                im -= (double) h[n] * std::sin (a);
            }
            const float mag = (float) std::sqrt (re * re + im * im);
            curve[(size_t) i] = juce::Decibels::gainToDecibels (mag, -100.0f);
        }
        curveReady = true;
    }

    float processFir (int channel, float x) noexcept
    {
        auto& hist = firHist[(size_t) channel];
        if ((int) hist.size() < kFirTaps || (int) firKernel.size() < kFirTaps)
            return x;

        int pos = firPos[(size_t) channel];
        hist[(size_t) pos] = x;
        float y = 0.0f;
        for (int k = 0; k < kFirTaps; ++k)
        {
            y += firKernel[(size_t) k] * hist[(size_t) pos];
            if (--pos < 0)
                pos = kFirTaps - 1;
        }
        firPos[(size_t) channel] = (firPos[(size_t) channel] + 1) % kFirTaps;
        return y;
    }

    double sr = 48000.0;
    bool active = false;
    bool anyDelay = false;
    bool anyPan = false;
    int used = 0;
    int engine = 0;
    int builtEngine = -1;
    bool firReady = false;
    bool curveReady = false;
    Band bands[(size_t) kMaxBands];
    std::array<float, 256> curve {};
    std::vector<float> firWork, firKernel;
    std::vector<float> firHist[2];
    int firPos[2] { 0, 0 };
    juce::dsp::FFT firFft { 10 };
};
}
