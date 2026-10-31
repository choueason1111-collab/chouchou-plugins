#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

// Display-only spectrum. pushBlock copies samples; it never writes them back.
class DisplayAnalyser
{
public:
    static constexpr int kWaterCols = 96;
    static constexpr int kWaterRows = 36;
    static constexpr int kMaxPeaks = 6;

    struct Frame
    {
        std::vector<float> db, dbR, dbSc, peakDb, waterfall;
        int waterfallRows = 0;
        double sampleRate = 48000.0;
        int fftSize = 2048;
        float fundamental = 0.0f, centroid = 0.0f, flatness = 0.0f, rolloff = 0.0f, tilt = 0.0f;
        float peaks[kMaxPeaks] {};
        float harmonics[kMaxPeaks] {};
        int peakCount = 0, harmonicCount = 0;
    };

    void prepare (double sr)
    {
        sampleRate = sr > 0.0 ? sr : 48000.0;
        const int cap = 65536;
        ringL.assign ((size_t) cap, 0.0f);
        ringR.assign ((size_t) cap, 0.0f);
        ringSc.assign ((size_t) cap, 0.0f);
        write = 0;
        count = 0;
        pending = 0;
        haveFrame = false;
    }

    void setEnabled (bool shouldRun) noexcept { enabled.store (shouldRun, std::memory_order_relaxed); }

    void configure (int windowSize, int windowType, float overlapPct, float averaging, float smoothing,
                    int padFactor, int channelMode, int displayMode, bool sidechain) noexcept
    {
        this->windowSize = juce::jlimit (1024, 32768, windowSize);
        this->windowType = juce::jlimit (0, 4, windowType);
        this->overlap = juce::jlimit (0.0f, 75.0f, overlapPct);
        this->averaging = juce::jlimit (0.0f, 0.98f, averaging);
        this->smoothing = juce::jlimit (0.0f, 1.0f, smoothing);
        this->padFactor = padFactor == 4 ? 4 : (padFactor == 2 ? 2 : 1);
        this->channelMode = juce::jlimit (0, 4, channelMode);
        this->displayMode = juce::jlimit (0, 4, displayMode);
        this->wantSc = sidechain;
    }

    void pushBlock (const float* l, const float* r, const float* sc, int n) noexcept
    {
        if (! enabled.load (std::memory_order_relaxed) || n <= 0 || l == nullptr)
            return;
        std::unique_lock<std::mutex> lock (mutex, std::try_to_lock);
        if (! lock.owns_lock())
            return;

        const int cap = (int) ringL.size();
        if (cap <= 0)
            return;
        for (int i = 0; i < n; ++i)
        {
            ringL[(size_t) write] = l[i];
            ringR[(size_t) write] = r != nullptr ? r[i] : l[i];
            ringSc[(size_t) write] = sc != nullptr ? sc[i] : 0.0f;
            write = (write + 1) % cap;
            count = juce::jmin (cap, count + 1);
            pending = juce::jmin (cap, pending + 1);
        }
    }

