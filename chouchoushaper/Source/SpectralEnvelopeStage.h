/*
  ==============================================================================

    chouchouShaper — spectral stage: per-bin envelope on an STFT

      Hann analysis + Hann synthesis, hop = N/4, overlap-add (same framework as
      chouchouCompressor). Each bin keeps its own fast / slow follower on the
      magnitude of the main or sidechain spectrum; the shaped gain is applied to
      the complex bin. Latency = N; the dry path is delayed by N so Dry/Wet is
      phase aligned.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "EnvelopeCore.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

struct SpectralStageParams
{
    chouchou::env::ShapeParams shape;
    float fastMs     = 20.0f;
    float slowMs     = 200.0f;
    float freqSmooth = 0.3f;    // 0..1
    float loHz       = 20.0f;
    float hiHz       = 20000.0f;
    float mix        = 1.0f;
    bool  useSidechain = false;
};

class SpectralEnvelopeStage
{
public:
    static constexpr int kMinOrder = 9;
    static constexpr int kMaxOrder = 12;
    static constexpr int kMaxBins  = (1 << kMaxOrder) / 2 + 1;

    /** Energy-weighted average over all bins of channel 0, already scaled by Dry/Wet. */
    struct HopSummary
    {
        float ratioDb = 0.0f, attackDb = 0.0f, sustainDb = 0.0f, totalDb = 0.0f;
    };

    SpectralEnvelopeStage()
    {
        clearMeters();
    }

    void prepare (double sampleRate, int numChannels, int order)
    {
        sr = sampleRate;
        channels.resize ((size_t) std::max (1, numChannels));
        configure (order);
    }

    /** Rebuilds the transform; caller must hold the audio callback lock. */
    void configure (int order)
    {
        order   = juce::jlimit (kMinOrder, kMaxOrder, order);
        fftOrder = order;
        fftSize = 1 << order;
        hopSize = fftSize / 4;
        fft = std::make_unique<juce::dsp::FFT> (order);

        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) fftSize));

        scratchDb.assign ((size_t) numBins(), 0.0f);
        scratchRatio.assign ((size_t) numBins(), 0.0f);
        scratchAttack.assign ((size_t) numBins(), 0.0f);
        scratchSustain.assign ((size_t) numBins(), 0.0f);
        reset();
    }

    void reset()
    {
        const int nb = numBins();
        for (auto& ch : channels)
        {
            ch.inputFifo.assign ((size_t) fftSize, 0.0f);
            ch.scFifo.assign ((size_t) fftSize, 0.0f);
            ch.ola.assign ((size_t) fftSize, 0.0f);
            ch.outFifo.assign ((size_t) hopSize, 0.0f);
            ch.fftData.assign ((size_t) fftSize * 2, 0.0f);
            ch.scFftData.assign ((size_t) fftSize * 2, 0.0f);
            ch.dryDelay.assign ((size_t) fftSize, 0.0f);
            ch.followers.assign ((size_t) nb, {});
            ch.gainDb.assign ((size_t) nb, 0.0f);
            // Start like the steady state so the first output samples get all overlapping frames.
            ch.writePos = fftSize - hopSize;
            ch.outRead = 0;
            ch.outAvailable = 0;
            ch.dryWrite = 0;
        }

        clearMeters();
    }

    int getOrder() const noexcept    { return fftOrder; }
    int getFftSize() const noexcept  { return fftSize; }
    int getHopSize() const noexcept  { return hopSize; }
    int numBins() const noexcept     { return fftSize / 2 + 1; }
    int getNumChannels() const noexcept { return (int) channels.size(); }

    /** Per-bin display data (channel 0): applied gain and slow envelope level. */
    float getMeterGainDb (int bin) const noexcept  { return meterGainDb[(size_t) bin].load (std::memory_order_relaxed); }
    float getMeterLevelDb (int bin) const noexcept { return meterLevelDb[(size_t) bin].load (std::memory_order_relaxed); }

    /** Per-bin scope data (channel 0): main input / output magnitude (dB re full-scale sine)
        and the Mode A / attack / sustain parts of the gain before frequency smoothing. */
    float getMeterInDb (int bin) const noexcept      { return meterInDb[(size_t) bin].load (std::memory_order_relaxed); }
    float getMeterOutDb (int bin) const noexcept     { return meterOutDb[(size_t) bin].load (std::memory_order_relaxed); }
    float getMeterRatioDb (int bin) const noexcept   { return meterRatioDb[(size_t) bin].load (std::memory_order_relaxed); }
    float getMeterAttackDb (int bin) const noexcept  { return meterAttackDb[(size_t) bin].load (std::memory_order_relaxed); }
    float getMeterSustainDb (int bin) const noexcept { return meterSustainDb[(size_t) bin].load (std::memory_order_relaxed); }

    HopSummary getHopSummary() const noexcept
    {
        return { hopRatioDb.load (std::memory_order_relaxed), hopAttackDb.load (std::memory_order_relaxed),
                 hopSustainDb.load (std::memory_order_relaxed), hopTotalDb.load (std::memory_order_relaxed) };
    }

    /** Processes one channel in place. sc may be null. enabled = false gives the delayed dry. */
    void processChannel (int channel, float* data, const float* sc, int numSamples,
                         const SpectralStageParams& p, bool enabled) noexcept
    {
        if (channel >= (int) channels.size() || fft == nullptr)
            return;

        auto& ch = channels[(size_t) channel];
        const float mix = enabled ? p.mix : 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float in = data[i];

            const float delayedDry = ch.dryDelay[(size_t) ch.dryWrite];
            ch.dryDelay[(size_t) ch.dryWrite] = in;
            if (++ch.dryWrite == fftSize)
                ch.dryWrite = 0;

            ch.inputFifo[(size_t) ch.writePos] = in;
            ch.scFifo[(size_t) ch.writePos] = sc != nullptr ? sc[i] : 0.0f;
            ++ch.writePos;

            float wet = 0.0f;
            if (ch.outAvailable > 0)
            {
                wet = ch.outFifo[(size_t) ch.outRead++];
                --ch.outAvailable;
            }

            data[i] = mix == 0.0f ? delayedDry
                                  : (mix == 1.0f ? wet : delayedDry + mix * (wet - delayedDry));

            if (ch.writePos == fftSize)
                processHop (ch, channel == 0, p, sc != nullptr && p.useSidechain, mix);
        }
    }

