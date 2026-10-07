/*
  ==============================================================================

    chouchouShaper — shared envelope math (time stage and spectral stage)

      Every detected signal is tracked by a fast envelope (envF) and a slow
      envelope (envS). Their level difference d = 20*log10(envF / envS) drives
      two modes that can run together:

        Mode A  contour compression   gainA_dB = d * (1/ratio - 1)
        Mode B  transient shaping     d > 0 : attackDb  * min(d / range, 1)
                                      d < 0 : sustainDb * min(-d / range, 1)

      ratio = 1 and Attack = Sustain = 0 dB give exactly 0 dB (unity gain).

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <cmath>

namespace chouchou::env
{
    /** Level difference (dB) at which Mode B reaches its full Attack / Sustain gain. */
    constexpr float kShapeRangeDb = 6.0f;

    /** Total gain clamp per sample / bin. */
    constexpr float kMaxGainDb = 30.0f;

    /** One-pole smoothing coefficient for a time constant, updated every stepSeconds. */
    inline float msToCoeff (float timeMs, float stepSeconds) noexcept
    {
        if (timeMs <= 0.0f || stepSeconds <= 0.0f)
            return 1.0f;

        return 1.0f - std::exp (-stepSeconds / (timeMs * 0.001f));
    }

    struct ShapeParams
    {
        float ratio     = 1.0f;
        float attackDb  = 0.0f;
        float sustainDb = 0.0f;

        bool isUnity() const noexcept
        {
            return ratio == 1.0f && attackDb == 0.0f && sustainDb == 0.0f;
        }
    };

    /** Where the gain comes from: Mode A (ratio), Mode B attack, Mode B sustain. */
    struct ShapeBreakdown
    {
        float ratioDb   = 0.0f;
        float attackDb  = 0.0f;
        float sustainDb = 0.0f;
        float totalDb   = 0.0f; // clamped sum
    };

    inline ShapeBreakdown shapeGainBreakdown (float d, const ShapeParams& p) noexcept
    {
        ShapeBreakdown b;
        b.ratioDb = d * (1.0f / p.ratio - 1.0f);

        if (d > 0.0f)
            b.attackDb = p.attackDb * std::min (d / kShapeRangeDb, 1.0f);
        else if (d < 0.0f)
            b.sustainDb = p.sustainDb * std::min (-d / kShapeRangeDb, 1.0f);

        b.totalDb = std::clamp (b.ratioDb + b.attackDb + b.sustainDb, -kMaxGainDb, kMaxGainDb);
        return b;
    }

    /** Gain (dB) for a fast/slow level difference d (dB). */
    inline float shapeGainDb (float d, const ShapeParams& p) noexcept
    {
        return shapeGainBreakdown (d, p).totalDb;
    }

    inline float dbToGain (float db) noexcept
    {
        return db == 0.0f ? 1.0f : std::pow (10.0f, db * 0.05f);
    }

    /** Fast / slow follower pair on a non-negative detector value (power or magnitude). */
    struct FollowerPair
    {
        float fast = 0.0f;
        float slow = 0.0f;

        void reset() noexcept { fast = slow = 0.0f; }

        void push (float x, float fastCoeff, float slowCoeff) noexcept
        {
            fast += fastCoeff * (x - fast);
            slow += slowCoeff * (x - slow);
        }
    };

    /** d in dB from two amplitude levels; eps keeps silence at d = 0. */
    inline float amplitudeDiffDb (float fast, float slow, float eps) noexcept
    {
        return 20.0f * std::log10 ((fast + eps) / (slow + eps));
    }

    /** d in dB from two power levels; eps is a power floor. */
    inline float powerDiffDb (float fast, float slow, float eps) noexcept
    {
        return 10.0f * std::log10 ((fast + eps) / (slow + eps));
    }
}