    void pump()
    {
        if (! enabled.load (std::memory_order_relaxed))
            return;

        std::lock_guard<std::mutex> lock (mutex);
        const int hop = juce::jmax (1, (int) std::lround ((double) windowSize * (1.0 - (double) overlap / 100.0)));
        if (displayMode == 3 && haveFrame)
            return;
        if (count < windowSize || pending < hop)
            return;

        int fftN = windowSize * padFactor;
        if (fftN > 32768)
            fftN = windowSize;
        fftN = juce::jmin (32768, juce::nextPowerOfTwo (fftN));
        const int order = (int) std::round (std::log ((double) fftN) / std::log (2.0));
        if (fft == nullptr || fft->getSize() != fftN)
            fft = std::make_unique<juce::dsp::FFT> (order);

        rebuildWindow();
        std::vector<float> mag, magR, magSc;
        runOne (channelMode == 4 ? 0 : channelMode, mag);
        if (channelMode == 4)
            runOne (1, magR);
        if (wantSc)
            runOne (5, magSc);

        const float avgMix = displayMode == 2 ? juce::jmax (averaging, 0.85f) : averaging;
        auto mixAvg = [&] (std::vector<float>& dst, const std::vector<float>& src)
        {
            if (dst.size() != src.size() || avgMix <= 0.0f)
                dst = src;
            else
                for (size_t i = 0; i < src.size(); ++i)
                    dst[i] = avgMix * dst[i] + (1.0f - avgMix) * src[i];
        };
        mixAvg (avg, mag);
        if (! magR.empty())
            mixAvg (avgR, magR);
        else
            avgR.clear();
        if (! magSc.empty())
            mixAvg (avgSc, magSc);
        else
            avgSc.clear();

        smoothInto (avg, shown);
        if (! avgR.empty())
            smoothInto (avgR, shownR);
        else
            shownR.clear();
        if (! avgSc.empty())
            smoothInto (avgSc, shownSc);
        else
            shownSc.clear();

        frame.db = toDb (shown);
        frame.dbR = toDb (shownR);
        frame.dbSc = toDb (shownSc);
        if (displayMode == 1)
        {
            if (frame.peakDb.size() != frame.db.size())
                frame.peakDb = frame.db;
            else
                for (size_t i = 0; i < frame.db.size(); ++i)
                    frame.peakDb[i] = juce::jmax (frame.peakDb[i], frame.db[i]);
        }
        else
            frame.peakDb.clear();

        frame.sampleRate = sampleRate;
        frame.fftSize = fftN;
        measure (shown, frame);
        pushWaterfall (frame.db);
        frame.waterfall = waterfall;
        frame.waterfallRows = waterCount;
        haveFrame = true;
        pending = juce::jmax (0, pending - hop);
    }

    Frame copy() const
    {
        std::lock_guard<std::mutex> lock (mutex);
        return frame;
    }

private:
    void rebuildWindow()
    {
        if ((int) window.size() == windowSize && builtType == windowType)
            return;
        window.resize ((size_t) windowSize);
        const float pi = juce::MathConstants<float>::pi;
        windowSum = 0.0f;
        for (int i = 0; i < windowSize; ++i)
        {
            const float t = windowSize > 1 ? (float) i / (float) (windowSize - 1) : 0.0f;
            float w = 1.0f;
            if (windowType == 0)
                w = 0.5f * (1.0f - std::cos (2.0f * pi * t));
            else if (windowType == 1)
                w = 0.54f - 0.46f * std::cos (2.0f * pi * t);
            else if (windowType == 2)
                w = 0.42f - 0.5f * std::cos (2.0f * pi * t) + 0.08f * std::cos (4.0f * pi * t);
            else if (windowType == 3)
            {
                const float beta = 8.0f;
                const float r = 2.0f * t - 1.0f;
                w = bessel0 (beta * std::sqrt (juce::jmax (0.0f, 1.0f - r * r))) / bessel0 (beta);
            }
            window[(size_t) i] = w;
            windowSum += w;
        }
        if (windowSum < 1.0f)
            windowSum = 1.0f;
        builtType = windowType;
    }

    static float bessel0 (float x)
    {
        float sum = 1.0f, term = 1.0f;
        for (int k = 1; k < 16; ++k)
        {
            term *= (x * x) / (4.0f * (float) k * (float) k);
            sum += term;
        }
        return sum;
    }

    void readLast (const std::vector<float>& ring, std::vector<float>& dest, int n) const
    {
        dest.resize ((size_t) n);
        const int cap = (int) ring.size();
        int start = write - n;
        while (start < 0)
            start += cap;
        for (int i = 0; i < n; ++i)
            dest[(size_t) i] = ring[(size_t) ((start + i) % cap)];
    }

