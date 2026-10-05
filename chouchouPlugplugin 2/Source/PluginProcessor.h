/*
  ==============================================================================

    chouchouPlugplugin2 — 10-slot serial plugin rack (hosts VST3 / AU / ARA)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ARAHostSupport.h"
#include "ARA/HostMode.h"
#include "ARA/DiagnosticReport.h"
#include "Audition/HostProbe.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

//==============================================================================
class NewProjectAudioProcessor  : public juce::AudioProcessor,
                                  public juce::ChangeBroadcaster,
                                  private juce::ChangeListener
{
public:
    static constexpr int kNumSlots = 10;

    NewProjectAudioProcessor();
    ~NewProjectAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void numChannelsChanged() override;
    void processorLayoutsChanged() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void setNonRealtime (bool isNonRealtime) noexcept override;
    void updateTrackProperties (const TrackProperties& properties) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList& getPluginList() noexcept { return pluginList; }

    void loadPluginAsync (int slotIndex, const juce::PluginDescription& desc);
    bool loadPluginBlocking (int slotIndex, const juce::PluginDescription& desc, juce::String& error);

    void clearSlot (int slotIndex);
    void setSlotBypassed (int slotIndex, bool shouldBypass);
    bool isSlotBypassed (int slotIndex) const;
    bool isSlotLoaded (int slotIndex) const;
    juce::String getSlotName (int slotIndex) const;
    std::unique_ptr<juce::AudioProcessorEditor> createSlotEditor (int slotIndex);

    std::shared_ptr<juce::AudioPluginInstance> getSlotInstanceShared (int slotIndex) const;

    bool isSlotARA (int slotIndex) const;
    bool captureLiveAudioToARASlot (int slotIndex, juce::String& error);
    /** @deprecated Prefer start/stop + finalizeNamedCapture (Bridge Rec UX). */
    bool finalizeBridgeCapture (juce::String& errorOrPath);

    /** Idle -> Recording -> ReadyToSave (Stop, or buffer full) -> Idle (saved / discarded). */
    enum class CaptureUxState { Idle, Recording, ReadyToSave };
    enum class CaptureSource { Undecided = 0, Output = 1, Input = 2 };

    static constexpr int kMaxCaptureSeconds = 180;

    CaptureUxState getCaptureUxState() const noexcept;
    CaptureSource getCaptureSource() const noexcept
    {
        return (CaptureSource) captureSource.load (std::memory_order_relaxed);
    }
    float getLiveInputPeak() const noexcept  { return liveInputPeak.load (std::memory_order_relaxed); }
    float getLiveOutputPeak() const noexcept { return liveOutputPeak.load (std::memory_order_relaxed); }

    /** Rec: refuses (returns false) while a take is waiting to be saved or discarded. */
    bool startBridgeRecording (juce::String& error);
    /** Stop: syncs with the audio thread, then keeps the take (true) or drops an empty/silent one. */
    bool stopBridgeRecording (juce::String& error);
    /** Drop a take waiting in ReadyToSave. */
    void discardBridgeTake();
    bool isBridgeRecording() const noexcept;
    /** Write the take to a user-chosen WAV + SpectraLayers sidecar session.json.
        On failure the take stays in ReadyToSave so the user can retry. */
    bool finalizeNamedCapture (const juce::File& wavFile, juce::String& errorOrPath);

    juce::File getDefaultCaptureDirectory() const;
    juce::File getLastCaptureWav() const;
    void rememberCaptureWav (const juce::File& wav);

    /** Standalone: load SpectraLayers, bind WAV, open editor when Ready. */
    void beginSpectraLayersOpenWithWav (const juce::File& wav);
    void beginSpectraLayersOpenWithSession (const juce::File& sessionJson);

    bool assignFileToARASlot (int slotIndex, const juce::File& file, juce::String& error);

    //==============================================================================
    // SpectraLayers round trip: import (drop / Live Import Track / take) -> edit -> in-place or Render.

    /** Send an audio file to the first ARA slot (SpectraLayers is loaded into an empty slot if
        none exists). hostStartSample = DAW timeline sample where the file's first sample sits. */
    void importFileToSpectraLayers (const juce::File& file, juce::int64 hostStartSample,
                                    const juce::String& note = {});
    /** Ask the chouchouLink Remote Script in Live for the selected track's audio clip, then import it. */
    void requestLiveImportTrack();
    /** Write the current take, bind it to the first ARA slot at the position it was recorded. */
    bool bindTakeToSpectraLayers (juce::String& error);

    int findFirstARASlot() const;
    /** False when nothing is bound; start/length are in seconds on the DAW timeline. */
    bool getSlotRegion (int slotIndex, double& startSeconds, double& lengthSeconds) const;
    /** Offline-render the slot's SpectraLayers output to <source>_SL.wav and reveal it. */
    void renderSlotToWavAsync (int slotIndex);
    bool isRenderRunning() const noexcept { return renderRunning.load(); }

    /** One-shot: record from the next DAW Play to the next Stop, then send the take to SpectraLayers. */
    void setAutoCapture (bool enabled);
    bool isAutoCaptureEnabled() const noexcept { return autoCapture.load(); }

    juce::int64 getLastHostTimeSamples() const noexcept { return lastHostTimeSamples.load(); }
    double getHostSampleRate() const noexcept { return currentSampleRate; }
    /** Transport state from the last block; false once the host stops calling processBlock. */
    bool isHostPlaying() const noexcept;
    /** Slot whose plug-in UI should open after a bind, or -1. The editor consumes it. */
    int takePendingOpenUiSlot() noexcept { return pendingOpenUiSlot.exchange (-1); }

    static constexpr int kChouChouLinkPort = 9017;
    std::unique_ptr<juce::AudioProcessorEditor> createARAHostControlEditor (int slotIndex);
    LiveCaptureBuffer& getLiveCapture() noexcept { return liveCapture; }

    void setUiStatus (const juce::String& text);
    juce::String getUiStatus() const;

    ChouChouDiagnosticReport getLastDiagnostic() const;
    void copyLastDiagnosticToClipboard() const;
    ChouChouHostMode getHostMode() const noexcept;

    juce::ApplicationProperties& getAppProperties() noexcept { return appProperties; }

    ChouChouHostProbe* getHostProbe() noexcept { return hostProbe.get(); }
    /** Probe experiments: report 1024 extra latency (no real delay) / 2 s extra tail. */
    void setProbeLatencyExperiment (bool enabled);
    void setProbeTailExperiment (bool enabled);

