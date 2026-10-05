#pragma once

#include <JuceHeader.h>
#include "Biquad.h"
#include <array>
#include <cmath>
#include <vector>

namespace chouchou
{
    // Half-period feedforward comb: y = (x + g * x(t - T/2)) / (1 + |g|), T = 1 / f0.
    // Harmonic k is scaled by (1 + g * (-1)^k) / (1 + |g|): g > 0 favours even, g < 0 odd.
    // Output is the delta y - x. Fundamental protect removes the H1 part of the delta with
    // two cascaded unity-peak band-passes at f0 (zero phase at f0, so H1 is restored exactly).
    //
    // Two comb voices share one delay line. A pitch jump larger than a semitone parks the idle
    // voice on the new delay and crossfades to it, so the comb never sweeps through wrong
    // pitches. Smaller changes glide the active voice in the log-delay domain.
    class HarmonicRebalancer
    {
    public:
        static constexpr double kMinF0            = 20.0;
        static constexpr double kMinDelaySamples  = 2.5;
        static constexpr double kProtectQ         = 3.0;
        static constexpr double kDelaySmoothSec   = 0.005;
        static constexpr double kJumpThreshold    = 0.057762265;   // ln (2^(1/12))
        static constexpr int    kCoeffInterval    = 32;

        void prepare (double sampleRate, int numChannels, float initialBalance, bool initialProtect)
        {
            sr = sampleRate;
            channels = juce::jmax (1, numChannels);

            const int needed = (int) std::ceil (sr / (2.0 * kMinF0)) + 8;
            size = juce::nextPowerOfTwo (needed);
            mask = size - 1;
            lines.setSize (channels, size);
            lines.clear();
            writePos = 0;

            gain.reset (sr, 0.03);
            gain.setCurrentAndTargetValue (juce::jlimit (-1.0f, 1.0f, initialBalance));
            activity.reset (sr, 0.05);
            activity.setCurrentAndTargetValue (0.0f);
            protectMix.reset (sr, 0.03);
            protectMix.setCurrentAndTargetValue (initialProtect ? 1.0f : 0.0f);

            smoothCoef = 1.0 - std::exp (-1.0 / (kDelaySmoothSec * sr));
            setGlide (glideMsPerOctave);
            setJumpFade (jumpFadeMs);

            const double startLog = std::log (sr / (2.0 * 220.0));
            for (auto& v : voices)
            {
                v.bp1.assign ((size_t) channels, Biquad {});
                v.bp2.assign ((size_t) channels, Biquad {});
                v.logDelay = v.targetLog = startLog;
                v.delaySamples = std::exp (startLog);
                updateBandpass (v);
            }
            activeVoice = 0;
            crossfade = 1.0;
        }

        // Empties the delay line and filters; the next process() call snaps to its pitch.
        void clearState (float balance, bool protect) noexcept
        {
            lines.clear();
            for (auto& v : voices)
                for (size_t ch = 0; ch < v.bp1.size(); ++ch)
                {
                    v.bp1[ch].reset();
                    v.bp2[ch].reset();
                }
            gain.setCurrentAndTargetValue (juce::jlimit (-1.0f, 1.0f, balance));
            protectMix.setCurrentAndTargetValue (protect ? 1.0f : 0.0f);
            activity.setCurrentAndTargetValue (0.0f);
            crossfade = 1.0;
        }

        // Fastest glide speed for small pitch changes, in milliseconds per octave.
        void setGlide (double msPerOctave) noexcept
        {
            glideMsPerOctave = juce::jmax (0.1, msPerOctave);
            maxLogSlew = std::log (2.0) / (glideMsPerOctave * 0.001 * sr);
        }

        void setJumpFade (double ms) noexcept
        {
            jumpFadeMs = juce::jmax (0.1, ms);
            crossfadeStep = 1.0 / (jumpFadeMs * 0.001 * sr);
        }

        double getDelaySamples() const noexcept         { return voices[(size_t) activeVoice].delaySamples; }
        double getVoiceDelay (int voice) const noexcept { return voices[(size_t) voice].delaySamples; }
        double getCrossfade() const noexcept            { return crossfade; }
        int    getActiveVoice() const noexcept          { return activeVoice; }

