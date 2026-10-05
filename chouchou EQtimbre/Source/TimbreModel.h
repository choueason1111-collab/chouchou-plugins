#pragma once

#include <JuceHeader.h>
#include "EngineVoice.h"
#include <array>
#include <cmath>
#include <complex>

namespace chouchou
{
    // Closed-form predictions of what one voice does, drawn on top of the measured spectrum.
    // They use the same shaper and comb formulas as the DSP, so picture and sound agree.
    namespace TimbreModel
    {
        constexpr int kHarmonics = 10;
        constexpr int kCyclePoints = 256;      // one cycle; enough to keep aliasing off H1..H10
        constexpr int kSpectrumHarmonics = 40; // used to redraw one cycle of a waveform
        constexpr int kWavePoints = 128;
        constexpr double kPi = juce::MathConstants<double>::pi;

        using Levels   = std::array<float, kHarmonics>;
        // Harmonic k at index k - 1; a cycle is sum of Re (S[k-1] * e^(i k theta)).
        using Spectrum = std::array<std::complex<double>, kSpectrumHarmonics>;
        using Waveform = std::array<float, kWavePoints>;

        inline double toneMagnitude (double hz, double cutoffHz)
        {
            const double r = hz / juce::jmax (1.0, cutoffHz);
            return 1.0 / std::sqrt (1.0 + r * r * r * r);
        }

        inline std::complex<double> cycleHarmonic (const std::array<double, kCyclePoints>& v, int k)
        {
            std::complex<double> sum;
            for (int j = 0; j < kCyclePoints; ++j)
                sum += v[(size_t) j] * std::polar (1.0, -2.0 * kPi * k * j / kCyclePoints);
            return sum * (2.0 / kCyclePoints);
        }

        // Generate engine alone on a sine with the given RMS: input spectrum and output spectrum
        // (Mix, Generate level and Tone included). autoGain is the voice's current Drive auto
        // scale (HarmonicGenerator::getAutoGain).
        inline void generateSpectrum (const EngineVoice::Settings& s, float inputRms, float autoGain, float f0,
                                      Spectrum& input, Spectrum& output)
        {
            const double amp = std::sqrt (2.0) * juce::jmax (1.0e-6f, inputRms);
            const auto& p = s.gen;
            const float a = p.autoDrive ? autoGain : 1.0f;
            const float comp = HarmonicGenerator::driveCompGain (p.drive, p.bias, juce::jlimit (0.0f, 1.0f, p.driveComp));
            const double w = (s.mode != 1 ? 1.0 : 0.0) * s.genLevel * s.mix;

            std::array<double, kCyclePoints> x {}, d {};
            for (int j = 0; j < kCyclePoints; ++j)
            {
                x[(size_t) j] = amp * std::sin (2.0 * kPi * j / kCyclePoints);
                float oddPart, evenPart;
                HarmonicGenerator::decompose ((float) x[(size_t) j] * a, p.drive, p.bias, oddPart, evenPart);
                oddPart *= comp / a;
                evenPart *= comp / a;
                d[(size_t) j] = p.odd * (oddPart - x[(size_t) j]) + p.even * evenPart;
            }

            for (int k = 1; k <= kSpectrumHarmonics; ++k)
            {
                const auto X = cycleHarmonic (x, k);
                const double t = toneMagnitude ((double) f0 * k, p.toneHz);
                input[(size_t) k - 1] = X;
                output[(size_t) k - 1] = X + w * t * cycleHarmonic (d, k);
            }
        }

        // Output harmonic amplitudes H1..H10 for a sine through Generate, times outputGain.
        inline Levels generateOutput (const EngineVoice::Settings& s, float inputRms, float autoGain, float f0, float outputGain)
        {
            Spectrum in {}, out {};
            generateSpectrum (s, inputRms, autoGain, f0, in, out);
            Levels levels {};
            for (int k = 1; k <= kHarmonics; ++k)
                levels[(size_t) k - 1] = (float) (std::abs (out[(size_t) k - 1]) * outputGain);
            return levels;
        }

        // Unity-peak band-pass used by fundamental protect, at frequency ratio r = f / f0.
        inline std::complex<double> protectBandpass (double r)
        {
            const std::complex<double> s (0.0, r);
            const double q = HarmonicRebalancer::kProtectQ;
            return (s / q) / (s * s + s / q + 1.0);
        }

        // Complex gain of the Rebalance engine at hz (Mix and level included, Generate off).
        inline std::complex<double> rebalanceResponse (const EngineVoice::Settings& s, double hz, double f0)
        {
            const double g = juce::jlimit (-1.0f, 1.0f, s.balance);
            const double w = (s.mode != 0 ? 1.0 : 0.0) * s.rebLevel * s.mix;
            const double r = hz / juce::jmax (1.0, f0);
            const auto comb = (1.0 + g * std::polar (1.0, -kPi * r)) / (1.0 + std::abs (g));
            auto delta = comb - 1.0;
            if (s.protect)
            {
                const auto bp = protectBandpass (r);
                delta *= (1.0 - bp * bp);
            }
            return 1.0 + w * delta;
        }

