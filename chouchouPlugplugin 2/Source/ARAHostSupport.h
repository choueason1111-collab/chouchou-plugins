/*
  ==============================================================================

    chouchouPlugplugin2 — ARA hosting + live processBlock capture

    Document graph + host interfaces follow Celemony TestHost / JUCE APH.
    Audition has no ARA. This mini-host owns the ARA document and feeds it either:
      1) a WAV file, or
      2) audio captured from the rack's processBlock (best-effort "Audition clip").

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

#if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)

#include "ARAPluginHostBase.h"

//==============================================================================
/** Linear stereo take captured from processBlock. Storage is allocated only when a take
    begins (message thread), never on the audio thread. Once full, recording stops and the
    take is kept (overflowed) until the owner discards or saves it. */
class LiveCaptureBuffer
{
public:
    static constexpr float kSilenceThreshold = 1.0e-5f; // about -100 dBFS

    /** Record the rate/length for the next take. If the rate changes mid-take, recording
        stops but the take (and its own sample rate) is kept. */
    void configure (double sampleRate, int maxSeconds);

    /** Message thread: allocate storage for a fresh take and arm recording. */
    bool beginTake();
    /** Message thread: drop the take and free its storage. */
    void discardTake();

    /** Stop recording and wait until any in-flight audio-thread push has finished. */
    void stopAndSync();

    bool isRecording() const noexcept { return recording.load (std::memory_order_acquire); }

    void push (const juce::AudioBuffer<float>& buffer);
    void push (const float* const* channels, int numChannels, int numSamples);

    double getSampleRate() const noexcept { return takeSampleRate; }
    int getNumChannels() const noexcept { return numChannels; }
    int64_t getNumSamplesCaptured() const noexcept { return written.load (std::memory_order_acquire); }
    int64_t getCapacitySamples() const noexcept { return capacity; }
    /** Peak-hold since the take began. */
    float getPeakAbs() const noexcept;
    /** Peak of the most recent pushed block (for a live meter). */
    float getLastBlockPeak() const noexcept { return lastBlockPeak.load (std::memory_order_acquire); }
    bool didOverflow() const noexcept { return overflowed.load (std::memory_order_acquire); }

    /** Copy captured audio into dest (up to dest.getNumSamples()). Returns samples copied. */
    int copyRecent (juce::AudioBuffer<float>& dest) const;

    /** Write the take to a WAV via a temporary file, replacing the target only on success. */
    bool writeToWavFile (const juce::File& file) const;

private:
    mutable juce::CriticalSection lock;
    juce::AudioBuffer<float> take;
    int64_t capacity = 0;
    std::atomic<int64_t> written { 0 };
    std::atomic<float> peakAbs { 0.0f };
    std::atomic<float> lastBlockPeak { 0.0f };
    std::atomic<bool> recording { false };
    std::atomic<bool> overflowed { false };
    double configuredSampleRate = 44100.0;
    int configuredMaxSeconds = 180;
    double takeSampleRate = 44100.0;
    int numChannels = 2;
};

//==============================================================================
/** True if this description is ARA / OnlyARA (SpectraLayers often reports OnlyARA
    with hasARAExtension=0 after VST3 scan — that mismatch crashed Audition on bare load). */
bool pluginDescriptionHasARA (const juce::PluginDescription& pd);

/** Persist OnlyARA → hasARAExtension repairs into the KnownPluginList. */
void repairKnownPluginARAFlags (juce::KnownPluginList& list);

/** Wrap a newly created instance with ARAPluginInstanceWrapper when it needs ARA. */
std::unique_ptr<juce::AudioPluginInstance>
    maybeWrapWithARAHost (std::unique_ptr<juce::AudioPluginInstance> instance);

/** Dynamic cast helper. */
ARAPluginInstanceWrapper* asARAWrapper (juce::AudioPluginInstance* instance);

/** Dump live capture → temp WAV → assign as ARA audio source. */
bool assignLiveCaptureToARA (ARAPluginInstanceWrapper& wrapper, const LiveCaptureBuffer& capture,
                             juce::String& error);

/** Assign an existing WAV/AIFF for ARA. */
bool assignFileToARA (ARAPluginInstanceWrapper& wrapper, const juce::File& file, juce::String& error);

/** Follow outer host playhead into the ARA wrapper's SimplePlayHead when possible. */
void syncARAPlayHeadFromHost (ARAPluginInstanceWrapper& wrapper, juce::AudioPlayHead* hostPlayHead,
                              int numSamples);

#else // ! ARA

class LiveCaptureBuffer
{
public:
    static constexpr float kSilenceThreshold = 1.0e-5f;
    void configure (double, int) {}
    bool beginTake() { return false; }
    void discardTake() {}
    void stopAndSync() {}
    bool isRecording() const noexcept { return false; }
    void push (const juce::AudioBuffer<float>&) {}
    void push (const float* const*, int, int) {}
    double getSampleRate() const noexcept { return 44100.0; }
    int getNumChannels() const noexcept { return 2; }
    int64_t getNumSamplesCaptured() const noexcept { return 0; }
    int64_t getCapacitySamples() const noexcept { return 0; }
    float getPeakAbs() const noexcept { return 0.0f; }
    float getLastBlockPeak() const noexcept { return 0.0f; }
    bool didOverflow() const noexcept { return false; }
    bool writeToWavFile (const juce::File&) const { return false; }
};

inline bool pluginDescriptionHasARA (const juce::PluginDescription& pd)
{
    if (pd.hasARAExtension)
        return true;
    if (pd.category.containsIgnoreCase ("ARA"))
        return true;
    return pd.name.containsIgnoreCase ("SpectraLayers")
        || pd.fileOrIdentifier.containsIgnoreCase ("SpectraLayers");
}

inline void repairKnownPluginARAFlags (juce::KnownPluginList&) {}

inline std::unique_ptr<juce::AudioPluginInstance>
    maybeWrapWithARAHost (std::unique_ptr<juce::AudioPluginInstance> instance)
{
    return instance;
}

inline void* asARAWrapper (juce::AudioPluginInstance*) { return nullptr; }

#endif