        // x and deltaOut both cover [0, numSamples).
        void process (const juce::AudioBuffer<float>& x, juce::AudioBuffer<float>& deltaOut, int numSamples,
                      float f0Hz, bool active, float balance, bool protect) noexcept
        {
            gain.setTargetValue (juce::jlimit (-1.0f, 1.0f, balance));
            activity.setTargetValue (active ? 1.0f : 0.0f);
            protectMix.setTargetValue (protect ? 1.0f : 0.0f);

            const double maxF0 = sr / (2.0 * kMinDelaySamples);
            const double f0 = juce::jlimit (kMinF0, maxF0, (double) f0Hz);
            const double targetDelay = juce::jlimit (kMinDelaySamples, (double) (size - 4), sr / (2.0 * f0));
            retarget (std::log (targetDelay));

            auto& incoming = voices[(size_t) activeVoice];
            auto& outgoing = voices[(size_t) (1 - activeVoice)];

            for (int i = 0; i < numSamples; ++i)
            {
                incoming.logDelay += juce::jlimit (-maxLogSlew, maxLogSlew,
                                                   (incoming.targetLog - incoming.logDelay) * smoothCoef);
                incoming.delaySamples = std::exp (incoming.logDelay);

                const bool fading = crossfade < 1.0;
                if (! fading)
                {
                    outgoing.logDelay = incoming.logDelay;
                    outgoing.targetLog = incoming.targetLog;
                    outgoing.delaySamples = incoming.delaySamples;
                }

                if (++coeffCounter >= kCoeffInterval)
                {
                    coeffCounter = 0;
                    refreshBandpass (incoming);
                    refreshBandpass (outgoing);
                }

                const float g = gain.getNextValue();
                const float a = activity.getNextValue();
                const float pm = protectMix.getNextValue();
                const float absG = std::abs (g);
                const float norm = 1.0f / (1.0f + absG);
                const float wIn = (float) crossfade;

                const Tap tapIn  = makeTap (incoming.delaySamples);
                const Tap tapOut = makeTap (outgoing.delaySamples);

                for (int ch = 0; ch < channels; ++ch)
                {
                    float* line = lines.getWritePointer (ch);
                    const float in = x.getReadPointer (ch)[i];
                    line[writePos] = in;

                    const float dIn  = voiceDelta (incoming, ch, read (line, tapIn), in, g, absG, norm, pm);
                    const float dOut = voiceDelta (outgoing, ch, read (line, tapOut), in, g, absG, norm, pm);
                    deltaOut.getWritePointer (ch)[i] = a * (wIn * dIn + (1.0f - wIn) * dOut);
                }

                if (fading)
                    crossfade = juce::jmin (1.0, crossfade + crossfadeStep);

                writePos = (writePos + 1) & mask;
            }
        }

    private:
        struct Voice
        {
            double logDelay = 4.6, targetLog = 4.6, delaySamples = 100.0, coeffDelay = 0.0;
            std::vector<Biquad> bp1, bp2;
        };

        struct Tap
        {
            int i0 = 0;
            float t = 0.0f;
        };

        void retarget (double targetLog) noexcept
        {
            auto& current = voices[(size_t) activeVoice];

            if (activity.getCurrentValue() <= 0.0f)
            {
                for (auto& v : voices)
                {
                    v.logDelay = v.targetLog = targetLog;
                    v.delaySamples = std::exp (targetLog);
                    refreshBandpass (v);
                }
                crossfade = 1.0;
                return;
            }

            if (std::abs (targetLog - current.targetLog) <= kJumpThreshold)
            {
                current.targetLog = targetLog;
                return;
            }

            if (crossfade >= 1.0)
            {
                activeVoice = 1 - activeVoice;
                crossfade = 0.0;
            }

            auto& parked = voices[(size_t) activeVoice];
            parked.logDelay = parked.targetLog = targetLog;
            parked.delaySamples = std::exp (targetLog);
            refreshBandpass (parked);
        }

        Tap makeTap (double delay) const noexcept
        {
            const double readPos = (double) writePos - delay + (double) size;
            Tap tap;
            tap.i0 = (int) readPos;
            tap.t = (float) (readPos - (double) tap.i0);
            return tap;
        }

        float read (const float* line, const Tap& tap) const noexcept
        {
            return lagrange3 (line[(tap.i0 - 1) & mask], line[tap.i0 & mask],
                              line[(tap.i0 + 1) & mask], line[(tap.i0 + 2) & mask], tap.t);
        }

        static float voiceDelta (Voice& v, int ch, float delayed, float in, float g, float absG,
                                 float norm, float protectMix) noexcept
        {
            const float raw = (g * delayed - absG * in) * norm;
            const float h1 = v.bp2[(size_t) ch].process (v.bp1[(size_t) ch].process (raw));
            return raw - protectMix * h1;
        }

        static float lagrange3 (float xm1, float x0, float x1, float x2, float t) noexcept
        {
            const float tp1 = t + 1.0f, tm1 = t - 1.0f, tm2 = t - 2.0f;
            return -t * tm1 * tm2 * (1.0f / 6.0f) * xm1
                 + tp1 * tm1 * tm2 * 0.5f * x0
                 - tp1 * t * tm2 * 0.5f * x1
                 + tp1 * t * tm1 * (1.0f / 6.0f) * x2;
        }

        // Coefficients only change when the voice's delay has moved, so a clean jump crossfade
        // runs both voices on fixed coefficients.
        void refreshBandpass (Voice& v) noexcept
        {
            if (std::abs (v.delaySamples - v.coeffDelay) > 1.0e-9 * v.delaySamples)
                updateBandpass (v);
        }

        void updateBandpass (Voice& v) noexcept
        {
            v.coeffDelay = v.delaySamples;
            const auto c = Biquad::bandpass (sr, sr / (2.0 * v.delaySamples), kProtectQ);
            for (size_t ch = 0; ch < v.bp1.size(); ++ch)
            {
                v.bp1[ch].setCoefficients (c);
                v.bp2[ch].setCoefficients (c);
            }
        }

        double sr = 44100.0;
        int channels = 2;
        int size = 1, mask = 0, writePos = 0;
        int coeffCounter = 0;
        double smoothCoef = 0.001, maxLogSlew = 0.001;
        double glideMsPerOctave = 20.0, jumpFadeMs = 15.0;
        double crossfade = 1.0, crossfadeStep = 0.001;
        int activeVoice = 0;

        juce::AudioBuffer<float> lines;
        std::array<Voice, 2> voices;
        juce::SmoothedValue<float> gain, activity, protectMix;
    };
}
