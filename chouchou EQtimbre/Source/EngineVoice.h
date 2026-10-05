#pragma once

#include <JuceHeader.h>
#include "HarmonicGenerator.h"
#include "HarmonicRebalancer.h"
#include "PitchSource.h"
#include <array>
#include <cmath>
#include <vector>

namespace chouchou
{
    // The engine parameters of one voice, in the order used by the randomizer and the UI.
    enum EngineParam
    {
        epMode = 0, epDrive, epDriveComp, epAutoDrive, epBias, epEven, epOdd, epTone, epDcCut, epGenLevel,
        epBalance, epRebLevel, epPitchSource, epManualHz, epMidiPriority, epMidiHold, epProtect,
        epConfidence, epGlide, epJumpFade,
        kNumEngineParams
    };

    struct EngineParamInfo
    {
        const char* id;
        int numChoices;          // 0 = continuous, 2 = on/off, otherwise a choice list
        const char* shortName;   // UTF-8
    };

    inline const std::array<EngineParamInfo, kNumEngineParams>& engineParamInfo()
    {
        static const std::array<EngineParamInfo, kNumEngineParams> table { {
            { "mode", 3, "Mode" }, { "drive", 0, "Drive" }, { "driveComp", 0, "Comp. Drive" },
            { "autoDrive", 2, "Drive auto" }, { "bias", 0, "Biais" }, { "even", 0, "Pairs" },
            { "odd", 0, "Impairs" }, { "tone", 0, "Tonalit\xc3\xa9" }, { "dcCut", 0, "Coupe DC" },
            { "genLevel", 0, "Niveau G\xc3\xa9n." }, { "balance", 0, "Balance" },
            { "rebLevel", 0, "Niveau R\xc3\xa9\xc3\xa9q." }, { "pitchSource", 3, "Source de hauteur" },
            { "manualHz", 0, "Hauteur" }, { "midiPriority", 3, "Priorit\xc3\xa9 MIDI" },
            { "midiHold", 2, "Maintien MIDI" }, { "protect", 2, "Protection fond." },
            { "confidence", 0, "Confiance" }, { "glide", 0, "Glissement" }, { "jumpFade", 0, "Fondu de saut" },
        } };
        return table;
    }

    using EngineValues = std::array<float, kNumEngineParams>;

    // One complete effect: Generate + Rebalance + pitch source, with its own latency-aligned dry
    // path. Output = dry(t - L) + mix * (genLevel * genDelta + rebLevel * rebDelta).
    class EngineVoice
    {
    public:
        static constexpr int    kDryRingSize   = 4096;
        static constexpr double kRampSeconds   = 0.03;
        static constexpr double kLevelSeconds  = 0.3;

        struct Settings
        {
            HarmonicGenerator::Params gen;
            int   mode = 0;          // 0 Generate, 1 Rebalance, 2 both
            float mix = 1.0f, genLevel = 1.0f, rebLevel = 1.0f, balance = 0.0f;
            bool  protect = true;
            int   pitchSource = 0;
            float manualHz = 220.0f;
            int   midiPriority = 0;
            bool  midiHold = true;
            float confidence = 0.7f, glide = 20.0f, jumpFade = 15.0f;

