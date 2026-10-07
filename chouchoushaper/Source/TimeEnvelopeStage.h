/*
  ==============================================================================

    chouchouShaper — time stage: broadband envelope, zero latency

      Detector: per-sample power (x^2) of the main input or the sidechain,
      followed by the fast / slow follower pair. With stereo link, all channels
      share one detector (max power across channels) and one gain.

  ==============================================================================
*/

#pragma once

#include "EnvelopeCore.h"
#include <algorithm>
#include <vector>

struct TimeStageParams
{
    chouchou::env::ShapeParams shape;
    float fastMs = 10.0f;
    float slowMs = 150.0f;
    float mix    = 1.0f;
    bool  link   = true;
};

/** Optional per-sample output for the scope (channel 0, or the shared linked gain). */
struct TimeStageTrace
{
    float* ratioDb   = nullptr;
    float* attackDb  = nullptr;
    float* sustainDb = nullptr;
    float* appliedDb = nullptr; // gain actually applied, after smoothing and Dry/Wet
};

class TimeEnvelopeStage
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        followers.assign ((size_t) std::max (1, numChannels), {});
        gains.assign (followers.size(), 1.0f);
    }

    void reset() noexcept
    {
        for (auto& f : followers) f.reset();
        std::fill (gains.begin(), gains.end(), 1.0f);
    }

    /** Smallest / largest applied gain (dB) over the last process() call. */
    float getLastMinGainDb() const noexcept { return lastMinDb; }
    float getLastMaxGainDb() const noexcept { return lastMaxDb; }

    /** data: main channels, processed in place. det: detector channels (main or sidechain). */
    void process (float* const* data, int numChannels,
                  const float* const* det, int numDetChannels,
                  int numSamples, const TimeStageParams& p,
                  const TimeStageTrace* trace = nullptr) noexcept
    {
        using namespace chouchou::env;

        numChannels = std::min (numChannels, (int) followers.size());
        if (numChannels <= 0 || numDetChannels <= 0)
            return;

        const float step  = (float) (1.0 / sr);
        const float cF    = msToCoeff (p.fastMs, step);
        const float cS    = msToCoeff (p.slowMs, step);
        const float cG    = msToCoeff (std::max (0.5f, p.fastMs * 0.25f), step);
        // Falling gain reacts almost at once, so a Sustain boost from the previous
        // tail is not carried into the next onset.
        const float cGDown = msToCoeff (0.1f, step);
        const bool  unity = p.shape.isUnity();

        float minDb = 0.0f, maxDb = 0.0f;
        float minG = 1.0f, maxG = 1.0f;

        auto targetGain = [&] (FollowerPair& f, bool traced, int i) -> float
        {
            ShapeBreakdown b;
            if (! unity)
                b = shapeGainBreakdown (powerDiffDb (f.fast, f.slow, kPowerEps), p.shape);

            if (traced)
            {
                if (trace->ratioDb != nullptr)   trace->ratioDb[i]   = b.ratioDb * p.mix;
                if (trace->attackDb != nullptr)  trace->attackDb[i]  = b.attackDb * p.mix;
                if (trace->sustainDb != nullptr) trace->sustainDb[i] = b.sustainDb * p.mix;
            }

            return unity ? 1.0f : dbToGain (b.totalDb);
        };

        auto traceApplied = [&] (bool traced, int i, float applied)
        {
            if (traced && trace->appliedDb != nullptr)
                trace->appliedDb[i] = 20.0f * std::log10 (std::max (applied, 1.0e-6f));
        };

        auto smooth = [&] (float& g, float target)
        {
            g += (target < g ? cGDown : cG) * (target - g);
            if (std::abs (g - target) < 1.0e-6f)
                g = target;
            minG = std::min (minG, g);
            maxG = std::max (maxG, g);
        };

        if (p.link)
        {
            auto& f = followers[0];
            auto& g = gains[0];

            for (int i = 0; i < numSamples; ++i)
            {
                float pw = 0.0f;
                for (int c = 0; c < numDetChannels; ++c)
                    pw = std::max (pw, det[c][i] * det[c][i]);

                f.push (pw, cF, cS);
                smooth (g, targetGain (f, trace != nullptr, i));

                const float applied = 1.0f + p.mix * (g - 1.0f);
                traceApplied (trace != nullptr, i, applied);
                for (int c = 0; c < numChannels; ++c)
                    data[c][i] *= applied;
            }

            for (size_t c = 1; c < followers.size(); ++c)
            {
                followers[c] = f;
                gains[c] = g;
            }
        }
        else
        {
            for (int c = 0; c < numChannels; ++c)
            {
                auto& f = followers[(size_t) c];
                auto& g = gains[(size_t) c];
                const float* d = det[std::min (c, numDetChannels - 1)];
                float* x = data[c];
                const bool traced = trace != nullptr && c == 0;

                for (int i = 0; i < numSamples; ++i)
                {
                    f.push (d[i] * d[i], cF, cS);
                    smooth (g, targetGain (f, traced, i));
                    const float applied = 1.0f + p.mix * (g - 1.0f);
                    traceApplied (traced, i, applied);
                    x[i] *= applied;
                }
            }
        }

        minDb = 20.0f * std::log10 (std::max (minG, 1.0e-6f));
        maxDb = 20.0f * std::log10 (std::max (maxG, 1.0e-6f));
        lastMinDb = minDb;
        lastMaxDb = maxDb;
    }

    /** Keeps followers tracking while bypassed so re-enabling does not jump. */
    void track (const float* const* det, int numDetChannels, int numSamples,
                const TimeStageParams& p, int numChannels) noexcept
    {
        using namespace chouchou::env;

        numChannels = std::min (numChannels, (int) followers.size());
        if (numChannels <= 0 || numDetChannels <= 0)
            return;

        const float step = (float) (1.0 / sr);
        const float cF = msToCoeff (p.fastMs, step);
        const float cS = msToCoeff (p.slowMs, step);

        for (int c = 0; c < numChannels; ++c)
        {
            const float* d = det[std::min (c, numDetChannels - 1)];
            for (int i = 0; i < numSamples; ++i)
                followers[(size_t) c].push (d[i] * d[i], cF, cS);
            gains[(size_t) c] = 1.0f;
        }

        lastMinDb = lastMaxDb = 0.0f;
    }

private:
    static constexpr float kPowerEps = 1.0e-10f; // -100 dBFS

    double sr = 44100.0;
    std::vector<chouchou::env::FollowerPair> followers;
    std::vector<float> gains;
    float lastMinDb = 0.0f, lastMaxDb = 0.0f;
};