private:
    struct Slot
    {
        std::shared_ptr<juce::AudioPluginInstance> instance;
        juce::PluginDescription description;
        bool bypassed = false;
        uint64_t loadGeneration = 0;
    };

    /** Immutable snapshot published to the audio thread (atomic shared_ptr load). */
    struct AudioRuntime
    {
        std::array<std::shared_ptr<juce::AudioPluginInstance>, kNumSlots> chain;
        std::array<bool, kNumSlots> bypass {};
        std::shared_ptr<juce::AudioBuffer<float>> scratch; // never mutated after publish
        int scratchSamples = 0;
    };

    struct RetiringPlugin
    {
        std::shared_ptr<juce::AudioPluginInstance> plugin;
        uint64_t earliestBlock = 0;
    };

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void updateLatency();
    void updateLatencyUnlocked();
    bool configureInnerBuses (juce::AudioPluginInstance& plugin);
    void prepareSlot (Slot& slot);
    void releaseSlot (Slot& slot);
    bool safePrepareInstance (juce::AudioPluginInstance& plugin, juce::String* errorOut = nullptr);
    void notifySlotsChanged();
    void resyncAllInnerPlugins();
    uint64_t beginSlotLoad (int slotIndex);
    bool isLoadStillCurrent (int slotIndex, uint64_t requestId) const;

    void ensureAdaptScratchCapacity (int numSamples);
    void requestInnerReprepare (int blockSize);
    void publishAudioRuntimeUnlocked(); // must hold slotLock
    void retirePlugin (std::shared_ptr<juce::AudioPluginInstance> plugin);
    void drainRetiredPluginsFromAudioThread();
    void flushRetiredPluginsNow();

    bool findSpectraLayersDescription (juce::PluginDescription& out) const;
    void pollPendingSpectraLayersOpen();
    bool writeSpectraLayersSessionSidecar (const juce::File& wav, float peak) const;
    void recordCaptureDiagnostic (ChouChouCaptureState state, const juce::String& code,
                                  const juce::String& message, const juce::File& wav = {});

    static bool isSelfDescription (const juce::PluginDescription& pd);
    static bool isUnsupportedPluginType (const juce::PluginDescription& pd, juce::String& why);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList pluginList;
    juce::ApplicationProperties appProperties;

    mutable juce::CriticalSection slotLock;
    std::array<Slot, kNumSlots> slots;

    /** Audio thread only does atomic_load — never waits on slotLock. */
    std::shared_ptr<AudioRuntime> audioRuntime;

    std::shared_ptr<std::atomic<bool>> aliveFlag { std::make_shared<std::atomic<bool>> (true) };
    std::atomic<uint64_t> nextLoadId { 1 };
    std::atomic<uint64_t> restoreGeneration { 0 };
    std::atomic<uint64_t> audioBlockCounter { 0 };

    std::atomic<bool> isActive { false };
    double currentSampleRate = 44100.0;
    std::atomic<int> currentBlockSize { 512 };
    std::atomic<bool> repreparePending { false };

    /** Published via atomic shared_ptr; audio never sees a buffer mid-realloc. */
    std::shared_ptr<juce::AudioBuffer<float>> adaptScratchPublished;

    mutable juce::CriticalSection statusLock;
    juce::String uiStatus;
    ChouChouDiagnosticReport lastDiagnostic;
    LiveCaptureBuffer liveCapture;
    /** A take exists that has not been saved or discarded yet (Recording or ReadyToSave). */
    std::atomic<bool> takePending { false };
    std::atomic<int> captureSource { (int) CaptureSource::Undecided };
    std::atomic<float> liveInputPeak { 0.0f }, liveOutputPeak { 0.0f };
    /** Pre-FX insert input for the current block; sized in prepareToPlay only. */
    juce::AudioBuffer<float> captureInputSnapshot;

    juce::File pendingSpectraLayersWav;
    int spectraLayersOpenAttempts = 0;
    bool spectraLayersLoadStarted = false;
    std::unique_ptr<juce::DocumentWindow> spectraLayersEditorWindow;

    juce::CriticalSection retireLock;
    std::vector<RetiringPlugin> retiring;

    std::unique_ptr<ChouChouHostProbe> hostProbe;
    void probeEvent (const juce::String& text);

    struct CallbackTimer final : public juce::Timer
    {
        std::function<void()> callback;
        void timerCallback() override { if (callback) callback(); }
    };

    struct PendingImport
    {
        juce::File wav;
        juce::int64 hostStart = 0;
        int slot = -1;
        int attempts = 0;
        bool loadStarted = false;
        juce::String note;
    };

    void pollPendingImport();
    bool bindFileToSlot (int slotIndex, const juce::File& wav, juce::int64 hostStart, juce::String& error);
    juce::File ensureWavForARA (const juce::File& source, juce::String& error) const;
    void handleLiveLinkReply (const juce::String& reply, const juce::String& error);
    void autoCaptureTick();

    std::atomic<juce::int64> lastHostTimeSamples { 0 };
    std::atomic<bool> lastHostPlaying { false };
    std::atomic<juce::uint32> lastBlockMs { 0 };
    /** Host sample of the take's first recorded sample. */
    std::atomic<juce::int64> takeStartSample { 0 };
    std::atomic<int> pendingOpenUiSlot { -1 };
    std::atomic<bool> renderRunning { false };
    std::atomic<bool> autoCapture { false };
    bool autoTakeRunning = false;
    CallbackTimer autoCaptureTimer;
    PendingImport pendingImport;

    /** Live clip guessed from the playhead or track (nothing explicitly selected); imported
        only if Import Track is pressed again before expiresMs. Message thread only. */
    struct LiveConfirm
    {
        juce::File file;
        juce::int64 hostStart = 0;
        juce::String note;
        juce::uint32 expiresMs = 0;
    };
    LiveConfirm liveConfirm;
    /** Dry copy for blocks that straddle a clip edge; sized in prepareToPlay only. */
    juce::AudioBuffer<float> araDryScratch;

    static constexpr int kMinScratchSamples = 8192;
    /** Adapt scratch must hold every bus of a hosted plug-in (main + sidechain), or
        processHostedInstance skips that slot. */
    static constexpr int kAdaptScratchChannels = 16;

    static void processHostedInstance (juce::AudioPluginInstance& plugin,
                                       juce::AudioBuffer<float>& rackBuffer,
                                       juce::MidiBuffer& midi,
                                       juce::AudioBuffer<float>& scratch,
                                       int scratchCapacitySamples);

    JUCE_DECLARE_WEAK_REFERENCEABLE (NewProjectAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessor)
};
