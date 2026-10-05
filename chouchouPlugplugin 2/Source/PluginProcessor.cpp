/*
  ==============================================================================

    chouchouPlugplugin2 - 10-slot serial plugin rack

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginScanSupport.h"

namespace
{
    constexpr const char* kPluginListKey = "pluginList";
    constexpr const char* kStateTag      = "chouchouPlugplugin2";
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

    // Official iZotope whitelist: Logic / Studio One / Pro Tools only - never nested hosts.
    if (pd.name.containsIgnoreCase ("Spectral Editor")
        && (pd.manufacturerName.containsIgnoreCase ("iZotope")
            || pd.fileOrIdentifier.containsIgnoreCase ("RX")))
    {
        why = "RX Spectral Editor ARA only works in Logic / Studio One / Pro Tools. "
              "In Audition use Effects -> RX Connect (opens RX standalone). "
              "Other RX modules (De-click, etc.) can load here as normal VST3.";
        return true;
    }

    // Shipping AuditionCaptureMode: reject ALL inner ARA loads (not only SpectraLayers).
    const auto mode = detectChouChouHostMode (isChouChouRunningAsStandaloneApp());
    if (! allowsInnerAraPluginLoad (mode) && pluginDescriptionHasARA (pd))
    {
        why = "INNER_LOAD_REJECTED: " + juce::String (toString (mode))
              + " does not load inner ARA plugs. Use Capture -> Standalone ARA Host.";
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
        opt.applicationName = "chouchouPlugplugin2";
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

    ensureAdaptScratchCapacity (kMinScratchSamples);
    {
        const juce::ScopedLock sl (slotLock);
        publishAudioRuntimeUnlocked();
    }

    hostProbe = std::make_unique<ChouChouHostProbe> (getWrapperTypeDescription (wrapperType));
    probeEvent ("PROCESSOR constructed mainIn=" + juce::String (getMainBusNumInputChannels())
                + " mainOut=" + juce::String (getMainBusNumOutputChannels()));

    autoCaptureTimer.callback = [this] { autoCaptureTick(); };
    autoCaptureTimer.startTimerHz (10);
}

void NewProjectAudioProcessor::probeEvent (const juce::String& text)
{
    if (hostProbe != nullptr)
        hostProbe->logEvent (text);
}

void NewProjectAudioProcessor::setProbeLatencyExperiment (bool enabled)
{
    if (hostProbe == nullptr)
        return;
    hostProbe->expLatency.store (enabled);
    updateLatency();
    probeEvent ("EXPERIMENT latency1024=" + juce::String ((int) enabled)
                + " reported=" + juce::String (getLatencySamples()));
}

void NewProjectAudioProcessor::setProbeTailExperiment (bool enabled)
{
    if (hostProbe == nullptr)
        return;
    hostProbe->expTail.store (enabled);
    updateHostDisplay (ChangeDetails().withLatencyChanged (true));
    probeEvent ("EXPERIMENT tail2s=" + juce::String ((int) enabled)
                + " reported=" + juce::String (getTailLengthSeconds(), 2) + "s");
}

void NewProjectAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (hostProbe != nullptr && buffer.getNumSamples() > 0)
    {
        ChouChouHostProbe::BlockInfo info;
        info.numSamples = buffer.getNumSamples();
        info.numChannels = buffer.getNumChannels();
        info.nonRealtime = isNonRealtime();
        info.bypassed = true;
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            info.inputPeak = juce::jmax (info.inputPeak, buffer.getMagnitude (c, 0, buffer.getNumSamples()));
        info.outputPeak = info.inputPeak;
        ChouChouHostProbe::readPlayhead (getPlayHead(), info);
        hostProbe->logBlock (info);
    }

    AudioProcessor::processBlockBypassed (buffer, midi);
}

void NewProjectAudioProcessor::setNonRealtime (bool shouldBeNonRealtime) noexcept
{
    const bool was = isNonRealtime();
    AudioProcessor::setNonRealtime (shouldBeNonRealtime);
    // VST3 hosts call this on every process(); only a real change is worth logging.
    if (was != shouldBeNonRealtime)
        probeEvent ("SET_NON_REALTIME " + juce::String ((int) shouldBeNonRealtime));
}

void NewProjectAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    AudioProcessor::updateTrackProperties (properties);
    if (hostProbe != nullptr)
        hostProbe->setTrackProperties (properties.name.value_or (juce::String()),
                                       properties.colourARGB.has_value()
                                           ? juce::String::toHexString ((int) *properties.colourARGB)
                                           : juce::String ("-"));
}

NewProjectAudioProcessor::~NewProjectAudioProcessor()
{
    probeEvent ("PROCESSOR destroying");

    autoCaptureTimer.stopTimer();
    autoCaptureTimer.callback = nullptr;
    aliveFlag->store (false, std::memory_order_release);
    pluginList.removeChangeListener (this);
    isActive.store (false, std::memory_order_release);
    repreparePending.store (false, std::memory_order_release);

    {
        const juce::ScopedLock sl (slotLock);
        for (auto& slot : slots)
        {
            if (slot.instance != nullptr)
                retirePlugin (std::move (slot.instance));
            slot.description = {};
            slot.bypassed = false;
        }
        publishAudioRuntimeUnlocked();
    }

    // Give the audio thread a moment to drop runtime snapshots, then flush.
    juce::Thread::sleep (30);
    flushRetiredPluginsNow();
    hostProbe.reset();
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
    if (hostProbe != nullptr && hostProbe->expTail.load())
        tail += 2.0;
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
    // hosts) are stereo throughout - do NOT silently fall back to mono for later
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

    // Last resort: leave plugin default but mute sidechain/aux - still require
    // main I/O channel count to match the rack.
    plugin.disableNonMainBuses();
    const int inCh  = plugin.getMainBusNumInputChannels();
    const int outCh = plugin.getMainBusNumOutputChannels();
    return (outCh == hostCh) && (inCh == 0 || inCh == hostCh);
}

void NewProjectAudioProcessor::resyncAllInnerPlugins()
{
    // Insert Basic can change our bus layout / buffer size after the first prepare.
    // Audition usually sets layout once up front - that's why nested comps work there.
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
        const int bs = currentBlockSize.load();
        instance->setPlayConfigDetails (juce::jmax (1, instance->getMainBusNumInputChannels()),
                                        juce::jmax (1, instance->getMainBusNumOutputChannels()),
                                        currentSampleRate, bs);
        instance->setRateAndBufferSizeDetails (currentSampleRate, bs);
        safePrepareInstance (*instance, nullptr);
    }

    {
        const juce::ScopedLock sl (slotLock);
        publishAudioRuntimeUnlocked();
    }
    updateLatency();
}

void NewProjectAudioProcessor::numChannelsChanged()
{
    probeEvent ("NUM_CHANNELS_CHANGED in=" + juce::String (getMainBusNumInputChannels())
                + " out=" + juce::String (getMainBusNumOutputChannels())
                + " totalIn=" + juce::String (getTotalNumInputChannels())
                + " totalOut=" + juce::String (getTotalNumOutputChannels()));
    if (isActive.load (std::memory_order_acquire))
        resyncAllInnerPlugins();
}

void NewProjectAudioProcessor::processorLayoutsChanged()
{
    probeEvent ("LAYOUTS_CHANGED inBuses=" + juce::String (getBusCount (true))
                + " outBuses=" + juce::String (getBusCount (false))
                + " mainIn=" + getChannelLayoutOfBus (true, 0).getDescription()
                + " mainOut=" + getChannelLayoutOfBus (false, 0).getDescription());
    if (isActive.load (std::memory_order_acquire))
        resyncAllInnerPlugins();
}

void NewProjectAudioProcessor::processHostedInstance (juce::AudioPluginInstance& plugin,
                                                      juce::AudioBuffer<float>& rackBuffer,
                                                      juce::MidiBuffer& midi,
                                                      juce::AudioBuffer<float>& scratch,
                                                      int scratchCapacitySamples)
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

    auto runProcess = [&] (juce::AudioBuffer<float>& buf)
    {
        try
        {
            plugin.processBlock (buf, midi);
        }
        catch (...)
        {
            // Never let a nested plugin exception unwind the host audio thread.
            buf.clear();
        }
    };

    // Fast path: main+total match the rack (no sidechain, no mono/stereo adapt).
    if (mainIn == rackCh && mainOut == rackCh
        && totalIn == rackCh && totalOut == rackCh)
    {
        runProcess (rackBuffer);
        return;
    }

    // Audio thread must NEVER realloc scratch. If too small, skip this slot this block.
    if (scratchCapacitySamples < numSamples
        || scratch.getNumSamples() < numSamples
        || scratch.getNumChannels() < totalCh)
    {
        return;
    }

    // Use existing storage only (no setSize / heap on audio thread).
    for (int c = 0; c < totalCh; ++c)
        scratch.clear (c, 0, numSamples);

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

    // The plug-in must see exactly its own channel count and this block's length, not the
    // whole scratch buffer. Built after the copy: a view cleared through itself would stay
    // flagged as silent.
    juce::AudioBuffer<float> io (scratch.getArrayOfWritePointers(), totalCh, numSamples);
    runProcess (io);

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
    // second prepare leave the inner AU with 0 input channels -> silence / no detect.
    const int inCh  = juce::jmax (1, plugin.getMainBusNumInputChannels() > 0
                                         ? plugin.getMainBusNumInputChannels()
                                         : plugin.getTotalNumInputChannels());
    const int outCh = juce::jmax (1, plugin.getMainBusNumOutputChannels() > 0
                                         ? plugin.getMainBusNumOutputChannels()
                                         : plugin.getTotalNumOutputChannels());

    const int bs = currentBlockSize.load();
    plugin.setPlayConfigDetails (inCh, outCh, currentSampleRate, bs);
    plugin.setRateAndBufferSizeDetails (currentSampleRate, bs);
    safePrepareInstance (plugin, nullptr);
}

void NewProjectAudioProcessor::releaseSlot (Slot& slot)
{
    // Prefer retirePlugin() for live instances. This path is for shutdown-only
    // cases where the instance will not be used on the audio thread anymore.
    if (slot.instance != nullptr)
    {
        try
        {
            slot.instance->releaseResources();
        }
        catch (...)
        {
        }
    }
}

bool NewProjectAudioProcessor::safePrepareInstance (juce::AudioPluginInstance& plugin, juce::String* errorOut)
{
    try
    {
        const int bs = currentBlockSize.load (std::memory_order_relaxed);
        plugin.prepareToPlay (currentSampleRate, bs);
        plugin.suspendProcessing (false);
        return true;
    }
    catch (const std::exception& e)
    {
        if (errorOut != nullptr)
            *errorOut = e.what();
    }
    catch (...)
    {
        if (errorOut != nullptr)
            *errorOut = "unknown exception";
    }

    try
    {
        plugin.suspendProcessing (true);
    }
    catch (...)
    {
    }

    return false;
}

void NewProjectAudioProcessor::ensureAdaptScratchCapacity (int numSamples)
{
    // Message / prepare thread only. Publish a brand-new buffer via atomic shared_ptr
    // so the audio thread never observes a mid-realloc AudioBuffer.
    const int need = juce::jmax (numSamples, kMinScratchSamples);
    auto cur = std::atomic_load_explicit (&adaptScratchPublished, std::memory_order_acquire);

    if (cur != nullptr && cur->getNumSamples() >= need && cur->getNumChannels() >= kAdaptScratchChannels)
        return;

    auto next = std::make_shared<juce::AudioBuffer<float>> (kAdaptScratchChannels, need);
    next->clear();
    std::atomic_store_explicit (&adaptScratchPublished, next, std::memory_order_release);
}

void NewProjectAudioProcessor::publishAudioRuntimeUnlocked()
{
    auto rt = std::make_shared<AudioRuntime>();
    for (int i = 0; i < kNumSlots; ++i)
    {
        rt->chain[(size_t) i] = slots[(size_t) i].instance;
        rt->bypass[(size_t) i] = slots[(size_t) i].bypassed;
    }

    rt->scratch = std::atomic_load_explicit (&adaptScratchPublished, std::memory_order_acquire);
    rt->scratchSamples = (rt->scratch != nullptr) ? rt->scratch->getNumSamples() : 0;
    std::atomic_store_explicit (&audioRuntime, rt, std::memory_order_release);
}

void NewProjectAudioProcessor::retirePlugin (std::shared_ptr<juce::AudioPluginInstance> plugin)
{
    if (plugin == nullptr)
        return;

    RetiringPlugin item;
    item.plugin = std::move (plugin);
    item.earliestBlock = audioBlockCounter.load (std::memory_order_acquire) + 3;

    const juce::ScopedLock sl (retireLock);
    retiring.push_back (std::move (item));
}

void NewProjectAudioProcessor::drainRetiredPluginsFromAudioThread()
{
    // Non-blocking: if message thread holds retireLock, skip this block.
    if (! retireLock.tryEnter())
        return;

    const uint64_t now = audioBlockCounter.load (std::memory_order_acquire);
    std::vector<std::shared_ptr<juce::AudioPluginInstance>> ripe;

    for (auto it = retiring.begin(); it != retiring.end();)
    {
        if (now >= it->earliestBlock)
        {
            ripe.push_back (std::move (it->plugin));
            it = retiring.erase (it);
        }
        else
        {
            ++it;
        }
    }

    retireLock.exit();

    if (ripe.empty())
        return;

    auto life = aliveFlag;
    juce::MessageManager::callAsync ([life, ripe = std::move (ripe)]() mutable
    {
        if (! life->load (std::memory_order_acquire))
        {
            ripe.clear();
            return;
        }

        for (auto& p : ripe)
        {
            if (p == nullptr)
                continue;
            try
            {
                p->releaseResources();
            }
            catch (...)
            {
            }
            p.reset();
        }
    });
}

void NewProjectAudioProcessor::flushRetiredPluginsNow()
{
    std::vector<std::shared_ptr<juce::AudioPluginInstance>> all;
    {
        const juce::ScopedLock sl (retireLock);
        for (auto& r : retiring)
            all.push_back (std::move (r.plugin));
        retiring.clear();
    }

    for (auto& p : all)
    {
        if (p == nullptr)
            continue;
        try
        {
            p->releaseResources();
        }
        catch (...)
        {
        }
        p.reset();
    }
}

void NewProjectAudioProcessor::requestInnerReprepare (int blockSize)
{
    if (blockSize <= 0)
        return;

    currentBlockSize.store (blockSize, std::memory_order_relaxed);

    bool expected = false;
    if (! repreparePending.compare_exchange_strong (expected, true, std::memory_order_acq_rel))
        return;

    auto life = aliveFlag;
    juce::MessageManager::callAsync ([life, this, blockSize]
    {
        if (! life->load (std::memory_order_acquire))
            return;

        ensureAdaptScratchCapacity (blockSize);

        juce::String prepError;
        std::array<std::shared_ptr<juce::AudioPluginInstance>, kNumSlots> snap;

        {
            const juce::ScopedLock sl (slotLock);
            for (int i = 0; i < kNumSlots; ++i)
                snap[(size_t) i] = slots[(size_t) i].instance;
        }

        if (isActive.load (std::memory_order_acquire))
        {
            for (auto& instance : snap)
            {
                if (instance == nullptr)
                    continue;

                configureInnerBuses (*instance);
                instance->setPlayConfigDetails (juce::jmax (1, instance->getMainBusNumInputChannels()),
                                                juce::jmax (1, instance->getMainBusNumOutputChannels()),
                                                currentSampleRate, blockSize);
                instance->setRateAndBufferSizeDetails (currentSampleRate, blockSize);

                juce::String err;
                if (! safePrepareInstance (*instance, &err) && prepError.isEmpty())
                    prepError = err;
            }
        }

        {
            const juce::ScopedLock sl (slotLock);
            publishAudioRuntimeUnlocked();
        }

        repreparePending.store (false, std::memory_order_release);
        updateLatency();

        if (prepError.isNotEmpty())
            setUiStatus ("prepareToPlay failed: " + prepError);
    });
}


void NewProjectAudioProcessor::updateLatencyUnlocked()
{
    int total = 0;
    for (auto& slot : slots)
        if (slot.instance != nullptr && ! slot.bypassed)
            total += slot.instance->getLatencySamples();

    if (hostProbe != nullptr && hostProbe->expLatency.load())
        total += 1024;

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
    currentBlockSize.store (samplesPerBlock, std::memory_order_relaxed);
    liveCapture.configure (sampleRate, kMaxCaptureSeconds);
    captureInputSnapshot.setSize (2, juce::jmax (samplesPerBlock, kMinScratchSamples), false, true, false);
    araDryScratch.setSize (juce::jmax (2, getTotalNumInputChannels(), getTotalNumOutputChannels()),
                           juce::jmax (samplesPerBlock, kMinScratchSamples), false, true, false);
    ensureAdaptScratchCapacity (samplesPerBlock);

    juce::String prepError;
    {
        const juce::ScopedLock sl (slotLock);
        isActive.store (true, std::memory_order_release);

        for (auto& slot : slots)
        {
            if (slot.instance == nullptr)
                continue;
            prepareSlot (slot); // uses safePrepare; no UI under lock
        }

        publishAudioRuntimeUnlocked();
        updateLatencyUnlocked();
    }

    repreparePending.store (false, std::memory_order_release);

    if (hostProbe != nullptr)
    {
        hostProbe->setSampleRate (sampleRate);
        probeEvent ("PREPARE sr=" + juce::String (sampleRate, 0) + " block=" + juce::String (samplesPerBlock)
                    + " nonRT=" + juce::String ((int) isNonRealtime())
                    + " in=" + juce::String (getMainBusNumInputChannels())
                    + " out=" + juce::String (getMainBusNumOutputChannels())
                    + " latency=" + juce::String (getLatencySamples())
                    + " tail=" + juce::String (getTailLengthSeconds(), 2));
    }
}

void NewProjectAudioProcessor::releaseResources()
{
    probeEvent ("RELEASE_RESOURCES");
    isActive.store (false, std::memory_order_release);
    lastHostPlaying.store (false);

    // Keep instances in slots (host may prepareToPlay again), but publish isActive=false
    // so processBlock skips nested calls. Do NOT releaseResources on live slot instances here.
    {
        const juce::ScopedLock sl (slotLock);
        publishAudioRuntimeUnlocked();
    }
}

void NewProjectAudioProcessor::reset()
{
    probeEvent ("RESET");
    lastHostPlaying.store (false);
    const juce::ScopedLock sl (slotLock);
    for (auto& slot : slots)
        if (slot.instance != nullptr)
            slot.instance->reset();
}

void NewProjectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int blockN = buffer.getNumSamples();
    const bool active = isActive.load (std::memory_order_acquire);

    juce::int64 blockHostTime = lastHostTimeSamples.load (std::memory_order_relaxed);
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getTimeInSamples())
                blockHostTime = *t;
            lastHostTimeSamples.store (blockHostTime, std::memory_order_relaxed);
            lastHostPlaying.store (pos->getIsPlaying(), std::memory_order_relaxed);
        }
    }
    lastBlockMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

    auto runtime = std::atomic_load_explicit (&audioRuntime, std::memory_order_acquire);
    const int scratchCap = (runtime != nullptr) ? runtime->scratchSamples : 0;

    // Insert Basic may change block size after prepare. Never realloc / prepare here.
    if (active && blockN > 0)
    {
        const int known = currentBlockSize.load (std::memory_order_relaxed);
        if (blockN != known || scratchCap < blockN)
            requestInnerReprepare (blockN);
    }

    const int bufCh = buffer.getNumSamples() > 0 ? buffer.getNumChannels() : 0;
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

    // Inner FX process in place, so the pre-FX insert input must be copied before the chain.
    const bool recording = blockN > 0 && bufCh > 0 && liveCapture.isRecording();
    bool inputSnapshotValid = false;
    float inputBlockPeak = 0.0f;

    if (blockN > 0 && bufCh > 0 && getBusCount (true) > 0)
    {
        auto inputBus = getBusBuffer (buffer, true, 0);
        const int inCh = inputBus.getNumChannels();

        for (int c = 0; c < inCh; ++c)
            inputBlockPeak = juce::jmax (inputBlockPeak, inputBus.getMagnitude (c, 0, blockN));

        if (recording && inCh > 0 && blockN <= captureInputSnapshot.getNumSamples())
        {
            for (int c = 0; c < captureInputSnapshot.getNumChannels(); ++c)
                captureInputSnapshot.copyFrom (c, 0, inputBus, juce::jmin (c, inCh - 1), 0, blockN);
            inputSnapshotValid = true;
        }
    }

    for (int i = numIn; i < numOut && i < bufCh; ++i)
        buffer.clear (i, 0, blockN);

    // While reprepare is in flight, pass audio through - do not call nested processBlock
    // with a mismatched prepared size.
    const bool skipInners = (! active)
                            || repreparePending.load (std::memory_order_acquire)
                            || runtime == nullptr
                            || runtime->scratch == nullptr
                            || runtime->scratchSamples < blockN;

    if (! skipInners)
    {
        auto* hostPH = getPlayHead();
        auto& scratch = *runtime->scratch;

        for (int i = 0; i < kNumSlots; ++i)
        {
            if (runtime->chain[(size_t) i] == nullptr || runtime->bypass[(size_t) i])
                continue;

           #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
            if (auto* ara = asARAWrapper (runtime->chain[(size_t) i].get()))
            {
                syncARAPlayHeadFromHost (*ara, hostPH, blockN);

                // Outside the bound clip (or while the DAW is stopped) the track stays dry.
                const auto gate = ara->getRegionGate (hostPH, blockN);
                if (! gate.process)
                    continue;

                const bool partial = gate.from > 0 || gate.to < blockN;
                if (partial && araDryScratch.getNumSamples() >= blockN && araDryScratch.getNumChannels() >= bufCh)
                {
                    for (int c = 0; c < bufCh; ++c)
                        araDryScratch.copyFrom (c, 0, buffer, c, 0, blockN);

                    processHostedInstance (*runtime->chain[(size_t) i], buffer, midi, scratch, runtime->scratchSamples);

                    for (int c = 0; c < bufCh; ++c)
                    {
                        if (gate.from > 0)
                            buffer.copyFrom (c, 0, araDryScratch, c, 0, gate.from);
                        if (gate.to < blockN)
                            buffer.copyFrom (c, gate.to, araDryScratch, c, gate.to, blockN - gate.to);
                    }
                    continue;
                }
            }
           #endif

            processHostedInstance (*runtime->chain[(size_t) i], buffer, midi, scratch, runtime->scratchSamples);
        }
    }

    float outputBlockPeak = 0.0f;
    juce::AudioBuffer<float> outputView;

    if (blockN > 0 && bufCh > 0)
    {
        outputView = getBusCount (false) > 0 ? getBusBuffer (buffer, false, 0)
                                             : juce::AudioBuffer<float> (buffer.getArrayOfWritePointers(),
                                                                         bufCh, blockN);
        if (outputView.getNumChannels() == 0)
            outputView = juce::AudioBuffer<float> (buffer.getArrayOfWritePointers(), bufCh, blockN);

        for (int c = 0; c < outputView.getNumChannels(); ++c)
            outputBlockPeak = juce::jmax (outputBlockPeak, outputView.getMagnitude (c, 0, blockN));
    }

    liveInputPeak.store (inputBlockPeak, std::memory_order_relaxed);
    liveOutputPeak.store (outputBlockPeak, std::memory_order_relaxed);

    if (hostProbe != nullptr && blockN > 0)
    {
        ChouChouHostProbe::BlockInfo info;
        info.numSamples = blockN;
        info.numChannels = bufCh;
        info.nonRealtime = isNonRealtime();
        info.inputPeak = inputBlockPeak;
        info.outputPeak = outputBlockPeak;
        ChouChouHostProbe::readPlayhead (getPlayHead(), info);

        const bool runStart = hostProbe->logBlock (info);

        // Alignment marker: a click at sample 0 of each run shows where the host writes our output.
        if (runStart && hostProbe->expMarkOutput.load (std::memory_order_relaxed))
            for (int c = 0; c < outputView.getNumChannels(); ++c)
                outputView.setSample (c, 0, 0.5f);
    }

    // Record the chouchou insert point. The source is locked for the whole take so dry and
    // processed audio are never spliced together; it stays undecided until a block is audible.
    if (recording)
    {
        const auto thr = LiveCaptureBuffer::kSilenceThreshold;
        auto source = (CaptureSource) captureSource.load (std::memory_order_relaxed);

        if (source == CaptureSource::Undecided)
        {
            if (outputBlockPeak >= thr)
                source = CaptureSource::Output;
            else if (inputSnapshotValid && inputBlockPeak >= thr)
                source = CaptureSource::Input;

            if (source != CaptureSource::Undecided)
                captureSource.store ((int) source, std::memory_order_relaxed);
        }

        if (source != CaptureSource::Undecided && liveCapture.getNumSamplesCaptured() == 0)
            takeStartSample.store (blockHostTime, std::memory_order_relaxed);

        if (source == CaptureSource::Input && inputSnapshotValid)
            liveCapture.push (captureInputSnapshot.getArrayOfReadPointers(),
                              captureInputSnapshot.getNumChannels(), blockN);
        else if (source != CaptureSource::Input)
            liveCapture.push (outputView);
    }

    audioBlockCounter.fetch_add (1, std::memory_order_acq_rel);
    drainRetiredPluginsFromAudioThread();
}

//==============================================================================
void NewProjectAudioProcessor::setUiStatus (const juce::String& text)
{
    {
        const juce::ScopedLock sl (statusLock);
        uiStatus = text;
    }

    probeEvent ("STATUS " + text);
    notifySlotsChanged();
}

juce::String NewProjectAudioProcessor::getUiStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return uiStatus;
}

ChouChouHostMode NewProjectAudioProcessor::getHostMode() const noexcept
{
    return detectChouChouHostMode (isChouChouRunningAsStandaloneApp());
}

ChouChouDiagnosticReport NewProjectAudioProcessor::getLastDiagnostic() const
{
    const juce::ScopedLock sl (statusLock);
    return lastDiagnostic;
}

void NewProjectAudioProcessor::copyLastDiagnosticToClipboard() const
{
    getLastDiagnostic().copyToClipboard();
}

//==============================================================================

void NewProjectAudioProcessor::loadPluginAsync (int slotIndex, const juce::PluginDescription& desc)
{
    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
        return;

    if (isSelfDescription (desc))
    {
        setUiStatus ("Cannot load chouchouPlugplugin2 inside itself.");
        return;
    }

    juce::String why;
    const bool unsupported = isUnsupportedPluginType (desc, why);
    if (unsupported)
    {
        ChouChouDiagnosticReport report;
        report.mode = getHostMode();
        report.errorCode = why.startsWith ("INNER_LOAD_REJECTED") ? "INNER_LOAD_REJECTED" : "UNSUPPORTED_PLUGIN";
        report.message = why;
        report.pluginName = desc.name;
        report.pluginUid = juce::String (desc.uniqueId);
        report.hostProcess = juce::PluginHostType().getHostDescription();
        {
            const juce::ScopedLock sl (statusLock);
            lastDiagnostic = report;
        }
        setUiStatus (why + "  [Copy Report]");
        return;
    }

    if (pluginList.getBlacklistedFiles().contains (desc.fileOrIdentifier))
    {
        setUiStatus ("Plugin is blacklisted (previously crashed during scan).");
        return;
    }

    // Never block the host message thread with createPluginInstance -
    // Insert Basic (systemwide AU host) freezes/hangs on sync nested loads.
    // Use async create; status line only (no AlertWindow).
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const int bs = juce::jmax (1, currentBlockSize.load());

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
        [life, this, slotIndex, requestId, pluginName = desc.name, pd]
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

            const bool isARA = pluginDescriptionHasARA (instance->getPluginDescription())
                               || pluginDescriptionHasARA (pd);
            instance = maybeWrapWithARAHost (std::move (instance));

            if (isARA && asARAWrapper (instance.get()) == nullptr)
            {
                setUiStatus ("Load failed: ARA-only plugin (e.g. SpectraLayers) needs the embedded ARA host.");
                return;
            }

            auto shared = std::shared_ptr<juce::AudioPluginInstance> (std::move (instance));

            {
                const juce::ScopedLock sl (slotLock);
                if (! isLoadStillCurrent (slotIndex, requestId))
                {
                    // Drop local shared - do not touch slots. Clear/load already invalidated us.
                    setUiStatus ("Load cancelled.");
                    return;
                }

                auto& slot = slots[(size_t) slotIndex];
                auto previous = std::move (slot.instance);
                slot.description = shared->getPluginDescription();
                slot.instance = shared; // keep local shared for post-lock ARA work
                slot.bypassed = false;

                if (isActive.load (std::memory_order_acquire))
                    prepareSlot (slot);

                publishAudioRuntimeUnlocked();
                updateLatencyUnlocked();

                // Defer release of the previous plugin until audio has dropped it.
                if (previous != nullptr)
                    retirePlugin (std::move (previous));
            }

            // Use the same shared_ptr we installed - never re-fetch by index (clear/load race).
            if (isARA)
            {
               #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
                juce::String err;
                bool stillOurs = false;
                {
                    const juce::ScopedLock sl (slotLock);
                    stillOurs = isLoadStillCurrent (slotIndex, requestId)
                                && slots[(size_t) slotIndex].instance == shared;
                }

                if (stillOurs)
                {
                    if (auto* ara = asARAWrapper (shared.get()))
                    {
                        if (assignLiveCaptureToARA (*ara, liveCapture, err))
                            setUiStatus ("Loaded ARA + live capture: " + shared->getName());
                        else
                            setUiStatus ("Loaded ARA (capture pending): " + err);
                    }
                    else
                        setUiStatus ("Loaded ARA: " + shared->getName() + " - use Capture when ready");
                }
               #else
                setUiStatus ("Loaded: " + shared->getName() + " (ARA host not compiled)");
               #endif
            }
            else
            {
                setUiStatus ("Loaded: " + shared->getName());
            }

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
        error = "Cannot load chouchouPlugplugin2 inside itself.";
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
    const int bs = juce::jmax (1, currentBlockSize.load());

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

    instance = maybeWrapWithARAHost (std::move (instance));

    if (pluginDescriptionHasARA (desc) && asARAWrapper (instance.get()) == nullptr)
    {
        error = "ARA-only plugin (e.g. SpectraLayers) needs the embedded ARA host.";
        return false;
    }

    auto shared = std::shared_ptr<juce::AudioPluginInstance> (std::move (instance));

    {
        const juce::ScopedLock sl (slotLock);
        beginSlotLoad (slotIndex);
        auto& slot = slots[(size_t) slotIndex];
        auto previous = std::move (slot.instance);
        slot.description = shared->getPluginDescription();
        slot.instance = shared;
        slot.bypassed = false;

        if (isActive.load (std::memory_order_acquire))
            prepareSlot (slot);

        publishAudioRuntimeUnlocked();
        updateLatencyUnlocked();

        if (previous != nullptr)
            retirePlugin (std::move (previous));
    }

    notifySlotsChanged();
    return true;
}

void NewProjectAudioProcessor::clearSlot (int slotIndex)
{
    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
        return;

    std::shared_ptr<juce::AudioPluginInstance> doomed;

    {
        const juce::ScopedLock sl (slotLock);
        beginSlotLoad (slotIndex); // invalidate any in-flight load for this slot
        auto& slot = slots[(size_t) slotIndex];
        doomed = std::move (slot.instance);
        slot.description = {};
        slot.bypassed = false;
        publishAudioRuntimeUnlocked();
        updateLatencyUnlocked();
    }

    retirePlugin (std::move (doomed));
    notifySlotsChanged();
}

void NewProjectAudioProcessor::setSlotBypassed (int slotIndex, bool shouldBypass)
{
    if (! juce::isPositiveAndBelow (slotIndex, kNumSlots))
        return;

    {
        const juce::ScopedLock sl (slotLock);
        slots[(size_t) slotIndex].bypassed = shouldBypass;
        publishAudioRuntimeUnlocked();
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
    probeEvent ("GET_STATE bytes=" + juce::String ((int) destData.getSize()));
}

void NewProjectAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    probeEvent ("SET_STATE bytes=" + juce::String (sizeInBytes));
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (kStateTag))
        return;

    const uint64_t thisRestore = restoreGeneration.fetch_add (1, std::memory_order_relaxed) + 1;

    // Single batch clear - retire old instances; don't releaseResources under the audio race window.
    std::array<std::shared_ptr<juce::AudioPluginInstance>, NewProjectAudioProcessor::kNumSlots> doomed {};
    {
        const juce::ScopedLock sl (slotLock);
        for (int i = 0; i < kNumSlots; ++i)
        {
            beginSlotLoad (i);
            doomed[(size_t) i] = std::move (slots[(size_t) i].instance);
            slots[(size_t) i].description = {};
            slots[(size_t) i].bypassed = false;
        }
        publishAudioRuntimeUnlocked();
        updateLatencyUnlocked();
    }
    for (auto& p : doomed)
        retirePlugin (std::move (p));
    notifySlotsChanged();

    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const int bs = currentBlockSize.load() > 0 ? currentBlockSize.load() : 512;
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

                    instance = maybeWrapWithARAHost (std::move (instance));

                    if (pluginDescriptionHasARA (instance->getPluginDescription())
                        && asARAWrapper (instance.get()) == nullptr)
                        return;

                    if (! innerState.isEmpty())
                        instance->setStateInformation (innerState.getData(), (int) innerState.getSize());

                    auto shared = std::shared_ptr<juce::AudioPluginInstance> (std::move (instance));

                    {
                        const juce::ScopedLock sl (slotLock);
                        if (! isLoadStillCurrent (index, requestId))
                            return;

                        auto& slot = slots[(size_t) index];
                        auto previous = std::move (slot.instance);
                        slot.description = shared->getPluginDescription();
                        slot.instance = shared;
                        slot.bypassed = bypassed;

                        if (isActive.load (std::memory_order_acquire))
                            prepareSlot (slot);

                        publishAudioRuntimeUnlocked();
                        updateLatencyUnlocked();

                        if (previous != nullptr)
                            retirePlugin (std::move (previous));
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
bool NewProjectAudioProcessor::isSlotARA (int slotIndex) const
{
    if (auto inst = getSlotInstanceShared (slotIndex))
        return pluginDescriptionHasARA (inst->getPluginDescription())
           #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
            || asARAWrapper (inst.get()) != nullptr
           #endif
            ;
    return false;
}

bool NewProjectAudioProcessor::isBridgeRecording() const noexcept
{
    return liveCapture.isRecording();
}

NewProjectAudioProcessor::CaptureUxState NewProjectAudioProcessor::getCaptureUxState() const noexcept
{
    if (liveCapture.isRecording())
        return CaptureUxState::Recording;

    // Covers Stop with audio, buffer overflow, and a rate change mid-take.
    return takePending.load (std::memory_order_acquire) ? CaptureUxState::ReadyToSave
                                                        : CaptureUxState::Idle;
}

void NewProjectAudioProcessor::recordCaptureDiagnostic (ChouChouCaptureState state, const juce::String& code,
                                                        const juce::String& message, const juce::File& wav)
{
    ChouChouDiagnosticReport report;
    report.mode = getHostMode();
    report.captureState = state;
    report.errorCode = code;
    report.message = message;
    report.hostProcess = juce::PluginHostType().getHostDescription();
    if (wav != juce::File())
    {
        report.sourceWav = wav.getFullPathName();
        report.sessionJson = wav.getFullPathName() + ".session.json";
    }

    const juce::ScopedLock sl (statusLock);
    lastDiagnostic = report;
}

bool NewProjectAudioProcessor::startBridgeRecording (juce::String& error)
{
    const auto state = getCaptureUxState();

    if (state == CaptureUxState::Recording)
    {
        error = "Already recording.";
        return false;
    }

    if (state == CaptureUxState::ReadyToSave)
    {
        error = "A take is waiting - Save... or Discard it before recording again.";
        setUiStatus (error);
        return false;
    }

    captureSource.store ((int) CaptureSource::Undecided, std::memory_order_relaxed);

    if (! liveCapture.beginTake())
    {
        error = "CAPTURE_ALLOC_FAILED: not enough memory for a "
                + juce::String (kMaxCaptureSeconds) + " s take.";
        recordCaptureDiagnostic (ChouChouCaptureState::Failed, "CAPTURE_ALLOC_FAILED", error);
        setUiStatus (error + "  [Copy Report]");
        return false;
    }

    takePending.store (true, std::memory_order_release);
    error.clear();
    setUiStatus ("Recording the chouchou insert point... press Stop when done.");
    return true;
}

bool NewProjectAudioProcessor::stopBridgeRecording (juce::String& error)
{
    liveCapture.stopAndSync();

    const auto samples = liveCapture.getNumSamplesCaptured();
    if (samples <= 0)
    {
        error = "CAPTURE_EMPTY: nothing reached the insert - press Rec, play the track that "
                "chouchou is inserted on, then Stop.";
        recordCaptureDiagnostic (ChouChouCaptureState::Failed, "CAPTURE_EMPTY", error);
        discardBridgeTake();
        setUiStatus (error + "  [Copy Report]");
        return false;
    }

    if (liveCapture.getPeakAbs() < LiveCaptureBuffer::kSilenceThreshold)
    {
        error = "CAPTURE_SILENT: recorded "
                + juce::String (samples / liveCapture.getSampleRate(), 1)
                + " s but everything stayed below -100 dBFS. "
                  "Check the track is playing into this insert (not muted/bypassed).";
        recordCaptureDiagnostic (ChouChouCaptureState::Failed, "CAPTURE_SILENT", error);
        discardBridgeTake();
        setUiStatus (error + "  [Copy Report]");
        return false;
    }

    if (liveCapture.didOverflow())
        recordCaptureDiagnostic (ChouChouCaptureState::Incomplete, "CAPTURE_INCOMPLETE",
                                 "Buffer filled after " + juce::String (kMaxCaptureSeconds)
                                     + " s; the take up to that point is kept.");

    error.clear();
    notifySlotsChanged();
    return true;
}

void NewProjectAudioProcessor::discardBridgeTake()
{
    liveCapture.discardTake();
    captureSource.store ((int) CaptureSource::Undecided, std::memory_order_relaxed);
    takePending.store (false, std::memory_order_release);
    notifySlotsChanged();
}

juce::File NewProjectAudioProcessor::getDefaultCaptureDirectory() const
{
    if (auto* settings = const_cast<juce::ApplicationProperties&> (appProperties).getUserSettings())
    {
        const auto lastDir = settings->getValue ("lastCaptureDir");
        if (lastDir.isNotEmpty())
        {
            juce::File d (lastDir);
            if (d.isDirectory())
                return d;
        }
    }

    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("chouchouPlugplugin2_ARA");
    dir.createDirectory();
    return dir;
}

juce::File NewProjectAudioProcessor::getLastCaptureWav() const
{
    if (auto* settings = const_cast<juce::ApplicationProperties&> (appProperties).getUserSettings())
    {
        const auto path = settings->getValue ("lastCaptureWav");
        if (path.isNotEmpty())
        {
            juce::File f (path);
            if (f.existsAsFile())
                return f;
        }
    }
    return {};
}

void NewProjectAudioProcessor::rememberCaptureWav (const juce::File& wav)
{
    if (auto* settings = appProperties.getUserSettings())
    {
        settings->setValue ("lastCaptureWav", wav.getFullPathName());
        settings->setValue ("lastCaptureDir", wav.getParentDirectory().getFullPathName());
        settings->saveIfNeeded();
    }
}

bool NewProjectAudioProcessor::writeSpectraLayersSessionSidecar (const juce::File& wav, float peak) const
{
    const auto sessionJson = juce::File (wav.getFullPathName() + ".session.json");
    const auto source = getCaptureSource();

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("schemaVersion", 2);
    root->setProperty ("origin", "CapturedFromAudition");
    root->setProperty ("targetPlugin", "SpectraLayers");
    root->setProperty ("sourceWav", wav.getFullPathName());
    root->setProperty ("captureSource", source == CaptureSource::Input ? "insertInputPreFX"
                                                                       : "insertOutput");
    root->setProperty ("autoLoad", true);
    root->setProperty ("openEditor", true);
    root->setProperty ("sampleRate", liveCapture.getSampleRate());
    root->setProperty ("channels", liveCapture.getNumChannels());
    root->setProperty ("sampleCount", (int) liveCapture.getNumSamplesCaptured());
    root->setProperty ("peakAbs", peak);
    root->setProperty ("overflow", liveCapture.didOverflow());
    return sessionJson.replaceWithText (juce::JSON::toString (juce::var (root.get())));
}

bool NewProjectAudioProcessor::finalizeNamedCapture (const juce::File& wavFile, juce::String& errorOrPath)
{
    if (liveCapture.isRecording())
        liveCapture.stopAndSync();

    // Content is always WAV, so the name must say so.
    const auto wav = wavFile.withFileExtension (".wav");

    if (liveCapture.getNumSamplesCaptured() <= 0)
    {
        errorOrPath = "CAPTURE_EMPTY: nothing to save.";
        setUiStatus (errorOrPath);
        return false;
    }

    const float peak = liveCapture.getPeakAbs();
    if (peak < LiveCaptureBuffer::kSilenceThreshold)
    {
        errorOrPath = "CAPTURE_SILENT: refusing to save a take below -100 dBFS.";
        setUiStatus (errorOrPath);
        return false;
    }

    if (! liveCapture.writeToWavFile (wav))
    {
        errorOrPath = "CAPTURE_WRITE_FAILED: could not write " + wav.getFullPathName()
                      + " - take kept, choose another folder and Save... again.";
        recordCaptureDiagnostic (ChouChouCaptureState::Failed, "CAPTURE_WRITE_FAILED", errorOrPath, wav);
        setUiStatus (errorOrPath + "  [Copy Report]");
        return false;
    }

    if (! writeSpectraLayersSessionSidecar (wav, peak))
    {
        errorOrPath = "SESSION_WRITE_FAILED: WAV saved but " + wav.getFileName()
                      + ".session.json could not be written - take kept, Save... again.";
        recordCaptureDiagnostic (ChouChouCaptureState::Failed, "SESSION_WRITE_FAILED", errorOrPath, wav);
        setUiStatus (errorOrPath + "  [Copy Report]");
        return false;
    }

    rememberCaptureWav (wav);
    recordCaptureDiagnostic (ChouChouCaptureState::WavReady, {}, "Named capture ready for SpectraLayers.", wav);

    liveCapture.discardTake();
    captureSource.store ((int) CaptureSource::Undecided, std::memory_order_relaxed);
    takePending.store (false, std::memory_order_release);

    errorOrPath = wav.getFullPathName();
    setUiStatus ("Saved " + wav.getFileName() + "  |  Open in SL... to pick this (or another) WAV");
    return true;
}

bool NewProjectAudioProcessor::finalizeBridgeCapture (juce::String& errorOrPath)
{
    // Legacy: write default-named file into default directory.
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    auto wav = getDefaultCaptureDirectory().getChildFile ("capture_" + stamp + ".wav");
    return finalizeNamedCapture (wav, errorOrPath);
}

bool NewProjectAudioProcessor::findSpectraLayersDescription (juce::PluginDescription& out) const
{
    for (const auto& d : pluginList.getTypes())
    {
        if (! d.name.containsIgnoreCase ("SpectraLayers")
            && ! d.fileOrIdentifier.containsIgnoreCase ("SpectraLayers"))
            continue;

        if (! d.pluginFormatName.containsIgnoreCase ("VST3"))
            continue;

        out = d;
        return true;
    }
    return false;
}

void NewProjectAudioProcessor::beginSpectraLayersOpenWithSession (const juce::File& sessionJson)
{
    if (! sessionJson.existsAsFile())
    {
        setUiStatus ("SESSION_MISSING: " + sessionJson.getFullPathName());
        return;
    }

    auto parsed = juce::JSON::parse (sessionJson);
    if (auto* obj = parsed.getDynamicObject())
    {
        const auto target = obj->getProperty ("targetPlugin").toString();
        if (target.isNotEmpty() && ! target.containsIgnoreCase ("SpectraLayers"))
        {
            setUiStatus ("SESSION_TARGET: expected SpectraLayers, got " + target);
            return;
        }

        const auto wavPath = obj->getProperty ("sourceWav").toString();
        if (wavPath.isNotEmpty())
        {
            beginSpectraLayersOpenWithWav (juce::File (wavPath));
            return;
        }
    }

    setUiStatus ("SESSION_INVALID: no sourceWav in " + sessionJson.getFileName());
}

void NewProjectAudioProcessor::beginSpectraLayersOpenWithWav (const juce::File& wav)
{
    if (! allowsInnerAraPluginLoad (getHostMode()))
    {
        setUiStatus ("INNER_LOAD_REJECTED: SpectraLayers open only in Standalone.");
        return;
    }

    if (! wav.existsAsFile())
    {
        ChouChouDiagnosticReport report;
        report.mode = getHostMode();
        report.errorCode = "WAV_MISSING";
        report.message = "WAV not found: " + wav.getFullPathName();
        report.hostProcess = juce::PluginHostType().getHostDescription();
        {
            const juce::ScopedLock sl (statusLock);
            lastDiagnostic = report;
        }
        setUiStatus (report.message + "  [Copy Report]");
        return;
    }

    pendingSpectraLayersWav = wav;
    spectraLayersOpenAttempts = 0;
    spectraLayersLoadStarted = false;
    setUiStatus ("Opening SpectraLayers with " + wav.getFileName() + "...");
    pollPendingSpectraLayersOpen();
}

void NewProjectAudioProcessor::pollPendingSpectraLayersOpen()
{
    if (! pendingSpectraLayersWav.existsAsFile())
        return;

    if (! allowsInnerAraPluginLoad (getHostMode()))
        return;

    juce::PluginDescription slDesc;
    if (! findSpectraLayersDescription (slDesc))
    {
        ChouChouDiagnosticReport report;
        report.mode = getHostMode();
        report.errorCode = "SL_NOT_SCANNED";
        report.message = "SpectraLayers VST3 not in list - Scan VST3 first, then reopen with --wav.";
        report.sourceWav = pendingSpectraLayersWav.getFullPathName();
        report.hostProcess = juce::PluginHostType().getHostDescription();
        {
            const juce::ScopedLock sl (statusLock);
            lastDiagnostic = report;
        }
        setUiStatus (report.message + "  [Copy Report]");
        pendingSpectraLayersWav = juce::File();
        return;
    }

    if (! spectraLayersLoadStarted)
    {
        spectraLayersLoadStarted = true;
        loadPluginAsync (0, slDesc);
    }

   #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
    auto inst = getSlotInstanceShared (0);
    auto* ara = asARAWrapper (inst.get());
    if (ara == nullptr)
    {
        if (++spectraLayersOpenAttempts < 40)
        {
            juce::Timer::callAfterDelay (250, [safe = juce::WeakReference<NewProjectAudioProcessor> (this)]
            {
                if (safe != nullptr)
                    safe->pollPendingSpectraLayersOpen();
            });
        }
        else
        {
            setUiStatus ("SL_LOAD_TIMEOUT: SpectraLayers did not become ARA-ready.  [Copy Report]");
            pendingSpectraLayersWav = juce::File();
        }
        return;
    }

    juce::String err;
    if (! assignFileToARA (*ara, pendingSpectraLayersWav, err))
    {
        if (err.containsIgnoreCase ("still starting") && ++spectraLayersOpenAttempts < 40)
        {
            juce::Timer::callAfterDelay (250, [safe = juce::WeakReference<NewProjectAudioProcessor> (this)]
            {
                if (safe != nullptr)
                    safe->pollPendingSpectraLayersOpen();
            });
            return;
        }

        ChouChouDiagnosticReport report;
        report.mode = getHostMode();
        report.errorCode = "ARA_ASSIGN_FAILED";
        report.message = err;
        report.sourceWav = pendingSpectraLayersWav.getFullPathName();
        report.pluginName = "SpectraLayers";
        report.hostProcess = juce::PluginHostType().getHostDescription();
        {
            const juce::ScopedLock sl (statusLock);
            lastDiagnostic = report;
        }
        setUiStatus (err + "  [Copy Report]");
        pendingSpectraLayersWav = juce::File();
        return;
    }

    rememberCaptureWav (pendingSpectraLayersWav);
    const auto boundWav = pendingSpectraLayersWav;
    pendingSpectraLayersWav = juce::File();

    setUiStatus ("SpectraLayers bound: " + boundWav.getFileName() + " - opening UI...");

    // Open plugin editor once document is ready (Standalone only path).
    if (inst != nullptr && inst->hasEditor())
    {
        auto* editor = inst->createEditorAndMakeActive();
        if (editor != nullptr)
        {
            class SlEditorWindow final : public juce::DocumentWindow
            {
            public:
                SlEditorWindow (const juce::String& title)
                    : DocumentWindow (title, juce::Colours::darkgrey, DocumentWindow::allButtons)
                {
                    setUsingNativeTitleBar (true);
                    setResizable (true, false);
                }

                void closeButtonPressed() override
                {
                    setVisible (false);
                }
            };

            auto win = std::make_unique<SlEditorWindow> ("SpectraLayers - " + boundWav.getFileName());
            win->setContentOwned (editor, true);
            win->centreWithSize (juce::jmax (800, editor->getWidth()),
                                 juce::jmax (600, editor->getHeight()));
            win->setVisible (true);
            win->toFront (true);
            spectraLayersEditorWindow = std::move (win);
        }
    }
   #else
    setUiStatus ("Built without JUCE_PLUGINHOST_ARA");
    pendingSpectraLayersWav = juce::File();
   #endif
}

bool NewProjectAudioProcessor::captureLiveAudioToARASlot (int slotIndex, juce::String& error)
{
    // In Bridge mode (Audition), never require an inner ARA slot - write session only.
    if (! allowsInnerAraPluginLoad (getHostMode()))
        return finalizeBridgeCapture (error);

   #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
    auto inst = getSlotInstanceShared (slotIndex);
    auto* ara = asARAWrapper (inst.get());
    if (ara == nullptr)
        return finalizeBridgeCapture (error);

    juce::String pathOrError;
    if (! assignLiveCaptureToARA (*ara, liveCapture, pathOrError))
    {
        error = pathOrError;
        ChouChouDiagnosticReport report;
        report.mode = getHostMode();
        report.captureState = ChouChouCaptureState::Failed;
        report.errorCode = "ARA_ASSIGN_FAILED";
        report.message = error;
        report.hostProcess = juce::PluginHostType().getHostDescription();
        {
            const juce::ScopedLock sl (statusLock);
            lastDiagnostic = report;
        }
        return false;
    }

    const auto wav = juce::File (pathOrError);
    if (wav.existsAsFile())
    {
        rememberCaptureWav (wav);
        wav.revealToUser();
    }

    setUiStatus ("Captured -> " + pathOrError
                 + "  |  Open in SpectraLayers... to choose this WAV");
    error = pathOrError;
    return true;
   #else
    juce::ignoreUnused (slotIndex);
    return finalizeBridgeCapture (error);
   #endif
}

bool NewProjectAudioProcessor::assignFileToARASlot (int slotIndex, const juce::File& file, juce::String& error)
{
   #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
    auto inst = getSlotInstanceShared (slotIndex);
    auto* ara = asARAWrapper (inst.get());
    if (ara == nullptr)
    {
        error = "Slot is not an ARA-hosted plugin.";
        return false;
    }

    if (! assignFileToARA (*ara, file, error))
        return false;

    setUiStatus ("ARA file: " + file.getFileName());
    return true;
   #else
    juce::ignoreUnused (slotIndex, file);
    error = "Built without JUCE_PLUGINHOST_ARA";
    return false;
   #endif
}

std::unique_ptr<juce::AudioProcessorEditor> NewProjectAudioProcessor::createARAHostControlEditor (int slotIndex)
{
   #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
    auto inst = getSlotInstanceShared (slotIndex);
    if (auto* ara = asARAWrapper (inst.get()))
        return std::unique_ptr<juce::AudioProcessorEditor> (ara->createARAHostEditor());
   #else
    juce::ignoreUnused (slotIndex);
   #endif
    return nullptr;
}

//==============================================================================
bool NewProjectAudioProcessor::isHostPlaying() const noexcept
{
    // Audition stops calling processBlock on Stop without a final isPlaying=false block.
    const auto age = juce::Time::getMillisecondCounter() - lastBlockMs.load (std::memory_order_relaxed);
    return lastHostPlaying.load (std::memory_order_relaxed) && age < 400;
}

int NewProjectAudioProcessor::findFirstARASlot() const
{
    for (int i = 0; i < kNumSlots; ++i)
        if (isSlotARA (i))
            return i;
    return -1;
}

bool NewProjectAudioProcessor::getSlotRegion (int slotIndex, double& startSeconds, double& lengthSeconds) const
{
   #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
    auto inst = getSlotInstanceShared (slotIndex);
    if (auto* ara = asARAWrapper (inst.get()))
    {
        lengthSeconds = ara->getBoundSourceSeconds();
        const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
        startSeconds = (double) ara->getRegionStartSample() / sr;
        return lengthSeconds > 0.0;
    }
   #else
    juce::ignoreUnused (slotIndex);
   #endif
    startSeconds = lengthSeconds = 0.0;
    return false;
}

juce::File NewProjectAudioProcessor::ensureWavForARA (const juce::File& source, juce::String& error) const
{
    if (! source.existsAsFile())
    {
        error = "File not found: " + source.getFullPathName();
        return {};
    }

    // The ARA audio source reads through a memory-mapped WAV reader.
    if (source.hasFileExtension ("wav"))
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::MemoryMappedAudioFormatReader> probe (wav.createMemoryMappedReader (source));
        if (probe != nullptr)
            return source;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr)
    {
        error = "Unsupported audio file: " + source.getFileName();
        return {};
    }

    auto dir = getDefaultCaptureDirectory().getChildFile ("imports");
    if (! dir.createDirectory())
    {
        error = "Cannot create " + dir.getFullPathName();
        return {};
    }

    auto target = dir.getChildFile (source.getFileNameWithoutExtension() + ".wav").getNonexistentSibling();
    std::unique_ptr<juce::OutputStream> out (target.createOutputStream());
    if (out == nullptr)
    {
        error = "Cannot write " + target.getFullPathName();
        return {};
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.get(), reader->sampleRate, reader->numChannels, 32, {}, 0));
    if (writer == nullptr)
    {
        error = "Cannot create WAV writer for " + source.getFileName();
        return {};
    }
    out.release();

    if (! writer->writeFromAudioReader (*reader, 0, -1) || ! writer->flush())
    {
        writer.reset();
        target.deleteFile();
        error = "Converting " + source.getFileName() + " to WAV failed.";
        return {};
    }

    return target;
}

bool NewProjectAudioProcessor::bindFileToSlot (int slotIndex, const juce::File& wav, juce::int64 hostStart,
                                               juce::String& error)
{
   #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
    auto inst = getSlotInstanceShared (slotIndex);
    auto* ara = asARAWrapper (inst.get());
    if (ara == nullptr)
    {
        error = "Slot " + juce::String (slotIndex + 1) + " is not an ARA plug-in.";
        return false;
    }

    const auto previousStart = ara->getRegionStartSample();
    ara->setRegionStartSample (hostStart);

    if (! assignFileToARA (*ara, wav, error))
    {
        ara->setRegionStartSample (previousStart);
        return false;
    }

    rememberCaptureWav (wav);
    pendingOpenUiSlot.store (slotIndex);
    notifySlotsChanged();
    return true;
   #else
    juce::ignoreUnused (slotIndex, wav, hostStart);
    error = "Built without JUCE_PLUGINHOST_ARA";
    return false;
   #endif
}

void NewProjectAudioProcessor::importFileToSpectraLayers (const juce::File& file, juce::int64 hostStartSample,
                                                          const juce::String& note)
{
    juce::String err;
    const auto wav = ensureWavForARA (file, err);
    if (wav == juce::File())
    {
        setUiStatus (err);
        return;
    }

    pendingImport = {};
    pendingImport.wav = wav;
    pendingImport.hostStart = hostStartSample;
    pendingImport.slot = findFirstARASlot();
    pendingImport.note = note;
    setUiStatus ("Sending " + file.getFileName() + " to SpectraLayers...");
    pollPendingImport();
}

void NewProjectAudioProcessor::pollPendingImport()
{
    if (pendingImport.wav == juce::File())
        return;

    auto retryLater = [this]
    {
        if (++pendingImport.attempts >= 60)
        {
            setUiStatus ("SL_LOAD_TIMEOUT: SpectraLayers did not become ready - try the import again.");
            pendingImport = {};
            return;
        }

        juce::Timer::callAfterDelay (250, [safe = juce::WeakReference<NewProjectAudioProcessor> (this)]
        {
            if (safe != nullptr)
                safe->pollPendingImport();
        });
    };

    int slot = pendingImport.slot;
    if (slot < 0 || ! isSlotARA (slot))
        slot = findFirstARASlot();

    if (slot < 0)
    {
        if (! pendingImport.loadStarted)
        {
            juce::PluginDescription slDesc;
            if (! findSpectraLayersDescription (slDesc))
            {
                setUiStatus ("SL_NOT_SCANNED: pick VST3 -> Scan first, then import again.");
                pendingImport = {};
                return;
            }

            int empty = -1;
            for (int i = 0; i < kNumSlots && empty < 0; ++i)
                if (! isSlotLoaded (i))
                    empty = i;

            if (empty < 0)
            {
                setUiStatus ("All 10 slots are full - clear one so SpectraLayers can load.");
                pendingImport = {};
                return;
            }

            pendingImport.loadStarted = true;
            pendingImport.slot = empty;
            loadPluginAsync (empty, slDesc);
        }

        retryLater();
        return;
    }

    juce::String err;
    if (bindFileToSlot (slot, pendingImport.wav, pendingImport.hostStart, err))
    {
        const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
        setUiStatus ("In SpectraLayers: " + pendingImport.wav.getFileName()
                     + "  |  starts at " + juce::String ((double) pendingImport.hostStart / sr, 3) + " s"
                     + "  |  play the DAW to hear the result, or Render"
                     + (pendingImport.note.isNotEmpty() ? "  |  " + pendingImport.note : juce::String()));
        pendingImport = {};
        return;
    }

    if (err.containsIgnoreCase ("still starting"))
    {
        retryLater();
        return;
    }

    setUiStatus (err);
    pendingImport = {};
}

bool NewProjectAudioProcessor::bindTakeToSpectraLayers (juce::String& error)
{
    if (liveCapture.isRecording())
        liveCapture.stopAndSync();

    if (liveCapture.getNumSamplesCaptured() <= 0)
    {
        error = "No take to send.";
        return false;
    }

    auto wav = getDefaultCaptureDirectory()
                   .getChildFile ("take_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".wav")
                   .getNonexistentSibling();

    if (! liveCapture.writeToWavFile (wav))
    {
        error = "Failed to write " + wav.getFullPathName();
        return false;
    }

    const auto start = takeStartSample.load();
    discardBridgeTake();
    importFileToSpectraLayers (wav, start);
    error.clear();
    return true;
}

void NewProjectAudioProcessor::setAutoCapture (bool enabled)
{
    autoCapture.store (enabled);
    if (! enabled && autoTakeRunning && liveCapture.isRecording())
    {
        juce::String err;
        stopBridgeRecording (err);
    }
    autoTakeRunning = false;
    setUiStatus (enabled ? "Auto: press Play in the DAW - recording stops on Stop and goes to SpectraLayers."
                         : "Auto off.");
}

void NewProjectAudioProcessor::autoCaptureTick()
{
    if (! autoCapture.load())
        return;

    const bool playing = isHostPlaying();
    const auto state = getCaptureUxState();

    if (! autoTakeRunning)
    {
        if (playing && state == CaptureUxState::Idle)
        {
            juce::String err;
            autoTakeRunning = startBridgeRecording (err);
            if (! autoTakeRunning)
                autoCapture.store (false);
        }
        return;
    }

    if (playing && state == CaptureUxState::Recording)
        return;

    // One take per arm: playing again afterwards is for listening to the result.
    autoTakeRunning = false;
    autoCapture.store (false);

    juce::String err;
    if (state == CaptureUxState::Recording && ! stopBridgeRecording (err))
        return;

    if (getCaptureUxState() != CaptureUxState::ReadyToSave)
        return;

    if (! bindTakeToSpectraLayers (err))
        setUiStatus (err + "  Take kept - Save... or Discard.");
    notifySlotsChanged();
}

void NewProjectAudioProcessor::renderSlotToWavAsync (int slotIndex)
{
   #if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
    if (renderRunning.load())
        return;

    if (isHostPlaying())
    {
        setUiStatus ("Stop the DAW first, then Render.");
        return;
    }

    auto inst = getSlotInstanceShared (slotIndex);
    auto* ara = asARAWrapper (inst.get());
    if (ara == nullptr || ara->getBoundSourceSeconds() <= 0.0)
    {
        setUiStatus ("Nothing to render - send audio to SpectraLayers first.");
        return;
    }

    const auto source = ara->getAssignedAudioSourceFile();
    auto dir = source.getParentDirectory();
    if (! dir.hasWriteAccess())
        dir = getDefaultCaptureDirectory();
    const auto outFile = dir.getChildFile (source.getFileNameWithoutExtension() + "_SL.wav").getNonexistentSibling();

    renderRunning.store (true);
    notifySlotsChanged();
    setUiStatus ("Rendering SpectraLayers -> " + outFile.getFileName() + " ...");

    juce::Thread::launch ([inst, outFile, safe = juce::WeakReference<NewProjectAudioProcessor> (this)]
    {
        juce::String err;
        const bool ok = asARAWrapper (inst.get())->renderBoundSourceToFile (outFile, err);

        juce::MessageManager::callAsync ([safe, ok, err, outFile]
        {
            if (safe == nullptr)
                return;

            safe->renderRunning.store (false);
            if (ok)
            {
                safe->setUiStatus ("Rendered -> " + outFile.getFileName() + "  |  drag it from Finder onto the track");
                outFile.revealToUser();
            }
            else
            {
                safe->setUiStatus ("Render failed: " + err);
            }
            safe->notifySlotsChanged();
        });
    });
   #else
    juce::ignoreUnused (slotIndex);
   #endif
}

void NewProjectAudioProcessor::requestLiveImportTrack()
{
    if (liveConfirm.file != juce::File() && juce::Time::getMillisecondCounter() < liveConfirm.expiresMs)
    {
        const auto confirmed = std::exchange (liveConfirm, {});
        importFileToSpectraLayers (confirmed.file, confirmed.hostStart, confirmed.note);
        return;
    }
    liveConfirm = {};

    setUiStatus ("Asking Live (chouchouLink) for the selected clip...");

    juce::Thread::launch ([safe = juce::WeakReference<NewProjectAudioProcessor> (this)]
    {
        juce::String reply, error;
        juce::StreamingSocket socket;

        if (! socket.connect ("127.0.0.1", kChouChouLinkPort, 1500))
        {
            error = "chouchouLink not reachable - install Tools/chouchouLink and pick it as a Control Surface "
                    "in Live's Link/Tempo/MIDI preferences.";
        }
        else
        {
            const juce::String request ("selected_clip\n");
            socket.write (request.toRawUTF8(), (int) request.getNumBytesAsUTF8());

            juce::MemoryOutputStream received;
            char chunk[4096];
            const auto deadline = juce::Time::getMillisecondCounter() + 4000;

            while (juce::Time::getMillisecondCounter() < deadline)
            {
                if (socket.waitUntilReady (true, 250) <= 0)
                    continue;

                const int n = socket.read (chunk, (int) sizeof (chunk), false);
                if (n <= 0)
                    break;

                received.write (chunk, (size_t) n);
                if (received.toString().containsChar ('\n'))
                    break;
            }

            reply = received.toUTF8().trim();
            if (reply.isEmpty())
                error = "Live did not answer (is a modal dialog open in Live?).";
        }

        juce::MessageManager::callAsync ([safe, reply, error]
        {
            if (safe != nullptr)
                safe->handleLiveLinkReply (reply, error);
        });
    });
}

void NewProjectAudioProcessor::handleLiveLinkReply (const juce::String& reply, const juce::String& error)
{
    if (error.isNotEmpty())
    {
        setUiStatus (error);
        return;
    }

    auto parsed = juce::JSON::parse (reply);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
    {
        setUiStatus ("chouchouLink sent an unreadable reply.");
        return;
    }

    if (! (bool) obj->getProperty ("ok"))
    {
        setUiStatus ("Live: " + obj->getProperty ("error").toString());
        return;
    }

    const juce::File file (obj->getProperty ("file_path").toString());
    const double fileStartSeconds = obj->getProperty ("file_start_seconds");
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;

    const auto hostStart = (juce::int64) std::llround (fileStartSeconds * sr);

    juce::StringArray notes;
    if ((bool) obj->getProperty ("tempo_automated"))
        notes.add ("Project has tempo automation: clip position may not line up");
    if ((bool) obj->getProperty ("warping"))
        notes.add ("Clip is warped: sync holds only at its original tempo (Warp off = exact)");
    const auto note = notes.joinIntoString (". ");

    const auto picked = obj->getProperty ("picked").toString();
    if (picked.isNotEmpty() && picked != "detail")
    {
        liveConfirm = { file, hostStart, note, juce::Time::getMillisecondCounter() + 10000 };
        setUiStatus ("No clip selected. Live picked " + juce::String (picked == "playhead" ? "the clip under the playhead" : "the track's first clip")
                     + ": '" + obj->getProperty ("clip_name").toString() + "' on '" + obj->getProperty ("track_name").toString()
                     + "' (" + file.getFileName() + ", beats " + juce::String ((double) obj->getProperty ("start_beat"), 1)
                     + "-" + juce::String ((double) obj->getProperty ("end_beat"), 1) + "). Press Import Track again within 10 s to use it."
                     + (note.isNotEmpty() ? " " + note + "." : juce::String()));
        return;
    }

    importFileToSpectraLayers (file, hostStart, note);
}

//==============================================================================
bool NewProjectAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* NewProjectAudioProcessor::createEditor()
{
    probeEvent ("EDITOR opened");
    return new NewProjectAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}