            // v holds plain parameter values (as shown on the knobs); mix is 0..1.
            static Settings fromValues (const EngineValues& v, float mixAmount) noexcept
            {
                Settings s;
                s.gen.drive     = v[epDrive];
                s.gen.driveComp = v[epDriveComp] * 0.01f;
                s.gen.bias      = v[epBias];
                s.gen.even      = v[epEven] * 0.01f;
                s.gen.odd       = v[epOdd] * 0.01f;
                s.gen.toneHz    = v[epTone];
                s.gen.dcHz      = v[epDcCut];
                s.gen.autoDrive = v[epAutoDrive] > 0.5f;
                s.mode          = juce::jlimit (0, 2, (int) std::lround (v[epMode]));
                s.mix           = mixAmount;
                s.genLevel      = v[epGenLevel] * 0.01f;
                s.rebLevel      = v[epRebLevel] * 0.01f;
                s.balance       = v[epBalance] * 0.01f;
                s.protect       = v[epProtect] > 0.5f;
                s.pitchSource   = juce::jlimit (0, 2, (int) std::lround (v[epPitchSource]));
                s.manualHz      = v[epManualHz];
                s.midiPriority  = juce::jlimit (0, 2, (int) std::lround (v[epMidiPriority]));
                s.midiHold      = v[epMidiHold] > 0.5f;
                s.confidence    = v[epConfidence] * 0.01f;
                s.glide         = v[epGlide];
                s.jumpFade      = v[epJumpFade];
                return s;
            }
        };

        void prepare (double sampleRate, int numChannels, int maxBlockSize, int factorIndex, const Settings& s)
        {
            sr = sampleRate;
            channels = juce::jmax (1, numChannels);
            maxBlock = juce::jmax (1, maxBlockSize);

            generator.setFactorIndex (factorIndex, true);
            generator.prepare (sr, channels, s.gen);
            rebalancer.prepare (sr, channels, s.balance, s.protect);
            rebalancer.setGlide (s.glide);
            rebalancer.setJumpFade (s.jumpFade);
            pitch.prepare (sr);
            lastPitchSource = s.pitchSource;

            genDelta.setSize (channels, maxBlock);
            rebDelta.setSize (channels, maxBlock);
            aligned.setSize (channels, maxBlock);
            dryRing.setSize (channels, kDryRingSize);
            dryRing.clear();
            dryWritePos = 0;
            updateLatency();

            for (auto* v : { &mix, &genLevel, &rebLevel, &genWeight, &rebWeight })
                v->reset (sr, kRampSeconds);
            snapLevels (s);

            inPower = 0.0f;
            levelCoef = (float) (1.0 - std::exp (-1.0 / (kLevelSeconds * sr)));
        }

        void setFactorIndex (int index)
        {
            generator.setFactorIndex (index);
            updateLatency();
        }

        int getFactorIndex() const noexcept    { return generator.getFactorIndex(); }
        int getLatencySamples() const noexcept { return latency; }

        void handleMidiMessage (const juce::MidiMessage& m) noexcept { pitch.handleMidiMessage (m); }

        // Clears every filter and delay line and jumps the smoothed settings to s. Used when a
        // voice resumes after being skipped; the dry ring is left alone so the latency holds.
        void resetDsp (const Settings& s) noexcept
        {
            generator.setFactorIndex (generator.getFactorIndex(), true);
            generator.snapTo (s.gen);
            rebalancer.clearState (s.balance, s.protect);
            pitch.resetAuto();
            snapLevels (s);
        }

        // Processes io[start, start + n) in place. wetGain (may be null) scales the processed
        // part per sample; 0 leaves exactly the delayed input.
        void process (juce::AudioBuffer<float>& io, int start, int n, const Settings& s, const float* wetGain) noexcept
        {
            const int chans = juce::jmin (channels, io.getNumChannels());

            generator.process (io, start, genDelta, n, s.gen);
            pushDry (io, start, n, chans);

            const auto source = (PitchSource::Mode) s.pitchSource;
            if (source == PitchSource::Mode::autoTrack)
            {
                if (lastPitchSource != s.pitchSource)
                    pitch.resetAuto();
                pitch.analyse (aligned.getArrayOfReadPointers(), chans, n);
            }
            lastPitchSource = s.pitchSource;

            pitch.update (source, s.manualHz, (PitchSource::Priority) s.midiPriority, s.midiHold, s.confidence);

            rebalancer.setGlide (s.glide);
            rebalancer.setJumpFade (s.jumpFade);
            rebalancer.process (aligned, rebDelta, n, pitch.getF0(), pitch.isActive(), s.balance, s.protect);

            setLevelTargets (s);
            for (int i = 0; i < n; ++i)
            {
                float m = mix.getNextValue();
                if (wetGain != nullptr)
                    m *= wetGain[i];
                const float gl = genLevel.getNextValue() * genWeight.getNextValue() * m;
                const float rl = rebLevel.getNextValue() * rebWeight.getNextValue() * m;

                for (int ch = 0; ch < chans; ++ch)
                {
                    const float dry = aligned.getReadPointer (ch)[i];
                    io.getWritePointer (ch, start)[i] = dry + gl * genDelta.getReadPointer (ch)[i]
                                                            + rl * rebDelta.getReadPointer (ch)[i];
                }
            }
        }

