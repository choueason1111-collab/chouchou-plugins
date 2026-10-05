#pragma once

#include <JuceHeader.h>
#include "Biquad.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace chouchou
{
    // Provides f0 for the rebalancer from a manual value, incoming MIDI notes, or YIN
    // detection. Auto detection is meant for monophonic material.
    class PitchSource
    {
    public:
        enum class Mode     { manual = 0, midi, autoTrack };
        enum class Priority { last = 0, highest, lowest };
        enum class Status   { manual = 0, midiNote, midiHeld, midiIdle, autoLocked, autoSearching };

        static constexpr float kMinAutoHz      = 50.0f;
        static constexpr float kMaxAutoHz      = 1500.0f;
        static constexpr float kHysteresis     = 0.1f;
        static constexpr float kMinThreshold   = 0.01f;
        static constexpr int   kMaxChannels    = 2;
        static constexpr int   kMaxHeld        = 128;
        static constexpr int   kAntiAliasStages = 3;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            decimation = juce::jmax (1, (int) std::round (sr / 11025.0));
            analysisRate = sr / decimation;
            maxLag = juce::jmin (kMaxLagCapacity - 2, (int) std::ceil (analysisRate / kMinAutoHz));
            minLag = juce::jmax (2, (int) std::floor (analysisRate / kMaxAutoHz));

            // 6th-order Butterworth anti-alias low-pass before decimation. The low cutoff keeps
            // content near the analysis Nyquist from folding into the 50-1500 Hz search range.
            const double cutoff = juce::jmin (0.25 * analysisRate, 0.45 * sr);
            constexpr std::array<double, kAntiAliasStages> stageQ { 0.51763809, 0.70710678, 1.93185165 };
            for (int s = 0; s < kAntiAliasStages; ++s)
            {
                const auto c = Biquad::lowpass (sr, cutoff, stageQ[(size_t) s]);
                for (int ch = 0; ch < kMaxChannels; ++ch)
                    antiAlias[(size_t) s][(size_t) ch].setCoefficients (c);
            }

            reset();
        }

        void reset()
        {
            resetMidi();
            resetAuto();
            f0 = 220.0f;
            active = false;
            status = Status::manual;
        }

        void resetMidi() noexcept
        {
            heldCount = 0;
            sustainDown.fill (false);
            hasMidiNote = false;
            lastMidiF0 = 220.0f;
        }

        void resetAuto() noexcept
        {
            for (auto& r : rings)
                r.fill (0.0f);
            for (auto& stage : antiAlias)
                for (auto& f : stage)
                    f.reset();
            ringPos = 0;
            validSamples = 0;
            accumulated = 0;
            sinceHop = 0;
            autoF0 = 0.0f;
            autoConfidence = 0.0f;
            autoLocked = false;
        }

        void handleMidiMessage (const juce::MidiMessage& msg) noexcept
        {
            const int channel = juce::jlimit (0, 16, msg.getChannel());

            if (msg.isNoteOn())
                noteOn (channel, msg.getNoteNumber());
            else if (msg.isNoteOff())
                noteOff (channel, msg.getNoteNumber());
            else if (msg.isSustainPedalOn())
                sustainDown[(size_t) channel] = true;
            else if (msg.isSustainPedalOff())
                releaseSustained (channel);
            else if (msg.isAllNotesOff())          // CC123: behaves like note-offs, so the pedal still holds
                allNotesOff (channel);
            else if (msg.isAllSoundOff())          // CC120: silence everything, pedal included
            {
                removeChannel (channel);
                sustainDown[(size_t) channel] = false;
            }
            else if (msg.isResetAllControllers())  // CC121: pedal returns to up
                releaseSustained (channel);
        }

        // channels[0 .. numChannels) cover numSamples each.
        void analyse (const float* const* channels, int numChannels, int numSamples) noexcept
        {
            analysisChannels = juce::jlimit (1, kMaxChannels, numChannels);

            for (int i = 0; i < numSamples; ++i)
            {
                for (int ch = 0; ch < analysisChannels; ++ch)
                {
                    float v = channels[ch][i];
                    for (auto& stage : antiAlias)
                        v = stage[(size_t) ch].process (v);
                    filtered[(size_t) ch] = v;
                }

                if (++accumulated < decimation)
                    continue;

                accumulated = 0;
                for (int ch = 0; ch < analysisChannels; ++ch)
                    rings[(size_t) ch][(size_t) ringPos] = filtered[(size_t) ch];
                ringPos = (ringPos + 1) & kRingMask;
                validSamples = juce::jmin (validSamples + 1, kRingSize);

                if (++sinceHop >= kHop)
                {
                    sinceHop = 0;
                    if (validSamples >= kWindow + maxLag)
                        runYin();
                }
            }
        }

        void update (Mode mode, float manualHz, Priority priority, bool midiHold, float confidenceThreshold) noexcept
        {
            switch (mode)
            {
                case Mode::manual:
                    f0 = manualHz;
                    active = true;
                    status = Status::manual;
                    break;

                case Mode::midi:
                    if (heldCount > 0)
                    {
                        lastMidiF0 = noteToHz (selectNote (priority));
                        hasMidiNote = true;
                        f0 = lastMidiF0;
                        active = true;
                        status = Status::midiNote;
                    }
                    else if (midiHold && hasMidiNote)
                    {
                        f0 = lastMidiF0;
                        active = true;
                        status = Status::midiHeld;
                    }
                    else
                    {
                        active = false;
                        status = Status::midiIdle;
                    }
                    break;

                case Mode::autoTrack:
                {
                    const float threshold = autoLocked ? confidenceThreshold - kHysteresis : confidenceThreshold;
                    autoLocked = autoF0 > 0.0f && autoConfidence >= juce::jmax (kMinThreshold, threshold);
                    if (autoLocked)
                        f0 = autoF0;
                    active = autoLocked;
                    status = autoLocked ? Status::autoLocked : Status::autoSearching;
                    break;
                }
            }
        }

        float  getF0() const noexcept             { return f0; }
        bool   isActive() const noexcept          { return active; }
        Status getStatus() const noexcept         { return status; }
        float  getAutoF0() const noexcept         { return autoF0; }
        float  getAutoConfidence() const noexcept { return autoConfidence; }
        int    getHeldCount() const noexcept      { return heldCount; }

        static float noteToHz (int note) noexcept
        {
            return 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
        }

    private:
        static constexpr int kRingSize        = 1024;
        static constexpr int kRingMask        = kRingSize - 1;
        static constexpr int kWindow          = 384;
        static constexpr int kMaxLagCapacity  = kRingSize - kWindow;
        static constexpr int kHop             = 128;
        static constexpr float kYinThreshold  = 0.2f;
        static constexpr float kSilencePower  = 1.0e-6f;   // -60 dBFS

        struct HeldNote
        {
            int channel = 0, note = 0;
            bool sustained = false;
        };

        int findHeld (int channel, int note) const noexcept
        {
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i].channel == channel && held[(size_t) i].note == note)
                    return i;
            return -1;
        }

        void removeAt (int index) noexcept
        {
            for (int j = index + 1; j < heldCount; ++j)
                held[(size_t) (j - 1)] = held[(size_t) j];
            --heldCount;
        }

        void noteOn (int channel, int note) noexcept
        {
            if (const int i = findHeld (channel, note); i >= 0)
                removeAt (i);
            if (heldCount == kMaxHeld)
                removeAt (0);
            held[(size_t) heldCount++] = { channel, note, false };
        }

        void noteOff (int channel, int note) noexcept
        {
            const int i = findHeld (channel, note);
            if (i < 0)
                return;
            if (sustainDown[(size_t) channel])
                held[(size_t) i].sustained = true;
            else
                removeAt (i);
        }

        void releaseSustained (int channel) noexcept
        {
            sustainDown[(size_t) channel] = false;
            for (int i = heldCount - 1; i >= 0; --i)
                if (held[(size_t) i].channel == channel && held[(size_t) i].sustained)
                    removeAt (i);
        }

        void allNotesOff (int channel) noexcept
        {
            if (! sustainDown[(size_t) channel])
            {
                removeChannel (channel);
                return;
            }
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i].channel == channel)
                    held[(size_t) i].sustained = true;
        }

        void removeChannel (int channel) noexcept
        {
            for (int i = heldCount - 1; i >= 0; --i)
                if (held[(size_t) i].channel == channel)
                    removeAt (i);
        }

        int selectNote (Priority priority) const noexcept
        {
            int note = held[(size_t) (heldCount - 1)].note;
            for (int i = 0; i < heldCount; ++i)
            {
                if (priority == Priority::highest) note = juce::jmax (note, held[(size_t) i].note);
                if (priority == Priority::lowest)  note = juce::jmin (note, held[(size_t) i].note);
            }
            return note;
        }

        // Difference functions are summed across channels, so anti-phase stereo does not cancel.
        void runYin() noexcept
        {
            const int frameLen = kWindow + maxLag;
            const int start = (ringPos - frameLen + kRingSize) & kRingMask;
            float power = 0.0f;

            for (int ch = 0; ch < analysisChannels; ++ch)
            {
                auto& frame = frames[(size_t) ch];
                for (int j = 0; j < frameLen; ++j)
                    frame[(size_t) j] = rings[(size_t) ch][(size_t) ((start + j) & kRingMask)];
                for (int j = maxLag; j < frameLen; ++j)
                    power += frame[(size_t) j] * frame[(size_t) j];
            }

            if (power / (float) (kWindow * analysisChannels) < kSilencePower)
            {
                autoConfidence = 0.0f;
                autoF0 = 0.0f;
                return;
            }

            std::fill (cmnd.begin(), cmnd.begin() + maxLag + 1, 0.0f);
            for (int ch = 0; ch < analysisChannels; ++ch)
            {
                const auto& frame = frames[(size_t) ch];
                for (int tau = 1; tau <= maxLag; ++tau)
                {
                    float d = 0.0f;
                    for (int j = 0; j < kWindow; ++j)
                    {
                        const float diff = frame[(size_t) j] - frame[(size_t) (j + tau)];
                        d += diff * diff;
                    }
                    cmnd[(size_t) tau] += d;
                }
            }

            cmnd[0] = 1.0f;
            float running = 0.0f;
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                const float d = cmnd[(size_t) tau];
                running += d;
                cmnd[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
            }

            int best = -1;
            for (int tau = minLag; tau < maxLag; ++tau)
            {
                if (cmnd[(size_t) tau] < kYinThreshold)
                {
                    while (tau + 1 < maxLag && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                        ++tau;
                    best = tau;
                    break;
                }
            }

            if (best < 0)
            {
                best = minLag;
                for (int tau = minLag + 1; tau < maxLag; ++tau)
                    if (cmnd[(size_t) tau] < cmnd[(size_t) best])
                        best = tau;
            }

            float refined = (float) best;
            if (best > 1 && best < maxLag)
            {
                const float a = cmnd[(size_t) (best - 1)], b = cmnd[(size_t) best], c = cmnd[(size_t) (best + 1)];
                const float denom = a - 2.0f * b + c;
                if (std::abs (denom) > 1.0e-9f)
                    refined += juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denom);
            }

            autoConfidence = juce::jlimit (0.0f, 1.0f, 1.0f - cmnd[(size_t) best]);
            autoF0 = (float) (analysisRate / refined);
        }

        double sr = 44100.0, analysisRate = 11025.0;
        int decimation = 4, minLag = 7, maxLag = 220;

        std::array<HeldNote, kMaxHeld> held {};
        int heldCount = 0;
        std::array<bool, 17> sustainDown {};
        bool hasMidiNote = false;
        float lastMidiF0 = 220.0f;

        std::array<std::array<Biquad, kMaxChannels>, kAntiAliasStages> antiAlias {};
        std::array<float, kMaxChannels> filtered {};
        std::array<std::array<float, kRingSize>, kMaxChannels> rings {}, frames {};
        std::array<float, kMaxLagCapacity + 1> cmnd {};
        int analysisChannels = 1;
        int ringPos = 0, validSamples = 0, accumulated = 0, sinceHop = 0;

        float autoF0 = 0.0f, autoConfidence = 0.0f;
        bool autoLocked = false;

        float f0 = 220.0f;
        bool active = false;
        Status status = Status::manual;
    };
}
