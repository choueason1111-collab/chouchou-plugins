#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

class ChouchouTriggerAudioProcessor : public juce::AudioProcessor,
                                      private juce::Timer
{
public:
    static constexpr int    kNumSlots          = 6;
    static constexpr int    kVoicesPerSlot     = 8;
    static constexpr int    kVoicePool         = kVoicesPerSlot + 4;   // spare voices for steal fade-outs
    static constexpr double kStealFadeSeconds  = 0.002;
    static constexpr double kEnvHoldSeconds    = 0.02;    // longer than half a cycle of a ~30 Hz kick
    static constexpr double kEnvReleaseSeconds = 0.005;
    static constexpr float  kLevelFloorDb      = -60.0f;              // treated as -inf
    static constexpr int    kMaxTriggersPerBlock = 256;
    static constexpr int    kScopeFifoSize     = 16384;
    static constexpr int    kTriggerLogSize    = 256;

    enum ScopeFlags : juce::uint8 { scopeTrigger = 1, scopeHoldoff = 2 };

    struct ScopeColumn
    {
        float lo = 0.0f, hi = 0.0f;
        juce::uint8 flags = 0;
    };

    struct SampleData
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        juce::String path;
    };

    ChouchouTriggerAudioProcessor();
    ~ChouchouTriggerAudioProcessor() override;

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
    double getTailLengthSeconds() const override { return tailSeconds.load(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    static juce::String idOn    (int slot) { return "on"    + juce::String (slot); }
    static juce::String idLevel (int slot) { return "level" + juce::String (slot); }
    static juce::String idPitch (int slot) { return "pitch" + juce::String (slot); }
    static juce::String idFine  (int slot) { return "fine"  + juce::String (slot); }

    // Sample slots (slot indices are 1-based). Not for the audio thread.
    bool loadSlot (int slot, const juce::File& file);
    void clearSlot (int slot);
    juce::String getSlotPath (int slot) const;
    bool isSlotMissing (int slot) const;
    std::shared_ptr<const SampleData> getSlotData (int slot) const;
    int getSlotsVersion() const { return slotsVersion.load(); }
    static juce::String getSupportedWildcard() { return "*.wav;*.mp3;*.aif;*.aiff;*.flac"; }

    void triggerPreview (int slot);

    // Monitoring.
    double getSampleRateSafe() const       { return currentSampleRate.load(); }
    int    getScopeSamplesPerColumn() const { return scopeSamplesPerColumn.load(); }
    int    popScopeColumns (ScopeColumn* dest, int maxColumns);
    float  takeInputPeak()                  { return meterPeak.exchange (0.0f); }
    int    getTriggerCount() const          { return triggerCount.load(); }
    // Copies the most recent trigger positions (absolute sample index since prepareToPlay), oldest first.
    std::vector<juce::int64> getTriggerLog() const;
    float  getThresholdDb() const;
    double getRetriggerSeconds() const;

private:
    struct Voice
    {
        const SampleData* data = nullptr;
        double pos = 0.0, inc = 1.0;
        float  gain = 1.0f;
        int    fade = -1;        // >= 0 while fading out after being stolen
        juce::uint32 age = 0;
        bool   active = false;
    };

    struct SlotStore
    {
        std::shared_ptr<SampleData> data;
        juce::String path;
        bool missing = false;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void timerCallback() override { collectGarbage(); }
    void collectGarbage();
    void publishSlot (int slotIndex, std::shared_ptr<SampleData> data, const juce::String& path, bool missing);
    void updateTail();

    void startVoice (int slotIndex, const SampleData& d);
    void renderSegment (juce::AudioBuffer<float>& out, int start, int end, int blockLen,
                        const std::array<float, kNumSlots>& g0, const std::array<float, kNumSlots>& g1, bool monoOut);

    std::atomic<double> currentSampleRate { 48000.0 };

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formatManager;

    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pRetrigger = nullptr;
    std::atomic<float>* pRelease   = nullptr;
    std::atomic<float>* pDry       = nullptr;
    std::atomic<float>* pWet       = nullptr;
    std::atomic<float>* pOutMode   = nullptr;
    std::array<std::atomic<float>*, kNumSlots> pOn {}, pLevel {}, pPitch {}, pFine {};

    // Slot data handshake: message side owns, audio side reads raw pointers.
    mutable juce::CriticalSection slotLock;
    std::array<SlotStore, kNumSlots> slots;
    std::vector<std::pair<int, std::shared_ptr<SampleData>>> graveyard;
    std::array<std::atomic<SampleData*>, kNumSlots> published {};
    std::array<std::atomic<SampleData*>, kNumSlots> audioAck {};
    std::atomic<bool> audioRunning { false };
    std::atomic<int> slotsVersion { 0 };
    std::atomic<double> tailSeconds { 0.0 };

    // Audio thread state.
    std::array<const SampleData*, kNumSlots> audioData {};
    std::array<std::array<Voice, kVoicePool>, kNumSlots> voices {};
    juce::uint32 voiceAgeCounter = 0;
    int stealFadeSamples = 96;
    std::array<float, kNumSlots> lastSlotGain {};
    float lastDryGain = 1.0f;
    bool  gainsInitialised = false;
    float env = 0.0f;
    int   envHold = 0;
    bool  armed = true;
    juce::int64 samplesSinceTrigger = std::numeric_limits<juce::int64>::max() / 2;
    juce::int64 totalSamples = 0;
    std::array<int, kMaxTriggersPerBlock> triggerOffsets {};
    std::atomic<juce::uint32> previewMask { 0 };

    // Scope.
    std::atomic<int> scopeSamplesPerColumn { 48 };
    juce::AbstractFifo scopeFifo { kScopeFifoSize };
    std::array<ScopeColumn, kScopeFifoSize> scopeData {};
    ScopeColumn scopeAccum;
    int scopeCount = 0;

    std::atomic<float> meterPeak { 0.0f };
    std::atomic<int> triggerCount { 0 };
    std::array<std::atomic<juce::int64>, kTriggerLogSize> triggerLog {};
    std::atomic<int> triggerLogWrite { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouTriggerAudioProcessor)
};
