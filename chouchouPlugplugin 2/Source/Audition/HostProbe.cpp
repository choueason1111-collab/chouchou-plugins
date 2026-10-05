#include "HostProbe.h"

#if JUCE_MAC || JUCE_LINUX
 #include <unistd.h>
#endif

namespace
{
    constexpr double kRunGapMs  = 100.0;
    constexpr double kRunIdleMs = 250.0;
    constexpr double kRunSplitMs = 5000.0;

    juce::CriticalSection& fileLock()
    {
        static juce::CriticalSection lock;
        return lock;
    }

    juce::String dbString (float peak)
    {
        if (peak < 1.0e-5f)
            return "-inf";
        return juce::String (juce::Decibels::gainToDecibels (peak), 1);
    }

    juce::String secs (double s) { return juce::String (s, 3) + "s"; }

    int currentPid()
    {
       #if JUCE_MAC || JUCE_LINUX
        return (int) getpid();
       #else
        return 0;
       #endif
    }
}

//==============================================================================
ChouChouHostProbe::ChouChouHostProbe (const juce::String& wrapperName)
    : juce::Thread ("chouchou host probe"),
      wrapper (wrapperName)
{
    blockStorage.resize ((size_t) kFifoSize);
    instanceId = juce::String::toHexString (juce::Random::getSystemRandom().nextInt())
                     .paddedLeft ('0', 8).substring (4);

    juce::PluginHostType host;
    hostDescription = host.getHostDescription();

    logEvent ("CREATED host=\"" + hostDescription + "\" hostPath=\"" + juce::PluginHostType::getHostPath()
              + "\" pid=" + juce::String (currentPid()));
    startThread (juce::Thread::Priority::low);
}

ChouChouHostProbe::~ChouChouHostProbe()
{
    logEvent ("DESTROYED");
    stopThread (2000);
}

juce::File ChouChouHostProbe::getLogFile()
{
    return juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
        .getChildFile ("chouchou_host_probe.log");
}

void ChouChouHostProbe::clearLog()
{
    const juce::ScopedLock sl (fileLock());
    auto f = getLogFile();
    f.replaceWithText ("=== chouchou host probe log - cleared "
                       + juce::Time::getCurrentTime().toString (true, true, true, true) + " ===\n");
}

//==============================================================================
void ChouChouHostProbe::readPlayhead (juce::AudioPlayHead* playHead, BlockInfo& info)
{
    info.hasPlayhead = playHead != nullptr;
    if (playHead == nullptr)
        return;

    const auto pos = playHead->getPosition();
    if (! pos.hasValue())
        return;

    info.hasPosition = true;
    info.isPlaying   = pos->getIsPlaying();
    info.isRecording = pos->getIsRecording();
    info.isLooping   = pos->getIsLooping();

    if (const auto t = pos->getTimeInSamples())   { info.hasTime = true; info.timeInSamples = *t; }
    if (const auto p = pos->getPpqPosition())     { info.hasPpq = true;  info.ppq = *p; }
    if (const auto b = pos->getBpm())             { info.hasBpm = true;  info.bpm = *b; }
    if (const auto l = pos->getLoopPoints())      { info.hasLoop = true; info.loopStartPpq = l->ppqStart; info.loopEndPpq = l->ppqEnd; }
    if (pos->getHostTimeNs().hasValue())          info.hasHostTime = true;
    if (const auto o = pos->getEditOriginTime())  { info.hasEditOrigin = true; info.editOrigin = *o; }
}

bool ChouChouHostProbe::logBlock (const BlockInfo& info) noexcept
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double last = lastAudioBlockMs.exchange (now, std::memory_order_acq_rel);
    const bool runStart = (last <= 0.0) || (now - last > kRunGapMs);

    const auto scope = blockFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0)
    {
        droppedBlocks.fetch_add (1, std::memory_order_relaxed);
        return runStart;
    }

    auto& rec = blockStorage[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
    rec.ms = now;
    rec.info = info;
    return runStart;
}

void ChouChouHostProbe::logEvent (const juce::String& text)
{
    const juce::ScopedLock sl (eventLock);
    pendingEvents.push_back ({ juce::Time::getMillisecondCounterHiRes(), text });
}

