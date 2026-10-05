/*
  ==============================================================================

    chouchouPlugplugin — 10-slot serial plugin rack (hosts VST3 / AU)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>

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

    /** Blocking load for offline self-tests (message-thread / test harness only). */
    bool loadPluginBlocking (int slotIndex, const juce::PluginDescription& desc, juce::String& error);

    void clearSlot (int slotIndex);
    void setSlotBypassed (int slotIndex, bool shouldBypass);
    bool isSlotBypassed (int slotIndex) const;
    bool isSlotLoaded (int slotIndex) const;
    juce::String getSlotName (int slotIndex) const;
    std::unique_ptr<juce::AudioProcessorEditor> createSlotEditor (int slotIndex);

    /** Keep-alive for Open UI — holds the hosted instance while its editor is open. */
    std::shared_ptr<juce::AudioPluginInstance> getSlotInstanceShared (int slotIndex) const;

    /** Non-modal status line for the editor (Insert Basic cannot dismiss AlertWindow). */
    void setUiStatus (const juce::String& text);
    juce::String getUiStatus() const;

private:
    struct Slot
    {
        std::shared_ptr<juce::AudioPluginInstance> instance;
        juce::PluginDescription description;
        bool bypassed = false;
        uint64_t loadGeneration = 0;
    };

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void updateLatency();
    void updateLatencyUnlocked();
    bool configureInnerBuses (juce::AudioPluginInstance& plugin);
    void prepareSlot (Slot& slot);
    void releaseSlot (Slot& slot);
    void notifySlotsChanged();
    void resyncAllInnerPlugins(); // match every slot to current host layout (Insert Basic)
    uint64_t beginSlotLoad (int slotIndex); // under lock: bumps generation, returns id
    bool isLoadStillCurrent (int slotIndex, uint64_t requestId) const;

    static bool isSelfDescription (const juce::PluginDescription& pd);
    static bool isUnsupportedPluginType (const juce::PluginDescription& pd, juce::String& why);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList pluginList;
    juce::ApplicationProperties appProperties;

    mutable juce::CriticalSection slotLock;
    std::array<Slot, kNumSlots> slots;

    std::shared_ptr<std::atomic<bool>> aliveFlag { std::make_shared<std::atomic<bool>> (true) };
    std::atomic<uint64_t> nextLoadId { 1 };
    std::atomic<uint64_t> restoreGeneration { 0 };

    bool isActive = false;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    juce::AudioBuffer<float> adaptScratch;
    mutable juce::CriticalSection statusLock;
    juce::String uiStatus;

    static void processHostedInstance (juce::AudioPluginInstance& plugin,
                                       juce::AudioBuffer<float>& rackBuffer,
                                       juce::MidiBuffer& midi,
                                       juce::AudioBuffer<float>& scratch);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessor)
};