    // channel: 0 L, 1 R, 2 mid, 3 side, 5 sidechain
    void runOne (int channel, std::vector<float>& mag)
    {
        const int fftN = fft->getSize();
        work.assign ((size_t) fftN * 2, 0.0f);
        std::vector<float> a, b, sc;
        readLast (ringL, a, windowSize);
        readLast (ringR, b, windowSize);
        if (channel == 5)
            readLast (ringSc, sc, windowSize);

        for (int i = 0; i < windowSize; ++i)
        {
            float x = a[(size_t) i];
            if (channel == 1)
                x = b[(size_t) i];
            else if (channel == 2)
                x = 0.5f * (a[(size_t) i] + b[(size_t) i]);
            else if (channel == 3)
                x = 0.5f * (a[(size_t) i] - b[(size_t) i]);
            else if (channel == 5)
                x = sc[(size_t) i];
            work[(size_t) i] = x * window[(size_t) i];
        }

        fft->performRealOnlyForwardTransform (work.data());
        const int bins = fftN / 2 + 1;
        mag.assign ((size_t) bins, 0.0f);
        const float scale = 2.0f / windowSum;
        mag[0] = std::abs (work[0]) * scale;
        mag[(size_t) (bins - 1)] = std::abs (work[1]) * scale;
        for (int k = 1; k < bins - 1; ++k)
            mag[(size_t) k] = std::hypot (work[(size_t) (2 * k)], work[(size_t) (2 * k + 1)]) * scale;
    }

    void smoothInto (const std::vector<float>& src, std::vector<float>& dst) const
    {
        dst = src;
        const int radius = (int) std::lround (smoothing * 24.0f);
        if (radius <= 0 || src.size() < 3)
            return;
        for (int i = 0; i < (int) src.size(); ++i)
        {
            float sum = 0.0f, w = 0.0f;
            for (int k = -radius; k <= radius; ++k)
            {
                const int j = juce::jlimit (0, (int) src.size() - 1, i + k);
                const float wk = 1.0f - std::abs ((float) k) / (float) (radius + 1);
                sum += src[(size_t) j] * wk;
                w += wk;
            }
            dst[(size_t) i] = sum / juce::jmax (1.0e-6f, w);
        }
    }

    static std::vector<float> toDb (const std::vector<float>& mag)
    {
        std::vector<float> db (mag.size(), -120.0f);
        for (size_t i = 0; i < mag.size(); ++i)
            db[i] = juce::Decibels::gainToDecibels (mag[i], -120.0f);
        return db;
    }

