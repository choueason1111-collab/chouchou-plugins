/*
  ==============================================================================

    chouchouPlugplugin — 10-slot serial plugin rack

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginScanSupport.h"

namespace
{
    constexpr const char* kPluginListKey = "pluginList";
    constexpr const char* kStateTag      = "chouchouPlugplugin";
    constexpr const char* kSlotTag       = "SLOT";
    constexpr const char* kInnerStateTag = "inner_state";
}

//==============================================================================
bool NewProjectAudioProcessor::isSelfDescription (const juce::PluginDescription& pd)
{
    if (pd.name.containsIgnoreCase (JucePlugin_Name))
        return true;

    if (pd.fileOrIdentifier.containsIgnoreCase ("chouchouPlugplugin"))
        return true;

    if (pd.manufacturerName.containsIgnoreCase (JucePlugin_Manufacturer)
        && pd.name.containsIgnoreCase ("Plugplugin"))
        return true;

    // AU fourCC / unique id when present
    if (pd.uniqueId != 0 && pd.uniqueId == (int) JucePlugin_PluginCode)
        return true;

    return false;
}

bool NewProjectAudioProcessor::isUnsupportedPluginType (const juce::PluginDescription& pd, juce::String& why)
{
    if (pd.isInstrument)
    {
        why = "Instruments / synths are not supported in this effect rack.";
        return true;
    }

    if (pd.category.containsIgnoreCase ("Synth")
        || pd.category.containsIgnoreCase ("Instrument"))
    {
        why = "Category \"" + pd.category + "\" is not supported in this effect rack.";
        return true;
    }

    return false;
}

//==============================================================================
NewProjectAudioProcessor::NewProjectAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#else
     :
#endif
{
    appProperties.setStorageParameters ([]
    {
        juce::PropertiesFile::Options opt;
        opt.applicationName = "chouchouPlugplugin";
        opt.filenameSuffix = ".props";
        opt.osxLibrarySubFolder = "Application Support";
        opt.folderName = "chouchou";
        opt.commonToAllUsers = false;
        return opt;
    }());

    juce::addDefaultFormatsToManager (formatManager);

    if (auto* settings = appProperties.getUserSettings())
        if (auto saved = settings->getXmlValue (kPluginListKey))
            pluginList.recreateFromXml (*saved);

    prepareChouChouPluginScanning (pluginList);

    pluginList.addChangeListener (this);
}

NewProjectAudioProcessor::~NewProjectAudioProcessor()
{
    aliveFlag->store (false, std::memory_order_release);
    pluginList.removeChangeListener (this);

    const juce::ScopedLock sl (slotLock);
    for (auto& slot : slots)
    {
        releaseSlot (slot);
        slot.instance.reset();
    }
}

//==============================================================================
const juce::String NewProjectAudioProcessor::getName() const { return JucePlugin_Name; }

bool NewProjectAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool NewProjectAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool NewProjectAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double NewProjectAudioProcessor::getTailLengthSeconds() const
{
    // Serial chain: sum tails (upper bound) rather than max of one stage.
    const juce::ScopedLock sl (slotLock);
    double tail = 0.0;
    for (auto& slot : slots)
        if (slot.instance != nullptr && ! slot.bypassed)
            tail += slot.instance->getTailLengthSeconds();
    return tail;
}

int NewProjectAudioProcessor::getNumPrograms()                     { return 1; }
int NewProjectAudioProcessor::getCurrentProgram()                 { return 0; }
void NewProjectAudioProcessor::setCurrentProgram (int)            {}
const juce::String NewProjectAudioProcessor::getProgramName (int) { return {}; }
void NewProjectAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
#ifndef JucePlugin_PreferredChannelConfigurations
bool NewProjectAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (! in.isDisabled() && in != out)
        return false;

    return true;
}
#endif

bool NewProjectAudioProcessor::configureInnerBuses (juce::AudioPluginInstance& plugin)
{
    // Keep the SAME channel layout as this rack end-to-end. Insert Basic (and most
    // hosts) are stereo throughout — do NOT silently fall back to mono for later
    // slots, or nested plugins "stop detecting" while Audition (stable stereo) works.
    const int hostCh = juce::jmax (1, getMainBusNumOutputChannels());
    const auto set = hostCh >= 2 ? juce::AudioChannelSet::stereo()
                                 : juce::AudioChannelSet::mono();

    auto tryMainIO = [&plugin] (const juce::AudioChannelSet& ioSet, bool disableExtras) -> bool
    {
        auto layout = plugin.getBusesLayout();

        if (layout.outputBuses.isEmpty())
            return false;

        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = ioSet;

        layout.outputBuses.getReference (0) = ioSet;

        if (disableExtras)
        {
            for (int i = 1; i < layout.inputBuses.size(); ++i)
                layout.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();

            for (int i = 1; i < layout.outputBuses.size(); ++i)
                layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();
        }

        return plugin.checkBusesLayoutSupported (layout) && plugin.setBusesLayout (layout);
    };

    if (tryMainIO (set, true) || tryMainIO (set, false))
        return true;

    // Last resort: leave plugin default but mute sidechain/aux — still require
    // main I/O channel count to match the rack.
    plugin.disableNonMainBuses();
    const int inCh  = plugin.getMainBusNumInputChannels();
    const int outCh = plugin.getMainBusNumOutputChannels();
    return (outCh == hostCh) && (inCh == 0 || inCh == hostCh);
}

void NewProjectAudioProcessor::resyncAllInnerPlugins()
{
    // Insert Basic can change our bus layout / buffer size after the first prepare.
    // Audition usually sets layout once up front — that's why nested comps work there.
    // Snapshot instances so we never call prepare while holding slotLock (non-recursive).
    std::array<std::shared_ptr<juce::AudioPluginInstance>, kNumSlots> snap;

    {
        const juce::ScopedLock sl (slotLock);
        for (int i = 0; i < kNumSlots; ++i)
            snap[(size_t) i] = slots[(size_t) i].instance;
    }

    for (auto& instance : snap)
    {
        if (instance == nullptr)
            continue;

        configureInnerBuses (*instance);
        instance->setPlayConfigDetails (juce::jmax (1, instance->getMainBusNumInputChannels()),
                                        juce::jmax (1, instance->getMainBusNumOutputChannels()),
                                        currentSampleRate, currentBlockSize);
        instance->setRateAndBufferSizeDetails (currentSampleRate, currentBlockSize);
        instance->prepareToPlay (currentSampleRate, currentBlockSize);
        instance->suspendProcessing (false);
    }

    updateLatency();
}

void NewProjectAudioProcessor::numChannelsChanged()
{
    if (isActive)
        resyncAllInnerPlugins();
}

void NewProjectAudioProcessor::processorLayoutsChanged()
{
    if (isActive)
        resyncAllInnerPlugins();
}

void NewProjectAudioProcessor::processHostedInstance (juce::AudioPluginInstance& plugin,
                                                      juce::AudioBuffer<float>& rackBuffer,
                                                      juce::MidiBuffer& midi,
                                                      juce::AudioBuffer<float>& scratch)
{
    const int numSamples = rackBuffer.getNumSamples();
    const int rackCh = rackBuffer.getNumChannels();
    int mainIn = plugin.getMainBusNumInputChannels();
    int mainOut = plugin.getMainBusNumOutputChannels();
    int totalIn = plugin.getTotalNumInputChannels();
    int totalOut = plugin.getTotalNumOutputChannels();

    // Some nested AUs briefly report 0 main inputs while still expecting stereo I/O.
    if (mainIn <= 0)
        mainIn = juce::jmax (1, juce::jmin (rackCh, totalIn > 0 ? totalIn : rackCh));
    if (mainOut <= 0)
        mainOut = juce::jmax (1, juce::jmin (rackCh, totalOut > 0 ? totalOut : rackCh));
    if (totalIn <= 0)
        totalIn = mainIn;
    if (totalOut <= 0)
        totalOut = mainOut;

    const int totalCh = juce::jmax (totalIn, totalOut, 1);

    // Fast path: main+total match the rack (no sidechain, no mono/stereo adapt).
    if (mainIn == rackCh && mainOut == rackCh
        && totalIn == rackCh && totalOut == rackCh)
    {
        plugin.processBlock (rackBuffer, midi);
        return;
    }

    scratch.setSize (totalCh, numSamples, false, false, true);
    scratch.clear();

    if (mainIn == 1 && rackCh >= 1)
    {
        scratch.copyFrom (0, 0, rackBuffer, 0, 0, numSamples);

        if (rackCh > 1)
        {
            scratch.addFrom (0, 0, rackBuffer, 1, 0, numSamples);
            scratch.applyGain (0, 0, numSamples, 0.5f);
        }
    }
    else if (mainIn >= 2)
    {
        scratch.copyFrom (0, 0, rackBuffer, 0, 0, numSamples);

        if (rackCh > 1)
            scratch.copyFrom (1, 0, rackBuffer, 1, 0, numSamples);
        else
            scratch.copyFrom (1, 0, rackBuffer, 0, 0, numSamples);
    }

    plugin.processBlock (scratch, midi);

    if (mainOut <= 0)
        return;

    if (mainOut == 1)
    {
        rackBuffer.copyFrom (0, 0, scratch, 0, 0, numSamples);

        if (rackCh > 1)
            rackBuffer.copyFrom (1, 0, scratch, 0, 0, numSamples);
    }
    else if (rackCh == 1)
    {
        rackBuffer.copyFrom (0, 0, scratch, 0, 0, numSamples);
        rackBuffer.addFrom (0, 0, scratch, 1, 0, numSamples);
        rackBuffer.applyGain (0, 0, numSamples, 0.5f);
    }
    else
    {
        rackBuffer.copyFrom (0, 0, scratch, 0, 0, numSamples);
        rackBuffer.copyFrom (1, 0, scratch, 1, 0, numSamples);
    }
}

void NewProjectAudioProcessor::prepareSlot (Slot& slot)
{
    if (slot.instance == nullptr)
        return;

    auto& plugin = *slot.instance;

    // Re-assert bus + play config after setBusesLayout. Nested hosts that skip a
    // second prepare leave the inner AU with 0 input channels → silence / no detect.
    const int inCh  = juce::jmax (1, plugin.getMainBusNumInputChannels() > 0
                                         ? plugin.getMainBusNumInputChannels()
                                         : plugin.getTotalNumInputChannels());
    const int outCh = juce::jmax (1, plugin.getMainBusNumOutputChannels() > 0
                                         ? plugin.getMainBusNumOutputChannels()
                                         : plugin.getTotalNumOutputChannels());

    plugin.setPlayConfigDetails (inCh, outCh, currentSampleRate, currentBlockSize);
    plugin.setRateAndBufferSizeDetails (currentSampleRate, currentBlockSize);
    plugin.prepareToPlay (currentSampleRate, currentBlockSize);
    plugin.suspendProcessing (false);
}

void NewProjectAudioProcessor::releaseSlot (Slot& slot)
{
    if (slot.instance != nullptr)
        slot.instance->releaseResources();
}

void NewProjectAudioProcessor::updateLatencyUnlocked()
{
    int total = 0;
    for (auto& slot : slots)
        if (slot.instance != nullptr && ! slot.bypassed)
            total += slot.instance->getLatencySamples();

    setLatencySamples (total);
}

void NewProjectAudioProcessor::updateLatency()
{
    const juce::ScopedLock sl (slotLock);
    updateLatencyUnlocked();
}

void NewProjectAudioProcessor::notifySlotsChanged()
{
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && ! juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread())
    {
        juce::MessageManager::callAsync ([life = aliveFlag, this]
        {
            if (life->load (std::memory_order_acquire))
                sendChangeMessage();
        });
        return;
    }

    sendChangeMessage();
}

uint64_t NewProjectAudioProcessor::beginSlotLoad (int slotIndex)
{
    const uint64_t id = nextLoadId.fetch_add (1, std::memory_order_relaxed);
    slots[(size_t) slotIndex].loadGeneration = id;
    return id;
}

bool NewProjectAudioProcessor::isLoadStillCurrent (int slotIndex, uint64_t requestId) const
{
    return slots[(size_t) slotIndex].loadGeneration == requestId;
}

void NewProjectAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    adaptScratch.setSize (2, samplesPerBlock);

    const juce::ScopedLock sl (slotLock);
    isActive = true;

    for (auto& slot : slots)
        prepareSlot (slot);

    updateLatencyUnlocked();
}

void NewProjectAudioProcessor::releaseResources()
{
    const juce::ScopedLock sl (slotLock);
    isActive = false;

    for (auto& slot : slots)
        releaseSlot (slot);
}

void NewProjectAudioProcessor::reset()
{
    const juce::ScopedLock sl (slotLock);
    for (auto& slot : slots)
        if (slot.instance != nullptr)
            slot.instance->reset();
}

void NewProjectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Insert Basic AudioProcessTap often delivers a different block size than the
    // one from the initial prepareToPlay (prefs ioBufferSize=2048). Re-prepare
    // inners when that drifts so nested compressors keep seeing audio.
    const int blockN = buffer.getNumSamples();
    if (isActive && blockN > 0
        && (blockN != currentBlockSize
            || adaptScratch.getNumSamples() < blockN
            || adaptScratch.getNumChannels() < 2))
    {
        currentBlockSize = blockN;
        adaptScratch.setSize (2, blockN, false, false, true);

        const juce::ScopedLock sl (slotLock);
        for (auto& slot : slots)
            if (slot.instance != nullptr)
                prepareSlot (slot);
    }

    const int bufCh = buffer.getNumChannels();
    int numIn  = getMainBusNumInputChannels();
    int numOut = getMainBusNumOutputChannels();

    if (numIn <= 0)
        numIn = getTotalNumInputChannels();
    if (numOut <= 0)
        numOut = getTotalNumOutputChannels();
    if (numIn <= 0)
        numIn = bufCh;
    if (numOut <= 0)
        numOut = bufCh;

    for (int i = numIn; i < numOut && i < bufCh; ++i)
        buffer.clear (i, 0, blockN);

    // Brief lock: copy shared_ptrs only. Never call processBlock while holding slotLock.
    std::array<std::shared_ptr<juce::AudioPluginInstance>, kNumSlots> chain;
    std::array<bool, kNumSlots> bypass {};

    {
        const juce::ScopedLock sl (slotLock);
        for (int i = 0; i < kNumSlots; ++i)
        {
            chain[(size_t) i] = slots[(size_t) i].instance;
            bypass[(size_t) i] = slots[(size_t) i].bypassed;
        }
    }

    for (int i = 0; i < kNumSlots; ++i)
        if (chain[(size_t) i] != nullptr && ! bypass[(size_t) i])
            processHostedInstance (*chain[(size_t) i], buffer, midi, adaptScratch);
}

//==============================================================================
void NewProjectAudioProcessor::setUiStatus (const juce::String& text)
{
    {
        const juce::ScopedLock sl (statusLock);
        uiStatus = text;
    }

    notifySlotsChanged();
}

juce::String NewProjectAudioProcessor::getUiStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return uiStatus;
}

//==============================================================================
void NewProjectAudioProcessor::loadPluginAsync (int slotIndex, const juce::PluginDescription& desc)
{
    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
        return;

    if (isSelfDescription (desc))
    {
        setUiStatus ("Cannot load chouchouPlugplugin inside itself.");
        return;
    }

    juce::String why;
    if (isUnsupportedPluginType (desc, why))
    {
        setUiStatus (why);
        return;
    }

    if (pluginList.getBlacklistedFiles().contains (desc.fileOrIdentifier))
    {
        setUiStatus ("Plugin is blacklisted (previously crashed during scan).");
        return;
    }

    // Never block the host message thread with createPluginInstance —
    // Insert Basic (systemwide AU host) freezes/hangs on sync nested loads.
    // Use async create; status line only (no AlertWindow).
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const int bs = currentBlockSize > 0 ? currentBlockSize : 512;

    uint64_t requestId = 0;
    {
        const juce::ScopedLock sl (slotLock);
        requestId = beginSlotLoad (slotIndex);
    }

    setUiStatus ("Loading " + desc.name + " (" + desc.pluginFormatName + ")...");

    auto life = aliveFlag;
    const auto pd = desc;

    formatManager.createPluginInstanceAsync (
        pd, sr, bs,
        [life, this, slotIndex, requestId, pluginName = desc.name]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (! life->load (std::memory_order_acquire))
                return;

            if (error.isNotEmpty() || instance == nullptr)
            {
                setUiStatus ("Load failed: " + (error.isNotEmpty() ? error : pluginName));
                return;
            }

            if (! configureInnerBuses (*instance))
            {
                setUiStatus ("Load failed: could not configure buses for " + pluginName);
                return;
            }

            auto shared = std::shared_ptr<juce::AudioPluginInstance> (std::move (instance));

            {
                const juce::ScopedLock sl (slotLock);
                if (! isLoadStillCurrent (slotIndex, requestId))
                {
                    setUiStatus ("Load cancelled.");
                    return;
                }

                auto& slot = slots[(size_t) slotIndex];
                releaseSlot (slot);
                slot.description = shared->getPluginDescription();
                slot.instance = std::move (shared);
                slot.bypassed = false;

                if (isActive)
                    prepareSlot (slot);

                updateLatencyUnlocked();
            }

            setUiStatus ("Loaded: " + getSlotName (slotIndex));
            notifySlotsChanged();
        });
}

bool NewProjectAudioProcessor::loadPluginBlocking (int slotIndex,
                                                   const juce::PluginDescription& desc,
                                                   juce::String& error)
{
    error.clear();

    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
    {
        error = "Invalid slot";
        return false;
    }

    if (isSelfDescription (desc))
    {
        error = "Cannot load chouchouPlugplugin inside itself.";
        return false;
    }

    if (isUnsupportedPluginType (desc, error))
        return false;

    if (pluginList.getBlacklistedFiles().contains (desc.fileOrIdentifier))
    {
        error = "Plugin is blacklisted (previously crashed during scan).";
        return false;
    }

    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const int bs = currentBlockSize > 0 ? currentBlockSize : 512;

    juce::String createError;
    auto instance = formatManager.createPluginInstance (desc, sr, bs, createError);

    if (instance == nullptr)
    {
        error = createError.isNotEmpty() ? createError : "createPluginInstance failed";
        return false;
    }

    if (! configureInnerBuses (*instance))
    {
        error = "Could not configure plugin buses for the rack.";
        return false;
    }

    auto shared = std::shared_ptr<juce::AudioPluginInstance> (std::move (instance));

    {
        const juce::ScopedLock sl (slotLock);
        beginSlotLoad (slotIndex);
        auto& slot = slots[(size_t) slotIndex];
        releaseSlot (slot);
        slot.description = shared->getPluginDescription();
        slot.instance = std::move (shared);
        slot.bypassed = false;

        if (isActive)
            prepareSlot (slot);

        updateLatencyUnlocked();
    }

    notifySlotsChanged();
    return true;
}

void NewProjectAudioProcessor::clearSlot (int slotIndex)
{
    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
        return;

    {
        const juce::ScopedLock sl (slotLock);
        beginSlotLoad (slotIndex); // invalidate any in-flight load for this slot
        auto& slot = slots[(size_t) slotIndex];
        releaseSlot (slot);
        slot.instance.reset();
        slot.description = {};
        slot.bypassed = false;
        updateLatencyUnlocked();
    }

    notifySlotsChanged();
}

void NewProjectAudioProcessor::setSlotBypassed (int slotIndex, bool shouldBypass)
{
    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
        return;

    {
        const juce::ScopedLock sl (slotLock);
        slots[(size_t) slotIndex].bypassed = shouldBypass;
        updateLatencyUnlocked();
    }

    notifySlotsChanged();
}

bool NewProjectAudioProcessor::isSlotBypassed (int slotIndex) const
{
    const juce::ScopedLock sl (slotLock);
    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
        return false;
    return slots[(size_t) slotIndex].bypassed;
}

bool NewProjectAudioProcessor::isSlotLoaded (int slotIndex) const
{
    const juce::ScopedLock sl (slotLock);
    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
        return false;
    return slots[(size_t) slotIndex].instance != nullptr;
}

juce::String NewProjectAudioProcessor::getSlotName (int slotIndex) const
{
    const juce::ScopedLock sl (slotLock);
    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
        return {};

    const auto& slot = slots[(size_t) slotIndex];
    if (slot.instance == nullptr)
        return "(empty)";

    return slot.description.name.isNotEmpty() ? slot.description.name
                                              : slot.instance->getName();
}

std::shared_ptr<juce::AudioPluginInstance> NewProjectAudioProcessor::getSlotInstanceShared (int slotIndex) const
{
    const juce::ScopedLock sl (slotLock);
    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
        return {};
    return slots[(size_t) slotIndex].instance;
}

std::unique_ptr<juce::AudioProcessorEditor> NewProjectAudioProcessor::createSlotEditor (int slotIndex)
{
    auto inst = getSlotInstanceShared (slotIndex);
    if (inst == nullptr || ! inst->hasEditor())
        return nullptr;

    // Instance kept alive by caller via getSlotInstanceShared / editorKeepAlive.
    return std::unique_ptr<juce::AudioProcessorEditor> (inst->createEditorAndMakeActive());
}

//==============================================================================
void NewProjectAudioProcessor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source != &pluginList)
        return;

    if (auto xml = pluginList.createXml())
    {
        if (auto* settings = appProperties.getUserSettings())
        {
            settings->setValue (kPluginListKey, xml.get());
            appProperties.saveIfNeeded();
        }
    }
}

//==============================================================================
void NewProjectAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root (kStateTag);

    std::array<std::shared_ptr<juce::AudioPluginInstance>, kNumSlots> snap;
    std::array<juce::PluginDescription, kNumSlots> descs;
    std::array<bool, kNumSlots> bypass {};

    {
        const juce::ScopedLock sl (slotLock);
        for (int i = 0; i < kNumSlots; ++i)
        {
            snap[(size_t) i] = slots[(size_t) i].instance;
            descs[(size_t) i] = slots[(size_t) i].description;
            bypass[(size_t) i] = slots[(size_t) i].bypassed;
        }
    }

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto* slotXml = root.createNewChildElement (kSlotTag);
        slotXml->setAttribute ("index", i);
        slotXml->setAttribute ("bypassed", bypass[(size_t) i] ? 1 : 0);

        if (snap[(size_t) i] == nullptr)
            continue;

        auto desc = descs[(size_t) i];
        if (desc.name.isEmpty())
            desc = snap[(size_t) i]->getPluginDescription();

        if (auto descXml = desc.createXml())
            slotXml->addChildElement (descXml.release());

        juce::MemoryBlock inner;
        snap[(size_t) i]->getStateInformation (inner);
        auto* stateNode = slotXml->createNewChildElement (kInnerStateTag);
        stateNode->addTextElement (inner.toBase64Encoding());
    }

    copyXmlToBinary (root, destData);
}

void NewProjectAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (kStateTag))
        return;

    const uint64_t thisRestore = restoreGeneration.fetch_add (1, std::memory_order_relaxed) + 1;

    // Single batch clear — one latency update + one UI notify
    {
        const juce::ScopedLock sl (slotLock);
        for (int i = 0; i < kNumSlots; ++i)
        {
            beginSlotLoad (i);
            releaseSlot (slots[(size_t) i]);
            slots[(size_t) i].instance.reset();
            slots[(size_t) i].description = {};
            slots[(size_t) i].bypassed = false;
        }
        updateLatencyUnlocked();
    }
    notifySlotsChanged();

    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const int bs = currentBlockSize > 0 ? currentBlockSize : 512;
    auto life = aliveFlag;

    for (auto* slotXml : xml->getChildWithTagNameIterator (kSlotTag))
    {
        const int index = slotXml->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (index, kNumSlots))
            continue;

        const bool bypassed = slotXml->getIntAttribute ("bypassed", 0) != 0;

        if (auto* pluginNode = slotXml->getChildByName ("PLUGIN"))
        {
            juce::PluginDescription pd;
            if (! pd.loadFromXml (*pluginNode))
                continue;

            if (isSelfDescription (pd))
                continue;

            juce::String why;
            if (isUnsupportedPluginType (pd, why))
                continue;

            juce::MemoryBlock innerState;
            if (auto* stateNode = slotXml->getChildByName (kInnerStateTag))
                innerState.fromBase64Encoding (stateNode->getAllSubText());

            uint64_t requestId = 0;
            {
                const juce::ScopedLock sl (slotLock);
                requestId = beginSlotLoad (index);
            }

            formatManager.createPluginInstanceAsync (
                pd, sr, bs,
                [life, this, index, bypassed, innerState, requestId, thisRestore]
                (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
                {
                    if (! life->load (std::memory_order_acquire))
                        return;

                    if (restoreGeneration.load (std::memory_order_relaxed) != thisRestore)
                        return;

                    if (error.isNotEmpty() || instance == nullptr)
                        return;

                    if (! configureInnerBuses (*instance))
                        return;

                    if (! innerState.isEmpty())
                        instance->setStateInformation (innerState.getData(), (int) innerState.getSize());

                    auto shared = std::shared_ptr<juce::AudioPluginInstance> (std::move (instance));

                    {
                        const juce::ScopedLock sl (slotLock);
                        if (! isLoadStillCurrent (index, requestId))
                            return;

                        auto& slot = slots[(size_t) index];
                        releaseSlot (slot);
                        slot.description = shared->getPluginDescription();
                        slot.instance = std::move (shared);
                        slot.bypassed = bypassed;

                        if (isActive)
                            prepareSlot (slot);

                        updateLatencyUnlocked();
                    }

                    notifySlotsChanged();
                });
        }
        else
        {
            setSlotBypassed (index, bypassed);
        }
    }
}

//==============================================================================
bool NewProjectAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* NewProjectAudioProcessor::createEditor()
{
    return new NewProjectAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}
