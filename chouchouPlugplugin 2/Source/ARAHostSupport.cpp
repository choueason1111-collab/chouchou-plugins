/*
  ==============================================================================

    chouchouPlugplugin2 - ARA hosting helpers

  ==============================================================================
*/

#include "ARAHostSupport.h"

//==============================================================================
void LiveCaptureBuffer::configure (double sr, int maxSeconds)
{
    // Message / prepare thread only - never called from the audio callback.
    const juce::ScopedLock sl (lock);
    const double newRate = sr > 0.0 ? sr : 44100.0;

    if (recording.load (std::memory_order_acquire) && newRate != takeSampleRate)
        recording.store (false, std::memory_order_release);

    configuredSampleRate = newRate;
    configuredMaxSeconds = juce::jlimit (8, 600, maxSeconds);
}

bool LiveCaptureBuffer::beginTake()
{
    const juce::ScopedLock sl (lock);
    takeSampleRate = configuredSampleRate;
    numChannels = 2;
    capacity = (int64_t) std::ceil (takeSampleRate * (double) configuredMaxSeconds);

    take.setSize (numChannels, (int) capacity, false, true, false);
    if (take.getNumSamples() < (int) capacity)
    {
        capacity = 0;
        return false;
    }

    written.store (0, std::memory_order_release);
    peakAbs.store (0.0f, std::memory_order_release);
    lastBlockPeak.store (0.0f, std::memory_order_release);
    overflowed.store (false, std::memory_order_release);
    recording.store (true, std::memory_order_release);
    return true;
}

void LiveCaptureBuffer::discardTake()
{
    const juce::ScopedLock sl (lock);
    recording.store (false, std::memory_order_release);
    take.setSize (0, 0);
    capacity = 0;
    written.store (0, std::memory_order_release);
    peakAbs.store (0.0f, std::memory_order_release);
    lastBlockPeak.store (0.0f, std::memory_order_release);
    overflowed.store (false, std::memory_order_release);
}

void LiveCaptureBuffer::stopAndSync()
{
    recording.store (false, std::memory_order_release);
    // push() re-checks the flag under this lock, so once we own it no further samples land.
    const juce::ScopedLock sl (lock);
}

void LiveCaptureBuffer::push (const juce::AudioBuffer<float>& buffer)
{
    push (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void LiveCaptureBuffer::push (const float* const* channels, int chIn, int numSamples)
{
    if (! recording.load (std::memory_order_acquire))
        return;

    if (numSamples <= 0 || channels == nullptr)
        return;

    // Audio thread: never block on capture (e.g. while a WAV snapshot is taken).
    if (! lock.tryEnter())
        return;

    if (! recording.load (std::memory_order_relaxed) || capacity <= 0)
    {
        lock.exit();
        return;
    }

    const int chUse = juce::jmin (numChannels, juce::jmax (1, chIn));
    float blockPeak = 0.0f;
    auto pos = written.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        if (pos >= capacity)
        {
            overflowed.store (true, std::memory_order_release);
            recording.store (false, std::memory_order_release);
            break;
        }

        for (int c = 0; c < numChannels; ++c)
        {
            float s = 0.0f;
            if (c < chUse && channels[c] != nullptr)
                s = channels[c][i];
            else if (chUse == 1 && channels[0] != nullptr)
                s = channels[0][i];

            blockPeak = juce::jmax (blockPeak, std::abs (s));
            take.setSample (c, (int) pos, s);
        }

        ++pos;
    }

    written.store (pos, std::memory_order_release);
    lastBlockPeak.store (blockPeak, std::memory_order_release);
    peakAbs.store (juce::jmax (peakAbs.load (std::memory_order_relaxed), blockPeak),
                   std::memory_order_release);
    lock.exit();
}

float LiveCaptureBuffer::getPeakAbs() const noexcept
{
    return peakAbs.load (std::memory_order_acquire);
}

int LiveCaptureBuffer::copyRecent (juce::AudioBuffer<float>& dest) const
{
    const juce::ScopedLock sl (lock);
    const int64_t avail = written.load (std::memory_order_acquire);
    if (avail <= 0 || capacity <= 0)
        return 0;

    const int toCopy = (int) juce::jmin (avail, (int64_t) dest.getNumSamples());

    for (int c = 0; c < dest.getNumChannels(); ++c)
        dest.copyFrom (c, 0, take, juce::jmin (c, numChannels - 1), 0, toCopy);

    return toCopy;
}

bool LiveCaptureBuffer::writeToWavFile (const juce::File& file) const
{
    // Snapshot under lock, then do all heap / disk I/O unlocked so the audio
    // thread's tryEnter() on push() is not blocked by file writes.
    juce::AudioBuffer<float> snapshot;
    double sr = 44100.0;
    int ch = 2;

    {
        const juce::ScopedLock sl (lock);
        const int64_t avail = written.load (std::memory_order_acquire);
        if (avail <= 0 || capacity <= 0)
            return false;

        sr = takeSampleRate;
        ch = numChannels;
        snapshot.setSize (ch, (int) avail, false, false, true);

        for (int c = 0; c < ch; ++c)
            snapshot.copyFrom (c, 0, take, c, 0, (int) avail);
    }

    if (! file.getParentDirectory().createDirectory())
        return false;

    // Existing file is only replaced after the temp WAV is fully written and closed.
    juce::TemporaryFile temp (file);

    {
        std::unique_ptr<juce::OutputStream> out (temp.getFile().createOutputStream());
        if (out == nullptr)
            return false;

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (out.get(), sr, (unsigned int) ch, 24, {}, 0));

        if (writer == nullptr)
            return false;

        out.release(); // writer owns the stream now

        if (! writer->writeFromAudioSampleBuffer (snapshot, 0, snapshot.getNumSamples()))
            return false;

        if (! writer->flush())
            return false;
    }

    return temp.overwriteTargetFileWithTemporary();
}