        // Only delays io by the latency (used while the voice is skipped).
        void processDelayOnly (juce::AudioBuffer<float>& io, int start, int n) noexcept
        {
            const int chans = juce::jmin (channels, io.getNumChannels());
            pushDry (io, start, n, chans);
            for (int ch = 0; ch < chans; ++ch)
                juce::FloatVectorOperations::copy (io.getWritePointer (ch, start), aligned.getReadPointer (ch), n);
        }

        // Latency-aligned input of the last processed chunk, [0, n).
        const juce::AudioBuffer<float>& getAligned() const noexcept { return aligned; }
        float getInputRms() const noexcept { return std::sqrt (inPower); }
        float getAutoGain() const noexcept { return generator.getAutoGain(); }

        float getF0() const noexcept                   { return pitch.getF0(); }
        bool  isPitchActive() const noexcept           { return pitch.isActive(); }
        float getAutoConfidence() const noexcept       { return pitch.getAutoConfidence(); }
        PitchSource::Status getPitchStatus() const noexcept { return pitch.getStatus(); }

    private:
        void updateLatency() noexcept
        {
            latency = juce::jlimit (0, kDryRingSize - 1, generator.getLatencySamples());
        }

        void pushDry (const juce::AudioBuffer<float>& io, int start, int n, int chans) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const int readPos = (dryWritePos - latency + kDryRingSize) & (kDryRingSize - 1);
                float power = 0.0f;
                for (int ch = 0; ch < chans; ++ch)
                {
                    float* ring = dryRing.getWritePointer (ch);
                    const float x = io.getReadPointer (ch, start)[i];
                    ring[dryWritePos] = x;
                    aligned.getWritePointer (ch)[i] = ring[readPos];
                    power += x * x;
                }
                inPower += levelCoef * (power / (float) chans - inPower);
                dryWritePos = (dryWritePos + 1) & (kDryRingSize - 1);
            }
        }

        void setLevelTargets (const Settings& s) noexcept
        {
            mix.setTargetValue (s.mix);
            genLevel.setTargetValue (s.genLevel);
            rebLevel.setTargetValue (s.rebLevel);
            genWeight.setTargetValue (s.mode != 1 ? 1.0f : 0.0f);
            rebWeight.setTargetValue (s.mode != 0 ? 1.0f : 0.0f);
        }

        void snapLevels (const Settings& s) noexcept
        {
            setLevelTargets (s);
            for (auto* v : { &mix, &genLevel, &rebLevel, &genWeight, &rebWeight })
                v->setCurrentAndTargetValue (v->getTargetValue());
        }

        double sr = 44100.0;
        int channels = 2, maxBlock = 512;
        int latency = 0, dryWritePos = 0, lastPitchSource = -1;

        HarmonicGenerator generator;
        HarmonicRebalancer rebalancer;
        PitchSource pitch;

        juce::AudioBuffer<float> genDelta, rebDelta, aligned, dryRing;
        juce::SmoothedValue<float> mix, genLevel, rebLevel, genWeight, rebWeight;
        float inPower = 0.0f, levelCoef = 0.0f;
    };
}