    void measure (const std::vector<float>& mag, Frame& out) const
    {
        const int n = (int) mag.size();
        out.peakCount = 0;
        out.harmonicCount = 0;
        out.fundamental = out.centroid = out.flatness = out.rolloff = out.tilt = 0.0f;
        if (n < 4 || sampleRate <= 0.0)
            return;
        const float nyquist = (float) sampleRate * 0.5f;
        auto hzOf = [&] (int i) { return (float) i * nyquist / (float) (n - 1); };

        double sumE = 0.0, sumF = 0.0, sumLog = 0.0;
        for (int i = 1; i < n; ++i)
        {
            const double e = juce::jmax (1.0e-12, (double) mag[(size_t) i]);
            sumE += e;
            sumF += e * (double) hzOf (i);
            sumLog += std::log (e);
        }
        const double inv = 1.0 / (double) juce::jmax (1, n - 1);
        out.centroid = sumE > 0.0 ? (float) (sumF / sumE) : 0.0f;
        const double geo = std::exp (sumLog * inv);
        const double ari = sumE * inv;
        out.flatness = ari > 0.0 ? (float) juce::jlimit (0.0, 1.0, geo / ari) : 0.0f;

        double acc = 0.0;
        out.rolloff = nyquist;
        for (int i = 1; i < n; ++i)
        {
            acc += (double) mag[(size_t) i];
            if (acc >= 0.85 * sumE)
            {
                out.rolloff = hzOf (i);
                break;
            }
        }

        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int m = 0;
        for (int i = 1; i < n; ++i)
        {
            const float hz = hzOf (i);
            if (hz < 40.0f || hz > 16000.0f)
                continue;
            const double x = std::log ((double) hz) / std::log (2.0);
            const double y = (double) juce::Decibels::gainToDecibels (mag[(size_t) i], -120.0f);
            sx += x; sy += y; sxx += x * x; sxy += x * y; ++m;
        }
        const double den = (double) m * sxx - sx * sx;
        out.tilt = std::abs (den) > 1.0e-6 ? (float) (((double) m * sxy - sx * sy) / den) : 0.0f;

        int best = 1;
        for (int i = 2; i < n - 1; ++i)
            if (mag[(size_t) i] > mag[(size_t) best])
                best = i;
        const float gate = mag[(size_t) best] * 0.15f;
        struct Hit { float hz, mag; };
        Hit hits[16];
        int hitN = 0;
        for (int i = 2; i < n - 1 && hitN < 16; ++i)
        {
            if (mag[(size_t) i] >= mag[(size_t) (i - 1)] && mag[(size_t) i] > mag[(size_t) (i + 1)] && mag[(size_t) i] >= gate)
                hits[hitN++] = { hzOf (i), mag[(size_t) i] };
        }
        for (int a = 0; a < hitN; ++a)
            for (int b = a + 1; b < hitN; ++b)
                if (hits[b].mag > hits[a].mag)
                    std::swap (hits[a], hits[b]);
        out.peakCount = juce::jmin (kMaxPeaks, hitN);
        for (int i = 0; i < out.peakCount; ++i)
            out.peaks[i] = hits[i].hz;

        for (int i = hitN - 1; i >= 0; --i)
        {
            const float f0 = hits[i].hz;
            if (f0 < 40.0f || f0 > 2000.0f)
                continue;
            const int h2 = juce::jlimit (1, n - 1, (int) std::lround (2.0f * f0 / nyquist * (float) (n - 1)));
            if (mag[(size_t) h2] > hits[i].mag * 0.08f)
            {
                out.fundamental = f0;
                break;
            }
        }
        if (out.fundamental <= 0.0f && hitN > 0)
            out.fundamental = hits[0].hz;
        if (out.fundamental > 0.0f)
        {
            for (int h = 2; h <= 8 && out.harmonicCount < kMaxPeaks; ++h)
            {
                const float hz = out.fundamental * (float) h;
                if (hz >= nyquist)
                    break;
                out.harmonics[out.harmonicCount++] = hz;
            }
        }
    }

    void pushWaterfall (const std::vector<float>& db)
    {
        if (waterfall.size() != (size_t) kWaterRows * (size_t) kWaterCols)
        {
            waterfall.assign ((size_t) kWaterRows * (size_t) kWaterCols, -120.0f);
            waterCount = 0;
        }
        if (waterCount < kWaterRows)
            ++waterCount;
        else
            std::move (waterfall.begin() + kWaterCols, waterfall.end(), waterfall.begin());

        float* row = waterfall.data() + (size_t) (waterCount - 1) * (size_t) kWaterCols;
        const int n = (int) db.size();
        for (int i = 0; i < kWaterCols; ++i)
        {
            const float t = (float) i / (float) (kWaterCols - 1);
            const int bin = n > 0 ? juce::jlimit (0, n - 1, (int) std::lround (std::pow ((double) t, 2.0) * (double) (n - 1))) : 0;
            row[i] = n > 0 ? db[(size_t) bin] : -120.0f;
        }
    }

    double sampleRate = 48000.0;
    int windowSize = 2048, windowType = 0, builtType = -1, padFactor = 1, channelMode = 0, displayMode = 0;
    float overlap = 50.0f, averaging = 0.0f, smoothing = 0.0f, windowSum = 1.0f;
    bool wantSc = false, haveFrame = false;
    std::atomic<bool> enabled { false };
    std::vector<float> ringL, ringR, ringSc, window, work, avg, avgR, avgSc, shown, shownR, shownSc, waterfall;
    int write = 0, count = 0, pending = 0, waterCount = 0;
    Frame frame;
    std::unique_ptr<juce::dsp::FFT> fft;
    mutable std::mutex mutex;
};

