/*
  Per-echo FX for chouchouDelay: spectral Gate, spectral Comp (boost+GR), Insert host helpers.
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>

//==============================================================================
struct GateParams
{
    float amount = 0.8f;
    float thresholdDb = -40.0f;
    float attackMs = 5.0f;
    float releaseMs = 80.0f;
    float mix = 1.0f;
    float makeupDb = 0.0f;
    bool  autoGain = false;
};

struct CompParams
{
    float boostThreshDb = -40.0f;
    float boostRatio = 2.0f;
    float boostAtkMs = 5.0f;
    float boostRelMs = 80.0f;
    float boostSpeed = 50.0f;

    float compThreshDb = -12.0f;
    float compRatio = 4.0f;
    float compAtkMs = 10.0f;
    float compRelMs = 100.0f;
    float compSpeed = 50.0f;

    float mix = 1.0f;
    float makeupDb = 0.0f;
    bool  autoGain = false;
};

//==============================================================================
class TapGateEngine
{
public:
    void prepare (double sampleRate, int fftOrder, int numChannels);
    void reset();
    void setFftOrder (int order);
    int  getFftSize() const noexcept { return fftSize; }
    int  getLatency() const noexcept { return fftSize; }

    /** In-place process of a stereo (or mono) block. */
    void process (juce::AudioBuffer<float>& buffer, const GateParams& params);

private:
    struct ChannelState
    {
        std::vector<float> inputFifo, ola, outFifo, fftData, window, smoothedMag, dryDelay;
        int writePos = 0, outRead = 0, outAvailable = 0, dryWrite = 0;
    };

    void configureTransform (int order);
    void resetChannel (ChannelState& ch);
    void processHop (ChannelState& ch, float amount, float thresholdLin, float mix,
                     float makeupLin, float attackCoeff, float releaseCoeff);

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<ChannelState> channels;
    int fftOrder = 9, fftSize = 512, hopSize = 128;
    double sampleRate = 48000.0;
    float agCoeff = 0.01f, inPower = 1.0e-8f, wetPower = 1.0e-8f;
};

//==============================================================================
class TapCompEngine
{
public:
    void prepare (double sampleRate, int fftOrder, int numChannels);
    void reset();
    void setFftOrder (int order);
    int  getFftSize() const noexcept { return fftSize; }
    int  getLatency() const noexcept { return fftSize; }

    void process (juce::AudioBuffer<float>& buffer, const CompParams& params);

private:
    struct ChannelState
    {
        std::vector<float> inputFifo, ola, outFifo, fftData, window, boostGain, grGain, dryDelay;
        std::vector<int> boostDelayHops, grDelayHops;
        int writePos = 0, outRead = 0, outAvailable = 0, dryWrite = 0;
    };

    struct HopParams
    {
        float boostThreshLin = 0.0f, boostRatio = 1.0f, boostSpeedCoeff = 1.0f, boostRelCoeff = 1.0f;
        int   boostAttackDelayHops = 0;
        float compThreshLin = 0.0f, compRatio = 1.0f, compSpeedCoeff = 1.0f, compRelCoeff = 1.0f;
        int   compAttackDelayHops = 0;
        float makeupLin = 1.0f;
    };

    void configureTransform (int order);
    void resetChannel (ChannelState& ch);
    void processHop (ChannelState& ch, const HopParams& hp);

    static float timeMsToCoeff (float timeMs, float hopSeconds) noexcept;
    static float speedToEngageCoeff (float speed0to100, float hopSeconds) noexcept;
    static int   attackMsToDelayHops (float attackMs, float hopSeconds) noexcept;
    static float boostTargetGain (float magLin, float lowLin, float upRatio) noexcept;
    static float grTargetGain (float magLin, float highLin, float downRatio) noexcept;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<ChannelState> channels;
    int fftOrder = 9, fftSize = 512, hopSize = 128;
    double sampleRate = 48000.0;
    float agCoeff = 0.01f, inPower = 1.0e-8f, wetPower = 1.0e-8f;
};

//==============================================================================
struct EchoInsertSlot
{
    std::shared_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription description;
    bool bypassed = false;
    uint64_t loadGeneration = 0;
};

void processHostedInsert (juce::AudioPluginInstance& plugin,
                          juce::AudioBuffer<float>& rackBuffer,
                          juce::MidiBuffer& midi,
                          juce::AudioBuffer<float>& scratch);

bool configureInsertBuses (juce::AudioPluginInstance& plugin, int hostChannels);
void prepareInsertInstance (juce::AudioPluginInstance& plugin, double sr, int blockSize);
