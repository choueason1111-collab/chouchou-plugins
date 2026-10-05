#pragma once

#include <JuceHeader.h>
#include "EngineVoice.h"
#include <array>

namespace chouchou
{
    // Holds one uniform draw per engine parameter and turns it into the random stage's values.
    // Everything works in the parameters' normalised 0..1 space, so skewed knobs are spread
    // evenly by ear. For base b, amount a and draw U:
    //   continuous:  value = b + a * (U - b)          (a = 1 is fully random)
    //   on/off/list: value = random choice from V when U < a, otherwise b
    // A new draw morphs from the current draw over the morph time; switches change at the
    // halfway point. Runs on the audio thread and never allocates.
    class Randomizer
    {
    public:
        static constexpr int kNum = kNumEngineParams;
        using Draws = std::array<float, kNum>;

        Randomizer() { rng.setSeedRandomly(); roll (0.0, 1.0); }

        void setSeed (juce::int64 seed) noexcept { rng.setSeed (seed); }

        void roll (double morphSeconds, double sampleRate) noexcept
        {
            for (int i = 0; i < kNum; ++i)
            {
                uStart[(size_t) i] = currentU (i);
                vStart[(size_t) i] = currentV (i);
                uTarget[(size_t) i] = rng.nextFloat();
                vTarget[(size_t) i] = rng.nextFloat();
            }
            const double samples = morphSeconds * sampleRate;
            if (samples < 1.0)
            {
                progress = 1.0;
                step = 1.0;
            }
            else
            {
                progress = 0.0;
                step = 1.0 / samples;
            }
        }

        void advance (int numSamples) noexcept
        {
            progress = juce::jmin (1.0, progress + step * numSamples);
        }

        void finishMorph() noexcept { progress = 1.0; }
        bool isMorphing() const noexcept { return progress < 1.0; }

        void setDraws (const Draws& u, const Draws& v) noexcept
        {
            uStart = uTarget = u;
            vStart = vTarget = v;
            progress = 1.0;
        }

        const Draws& getTargetU() const noexcept { return uTarget; }
        const Draws& getTargetV() const noexcept { return vTarget; }

        float currentU (int i) const noexcept
        {
            if (progress >= 1.0)
                return uTarget[(size_t) i];
            if (isSwitch (i))
                return progress < 0.5 ? uStart[(size_t) i] : uTarget[(size_t) i];
            const float t = (float) progress;
            return uStart[(size_t) i] + t * (uTarget[(size_t) i] - uStart[(size_t) i]);
        }

        float currentV (int i) const noexcept
        {
            return progress < 0.5 ? vStart[(size_t) i] : vTarget[(size_t) i];
        }

        static bool isSwitch (int i) noexcept { return engineParamInfo()[(size_t) i].numChoices > 0; }

        // base, amount and out are normalised (amount 0..1).
        static float evaluateOne (int i, float base, float amount, float u, float v) noexcept
        {
            const int choices = engineParamInfo()[(size_t) i].numChoices;
            if (choices == 0)
                return juce::jlimit (0.0f, 1.0f, base + amount * (u - base));
            if (u >= amount)
                return base;
            const int pick = juce::jmin (choices - 1, (int) (v * (float) choices));
            return (float) pick / (float) (choices - 1);
        }

        void evaluate (const Draws& base, const Draws& amount, Draws& out) const noexcept
        {
            for (int i = 0; i < kNum; ++i)
                out[(size_t) i] = evaluateOne (i, base[(size_t) i], amount[(size_t) i], currentU (i), currentV (i));
        }

    private:
        juce::Random rng;
        Draws uStart {}, uTarget {}, vStart {}, vTarget {};
        double progress = 1.0, step = 1.0;
    };
}