        // Rebalance gain in dB at each harmonic k * f0.
        inline Levels rebalanceHarmonicDb (const EngineVoice::Settings& s)
        {
            Levels out {};
            for (int k = 1; k <= kHarmonics; ++k)
                out[(size_t) k - 1] = (float) (20.0 * std::log10 (std::abs (rebalanceResponse (s, (double) k, 1.0)) + 1.0e-9));
            return out;
        }

        // Rebalance engine alone on a saw made of the first 10 harmonics.
        inline void rebalanceSpectrum (const EngineVoice::Settings& s, Spectrum& input, Spectrum& output)
        {
            for (int k = 1; k <= kSpectrumHarmonics; ++k)
            {
                const auto x = k <= kHarmonics ? std::complex<double> (0.0, -1.0 / k) : std::complex<double>();
                input[(size_t) k - 1] = x;
                output[(size_t) k - 1] = x * rebalanceResponse (s, (double) k, 1.0);
            }
        }

        inline Waveform waveformOf (const Spectrum& spec)
        {
            Waveform w {};
            for (int j = 0; j < kWavePoints; ++j)
            {
                double y = 0.0;
                for (int k = 1; k <= kSpectrumHarmonics; ++k)
                    y += std::real (spec[(size_t) k - 1] * std::polar (1.0, 2.0 * kPi * k * j / kWavePoints));
                w[(size_t) j] = (float) y;
            }
            return w;
        }

        inline double harmonicDb (const Spectrum& spec, int k)
        {
            return 20.0 * std::log10 (std::abs (spec[(size_t) k - 1]) + 1.0e-10);
        }

        // Shaper transfer at x: full curve, odd part (x Odd) and DC-free even part (x Even), after Comp.
        inline void shaperCurve (const EngineVoice::Settings& s, float x, float& full, float& odd, float& even)
        {
            const auto& p = s.gen;
            const float comp = HarmonicGenerator::driveCompGain (p.drive, p.bias, juce::jlimit (0.0f, 1.0f, p.driveComp));
            float o, e;
            HarmonicGenerator::decompose (x, p.drive, p.bias, o, e);
            full = (o + e) * comp;
            odd = o * comp * p.odd;
            even = e * comp * p.even;
        }

        //==============================================================================
        // What one knob does: the sound with the knob at its current value against the same
        // settings with only that knob at its neutral value.

        // Knobs outside the engine parameters.
        enum ExtraFocus { focusInput = kNumEngineParams, focusOutput, focusMix, kNumFocus };

        enum class FocusKind { generate, tone, dcCut, rebalance, pitch, confidence, glide, level, mode };

        inline FocusKind focusKind (int focus)
        {
            switch (focus)
            {
                case epDrive: case epDriveComp: case epAutoDrive: case epBias: case epEven: case epOdd:
                case epGenLevel: case focusInput: case focusMix:
                    return FocusKind::generate;
                case epTone:        return FocusKind::tone;
                case epDcCut:       return FocusKind::dcCut;
                case epBalance: case epRebLevel: case epProtect:
                    return FocusKind::rebalance;
                case epPitchSource: case epManualHz: case epMidiPriority: case epMidiHold:
                    return FocusKind::pitch;
                case epConfidence:  return FocusKind::confidence;
                case epGlide: case epJumpFade:
                    return FocusKind::glide;
                case epMode:        return FocusKind::mode;
                default:            return FocusKind::level;
            }
        }

        // Plain value that switches the knob's own contribution off.
        inline float neutralValue (int param, float current)
        {
            switch (param)
            {
                case epDrive: case epDriveComp: case epAutoDrive: case epBias: case epEven: case epOdd:
                case epGenLevel: case epBalance: case epRebLevel: case epProtect:
                    return 0.0f;
                case epTone:
                    return 20000.0f;
                default:
                    return current;
            }
        }

        struct KnobEffect
        {
            bool sawInput = false;            // Rebalance knobs work on existing harmonics
            Spectrum input {}, neutral {}, current {};
        };

        // values are the main stage's plain values; mix 0..1; inputGain is the Entrée gain (linear).
        inline KnobEffect knobEffect (const EngineValues& values, float mix, int focus, float inputRms,
                                      float autoGain, float f0, float inputGain)
        {
            KnobEffect e;
            auto neutralValues = values;
            float neutralMix = mix, neutralRms = inputRms;
            const auto kind = focusKind (focus);

            if (focus < kNumEngineParams)
                neutralValues[(size_t) focus] = neutralValue (focus, values[(size_t) focus]);
            else if (focus == focusMix)
                neutralMix = 0.0f;
            else if (focus == focusInput)
                neutralRms = inputRms / juce::jmax (1.0e-6f, inputGain);

            const auto now = EngineVoice::Settings::fromValues (values, mix);
            const auto off = EngineVoice::Settings::fromValues (neutralValues, neutralMix);

            if (kind == FocusKind::rebalance)
            {
                e.sawInput = true;
                rebalanceSpectrum (now, e.input, e.current);
                rebalanceSpectrum (off, e.input, e.neutral);
            }
            else
            {
                Spectrum ignored {};
                generateSpectrum (now, inputRms, autoGain, f0, e.input, e.current);
                generateSpectrum (off, neutralRms, autoGain, f0, ignored, e.neutral);
            }
            return e;
        }
    }
}
