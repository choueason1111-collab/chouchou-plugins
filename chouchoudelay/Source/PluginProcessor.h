#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>

class ChouchouDelayAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int    kMaxEchoes          = 32;
    static constexpr double kMaxIntervalSeconds = 10.0;
    static constexpr double kMinIntervalSeconds = 0.000005;
    static constexpr double kMaxOffsetPercent   = 50.0;
    static constexpr double kPitchWindowSeconds = 0.05;
    static constexpr double kMarginSeconds      = 0.05;
    static constexpr double kMinDecaySeconds    = 0.00001;
    static constexpr double kMaxDecaySeconds    = 60.0;
    static constexpr double kDecayTargetDb      = -80.0;         // reached at the decay time
    static constexpr float  kSilenceGain        = 1.0e-5f;       // -100 dB
    static constexpr float  kLevelFloorDb       = -60.0f;        // treated as -inf
    static constexpr double kGainRampSeconds    = 0.02;
    static constexpr double kDelayRampSeconds   = 0.05;

    ChouchouDelayAudioProcessor();
    ~ChouchouDelayAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
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

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    static juce::String idOn     (int echo) { return "on"     + juce::String (echo); }
    static juce::String idPitch  (int echo) { return "pitch"  + juce::String (echo); }
    static juce::String idFine   (int echo) { return "fine"   + juce::String (echo); }
    static juce::String idLevel  (int echo) { return "level"  + juce::String (echo); }
    static juce::String idOffset (int echo) { return "offset" + juce::String (echo); }

    // Echo model, computed from current parameter values. Echo indices are 1-based.
    double getSampleRateSafe() const { return currentSampleRate.load(); }
    int    getEchoCount() const;
    double getIntervalSeconds() const;
    double getIntervalSamplesExact() const;
    bool   isEchoOn (int echo) const;
    double getEchoOffsetPercent (int echo) const;
    double getEchoOffsetSeconds (int echo) const;
    double getEchoPitchSemitones (int echo) const;
    double getEchoDelaySamples (int echo) const;
    double getEchoTimeSeconds (int echo) const;
    float  getEchoDecayGain (int echo) const;
    float  getEchoLevelGain (int echo) const;
    float  getEchoTotalGain (int echo) const;

    // Delay buffer (sized from the echo count).
    static int computeBufferLength (int echoCapacity, double sampleRate);
    int          getBufferCapacityEchoes() const { return activeCapacity.load(); }
    int          getBufferLengthSamples() const  { return activeLength.load(); }
    int          getBufferChannels() const       { return activeChannels.load(); }
    juce::int64  getBufferBytes() const;
    bool         isBufferResizePending() const;

private:
    struct DelayStore
    {
        std::array<juce::HeapBlock<float>, 2> ch;
        int numChannels = 0;
        int length = 0;
        int capacityEchoes = 0;
        juce::int64 written = 0;
        juce::int64 syncedTo = 0;
    };

    struct TapState
    {
        double delay = 1.0, delayTarget = 1.0, delayStep = 0.0;
        int    delayRamp = 0;
        float  gain = 0.0f, gainTarget = 0.0f, gainStep = 0.0f;
        int    gainRamp = 0;
        double ratio = 1.0;
        double window = 32.0;
        double phaseInc = 0.0;
        double phase = 0.0;
    };

    class ResizeWorker : public juce::Thread
    {
    public:
        explicit ResizeWorker (ChouchouDelayAudioProcessor& o) : juce::Thread ("chouchouDelay buffer"), owner (o) {}
        void run() override;
    private:
        ChouchouDelayAudioProcessor& owner;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (ChouchouDelayAudioProcessor& self);
    static std::unique_ptr<DelayStore> makeStore (int echoCapacity, double sampleRate, int numChannels);

    void workerTick();
    void adoptPendingStore();
    void updateTapTargets (const DelayStore& store, bool snap);
    void freeAllStores();

    std::atomic<double> currentSampleRate { 48000.0 };

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* pEchoCount = nullptr;
    std::atomic<float>* pInterval  = nullptr;
    std::atomic<float>* pDecay     = nullptr;
    std::atomic<float>* pMix       = nullptr;
    std::array<std::atomic<float>*, kMaxEchoes> pOn {}, pPitch {}, pFine {}, pLevel {}, pOffset {};

    std::array<TapState, kMaxEchoes> taps {};
    juce::SmoothedValue<float> mixSmoothed;

    DelayStore* active = nullptr;                      // owned; audio thread (and prepare)
    std::atomic<DelayStore*> activePtr { nullptr };    // read-only view for the worker
    std::atomic<DelayStore*> pending   { nullptr };    // worker -> audio thread
    std::atomic<DelayStore*> retired   { nullptr };    // audio thread -> worker (to free)
    std::atomic<juce::int64> publishedWritten { 0 };

    std::atomic<int> activeCapacity { 0 };
    std::atomic<int> activeLength   { 0 };
    std::atomic<int> activeChannels { 0 };

    ResizeWorker worker { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouDelayAudioProcessor)
};