private:
    struct ChannelState
    {
        std::vector<float> inputFifo, scFifo, ola, outFifo, fftData, scFftData, dryDelay;
        std::vector<chouchou::env::FollowerPair> followers;
        std::vector<float> gainDb;
        int writePos = 0, outRead = 0, outAvailable = 0, dryWrite = 0;
    };

    static constexpr float kColaNorm = 2.0f / 3.0f; // Hann^2 summed at hop N/4 = 1.5
    static constexpr float kMagEps   = 1.0e-5f;     // -100 dB re full-scale sine

    void clearMeters() noexcept
    {
        for (auto* arr : { &meterGainDb, &meterRatioDb, &meterAttackDb, &meterSustainDb })
            for (auto& v : *arr) v.store (0.0f, std::memory_order_relaxed);
        for (auto* arr : { &meterLevelDb, &meterInDb, &meterOutDb })
            for (auto& v : *arr) v.store (-120.0f, std::memory_order_relaxed);
        for (auto* v : { &hopRatioDb, &hopAttackDb, &hopSustainDb, &hopTotalDb })
            v->store (0.0f, std::memory_order_relaxed);
    }

    void processHop (ChannelState& ch, bool publishMeter, const SpectralStageParams& p, bool useSc,
                     float effectiveMix) noexcept
    {
        using namespace chouchou::env;

        const int nb = numBins();
        const float magNorm = 4.0f / (float) fftSize; // full-scale bin-centred sine -> 1

        for (int i = 0; i < fftSize; ++i)
            ch.fftData[(size_t) i] = ch.inputFifo[(size_t) i] * window[(size_t) i];
        std::fill (ch.fftData.begin() + fftSize, ch.fftData.end(), 0.0f);
        fft->performRealOnlyForwardTransform (ch.fftData.data(), true);

        if (useSc)
        {
            for (int i = 0; i < fftSize; ++i)
                ch.scFftData[(size_t) i] = ch.scFifo[(size_t) i] * window[(size_t) i];
            std::fill (ch.scFftData.begin() + fftSize, ch.scFftData.end(), 0.0f);
            fft->performRealOnlyForwardTransform (ch.scFftData.data(), true);
        }

        const float hopSec = (float) hopSize / (float) sr;
        const float cF = msToCoeff (p.fastMs, hopSec);
        const float cS = msToCoeff (p.slowMs, hopSec);
        const float cG = msToCoeff (std::max (0.5f, p.fastMs * 0.25f), hopSec);
        const bool unity = p.shape.isUnity();

        const float binHz = (float) sr / (float) fftSize;
        const int loBin = juce::jlimit (0, nb - 1, (int) std::floor (p.loHz / binHz));
        const int hiBin = juce::jlimit (0, nb - 1, (int) std::ceil (p.hiHz / binHz));

        // Detection + target gain per bin.
        for (int k = 0; k < nb; ++k)
        {
            const float* src = useSc ? ch.scFftData.data() : ch.fftData.data();
            const float re = src[2 * k], im = src[2 * k + 1];
            const float mag = std::sqrt (re * re + im * im) * magNorm;

            auto& f = ch.followers[(size_t) k];
            f.push (mag, cF, cS);

            ShapeBreakdown b;
            if (! unity && k >= loBin && k <= hiBin)
                b = shapeGainBreakdown (amplitudeDiffDb (f.fast, f.slow, kMagEps), p.shape);
            scratchDb[(size_t) k]      = b.totalDb;
            scratchRatio[(size_t) k]   = b.ratioDb;
            scratchAttack[(size_t) k]  = b.attackDb;
            scratchSustain[(size_t) k] = b.sustainDb;
        }

        double wSum = 0.0, wRatio = 0.0, wAttack = 0.0, wSustain = 0.0, wTotal = 0.0;

        // Smoothing across frequency (forward + backward one-pole, in dB).
        if (! unity && p.freqSmooth > 0.0f)
        {
            const float a = juce::jlimit (0.0f, 0.95f, p.freqSmooth * 0.95f);
            for (int k = 1; k < nb; ++k)
                scratchDb[(size_t) k] += a * (scratchDb[(size_t) (k - 1)] - scratchDb[(size_t) k]);
            for (int k = nb - 2; k >= 0; --k)
                scratchDb[(size_t) k] += a * (scratchDb[(size_t) (k + 1)] - scratchDb[(size_t) k]);
        }

        for (int k = 0; k < nb; ++k)
        {
            float& g = ch.gainDb[(size_t) k];
            const float target = scratchDb[(size_t) k];
            g = target < g ? target : g + cG * (target - g);
            if (std::abs (g - target) < 1.0e-4f)
                g = target;

            float& re = ch.fftData[(size_t) (2 * k)];
            float& im = ch.fftData[(size_t) (2 * k + 1)];
            const float inMag = publishMeter ? std::sqrt (re * re + im * im) * magNorm : 0.0f;

            const float lin = dbToGain (g);
            re *= lin;
            im *= lin;

            if (publishMeter)
            {
                const auto kk = (size_t) k;
                meterGainDb[kk].store (g, std::memory_order_relaxed);
                meterLevelDb[kk].store (20.0f * std::log10 (ch.followers[kk].slow + 1.0e-6f), std::memory_order_relaxed);
                meterInDb[kk].store (20.0f * std::log10 (inMag + 1.0e-6f), std::memory_order_relaxed);
                meterOutDb[kk].store (20.0f * std::log10 (inMag * lin + 1.0e-6f), std::memory_order_relaxed);
                meterRatioDb[kk].store (scratchRatio[kk], std::memory_order_relaxed);
                meterAttackDb[kk].store (scratchAttack[kk], std::memory_order_relaxed);
                meterSustainDb[kk].store (scratchSustain[kk], std::memory_order_relaxed);

                const double w = (double) inMag * inMag;
                wSum     += w;
                wRatio   += w * scratchRatio[kk];
                wAttack  += w * scratchAttack[kk];
                wSustain += w * scratchSustain[kk];
                wTotal   += w * g;
            }
        }

        if (publishMeter)
        {
            const double norm = wSum > 1.0e-12 ? (double) effectiveMix / wSum : 0.0;
            hopRatioDb.store ((float) (wRatio * norm), std::memory_order_relaxed);
            hopAttackDb.store ((float) (wAttack * norm), std::memory_order_relaxed);
            hopSustainDb.store ((float) (wSustain * norm), std::memory_order_relaxed);
            hopTotalDb.store ((float) (wTotal * norm), std::memory_order_relaxed);
        }

        fft->performRealOnlyInverseTransform (ch.fftData.data());

        for (int i = 0; i < fftSize; ++i)
            ch.ola[(size_t) i] += ch.fftData[(size_t) i] * window[(size_t) i] * kColaNorm;

        std::copy (ch.ola.begin(), ch.ola.begin() + hopSize, ch.outFifo.begin());
        std::copy (ch.ola.begin() + hopSize, ch.ola.end(), ch.ola.begin());
        std::fill (ch.ola.end() - hopSize, ch.ola.end(), 0.0f);

        std::copy (ch.inputFifo.begin() + hopSize, ch.inputFifo.end(), ch.inputFifo.begin());
        std::copy (ch.scFifo.begin() + hopSize, ch.scFifo.end(), ch.scFifo.begin());

        ch.writePos = fftSize - hopSize;
        ch.outRead = 0;
        ch.outAvailable = hopSize;
    }

    double sr = 44100.0;
    int fftOrder = 11, fftSize = 2048, hopSize = 512;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, scratchDb, scratchRatio, scratchAttack, scratchSustain;
    std::vector<ChannelState> channels;

    using BinMeters = std::array<std::atomic<float>, kMaxBins>;
    BinMeters meterGainDb, meterLevelDb, meterInDb, meterOutDb;
    BinMeters meterRatioDb, meterAttackDb, meterSustainDb;
    std::atomic<float> hopRatioDb { 0.0f }, hopAttackDb { 0.0f }, hopSustainDb { 0.0f }, hopTotalDb { 0.0f };
};
