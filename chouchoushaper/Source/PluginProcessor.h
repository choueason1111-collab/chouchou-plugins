/*
  ==============================================================================

    chouchouShaper — dual envelope compressor

      Stage 1  TIME      broadband envelope (zero latency), own Dry/Wet
      Stage 2  SPECTRAL  per-frequency envelope on an STFT (latency = FFT size),
                         own Dry/Wet with latency-aligned dry
      Output gain

    Both stages run contour compression (ratio) and transient shaping
    (attack / sustain) together. Each stage can detect from the sidechain bus.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "TimeEnvelopeStage.h"
#include "SpectralEnvelopeStage.h"
#include "ScopeData.h"
#include <atomic>
#include <cstdint>

//==============================================================================
/** RBJ biquad used for the sidechain HPF / LPF. */
struct ScBiquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }

    void setup (bool highPass, double hz, double sr) noexcept
    {
        const double w0 = juce::MathConstants<double>::twoPi * hz / sr;
        const double cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.70710678);
        const double a0 = 1.0 + alpha;

        if (highPass)
        {
            b0 = (float) (((1.0 + cosw) * 0.5) / a0);
            b1 = (float) (-(1.0 + cosw) / a0);
        }
        else
        {
            b0 = (float) (((1.0 - cosw) * 0.5) / a0);
            b1 = (float) ((1.0 - cosw) / a0);
        }
        b2 = b0;
        a1 = (float) ((-2.0 * cosw) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    float process (int ch, float x) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
};

//==============================================================================
class ChouchouShaperAudioProcessor  : public juce::AudioProcessor,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::AsyncUpdater
{
public:
    ChouchouShaperAudioProcessor();
    ~ChouchouShaperAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    void numChannelsChanged() override;
    void processorLayoutsChanged() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "chouchouShaper"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    const SpectralEnvelopeStage& getSpectralStage() const noexcept { return spectral; }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }
    int getFftSize() const noexcept { return spectral.getFftSize(); }

    float getMeterTimeMinDb() const noexcept { return meterTimeMinDb.load (std::memory_order_relaxed); }
    float getMeterTimeMaxDb() const noexcept { return meterTimeMaxDb.load (std::memory_order_relaxed); }
    float getMeterInputDb() const noexcept   { return meterInputDb.load (std::memory_order_relaxed); }
    float getMeterScDb() const noexcept      { return meterScDb.load (std::memory_order_relaxed); }
    bool isSidechainConnected() const noexcept { return sidechainConnected.load (std::memory_order_relaxed); }
    uint32_t getProcessCounter() const noexcept { return processCounter.load (std::memory_order_relaxed); }

    /** Applies the FFT size parameter off the audio thread (holds the callback lock). */
    void applyFftSizeFromParameterLocked (bool force);

    ScopeFifo& getScopeFifo() noexcept { return scopeFifo; }

    /** Output lags the input by this many scope columns. */
    int getScopeLatencyColumns() const noexcept { return spectral.getFftSize() / kScopeColumnSamples; }

    bool isScopeOpen() const { return (bool) apvts.state.getProperty ("scopeOpen", false); }
    void setScopeOpen (bool open) { apvts.state.setProperty ("scopeOpen", open, nullptr); }

private:
    static int fftChoiceToOrder (int choice) noexcept { return 9 + juce::jlimit (0, 3, choice); }

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void updateScFilters (float hpfHz, float lpfHz);
    void ensureScratch (int numSamples);
    void pushScopeColumns (const float* out, const float* sc, int numSamples, bool timeOn, bool scActive);

    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    /** dB parameters snapped to their 0.1 dB grid, so 0 dB is exactly 0 (true unity). */
    float paramDb (const char* id) const noexcept { return std::round (param (id) * 10.0f) / 10.0f; }

    juce::AudioProcessorValueTreeState apvts;

    TimeEnvelopeStage timeStage;
    SpectralEnvelopeStage spectral;

    juce::AudioBuffer<float> scWork;
    ScBiquad scHpf, scLpf;
    float lastHpfHz = -1.0f, lastLpfHz = -1.0f;
    juce::SmoothedValue<float> outGain { 1.0f };

    // Scope capture (sized in prepareToPlay).
    ScopeFifo scopeFifo;
    std::vector<float> scopeIn, traceRatio, traceAttack, traceSustain, traceApplied;
    ScopeColumn scopeAcc;
    double accRatio = 0, accAttack = 0, accSustain = 0, accTotal = 0;
    int accCount = 0;

    double currentSampleRate = 44100.0;
    int preparedBlockSize = 512;

    std::atomic<float> meterTimeMinDb { 0.0f }, meterTimeMaxDb { 0.0f };
    std::atomic<float> meterInputDb { -120.0f }, meterScDb { -120.0f };
    std::atomic<bool> sidechainConnected { false };
    std::atomic<uint32_t> processCounter { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouShaperAudioProcessor)
};