class PhaseCapture
{
public:
    struct Frame
    {
        std::vector<float> x, y;
        float correlation = 1.0f, phaseDeg = 0.0f, width = 0.0f, mono = 1.0f;
    };

    void setEnabled (bool shouldRun) noexcept { enabled.store (shouldRun, std::memory_order_relaxed); }

    void pushBlock (const float* l, const float* r, int n) noexcept
    {
        if (! enabled.load (std::memory_order_relaxed) || l == nullptr || r == nullptr || n <= 0)
            return;
        std::unique_lock<std::mutex> lock (mutex, std::try_to_lock);
        if (! lock.owns_lock())
            return;

        for (int i = 0; i < n; ++i)
        {
            const float nl = l[i], nr = r[i];
            const float ol = ringL[(size_t) pos], orr = ringR[(size_t) pos];
            const float oa = ringA[(size_t) pos];
            const float ang = std::atan2 (nl - nr, nl + nr);
            if (filled < kN)
            {
                sumLL += nl * nl;
                sumRR += nr * nr;
                sumLR += nl * nr;
                sumAng += ang;
                ++filled;
            }
            else
            {
                sumLL += nl * nl - ol * ol;
                sumRR += nr * nr - orr * orr;
                sumLR += nl * nr - ol * orr;
                sumAng += ang - oa;
            }
            ringL[(size_t) pos] = nl;
            ringR[(size_t) pos] = nr;
            ringA[(size_t) pos] = ang;
            pos = (pos + 1) % kN;

            if (++hold >= 8)
            {
                hold = 0;
                ptsX[(size_t) p] = nl;
                ptsY[(size_t) p] = nr;
                p = (p + 1) % kPts;
                if (pCount < kPts)
                    ++pCount;
            }
        }

        frame.x.assign (ptsX.begin(), ptsX.begin() + pCount);
        frame.y.assign (ptsY.begin(), ptsY.begin() + pCount);
        if (pCount == kPts)
        {
            std::rotate (frame.x.begin(), frame.x.begin() + p, frame.x.end());
            std::rotate (frame.y.begin(), frame.y.begin() + p, frame.y.end());
        }
        const float den = std::sqrt (juce::jmax (0.0f, sumLL) * juce::jmax (0.0f, sumRR));
        frame.correlation = den > 1.0e-8f ? juce::jlimit (-1.0f, 1.0f, sumLR / den) : 1.0f;
        frame.phaseDeg = filled > 0 ? sumAng / (float) filled * (180.0f / juce::MathConstants<float>::pi) : 0.0f;
        frame.width = juce::jlimit (0.0f, 2.0f, 1.0f - frame.correlation);
        frame.mono = juce::jlimit (0.0f, 1.0f, 0.5f * (frame.correlation + 1.0f));
    }

    Frame copy() const
    {
        std::lock_guard<std::mutex> lock (mutex);
        return frame;
    }

private:
    static constexpr int kN = 2048;
    static constexpr int kPts = 512;
    std::atomic<bool> enabled { false };
    std::vector<float> ringL = std::vector<float> ((size_t) kN, 0.0f);
    std::vector<float> ringR = std::vector<float> ((size_t) kN, 0.0f);
    std::vector<float> ringA = std::vector<float> ((size_t) kN, 0.0f);
    std::vector<float> ptsX = std::vector<float> ((size_t) kPts, 0.0f);
    std::vector<float> ptsY = std::vector<float> ((size_t) kPts, 0.0f);
    int pos = 0, filled = 0, hold = 0, p = 0, pCount = 0;
    float sumLL = 0, sumRR = 0, sumLR = 0, sumAng = 0;
    Frame frame;
    mutable std::mutex mutex;
};