#if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)

const juce::Identifier ARAPluginInstanceWrapper::ARATestHost::Context::xmlRootTag { "ARATestHostContext" };
const juce::Identifier ARAPluginInstanceWrapper::ARATestHost::Context::xmlAudioFileAttrib { "AudioFile" };

bool pluginDescriptionHasARA (const juce::PluginDescription& pd)
{
    // iZotope RX Spectral Editor ARA is whitelist-only (Logic AU / Studio One VST3 /
    // Pro Tools AAX). Loading it here always shows "not available for this host".
    if (pd.name.containsIgnoreCase ("Spectral Editor")
        && (pd.manufacturerName.containsIgnoreCase ("iZotope")
            || pd.fileOrIdentifier.containsIgnoreCase ("iZotope")
            || pd.fileOrIdentifier.containsIgnoreCase ("RX")))
        return false;

    if (pd.hasARAExtension)
        return true;

    // Steinberg SpectraLayers VST3 scans as category "OnlyARA" with hasARAExtension=0.
    // Treating it as a normal FX and instantiating bare inside Audition is a known crash path.
    if (pd.name.containsIgnoreCase ("SpectraLayers")
        || pd.fileOrIdentifier.containsIgnoreCase ("SpectraLayers"))
        return true;

    // Only promote generic "ARA" category when it is Steinberg (SpectraLayers family).
    if (pd.category.containsIgnoreCase ("ARA")
        && pd.manufacturerName.containsIgnoreCase ("Steinberg"))
        return true;

    return false;
}

void repairKnownPluginARAFlags (juce::KnownPluginList& list)
{
    for (const auto& type : list.getTypes())
    {
        if (type.hasARAExtension)
            continue;

        if (! pluginDescriptionHasARA (type))
            continue;

        auto fixed = type;
        fixed.hasARAExtension = true;
        list.addType (fixed);
    }
}

std::unique_ptr<juce::AudioPluginInstance>
    maybeWrapWithARAHost (std::unique_ptr<juce::AudioPluginInstance> instance)
{
    if (instance == nullptr)
        return nullptr;

    if (! pluginDescriptionHasARA (instance->getPluginDescription()))
        return instance;

    return std::make_unique<ARAPluginInstanceWrapper> (std::move (instance));
}

ARAPluginInstanceWrapper* asARAWrapper (juce::AudioPluginInstance* instance)
{
    return dynamic_cast<ARAPluginInstanceWrapper*> (instance);
}

bool assignLiveCaptureToARA (ARAPluginInstanceWrapper& wrapper, const LiveCaptureBuffer& capture,
                             juce::String& error)
{
    if (capture.getNumSamplesCaptured() <= 0)
    {
        error = "No audio captured yet - play Audition through this rack first, then Capture.";
        return false;
    }

    // Stable folder (not /tmp) so Audition ↔ Standalone round-trip is easy to find.
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("chouchouPlugplugin2_ARA");
    dir.createDirectory();
    auto file = dir.getChildFile ("from_audition_"
                                  + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S")
                                  + ".wav");

    if (! capture.writeToWavFile (file))
    {
        error = "Failed to write live capture WAV.";
        return false;
    }

    if (! assignFileToARA (wrapper, file, error))
        return false;

    error = file.getFullPathName(); // caller may show / reveal this path
    return true;
}

bool assignFileToARA (ARAPluginInstanceWrapper& wrapper, const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File does not exist: " + file.getFullPathName();
        return false;
    }

    if (! wrapper.isARADocumentReady())
    {
        error = "ARA document still starting - wait 1-2s and press Capture again.";
        return false;
    }

    // Probe that the file is a readable WAV before binding ARA (MemoryMappedReader can be null).
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::MemoryMappedAudioFormatReader> probe (
            wav.createMemoryMappedReader (file));

        if (probe == nullptr)
        {
            error = "Cannot open as WAV (unsupported/corrupt): " + file.getFileName();
            return false;
        }
    }

    wrapper.assignAudioSourceFile (file);

    if (! wrapper.hasBoundAudioSource())
    {
        error = "ARA bind failed (check WAV / try again).";
        return false;
    }

    error.clear();
    return true;
}

void syncARAPlayHeadFromHost (ARAPluginInstanceWrapper& wrapper, juce::AudioPlayHead* hostPlayHead,
                              int numSamples)
{
    wrapper.syncFromOuterPlayHead (hostPlayHead, numSamples);
}

#endif
