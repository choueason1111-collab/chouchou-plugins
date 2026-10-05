/*
  ==============================================================================

    HostProbe — logs how the host drives this plugin instance (lifecycle calls,
    processBlock runs, realtime vs offline, playhead, track info) to a shared text
    file, so we can compare Audition / Live / REAPER behaviour.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

class ChouChouHostProbe  : private juce::Thread
{
public:
    explicit ChouChouHostProbe (const juce::String& wrapperName);
    ~ChouChouHostProbe() override;

    static juce::File getLogFile();

    struct BlockInfo
    {
        int numSamples = 0;
        int numChannels = 0;
        bool nonRealtime = false;
        bool bypassed = false;
        bool hasPlayhead = false;
        bool hasPosition = false;
        bool isPlaying = false;
        bool isRecording = false;
        bool isLooping = false;
        bool hasTime = false, hasPpq = false, hasBpm = false, hasLoop = false;
        bool hasHostTime = false, hasEditOrigin = false;
        int64_t timeInSamples = 0;
        double ppq = 0.0, bpm = 0.0, loopStartPpq = 0.0, loopEndPpq = 0.0, editOrigin = 0.0;
        float inputPeak = 0.0f;
        float outputPeak = 0.0f;
    };

    /** Audio thread only: lock-free, never allocates. Returns true if this block starts a
        new run (first block after a gap), which the Mark-output experiment uses. */
    bool logBlock (const BlockInfo& info) noexcept;

    /** Fill playhead fields of info from the host playhead (audio thread). */
    static void readPlayhead (juce::AudioPlayHead* playHead, BlockInfo& info);

    /** Any non-audio thread. */
    void logEvent (const juce::String& text);
    void mark();
    static void clearLog();

    void setSampleRate (double sr) noexcept { sampleRate.store (sr, std::memory_order_relaxed); }

    const juce::String& getInstanceId() const noexcept { return instanceId; }
    const juce::String& getHostDescription() const noexcept { return hostDescription; }

    void setTrackProperties (const juce::String& name, const juce::String& colourHex);
    juce::String getTrackName() const;

    // Experiments (default off).
    std::atomic<bool> expLatency { false }, expTail { false }, expMarkOutput { false };

private:
    struct BlockRecord
    {
        double ms = 0.0;
        BlockInfo info;
    };

    struct EventRecord
    {
        double ms = 0.0;
        juce::String text;
    };

    struct Run
    {
        bool open = false;
        double startMs = 0.0, lastMs = 0.0;
        int64_t blocks = 0, samples = 0, silentBlocks = 0, bypassedBlocks = 0;
        int minBlock = 0, maxBlock = 0, minCh = 0, maxCh = 0;
        bool nonRtFirst = false, nonRtLast = false, nonRtChanged = false;
        bool playingFirst = false, playingLast = false, anyRecording = false, anyLooping = false;
        bool anyPlayhead = false;
        BlockInfo first;
        int64_t posFirst = 0, posLast = 0, posJumps = 0, expectedNext = 0;
        bool anyTime = false;
        float maxIn = 0.0f, maxOut = 0.0f;
    };

    void run() override;
    void flush();
    void addBlockToRun (const BlockRecord& r);
    void closeRun (const char* reason);
    void appendLine (const juce::String& line);
    juce::String prefix (double ms) const;

    static constexpr int kFifoSize = 1 << 15;
    juce::AbstractFifo blockFifo { kFifoSize };
    std::vector<BlockRecord> blockStorage;
    std::atomic<int64_t> droppedBlocks { 0 };
    int64_t reportedDropped = 0;
    std::atomic<double> lastAudioBlockMs { 0.0 };
    std::atomic<double> sampleRate { 0.0 };

    juce::CriticalSection eventLock;
    std::vector<EventRecord> pendingEvents;

    mutable juce::CriticalSection trackLock;
    juce::String trackName;

    Run current;
    juce::StringArray pendingLines;
    juce::String instanceId, wrapper, hostDescription;
    int markCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouChouHostProbe)
};
