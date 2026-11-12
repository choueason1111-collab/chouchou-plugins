#pragma once

#include <JuceHeader.h>
#include "EchoFx.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

class ChouchouDelayAudioProcessor : public juce::AudioProcessor,
                                    private juce::ChangeListener
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
    static constexpr double kDecayTargetDb      = -80.0;
    static constexpr float  kSilenceGain        = 1.0e-5f;
    static constexpr float  kLevelFloorDb       = -60.0f;
    static constexpr double kGainRampSeconds    = 0.02;
    static constexpr double kDelayRampSeconds   = 0.05;
    static constexpr int    kDefaultFxFftOrder  = 9; // 512

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
    static juce::String idPan    (int echo) { return "pan"    + juce::String (echo); }
    static juce::String idRhythm (int echo) { return "rhythm" + juce::String (echo); }
    static juce::String idRhythmCount (int echo) { return "rhythmN" + juce::String (echo); }
    static juce::String idTuplet (int echo) { return "tuplet" + juce::String (echo); }
    static juce::String idGateOn (int echo) { return "gateOn" + juce::String (echo); }
    static juce::String idCompOn (int echo) { return "compOn" + juce::String (echo); }
    static juce::String idInsBypass (int echo) { return "insBypass" + juce::String (echo); }

    static constexpr int kRhythmCount = 6;
    static constexpr int kTupletCount = 5;
    static juce::String rhythmName (int index);
    static double rhythmBeats (int index);
    static juce::String tupletName (int index);
    static double tupletFactor (int index);

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
    float  getEchoPan (int echo) const;
    int    getEchoRhythm (int echo) const;
    int    getEchoRhythmCount (int echo) const;
    int    getEchoTuplet (int echo) const;

    int    getTempoMode() const;
    float  getCurrentBpm() const;
    float  getDetectedBpm() const noexcept { return detectedBpm.load(); }
    float  getTapBpm() const noexcept { return tapBpm.load(); }
    void   tapTempo() noexcept;

    static int computeBufferLength (int echoCapacity, double sampleRate);
    int          getBufferCapacityEchoes() const { return activeCapacity.load(); }
    int          getBufferLengthSamples() const  { return activeLength.load(); }
    int          getBufferChannels() const       { return activeChannels.load(); }
    juce::int64  getBufferBytes() const;
    bool         isBufferResizePending() const;

    // Per-echo FX
    bool isEchoGateOn (int echo) const;
    bool isEchoCompOn (int echo) const;
    bool isEchoInsertBypassed (int echo) const;
    bool isEchoInsertLoaded (int echo) const;
    juce::String getEchoInsertName (int echo) const;

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList& getPluginList() noexcept { return pluginList; }

    void loadEchoInsertAsync (int echo, const juce::PluginDescription& desc);
    bool loadEchoInsertBlocking (int echo, const juce::PluginDescription& desc, juce::String& error);
    void setEchoInsertForTest (int echo, std::shared_ptr<juce::AudioPluginInstance> instance);
    void clearEchoInsert (int echo);
    void setEchoInsertBypassed (int echo, bool shouldBypass);
    std::unique_ptr<juce::AudioProcessorEditor> createEchoInsertEditor (int echo);
    std::shared_ptr<juce::AudioPluginInstance> getEchoInsertShared (int echo) const;

    int getFxLatencySamples() const noexcept { return reportedFxLatency.load(); }
    void setUiStatus (const juce::String& text);
    juce::String getUiStatus() const;

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
        float  panL = 1.0f, panR = 1.0f;
    };

    struct EchoFxChain
    {
        TapGateEngine gate;
        TapCompEngine comp;
        EchoInsertSlot insert;
        juce::AudioBuffer<float> buf;
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
    static int fftSizeChoiceToOrder (int choiceIndex) noexcept;
    static bool isSelfDescription (const juce::PluginDescription& pd);

    void workerTick();
    void adoptPendingStore();
    void updateTapTargets (const DelayStore& store, bool snap);
    void freeAllStores();
    void analyseTempo (const float* left, const float* right, int numSamples, int numChannels);
    void estimateBpm();
    void prepareEchoFx (double sr, int blockSize, int numChannels);
    void applyFxFftSizes();
    void updateReportedLatency();
    void processEchoFx (int tapIndex, int numSamples, int numChannels, juce::MidiBuffer& midi);
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    uint64_t beginInsertLoad (int echo);
    bool isInsertLoadCurrent (int echo, uint64_t requestId) const;
    void releaseInsert (EchoInsertSlot& slot);

    std::atomic<double> currentSampleRate { 48000.0 };

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* pEchoCount = nullptr;
    std::atomic<float>* pInterval  = nullptr;
    std::atomic<float>* pDecay     = nullptr;
    std::atomic<float>* pMix       = nullptr;
    std::atomic<float>* pTempoMode = nullptr;
    std::array<std::atomic<float>*, kMaxEchoes> pOn {}, pPitch {}, pFine {}, pLevel {}, pOffset {}, pPan {}, pRhythm {}, pRhythmCount {}, pTuplet {};
    std::array<std::atomic<float>*, kMaxEchoes> pGateOn {}, pCompOn {}, pInsBypass {};

    std::atomic<float>* pGateAmt = nullptr;
    std::atomic<float>* pGateThresh = nullptr;
    std::atomic<float>* pGateAtk = nullptr;
    std::atomic<float>* pGateRel = nullptr;
    std::atomic<float>* pGateMix = nullptr;
    std::atomic<float>* pGateMakeup = nullptr;
    std::atomic<float>* pGateAutoGain = nullptr;
    std::atomic<float>* pGateFft = nullptr;

    std::atomic<float>* pCompLowThresh = nullptr;
    std::atomic<float>* pCompUpRatio = nullptr;
    std::atomic<float>* pCompBoostAtk = nullptr;
    std::atomic<float>* pCompBoostRel = nullptr;
    std::atomic<float>* pCompBoostSpeed = nullptr;
    std::atomic<float>* pCompHighThresh = nullptr;
    std::atomic<float>* pCompDownRatio = nullptr;
    std::atomic<float>* pCompGrAtk = nullptr;
    std::atomic<float>* pCompGrRel = nullptr;
    std::atomic<float>* pCompSpeed = nullptr;
    std::atomic<float>* pCompMix = nullptr;
    std::atomic<float>* pCompMakeup = nullptr;
    std::atomic<float>* pCompAutoGain = nullptr;
    std::atomic<float>* pCompFft = nullptr;

    std::vector<float> envRing, envWork;
    int envPos = 0, envCount = 0, hopFill = 0, envSince = 0;
    float hopSum = 0.0f;
    std::atomic<float> detectedBpm { 120.0f };
    std::atomic<float> tapBpm { 120.0f };
    std::atomic<juce::int64> lastTapMs { 0 };
    std::array<double, 4> tapHist {};
    int tapHistPos = 0;
    int tapIntervals = 0;

    std::array<TapState, kMaxEchoes> taps {};
    juce::SmoothedValue<float> mixSmoothed;

    DelayStore* active = nullptr;
    std::atomic<DelayStore*> activePtr { nullptr };
    std::atomic<DelayStore*> pending   { nullptr };
    std::atomic<DelayStore*> retired   { nullptr };
    std::atomic<juce::int64> publishedWritten { 0 };

    std::atomic<int> activeCapacity { 0 };
    std::atomic<int> activeLength   { 0 };
    std::atomic<int> activeChannels { 0 };

    ResizeWorker worker { *this };

    // FX
    std::array<EchoFxChain, kMaxEchoes> echoFx {};
    juce::AudioBuffer<float> wetSumBuf;
    juce::AudioBuffer<float> insertScratch;
    juce::AudioBuffer<float> dryDelayBuf;
    int dryDelayWrite = 0;
    int dryDelaySize = 0;
    int preparedBlockSize = 512;
    bool fxPrepared = false;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList pluginList;
    juce::ApplicationProperties appProperties;
    mutable juce::CriticalSection insertLock;
    std::shared_ptr<std::atomic<bool>> aliveFlag { std::make_shared<std::atomic<bool>> (true) };
    std::atomic<uint64_t> nextLoadId { 1 };
    std::atomic<uint64_t> restoreGeneration { 0 };
    std::atomic<int> reportedFxLatency { 0 };
    mutable juce::CriticalSection statusLock;
    juce::String uiStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouDelayAudioProcessor)
};
