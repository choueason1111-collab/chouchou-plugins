/*
  ==============================================================================

    ChouChou Compressor — three serial stages in one plugin

      Stage 1  BOOST   — upward expand quiet bins (own thresh / ratio / ballistics)
      Stage 2  COMP    — downward compress loud bins after boost (own params)
      Stage 3  MIX     — latency-aligned dry/wet + Makeup on wet only

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

//==============================================================================
class NewProjectAudioProcessor  : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::AsyncUpdater
{
public:
    //==============================================================================
    NewProjectAudioProcessor();
    ~NewProjectAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void numChannelsChanged() override;
    void processorLayoutsChanged() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    int getFftSize() const noexcept { return fftSize; }
    int getHopSize() const noexcept { return hopSize; }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }
    float getHopMilliseconds() const noexcept;

    /** Samples reported to the host via setLatencySamples (= FFT size / dry-align delay). */
    int getReportedLatencySamples() const noexcept { return getLatencySamples(); }

    /** Same latency in milliseconds at the current sample rate. */
    float getReportedLatencyMilliseconds() const noexcept
    {
        const double sr = juce::jmax (1.0, currentSampleRate);
        return (float) (1000.0 * (double) getLatencySamples() / sr);
    }

    /** Mix-stage dry delay length (samples) — matches reported latency for phase align. */
    int getDryAlignSamples() const noexcept { return fftSize; }

    float getMeterBoostDb() const noexcept { return meterBoostDb.load (std::memory_order_relaxed); }
    float getMeterGrDb() const noexcept { return meterGrDb.load (std::memory_order_relaxed); }
    float getMeterInputDb() const noexcept { return meterInputDb.load (std::memory_order_relaxed); }
    float getMeterScDb() const noexcept { return meterScDb.load (std::memory_order_relaxed); }
    bool isSidechainConnected() const noexcept { return sidechainConnected.load (std::memory_order_relaxed); }

    /** Bumps every processBlock; editor uses this to detect host pause (no callbacks). */
    uint32_t getProcessCounter() const noexcept { return processCounter.load (std::memory_order_relaxed); }

    /** Apply FFT size off the audio thread (holds callback lock). */
    void applyFftSizeFromParameterLocked (bool forceRebuild);

private:
    //==============================================================================
    static constexpr int kMinFftOrder = 9;
    static constexpr int kMaxFftOrder = 12;
    static constexpr int kDefaultFftOrder = 11;

    struct ChannelState
    {
        std::vector<float> inputFifo;
        std::vector<float> ola;
        std::vector<float> outFifo;
        std::vector<float> fftData;
        std::vector<float> window;
        std::vector<float> boostGain;      // Stage 1 smoothed gain (>= 1)
        std::vector<float> grGain;         // Stage 2 smoothed gain (<= 1)
        std::vector<int>   boostDelayHops; // Attack wait before Boost engages (-1 = idle)
        std::vector<int>   grDelayHops;    // Attack wait before Comp engages (-1 = idle)
        std::vector<float> dryDelay;       // Stage 3 latency-aligned dry
        std::vector<float> scInputFifo;    // Sidechain samples, same writePos as inputFifo
        std::vector<float> scFftData;
        int writePos = 0;
        int outRead = 0;
        int outAvailable = 0;
        int dryWrite = 0;
    };

    struct HopParams
    {
        float boostThreshLin = 0.0f;
        float boostRatio = 1.0f;
        int   boostAttackDelayHops = 0;  // Attack: wait this many hops before moving
        float boostSpeedCoeff = 1.0f;    // Speed: how fast to reach target once moving
        float boostRelCoeff = 1.0f;

        float compThreshLin = 0.0f;
        float compRatio = 1.0f;
        int   compAttackDelayHops = 0;
        float compSpeedCoeff = 1.0f;
        float compRelCoeff = 1.0f;

        float makeupLin = 1.0f;

        bool useBoostSc = false;  // Already false when no sidechain is connected
        bool useCompSc = false;
    };

    void applyFftSizeFromParameter (bool forceRebuild);
    void configureTransform (int order);
    void resetChannel (ChannelState& ch);

    /** STFT hop: Stage 1 Boost → Stage 2 Comp (+ Makeup). Mix is Stage 3 in processBlock. */
    void processHop (ChannelState& ch,
                     const HopParams& hp,
                     float& hopBoostDb,
                     float& hopGrDb);

    void resetDynamicsToUnity() noexcept;

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    static float timeMsToCoeff (float timeMs, float hopSeconds) noexcept;
    static float speedToEngageCoeff (float speed0to100, float hopSeconds) noexcept;
    static int attackMsToDelayHops (float attackMs, float hopSeconds) noexcept;
    static int fftSizeChoiceToOrder (int choiceIndex) noexcept;
    static float boostTargetGain (float magLin, float lowLin, float upRatio) noexcept;
    static float grTargetGain (float magLin, float highLin, float downRatio) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<ChannelState> channels;

    int fftOrder = kDefaultFftOrder;
    int fftSize  = 1 << kDefaultFftOrder;
    int hopSize  = (1 << kDefaultFftOrder) / 4;
    double currentSampleRate = 44100.0;
    int preparedBlockSize = 512;

    std::atomic<float> meterBoostDb { 0.0f };
    std::atomic<float> meterGrDb { 0.0f };
    std::atomic<float> meterInputDb { -120.0f };
    std::atomic<float> meterScDb { -120.0f };
    std::atomic<bool> sidechainConnected { false };
    std::atomic<uint32_t> processCounter { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessor)
};
