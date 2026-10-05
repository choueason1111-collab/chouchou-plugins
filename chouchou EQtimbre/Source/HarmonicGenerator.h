#pragma once

#include <JuceHeader.h>
#include "Biquad.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace chouchou
{
    // Adds new harmonics with a biased tanh shaper split into its odd and even parts:
    //   f(x)      = tanh (d * (x + b)) / d          (x + b when d is ~0)
    //   f_odd(x)  = (f(x) - f(-x)) / 2              odd harmonics, includes H1
    //   f_even(x) = (f(x) + f(-x)) / 2              even harmonics plus DC
    // Drive Comp scales both parts by c = (cosh^2(d*b))^comp, so at 100% the small-signal
    // gain is 1 (capped at kMaxDriveComp for extreme drive/bias settings).
    // Output is a delta relative to the (latency-delayed) input:
    //   delta = Tone (Odd * (c*f_odd(x) - x) + Even * DCBlock (c*(f_even(x) - f(0))))
    // Strict odd/even separation only holds for a single sinusoid; complex input also
    // produces intermodulation products.
    //
    // Auto Drive scales the shaper input by a = kAutoRefRms / RMS(input) and the shaper output
    // by 1/a, so the harmonic amount no longer depends on the input level. The dry path and
    // the small-signal gain are unaffected.
    class HarmonicGenerator
    {
    public:
        struct Params
        {
            float drive  = 2.0f;
            float driveComp = 0.0f;
            float bias   = 0.3f;
            float even   = 0.5f;
            float odd    = 0.0f;
            float toneHz = 20000.0f;
            float dcHz   = 7.0f;
            bool  autoDrive = false;
        };

        static constexpr int   kNumFactors           = 3;     // 2x, 4x, 8x
        static constexpr int   kChunk                = 64;
        static constexpr float kLinearDriveThreshold = 1.0e-4f;
        static constexpr double kRampSeconds         = 0.05;
        static constexpr float kMaxDriveComp         = 16.0f;   // +24 dB
        static constexpr float kAutoRefRms           = 0.25f;   // -12 dBFS RMS
        static constexpr float kMaxAutoGain          = 16.0f;   // +/-24 dB
        static constexpr double kAutoAttackSec       = 0.02;
        static constexpr double kAutoReleaseSec      = 0.3;

        static float driveCompGain (float d, float b, float comp) noexcept
        {
            if (d < kLinearDriveThreshold || comp <= 0.0f)
                return 1.0f;
            const float ch = std::cosh (juce::jmin (d * std::abs (b), 10.0f));
            return juce::jmin (kMaxDriveComp, std::pow (ch * ch, comp));
        }

        // Odd part and DC-free even part of the shaper for one sample.
        static void decompose (float x, float d, float b, float& oddPart, float& evenPart) noexcept
        {
            float pos, neg, zero;

            if (d < kLinearDriveThreshold)
            {
                pos = x + b;
                neg = b - x;
                zero = b;
            }
            else
            {
                const float invD = 1.0f / d;
                pos  = std::tanh (d * (x + b)) * invD;
                neg  = std::tanh (d * (b - x)) * invD;
                zero = std::tanh (d * b) * invD;
            }

            oddPart  = 0.5f * (pos - neg);
            evenPart = 0.5f * (pos + neg) - zero;
        }

        void prepare (double sampleRate, int numChannels, const Params& initial)
        {
            sr = sampleRate;
            channels = juce::jmax (1, numChannels);

            for (size_t i = 0; i < (size_t) kNumFactors; ++i)
            {
                oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (
                    (size_t) channels, i + 1,
                    juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
                oversamplers[i]->initProcessing ((size_t) kChunk);
            }

            dcX1.assign ((size_t) channels, 0.0f);
            dcY1.assign ((size_t) channels, 0.0f);
            tone.assign ((size_t) channels, Biquad {});

            for (auto* s : { &drive, &driveComp, &bias, &even, &odd, &toneHz, &dcHz })
                s->reset (sr, kRampSeconds);

            autoAmount.reset (sr, kRampSeconds);
            attackCoef  = (float) (1.0 - std::exp (-1.0 / (kAutoAttackSec * sr)));
            releaseCoef = (float) (1.0 - std::exp (-1.0 / (kAutoReleaseSec * sr)));

            snapTo (initial);
            setFactorIndex (factorIndex, true);
        }

        // Jumps all smoothed parameters to their targets and clears the level envelope.
        void snapTo (const Params& p) noexcept
        {
            drive.setCurrentAndTargetValue (juce::jmax (0.0f, p.drive));
            driveComp.setCurrentAndTargetValue (juce::jlimit (0.0f, 1.0f, p.driveComp));
            bias.setCurrentAndTargetValue (p.bias);
            even.setCurrentAndTargetValue (p.even);
            odd.setCurrentAndTargetValue (p.odd);
            toneHz.setCurrentAndTargetValue (p.toneHz);
            dcHz.setCurrentAndTargetValue (p.dcHz);
            autoAmount.setCurrentAndTargetValue (p.autoDrive ? 1.0f : 0.0f);
            autoPower = kAutoRefRms * kAutoRefRms;
        }

        void setFactorIndex (int index, bool force = false)
        {
            index = juce::jlimit (0, kNumFactors - 1, index);
            if (index == factorIndex && ! force)
                return;

            factorIndex = index;
            if (oversamplers[(size_t) factorIndex] != nullptr)
                oversamplers[(size_t) factorIndex]->reset();

            std::fill (dcX1.begin(), dcX1.end(), 0.0f);
            std::fill (dcY1.begin(), dcY1.end(), 0.0f);
            for (auto& t : tone)
                t.reset();
        }

        // Shaper input scale Drive auto would apply right now (before its on/off fade).
        float getAutoGain() const noexcept
        {
            return juce::jlimit (1.0f / kMaxAutoGain, kMaxAutoGain, kAutoRefRms / std::sqrt (autoPower + 1.0e-12f));
        }

        int getFactorIndex() const noexcept { return factorIndex; }
        int getFactor() const noexcept      { return 2 << factorIndex; }

        int getLatencySamples() const
        {
            const auto& os = oversamplers[(size_t) factorIndex];
            return os != nullptr ? (int) std::lround (os->getLatencyInSamples()) : 0;
        }

        // Writes the delta for input[inputStart, inputStart + numSamples) into deltaOut[0, numSamples).
        void process (const juce::AudioBuffer<float>& input, int inputStart,
                      juce::AudioBuffer<float>& deltaOut, int numSamples, const Params& target) noexcept
        {
            drive.setTargetValue (juce::jmax (0.0f, target.drive));
            driveComp.setTargetValue (juce::jlimit (0.0f, 1.0f, target.driveComp));
            bias.setTargetValue (target.bias);
            even.setTargetValue (target.even);
            odd.setTargetValue (target.odd);
            toneHz.setTargetValue (target.toneHz);
            dcHz.setTargetValue (target.dcHz);
            autoAmount.setTargetValue (target.autoDrive ? 1.0f : 0.0f);

            for (int done = 0; done < numSamples;)
            {
                const int n = juce::jmin (kChunk, numSamples - done);
                processChunk (input, inputStart + done, deltaOut, done, n);
                done += n;
            }
        }

    private:
        void processChunk (const juce::AudioBuffer<float>& input, int inStart,
                           juce::AudioBuffer<float>& deltaOut, int outStart, int n) noexcept
        {
            for (int h = 0; h < n; ++h)
            {
                drvBuf[(size_t) h]  = drive.getNextValue();
                biasBuf[(size_t) h] = bias.getNextValue();
                compBuf[(size_t) h] = driveCompGain (drvBuf[(size_t) h], biasBuf[(size_t) h], driveComp.getNextValue());
                evenBuf[(size_t) h] = even.getNextValue();
                oddBuf[(size_t) h]  = odd.getNextValue();

                float power = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                {
                    const float s = input.getReadPointer (ch)[inStart + h];
                    power += s * s;
                }
                power /= (float) channels;
                autoPower += (power > autoPower ? attackCoef : releaseCoef) * (power - autoPower);

                const float target = juce::jlimit (1.0f / kMaxAutoGain, kMaxAutoGain,
                                                   kAutoRefRms / std::sqrt (autoPower + 1.0e-12f));
                autoBuf[(size_t) h] = std::pow (target, autoAmount.getNextValue());
            }

            toneHz.skip (n);
            dcHz.skip (n);

            const double toneCut = juce::jlimit (20.0, 0.45 * sr, (double) toneHz.getCurrentValue());
            const auto toneCoeffs = Biquad::lowpass (sr, toneCut, 0.70710678);
            for (auto& t : tone)
                t.setCoefficients (toneCoeffs);

            auto& os = *oversamplers[(size_t) factorIndex];
            const double osRate = sr * getFactor();
            const float r = (float) std::exp (-2.0 * juce::MathConstants<double>::pi
                                              * juce::jmax (0.1f, dcHz.getCurrentValue()) / osRate);
            const int shift = factorIndex + 1;

            juce::dsp::AudioBlock<const float> inBlock (input.getArrayOfReadPointers(), (size_t) channels,
                                                        (size_t) inStart, (size_t) n);
            auto up = os.processSamplesUp (inBlock);
            const int upN = (int) up.getNumSamples();

            for (int ch = 0; ch < channels; ++ch)
            {
                float* data = up.getChannelPointer ((size_t) ch);
                float x1 = dcX1[(size_t) ch], y1 = dcY1[(size_t) ch];

                for (int j = 0; j < upN; ++j)
                {
                    const int h = j >> shift;
                    const float x = data[j];
                    const float a = autoBuf[(size_t) h];
                    float oddPart, evenPart;
                    decompose (x * a, drvBuf[(size_t) h], biasBuf[(size_t) h], oddPart, evenPart);
                    const float post = compBuf[(size_t) h] / a;
                    oddPart *= post;
                    evenPart *= post;

                    const float evenBlocked = evenPart - x1 + r * y1;
                    x1 = evenPart;
                    y1 = evenBlocked;

                    data[j] = oddBuf[(size_t) h] * (oddPart - x) + evenBuf[(size_t) h] * evenBlocked;
                }

                dcX1[(size_t) ch] = x1;
                dcY1[(size_t) ch] = y1;
            }

            juce::dsp::AudioBlock<float> outBlock (deltaOut.getArrayOfWritePointers(), (size_t) channels,
                                                   (size_t) outStart, (size_t) n);
            os.processSamplesDown (outBlock);

            for (int ch = 0; ch < channels; ++ch)
            {
                float* out = deltaOut.getWritePointer (ch, outStart);
                auto& t = tone[(size_t) ch];
                for (int i = 0; i < n; ++i)
                    out[i] = t.process (out[i]);
            }
        }

        double sr = 44100.0;
        int channels = 2;
        int factorIndex = 1;

        std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, kNumFactors> oversamplers;
        juce::SmoothedValue<float> drive, driveComp, bias, even, odd, toneHz, dcHz, autoAmount;
        std::array<float, kChunk> drvBuf {}, biasBuf {}, compBuf {}, evenBuf {}, oddBuf {}, autoBuf {};
        float autoPower = 0.0625f, attackCoef = 0.001f, releaseCoef = 0.0001f;
        std::vector<float> dcX1, dcY1;
        std::vector<Biquad> tone;
    };
}
