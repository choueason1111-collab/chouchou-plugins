#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace chouchou
{
    // Single-producer / single-consumer sample FIFO from the audio thread to the GUI.
    class AnalyzerFifo
    {
    public:
        static constexpr int kSize = 1 << 15;

        void push (const float* data, int numSamples) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
            std::copy (data, data + size1, buffer.begin() + start1);
            std::copy (data + size1, data + size1 + size2, buffer.begin() + start2);
            fifo.finishedWrite (size1 + size2);
        }

        int pull (float* dest, int maxSamples) noexcept
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (maxSamples, start1, size1, start2, size2);
            std::copy (buffer.begin() + start1, buffer.begin() + start1 + size1, dest);
            std::copy (buffer.begin() + start2, buffer.begin() + start2 + size2, dest + size1);
            fifo.finishedRead (size1 + size2);
            return size1 + size2;
        }

    private:
        juce::AbstractFifo fifo { kSize };
        std::array<float, kSize> buffer {};
    };

    // GUI-side FFT: smoothed spectrum plus H1..H10 levels at multiples of f0.
    class HarmonicAnalyzer
    {
    public:
        static constexpr int kOrder     = 13;
        static constexpr int kSize      = 1 << kOrder;
        static constexpr int kHarmonics = 10;
        static constexpr float kFloorDb = -120.0f;

        HarmonicAnalyzer()
            : fft (kOrder),
              window ((size_t) kSize, juce::dsp::WindowingFunction<float>::blackmanHarris, false)
        {
            history.assign ((size_t) kSize, 0.0f);
            fftData.assign ((size_t) kSize * 2, 0.0f);
            spectrumDb.assign ((size_t) kSize / 2, kFloorDb);
            harmonicDb.fill (kFloorDb);

            std::vector<float> ones ((size_t) kSize, 1.0f);
            window.multiplyWithWindowingTable (ones.data(), (size_t) kSize);
            float sum = 0.0f;
            for (auto w : ones)
                sum += w;
            amplitudeScale = 2.0f / sum;
        }

        void pushSamples (const float* data, int numSamples)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                history[(size_t) historyPos] = data[i];
                historyPos = (historyPos + 1) & (kSize - 1);
            }
        }

        void compute (double sampleRate, float f0, bool f0Valid)
        {
            for (int i = 0; i < kSize; ++i)
                fftData[(size_t) i] = history[(size_t) ((historyPos + i) & (kSize - 1))];
            std::fill (fftData.begin() + kSize, fftData.end(), 0.0f);

            window.multiplyWithWindowingTable (fftData.data(), (size_t) kSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

            for (int b = 0; b < kSize / 2; ++b)
            {
                const float db = juce::Decibels::gainToDecibels (fftData[(size_t) b] * amplitudeScale, kFloorDb);
                auto& s = spectrumDb[(size_t) b];
                s = db > s ? db : s + 0.25f * (db - s);
            }

            const double binHz = sampleRate / kSize;
            for (int k = 1; k <= kHarmonics; ++k)
            {
                float target = kFloorDb;
                const double fk = (double) k * f0;

                if (f0Valid && f0 > 0.0f && fk < sampleRate * 0.5)
                {
                    const double spread = juce::jmax (2.0 * binHz, fk * 0.03);
                    const int lo = juce::jmax (1, (int) std::floor ((fk - spread) / binHz));
                    const int hi = juce::jmin (kSize / 2 - 1, (int) std::ceil ((fk + spread) / binHz));
                    float peak = 0.0f;
                    for (int b = lo; b <= hi; ++b)
                        peak = juce::jmax (peak, fftData[(size_t) b]);
                    target = juce::Decibels::gainToDecibels (peak * amplitudeScale, kFloorDb);
                }

                auto& h = harmonicDb[(size_t) (k - 1)];
                h += (target > h ? 0.5f : 0.15f) * (target - h);
            }
        }

        const std::vector<float>& getSpectrumDb() const noexcept { return spectrumDb; }
        float getHarmonicDb (int harmonic) const noexcept      { return harmonicDb[(size_t) (harmonic - 1)]; }

    private:
        juce::dsp::FFT fft;
        juce::dsp::WindowingFunction<float> window;
        std::vector<float> history, fftData, spectrumDb;
        std::array<float, kHarmonics> harmonicDb {};
        int historyPos = 0;
        float amplitudeScale = 1.0f;
    };
}