void ChouChouHostProbe::mark()
{
    logEvent ("------------------------------ MARK " + juce::String (++markCounter)
              + " ------------------------------");
}

void ChouChouHostProbe::setTrackProperties (const juce::String& name, const juce::String& colourHex)
{
    {
        const juce::ScopedLock sl (trackLock);
        trackName = name;
    }
    logEvent ("TRACK name=\"" + name + "\" colour=" + colourHex);
}

juce::String ChouChouHostProbe::getTrackName() const
{
    const juce::ScopedLock sl (trackLock);
    return trackName;
}

//==============================================================================
void ChouChouHostProbe::run()
{
    while (! threadShouldExit())
    {
        wait (50);
        flush();
    }

    flush();
    if (current.open)
        closeRun ("instance end");
    flush();
}

juce::String ChouChouHostProbe::prefix (double ms) const
{
    const double ageMs = juce::Time::getMillisecondCounterHiRes() - ms;
    const auto when = juce::Time::getCurrentTime() - juce::RelativeTime::milliseconds ((juce::int64) ageMs);
    return when.formatted ("%H:%M:%S") + "." + juce::String (when.getMilliseconds()).paddedLeft ('0', 3)
         + " [" + instanceId + " " + wrapper + "] ";
}

void ChouChouHostProbe::flush()
{
    std::vector<EventRecord> events;
    {
        const juce::ScopedLock sl (eventLock);
        events.swap (pendingEvents);
    }

    std::vector<BlockRecord> blocks;
    {
        const auto ready = blockFifo.getNumReady();
        if (ready > 0)
        {
            blocks.reserve ((size_t) ready);
            const auto scope = blockFifo.read (ready);
            for (int i = 0; i < scope.blockSize1; ++i)
                blocks.push_back (blockStorage[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i)
                blocks.push_back (blockStorage[(size_t) (scope.startIndex2 + i)]);
        }
    }

    size_t bi = 0, ei = 0;
    while (bi < blocks.size() || ei < events.size())
    {
        const bool takeBlock = ei >= events.size()
                            || (bi < blocks.size() && blocks[bi].ms <= events[ei].ms);
        if (takeBlock)
        {
            const auto& r = blocks[bi++];
            if (current.open && r.ms - current.lastMs > kRunGapMs)
                closeRun ("gap");
            else if (current.open && r.ms - current.startMs > kRunSplitMs)
                closeRun ("split (still running)");
            addBlockToRun (r);
        }
        else
        {
            const auto& e = events[ei++];
            if (current.open)
                closeRun ("event");
            pendingLines.add (prefix (e.ms) + e.text);
        }
    }

    if (current.open && juce::Time::getMillisecondCounterHiRes() - current.lastMs > kRunIdleMs)
        closeRun ("idle");

    const auto dropped = droppedBlocks.load (std::memory_order_relaxed);
    if (dropped != reportedDropped)
    {
        pendingLines.add (prefix (juce::Time::getMillisecondCounterHiRes())
                          + "WARNING probe dropped " + juce::String (dropped - reportedDropped) + " block records");
        reportedDropped = dropped;
    }

    if (! pendingLines.isEmpty())
    {
        juce::String text;
        for (auto& l : pendingLines)
            text << l << "\n";
        pendingLines.clear();
        appendLine (text);
    }
}

void ChouChouHostProbe::addBlockToRun (const BlockRecord& r)
{
    const auto& b = r.info;
    auto& run = current;

    if (! run.open)
    {
        run = Run{};
        run.open = true;
        run.startMs = r.ms;
        run.first = b;
        run.minBlock = run.maxBlock = b.numSamples;
        run.minCh = run.maxCh = b.numChannels;
        run.nonRtFirst = b.nonRealtime;
        run.playingFirst = b.isPlaying;
        if (b.hasTime)
        {
            run.anyTime = true;
            run.posFirst = b.timeInSamples;
            run.expectedNext = b.timeInSamples;
        }
    }

    if (run.anyTime && b.hasTime && run.blocks > 0 && b.timeInSamples != run.expectedNext)
        ++run.posJumps;
    if (b.hasTime)
    {
        if (! run.anyTime) { run.anyTime = true; run.posFirst = b.timeInSamples; }
        run.posLast = b.timeInSamples;
        run.expectedNext = b.timeInSamples + b.numSamples;
    }

    run.lastMs = r.ms;
    ++run.blocks;
    run.samples += b.numSamples;
    run.minBlock = juce::jmin (run.minBlock, b.numSamples);
    run.maxBlock = juce::jmax (run.maxBlock, b.numSamples);
    run.minCh = juce::jmin (run.minCh, b.numChannels);
    run.maxCh = juce::jmax (run.maxCh, b.numChannels);
    if (b.nonRealtime != run.nonRtLast && run.blocks > 1)
        run.nonRtChanged = true;
    run.nonRtLast = b.nonRealtime;
    run.playingLast = b.isPlaying;
    run.anyRecording |= b.isRecording;
    run.anyLooping |= b.isLooping;
    run.anyPlayhead |= b.hasPlayhead;
    if (juce::jmax (b.inputPeak, b.outputPeak) < 1.0e-5f)
        ++run.silentBlocks;
    if (b.bypassed)
        ++run.bypassedBlocks;
    run.maxIn = juce::jmax (run.maxIn, b.inputPeak);
    run.maxOut = juce::jmax (run.maxOut, b.outputPeak);
}

void ChouChouHostProbe::closeRun (const char* reason)
{
    auto& run = current;
    if (! run.open)
        return;

    const double sr = sampleRate.load (std::memory_order_relaxed);
    const double audioSecs = sr > 0.0 ? (double) run.samples / sr : 0.0;
    // Wall time spans first..last block start; add one block so a single block isn't 0 s.
    const double wallSecs = (run.lastMs - run.startMs) / 1000.0 + (sr > 0.0 ? run.maxBlock / sr : 0.0);
    const double speed = wallSecs > 0.0 ? audioSecs / wallSecs : 0.0;

    juce::String s;
    s << "RUN " << secs (audioSecs) << " audio / " << secs (wallSecs) << " wall ("
      << juce::String (speed, 1) << "x)"
      << " sr=" << juce::String (sr, 0)
      << " nonRT=" << (int) run.nonRtFirst << (run.nonRtChanged ? "->changed" : "")
      << " blocks=" << run.blocks
      << " blk=" << run.minBlock << ".." << run.maxBlock
      << " ch=" << run.minCh << (run.maxCh != run.minCh ? ".." + juce::String (run.maxCh) : juce::String())
      << " silent=" << run.silentBlocks
      << " bypassed=" << run.bypassedBlocks
      << " in=" << dbString (run.maxIn) << "dB out=" << dbString (run.maxOut) << "dB";

    if (! run.anyPlayhead)
        s << " playhead=none";
    else if (! run.first.hasPosition)
        s << " playhead=noPosition";
    else
    {
        s << " playing=" << (int) run.playingFirst << "->" << (int) run.playingLast
          << " rec=" << (int) run.anyRecording
          << " looping=" << (int) run.anyLooping;

        if (run.anyTime && sr > 0.0)
            s << " pos=" << secs (run.posFirst / sr) << "->" << secs (run.posLast / sr)
              << " (" << run.posFirst << ") jumps=" << run.posJumps;
        else
            s << " pos=-";

        s << " ppq=" << (run.first.hasPpq ? juce::String (run.first.ppq, 3) : juce::String ("-"))
          << " bpm=" << (run.first.hasBpm ? juce::String (run.first.bpm, 2) : juce::String ("-"))
          << " loopPpq=" << (run.first.hasLoop ? juce::String (run.first.loopStartPpq, 2) + ".."
                                                   + juce::String (run.first.loopEndPpq, 2)
                                               : juce::String ("-"))
          << " hostTime=" << (int) run.first.hasHostTime
          << " editOrigin=" << (run.first.hasEditOrigin ? secs (run.first.editOrigin) : juce::String ("-"));
    }

    s << " end=" << reason;
    pendingLines.add (prefix (run.startMs) + s);
    run.open = false;
}

void ChouChouHostProbe::appendLine (const juce::String& text)
{
    const juce::ScopedLock sl (fileLock());
    auto f = getLogFile();
    juce::FileOutputStream os (f);
    if (os.openedOk())
    {
        os.writeText (text, false, false, "\n");
        os.flush();
    }
}
