/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-9-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

#pragma once

#include <juce_core/system/juce_TargetPlatform.h>

#if JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)

#include <JuceHeader.h>
#include <cmath>
#include <stdexcept>
#include <string>

#include <ARA_API/ARAInterface.h>
#include <ARA_Library/Dispatch/ARAHostDispatch.h>

// AudioPluginHost original assumes juce types in global NS via JuceHeader setting.
using namespace juce;

class FileAudioSource
{
    auto getAudioSourceProperties() const
    {
        auto properties = ARAHostModel::AudioSource::getEmptyProperties();
        properties.name = formatReader->getFile().getFullPathName().toRawUTF8();
        properties.persistentID = formatReader->getFile().getFullPathName().toRawUTF8();
        properties.sampleCount = formatReader->lengthInSamples;
        properties.sampleRate = formatReader->sampleRate;
        properties.channelCount = (int) formatReader->numChannels;
        properties.merits64BitSamples = false;
        return properties;
    }

public:
    FileAudioSource (ARA::Host::DocumentController& dc, const juce::File& file)
        : formatReader ([&file]() -> std::unique_ptr<MemoryMappedAudioFormatReader>
          {
              auto result = rawToUniquePtr (WavAudioFormat().createMemoryMappedReader (file));
              if (result == nullptr)
                  throw std::runtime_error ("createMemoryMappedReader failed");
              if (! result->mapEntireFile())
                  throw std::runtime_error ("mapEntireFile failed");
              return result;
          }()),
          audioSource (Converter::toHostRef (this), dc, getAudioSourceProperties())
    {
        audioSource.enableAudioSourceSamplesAccess (true);
    }

    bool readAudioSamples (float* const* buffers, int64 startSample, int64 numSamples)
    {
        // TODO: the ARA interface defines numSamples as int64. We should do multiple reads if necessary with the reader.
        if (numSamples > std::numeric_limits<int>::max())
            return false;

        return formatReader->read (buffers, (int) formatReader->numChannels, startSample, (int) (numSamples));
    }

    bool readAudioSamples (double* const* buffers, int64 startSample, int64 numSamples)
    {
        ignoreUnused (buffers, startSample, numSamples);
        return false;
    }

    MemoryMappedAudioFormatReader& getFormatReader() const { return *formatReader; }

    auto getPluginRef() const { return audioSource.getPluginRef(); }

    auto& getSource() { return audioSource; }

    using Converter = ARAHostModel::ConversionFunctions<FileAudioSource*, ARA::ARAAudioSourceHostRef>;

private:
    std::unique_ptr<MemoryMappedAudioFormatReader> formatReader;
    ARAHostModel::AudioSource audioSource;
};

//==============================================================================
class MusicalContext
{
    auto getMusicalContextProperties() const
    {
        // Celemony TestHost-style: named context + optional color for host UI.
        auto properties = ARAHostModel::MusicalContext::getEmptyProperties();
        properties.name = "chouchou Musical Context";
        properties.orderIndex = 0;
        properties.color = &color;
        return properties;
    }

public:
    MusicalContext (ARA::Host::DocumentController& dc)
        : color { 0.25f, 0.55f, 0.95f },
          context (Converter::toHostRef (this), dc, getMusicalContextProperties())
    {
    }

    auto getPluginRef() const { return context.getPluginRef(); }

private:
    using Converter = ARAHostModel::ConversionFunctions<MusicalContext*, ARA::ARAMusicalContextHostRef>;

    ARA::ARAColor color;
    ARAHostModel::MusicalContext context;
};

//==============================================================================
class RegionSequence
{
    auto getRegionSequenceProperties() const
    {
        auto properties = ARAHostModel::RegionSequence::getEmptyProperties();
        properties.name = name.toRawUTF8();
        properties.orderIndex = 0;
        properties.musicalContextRef = context.getPluginRef();
        properties.color = nullptr;
        return properties;
    }

public:
    RegionSequence (ARA::Host::DocumentController& dc, MusicalContext& contextIn, String nameIn)
        : context (contextIn),
          name (std::move (nameIn)),
          sequence (Converter::toHostRef (this), dc, getRegionSequenceProperties())
    {
    }

    auto& getMusicalContext() const { return context; }
    auto getPluginRef() const { return sequence.getPluginRef(); }

private:
    using Converter = ARAHostModel::ConversionFunctions<RegionSequence*, ARA::ARARegionSequenceHostRef>;

    MusicalContext& context;
    String name;
    ARAHostModel::RegionSequence sequence;
};

class AudioModification
{
    auto getProperties() const
    {
        // Celemony TestHost: name + stable persistentID (not a throwaway "x").
        auto properties = ARAHostModel::AudioModification::getEmptyProperties();
        properties.name = nameUTF8.data();
        properties.persistentID = persistentIDUTF8.data();
        return properties;
    }

public:
    AudioModification (ARA::Host::DocumentController& dc, FileAudioSource& source)
        : nameUTF8 (("Mod: " + source.getFormatReader().getFile().getFileName()).toStdString()),
          persistentIDUTF8 ((source.getFormatReader().getFile().getFullPathName() + "|mod").toStdString()),
          modification (Converter::toHostRef (this), dc, source.getSource(), getProperties())
    {
    }

    auto& getModification() { return modification; }

private:
    using Converter = ARAHostModel::ConversionFunctions<AudioModification*, ARA::ARAAudioModificationHostRef>;

    std::string nameUTF8;
    std::string persistentIDUTF8;
    ARAHostModel::AudioModification modification;
};

//==============================================================================
class PlaybackRegion
{
    auto getPlaybackRegionProperties() const
    {
        auto properties = ARAHostModel::PlaybackRegion::getEmptyProperties();
        properties.transformationFlags = ARA::kARAPlaybackTransformationNoChanges;
        properties.startInModificationTime = 0.0;
        const auto& formatReader = audioSource.getFormatReader();
        properties.durationInModificationTime = (double) formatReader.lengthInSamples / formatReader.sampleRate;
        properties.startInPlaybackTime = 0.0;
        properties.durationInPlaybackTime = properties.durationInModificationTime;
        properties.musicalContextRef = sequence.getMusicalContext().getPluginRef();
        properties.regionSequenceRef = sequence.getPluginRef();

        properties.name = nullptr;
        properties.color = nullptr;
        return properties;
    }

public:
    PlaybackRegion (ARA::Host::DocumentController& dc,
                    RegionSequence& s,
                    AudioModification& m,
                    FileAudioSource& source)
        : sequence (s),
          audioSource (source),
          region (Converter::toHostRef (this), dc, m.getModification(), getPlaybackRegionProperties())
    {
        jassert (source.getPluginRef() == m.getModification().getAudioSource().getPluginRef());
    }

    auto& getPlaybackRegion() { return region; }

private:
    using Converter = ARAHostModel::ConversionFunctions<PlaybackRegion*, ARA::ARAPlaybackRegionHostRef>;

    RegionSequence& sequence;
    FileAudioSource& audioSource;
    ARAHostModel::PlaybackRegion region;
};

//==============================================================================
class AudioAccessController final : public ARA::Host::AudioAccessControllerInterface
{
public:
    ARA::ARAAudioReaderHostRef createAudioReaderForSource (ARA::ARAAudioSourceHostRef audioSourceHostRef,
                                                           bool use64BitSamples) noexcept override
    {
        auto audioReader = std::make_unique<AudioReader> (audioSourceHostRef, use64BitSamples);
        auto audioReaderHostRef = Converter::toHostRef (audioReader.get());
        auto* readerPtr = audioReader.get();
        audioReaders.emplace (readerPtr, std::move (audioReader));
        return audioReaderHostRef;
    }

    bool readAudioSamples (ARA::ARAAudioReaderHostRef readerRef,
                           ARA::ARASamplePosition samplePosition,
                           ARA::ARASampleCount samplesPerChannel,
                           void* const* buffers) noexcept override
    {
        const auto use64BitSamples = Converter::fromHostRef (readerRef)->use64Bit;
        auto* audioSource = FileAudioSource::Converter::fromHostRef (Converter::fromHostRef (readerRef)->sourceHostRef);

        if (use64BitSamples)
            return audioSource->readAudioSamples (
                reinterpret_cast<double* const*> (buffers), samplePosition, samplesPerChannel);

        return audioSource->readAudioSamples (
            reinterpret_cast<float* const*> (buffers), samplePosition, samplesPerChannel);
    }

    void destroyAudioReader (ARA::ARAAudioReaderHostRef readerRef) noexcept override
    {
        audioReaders.erase (Converter::fromHostRef (readerRef));
    }

private:
    struct AudioReader
    {
        AudioReader (ARA::ARAAudioSourceHostRef source, bool use64BitSamples)
            : sourceHostRef (source), use64Bit (use64BitSamples)
        {
        }

        ARA::ARAAudioSourceHostRef sourceHostRef;
        bool use64Bit;
    };

    using Converter = ARAHostModel::ConversionFunctions<AudioReader*, ARA::ARAAudioReaderHostRef>;

    std::map<AudioReader*, std::unique_ptr<AudioReader>> audioReaders;
};

class ArchivingController final : public ARA::Host::ArchivingControllerInterface
{
public:
    using ReaderConverter = ARAHostModel::ConversionFunctions<MemoryBlock*, ARA::ARAArchiveReaderHostRef>;
    using WriterConverter = ARAHostModel::ConversionFunctions<MemoryOutputStream*, ARA::ARAArchiveWriterHostRef>;

    ARA::ARASize getArchiveSize (ARA::ARAArchiveReaderHostRef archiveReaderHostRef) noexcept override
    {
        return (ARA::ARASize) ReaderConverter::fromHostRef (archiveReaderHostRef)->getSize();
    }

    bool readBytesFromArchive (ARA::ARAArchiveReaderHostRef archiveReaderHostRef,
                               ARA::ARASize position,
                               ARA::ARASize length,
                               ARA::ARAByte* buffer) noexcept override
    {
        auto* archiveReader = ReaderConverter::fromHostRef (archiveReaderHostRef);

        if ((position + length) <= archiveReader->getSize())
        {
            std::memcpy (buffer, addBytesToPointer (archiveReader->getData(), position), length);
            return true;
        }

        return false;
    }

    bool writeBytesToArchive (ARA::ARAArchiveWriterHostRef archiveWriterHostRef,
                              ARA::ARASize position,
                              ARA::ARASize length,
                              const ARA::ARAByte* buffer) noexcept override
    {
        auto* archiveWriter = WriterConverter::fromHostRef (archiveWriterHostRef);

        if (archiveWriter->setPosition ((int64) position) && archiveWriter->write (buffer, length))
            return true;

        return false;

    }

    void notifyDocumentArchivingProgress (float value) noexcept override
    {
        // Celemony TestHost pattern: surface archive progress for diagnostics.
        DBG ("ARA archiving progress: " << value);
        ignoreUnused (value);
    }

    void notifyDocumentUnarchivingProgress (float value) noexcept override
    {
        DBG ("ARA unarchiving progress: " << value);
        ignoreUnused (value);
    }

    ARA::ARAPersistentID getDocumentArchiveID (ARA::ARAArchiveReaderHostRef archiveReaderHostRef) noexcept override
    {
        ignoreUnused (archiveReaderHostRef);

        // Stable archive ID so SpectraLayers-style plugins can round-trip state.
        return "com.chouchou.chouchouPlugplugin2.ara.archive";
    }
};

class ContentAccessController final : public ARA::Host::ContentAccessControllerInterface
{
public:
    using Converter = ARAHostModel::ConversionFunctions<ARA::ARAContentType, ARA::ARAContentReaderHostRef>;

    bool isMusicalContextContentAvailable (ARA::ARAMusicalContextHostRef musicalContextHostRef,
                                           ARA::ARAContentType type) noexcept override
    {
        ignoreUnused (musicalContextHostRef);

        return (type == ARA::kARAContentTypeTempoEntries || type == ARA::kARAContentTypeBarSignatures);
    }

    ARA::ARAContentGrade getMusicalContextContentGrade (ARA::ARAMusicalContextHostRef musicalContextHostRef,
                                                        ARA::ARAContentType type) noexcept override
    {
        ignoreUnused (musicalContextHostRef, type);

        return ARA::kARAContentGradeInitial;
    }

    ARA::ARAContentReaderHostRef
        createMusicalContextContentReader (ARA::ARAMusicalContextHostRef musicalContextHostRef,
                                           ARA::ARAContentType type,
                                           const ARA::ARAContentTimeRange* range) noexcept override
    {
        ignoreUnused (musicalContextHostRef, range);

        return Converter::toHostRef (type);
    }

    bool isAudioSourceContentAvailable (ARA::ARAAudioSourceHostRef audioSourceHostRef,
                                        ARA::ARAContentType type) noexcept override
    {
        ignoreUnused (audioSourceHostRef, type);

        return false;
    }

    ARA::ARAContentGrade getAudioSourceContentGrade (ARA::ARAAudioSourceHostRef audioSourceHostRef,
                                                     ARA::ARAContentType type) noexcept override
    {
        ignoreUnused (audioSourceHostRef, type);

        return 0;
    }

    ARA::ARAContentReaderHostRef
        createAudioSourceContentReader (ARA::ARAAudioSourceHostRef audioSourceHostRef,
                                        ARA::ARAContentType type,
                                        const ARA::ARAContentTimeRange* range) noexcept override
    {
        ignoreUnused (audioSourceHostRef, type, range);

        return nullptr;
    }

    ARA::ARAInt32 getContentReaderEventCount (ARA::ARAContentReaderHostRef contentReaderHostRef) noexcept override
    {
        const auto contentType = Converter::fromHostRef (contentReaderHostRef);

        if (contentType == ARA::kARAContentTypeTempoEntries || contentType == ARA::kARAContentTypeBarSignatures)
            return 2;

        return 0;
    }

    const void* getContentReaderDataForEvent (ARA::ARAContentReaderHostRef contentReaderHostRef,
                                              ARA::ARAInt32 eventIndex) noexcept override
    {
        // Default host timeline @ 120 BPM / 4/4 — same shape as Celemony MiniHost / TestHost demos.
        if (Converter::fromHostRef (contentReaderHostRef) == ARA::kARAContentTypeTempoEntries)
        {
            if (eventIndex == 0)
            {
                tempoEntry.timePosition = 0.0;
                tempoEntry.quarterPosition = 0.0;
            }
            else if (eventIndex == 1)
            {
                // 60s → 120 quarters @ 120 BPM
                tempoEntry.timePosition = 60.0;
                tempoEntry.quarterPosition = 120.0;
            }

            return &tempoEntry;
        }
        else if (Converter::fromHostRef (contentReaderHostRef) == ARA::kARAContentTypeBarSignatures)
        {
            if (eventIndex == 0)
            {
                barSignature.position = 0.0;
                barSignature.numerator = 4;
                barSignature.denominator = 4;
            }

            if (eventIndex == 1)
            {
                barSignature.position = 120.0; // quarters
                barSignature.numerator = 4;
                barSignature.denominator = 4;
            }

            return &barSignature;
        }

        jassertfalse;
        return nullptr;
    }

    void destroyContentReader (ARA::ARAContentReaderHostRef contentReaderHostRef) noexcept override
    {
        ignoreUnused (contentReaderHostRef);
    }

    ARA::ARAContentTempoEntry tempoEntry;
    ARA::ARAContentBarSignature barSignature;
};

class ModelUpdateController final : public ARA::Host::ModelUpdateControllerInterface
{
public:
    void notifyAudioSourceAnalysisProgress (ARA::ARAAudioSourceHostRef audioSourceHostRef,
                                            ARA::ARAAnalysisProgressState state,
                                            float value) noexcept override
    {
        // Celemony TestHost / real hosts observe analysis; SpectraLayers relies on this path.
        DBG ("ARA analysis progress state=" << (int) state << " value=" << value);
        ignoreUnused (audioSourceHostRef, state, value);
    }

    void notifyAudioSourceContentChanged (ARA::ARAAudioSourceHostRef audioSourceHostRef,
                                          const ARA::ARAContentTimeRange* range,
                                          ARA::ContentUpdateScopes scopeFlags) noexcept override
    {
        ignoreUnused (audioSourceHostRef, range, scopeFlags);
        DBG ("ARA audio source content changed");
    }

    void notifyAudioModificationContentChanged (ARA::ARAAudioModificationHostRef audioModificationHostRef,
                                                const ARA::ARAContentTimeRange* range,
                                                ARA::ContentUpdateScopes scopeFlags) noexcept override
    {
        ignoreUnused (audioModificationHostRef, range, scopeFlags);
        DBG ("ARA audio modification content changed");
    }

    void notifyPlaybackRegionContentChanged (ARA::ARAPlaybackRegionHostRef playbackRegionHostRef,
                                             const ARA::ARAContentTimeRange* range,
                                             ARA::ContentUpdateScopes scopeFlags) noexcept override
    {
        ignoreUnused (playbackRegionHostRef, range, scopeFlags);
        DBG ("ARA playback region content changed");
    }

    void notifyDocumentDataChanged() noexcept override
    {
        DBG ("ARA document data changed");
    }
};

//==============================================================================
/** Transport sink so PlaybackController can drive the embedded host (TestHost documents
    this as the plug-in's means of controlling host transport — APH left it empty). */
struct ARATransportSink
{
    virtual ~ARATransportSink() = default;
    virtual void araRequestStartPlayback() = 0;
    virtual void araRequestStopPlayback() = 0;
    virtual void araRequestSetPlaybackPositionSeconds (double seconds) = 0;
    virtual void araRequestSetCycleRangeSeconds (double start, double duration) = 0;
    virtual void araRequestEnableCycle (bool enable) = 0;
    virtual double araGetTransportSampleRate() const = 0;
};

class PlaybackController final : public ARA::Host::PlaybackControllerInterface
{
public:
    void attach (ARATransportSink& sinkIn) noexcept { sink = &sinkIn; }
    void detach() noexcept { sink = nullptr; }

    void requestStartPlayback() noexcept override
    {
        if (sink != nullptr)
            sink->araRequestStartPlayback();
    }

    void requestStopPlayback() noexcept override
    {
        if (sink != nullptr)
            sink->araRequestStopPlayback();
    }

    void requestSetPlaybackPosition (ARA::ARATimePosition timePosition) noexcept override
    {
        if (sink != nullptr)
            sink->araRequestSetPlaybackPositionSeconds (timePosition);
    }

    void requestSetCycleRange (ARA::ARATimePosition startTime, ARA::ARATimeDuration duration) noexcept override
    {
        if (sink != nullptr)
            sink->araRequestSetCycleRangeSeconds (startTime, duration);
    }

    void requestEnableCycle (bool enable) noexcept override
    {
        if (sink != nullptr)
            sink->araRequestEnableCycle (enable);
    }

private:
    ARATransportSink* sink = nullptr;
};

struct SimplePlayHead final : public juce::AudioPlayHead
{
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo result;
        const auto samples = timeInSamples.load();
        const auto sr = sampleRate.load();
        const auto bpm = tempoBpm.load();

        result.setTimeInSamples (samples);
        result.setIsPlaying (isPlaying.load());
        result.setBpm (bpm);

        if (sr > 0.0)
        {
            result.setTimeInSeconds ((double) samples / sr);
            // Quarters from samples at current BPM (120 default ⇒ 2 beats/sec).
            const double seconds = (double) samples / sr;
            result.setPpqPosition (seconds * (bpm / 60.0));
        }

        return result;
    }

    std::atomic<int64_t> timeInSamples { 0 };
    std::atomic<bool> isPlaying { false };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<double> tempoBpm { 120.0 };
};

struct HostPlaybackController
{
    virtual ~HostPlaybackController() = default;

    virtual void setPlaying (bool isPlaying) = 0;
    virtual void goToStart() = 0;
    virtual File getAudioSource() const = 0;
    virtual void setAudioSource (File audioSourceFile) = 0;
    virtual void clearAudioSource() = 0;
    virtual double getPlayHeadSeconds() const = 0;
    virtual bool getIsPlaying() const = 0;
};

class AudioSourceComponent final : public Component,
                                   public FileDragAndDropTarget,
                                   public ChangeListener,
                                   private Timer
{
public:
    explicit AudioSourceComponent (HostPlaybackController& controller, juce::ChangeBroadcaster& bc)
        : hostPlaybackController (controller),
          broadcaster (bc),
          waveformComponent (*this)
    {
        audioSourceLabel.setText ("You can drag and drop .wav files here", NotificationType::dontSendNotification);

        addAndMakeVisible (audioSourceLabel);
        addAndMakeVisible (waveformComponent);
        addAndMakeVisible (transportLabel);

        transportLabel.setJustificationType (Justification::centred);
        transportLabel.setColour (Label::textColourId, Colours::lightgrey);

        playButton.setButtonText ("Play / Pause");
        playButton.onClick = [this]
        {
            const bool next = ! hostPlaybackController.getIsPlaying();
            hostPlaybackController.setPlaying (next);
            refreshTransportLabel();
        };

        goToStartButton.setButtonText ("Go to start");
        goToStartButton.onClick = [this]
        {
            hostPlaybackController.goToStart();
            refreshTransportLabel();
            waveformComponent.repaint();
        };

        addAndMakeVisible (goToStartButton);
        addAndMakeVisible (playButton);

        broadcaster.addChangeListener (this);
        startTimerHz (20);

        update();
    }

    ~AudioSourceComponent() override
    {
        stopTimer();
        broadcaster.removeChangeListener (this);
    }

    void changeListenerCallback (ChangeBroadcaster*) override
    {
        update();
    }

    void timerCallback() override
    {
        refreshTransportLabel();
        waveformComponent.repaint();
    }

    void refreshTransportLabel()
    {
        const double t = hostPlaybackController.getPlayHeadSeconds();
        const bool playing = hostPlaybackController.getIsPlaying();
        transportLabel.setText (String (playing ? "PLAY  " : "STOP  ")
                                    + String (t, 3) + " s",
                                NotificationType::dontSendNotification);
    }

    void resized() override
    {
        auto localBounds = getLocalBounds();
        auto buttonsArea = localBounds.removeFromBottom (40).reduced (5);
        auto transportArea = localBounds.removeFromBottom (22).reduced (5, 0);
        auto waveformArea = localBounds.removeFromBottom (150).reduced (5);

        juce::FlexBox fb;
        fb.justifyContent = juce::FlexBox::JustifyContent::center;
        fb.alignContent = juce::FlexBox::AlignContent::center;

        fb.items = { juce::FlexItem (goToStartButton).withMinWidth (100.0f).withMinHeight ((float) buttonsArea.getHeight()),
                     juce::FlexItem (playButton).withMinWidth (100.0f).withMinHeight ((float) buttonsArea.getHeight()) };

        fb.performLayout (buttonsArea);

        transportLabel.setBounds (transportArea);
        waveformComponent.setBounds (waveformArea);

        audioSourceLabel.setBounds (localBounds);
    }

    bool isInterestedInFileDrag (const StringArray& files) override
    {
        if (files.size() != 1)
            return false;

        if (files.getReference (0).endsWithIgnoreCase (".wav"))
            return true;

        return false;
    }

    void update()
    {
        const auto currentAudioSource = hostPlaybackController.getAudioSource();

        if (currentAudioSource.existsAsFile())
        {
            waveformComponent.setSource (currentAudioSource);
            audioSourceLabel.setText (currentAudioSource.getFullPathName(),
                                      NotificationType::dontSendNotification);
        }
        else
        {
            waveformComponent.clearSource();
            audioSourceLabel.setText ("You can drag and drop .wav files here", NotificationType::dontSendNotification);
        }
    }

    void filesDropped (const StringArray& files, int, int) override
    {
        hostPlaybackController.setAudioSource (files.getReference (0));
        update();
    }

private:
    class WaveformComponent final : public Component,
                                    public ChangeListener
    {
    public:
        WaveformComponent (AudioSourceComponent& p)
            : parent (p),
              thumbCache (7),
              audioThumb (128, formatManager, thumbCache)
        {
            setWantsKeyboardFocus (true);
            formatManager.registerBasicFormats();
            audioThumb.addChangeListener (this);
        }

        ~WaveformComponent() override
        {
            audioThumb.removeChangeListener (this);
        }

        void mouseDown (const MouseEvent&) override
        {
            isSelected = true;
            repaint();
        }

        void changeListenerCallback (ChangeBroadcaster*) override
        {
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            if (! isEmpty)
            {
                auto rect = getLocalBounds();

                const auto waveformColour = Colours::cadetblue;

                if (rect.getWidth() > 2)
                {
                    g.setColour (isSelected ? juce::Colours::yellow : juce::Colours::black);
                    g.drawRect (rect);
                    rect.reduce (1, 1);
                    g.setColour (waveformColour.darker (1.0f));
                    g.fillRect (rect);
                }

                g.setColour (Colours::cadetblue);
                const auto total = audioThumb.getTotalLength();
                audioThumb.drawChannels (g, rect, 0.0, total, 1.0f);

                if (total > 0.0)
                {
                    const auto t = parent.hostPlaybackController.getPlayHeadSeconds();
                    const auto x = rect.getX() + (int) std::lround ((t / total) * (double) rect.getWidth());
                    g.setColour (Colours::yellow);
                    g.drawVerticalLine (juce::jlimit (rect.getX(), rect.getRight() - 1, x),
                                        (float) rect.getY(),
                                        (float) rect.getBottom());
                }
            }
        }

        void setSource (const File& source)
        {
            isEmpty = false;
            audioThumb.setSource (new FileInputSource (source));
        }

        void clearSource()
        {
            isEmpty = true;
            isSelected = false;
            audioThumb.clear();
        }

        bool keyPressed (const KeyPress& key) override
        {
            if (isSelected && key == KeyPress::deleteKey)
            {
                parent.hostPlaybackController.clearAudioSource();
                return true;
            }

            return false;
        }

    private:
        AudioSourceComponent& parent;

        bool isEmpty = true;
        bool isSelected = false;
        AudioFormatManager formatManager;
        AudioThumbnailCache thumbCache;
        AudioThumbnail audioThumb;
    };

    HostPlaybackController& hostPlaybackController;
    juce::ChangeBroadcaster& broadcaster;
    Label audioSourceLabel;
    Label transportLabel;
    WaveformComponent waveformComponent;
    TextButton playButton, goToStartButton;
};

class ARAPluginInstanceWrapper final : public AudioPluginInstance
{
public:
    class ARATestHost final : public HostPlaybackController,
                              public ARATransportSink,
                              public juce::ChangeBroadcaster
    {
    public:
        class Editor final : public AudioProcessorEditor
        {
        public:
            explicit Editor (ARATestHost& araTestHost)
                : AudioProcessorEditor (araTestHost.getAudioPluginInstance()),
                  audioSourceComponent (araTestHost, araTestHost)
            {
                audioSourceComponent.update();
                addAndMakeVisible (audioSourceComponent);
                setSize (512, 250);
            }

            ~Editor() override { getAudioProcessor()->editorBeingDeleted (this); }

            void resized() override { audioSourceComponent.setBounds (getLocalBounds()); }

        private:
            AudioSourceComponent audioSourceComponent;
        };

        explicit ARATestHost (ARAPluginInstanceWrapper& instanceIn)
            : instance (instanceIn)
        {
            // SpectraLayers VST3 often reports category "OnlyARA" with hasARAExtension=0.
            // Still must start the ARA factory or Capture / Host UI are no-ops.
            const auto pd = instance.inner->getPluginDescription();
            const bool wantsARA = pd.hasARAExtension
                                  || pd.category.containsIgnoreCase ("ARA")
                                  || pd.name.containsIgnoreCase ("SpectraLayers")
                                  || pd.fileOrIdentifier.containsIgnoreCase ("SpectraLayers");

            if (wantsARA)
            {
                instance.inner->setPlayHead (&playHead);

                createARAFactoryAsync (*instance.inner, [this] (ARAFactoryWrapper araFactory)
                                                        {
                                                            init (std::move (araFactory));
                                                        });
            }
        }

        void init (ARAFactoryWrapper araFactory)
        {
            if (araFactory.get() != nullptr)
            {
                // Keep raw pointers so we can wire Controllers after ownership moves into
                // ARAHostDocumentController (Celemony DocumentControllerHostInstance pattern).
                auto playback = std::make_unique<PlaybackController>();
                playbackController = playback.get();

                documentController = ARAHostDocumentController::create (
                    std::move (araFactory),
                    "chouchouPlugplugin2Document",
                    std::make_unique<AudioAccessController>(),
                    std::make_unique<ArchivingController>(),
                    std::make_unique<ContentAccessController>(),
                    std::make_unique<ModelUpdateController>(),
                    std::move (playback));

                if (documentController != nullptr)
                {
                    if (playbackController != nullptr)
                        playbackController->attach (*this);

                    const auto allRoles = ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole;
                    const auto plugInExtensionInstance = documentController->bindDocumentToPluginInstance (*instance.inner,
                                                                                                           allRoles,
                                                                                                           allRoles);
                    playbackRenderer = plugInExtensionInstance.getPlaybackRendererInterface();
                    editorRenderer   = plugInExtensionInstance.getEditorRendererInterface();
                    synchronizeStateWithDocumentController();
                    sendChangeMessage();
                }
                else
                    jassertfalse;
            }
            else
                jassertfalse;
        }

        //==============================================================================
        // ARATransportSink — plugin → host transport (TestHost Host Interfaces role)
        void araRequestStartPlayback() override { setPlaying (true); }
        void araRequestStopPlayback() override  { setPlaying (false); }

        void araRequestSetPlaybackPositionSeconds (double seconds) override
        {
            followOuterPlayHead.store (false);
            const double sr = araGetTransportSampleRate();
            if (sr > 0.0)
                playHead.timeInSamples.store ((int64_t) std::llround (seconds * sr));
        }

        void araRequestSetCycleRangeSeconds (double start, double duration) override
        {
            cycleStartSeconds.store (start);
            cycleDurationSeconds.store (duration);
        }

        void araRequestEnableCycle (bool enable) override { cycleEnabled.store (enable); }

        double araGetTransportSampleRate() const override
        {
            if (instance.prepareToPlayParams.isValid)
                return instance.prepareToPlayParams.sampleRate;

            return playHead.sampleRate.load();
        }

        void afterProcessBlock (int numSamples)
        {
            // Apply pending seek before advancing (and before the next outer sync).
            if (goToStartSignal.exchange (false))
                playHead.timeInSamples.store (0);

            const auto isPlayingNow = isPlaying.load();
            playHead.isPlaying.store (isPlayingNow);

            if (instance.prepareToPlayParams.isValid)
                playHead.sampleRate.store (instance.prepareToPlayParams.sampleRate);

            if (isPlayingNow)
            {
                const auto currentAudioSourceLength = audioSourceLength.load();
                const auto currentPlayHeadPosition = playHead.timeInSamples.load();
                const double sr = araGetTransportSampleRate();

                if (currentAudioSourceLength - currentPlayHeadPosition < numSamples)
                {
                    if (cycleEnabled.load() && cycleDurationSeconds.load() > 0.0 && sr > 0.0)
                    {
                        const auto cycleStart = (int64_t) std::llround (cycleStartSeconds.load() * sr);
                        playHead.timeInSamples.store (cycleStart);
                    }
                    else
                        playHead.timeInSamples.store (0);
                }
                else
                    playHead.timeInSamples.fetch_add (numSamples);
            }
        }

        File getAudioSource() const override
        {
            std::lock_guard<std::mutex> lock { instance.innerMutex };

            if (context != nullptr)
                return context->audioFile;

            return {};
        }

        void setAudioSource (File audioSourceFile) override
        {
            if (audioSourceFile.existsAsFile())
            {
                {
                    std::lock_guard<std::mutex> lock { contextUpdateSourceMutex };
                    contextUpdateSource = ContextUpdateSource (std::move (audioSourceFile));
                }

                synchronise();
            }
        }

        void clearAudioSource() override
        {
            {
                std::lock_guard<std::mutex> lock { contextUpdateSourceMutex };
                contextUpdateSource = ContextUpdateSource (ContextUpdateSource::Type::reset);
            }

            synchronise();
        }

        void setPlaying (bool isPlayingIn) override
        {
            // Local Play / Pause takes ownership of the playhead (Audition sync would fight us).
            followOuterPlayHead.store (false);
            isPlaying.store (isPlayingIn);
            playHead.isPlaying.store (isPlayingIn);
        }

        void goToStart() override
        {
            followOuterPlayHead.store (false);
            goToStartSignal.store (true);
            playHead.timeInSamples.store (0); // immediate — don't wait for next processBlock
            sendChangeMessage();
        }

        double getPlayHeadSeconds() const override
        {
            const double sr = araGetTransportSampleRate();
            if (sr <= 0.0)
                return 0.0;

            return (double) playHead.timeInSamples.load() / sr;
        }

        bool getIsPlaying() const override { return isPlaying.load(); }

        void syncFromOuterPlayHead (AudioPlayHead* outer, int /*numSamples*/)
        {
            if (outer == nullptr)
                return;

            auto pos = outer->getPosition();
            if (! pos)
                return;

            const bool playingNow = pos->getIsPlaying();
            const bool wasPlaying = lastOuterPlaying.exchange (playingNow);

            // DAW transport starting again takes the playhead back from SpectraLayers' own Play.
            if (playingNow && ! wasPlaying)
                followOuterPlayHead.store (true);

            // While the ARA Host panel / plug-in drives transport, ignore outer DAW playhead.
            if (! followOuterPlayHead.load())
                return;

            if (auto ts = pos->getTimeInSamples())
                playHead.timeInSamples.store (*ts - regionStartSample.load());

            if (auto bpm = pos->getBpm())
                playHead.tempoBpm.store (*bpm);

            playHead.isPlaying.store (playingNow);
            isPlaying.store (playingNow);
        }

        /** Which part of the next block lies inside the bound clip on the DAW timeline.
            process == false: leave the block dry (DAW stopped, or clip not under the playhead). */
        struct RegionGate
        {
            bool process = true;
            int from = 0, to = 0;
        };

        RegionGate getRegionGate (AudioPlayHead* outer, int numSamples) const
        {
            RegionGate gate { true, 0, numSamples };
            const double secs = audioSourceSeconds.load();
            const double sr = araGetTransportSampleRate();

            if (secs <= 0.0 || sr <= 0.0 || outer == nullptr || ! followOuterPlayHead.load())
                return gate;

            auto pos = outer->getPosition();
            if (! pos)
                return gate;

            if (! pos->getIsPlaying())
            {
                gate.process = false;
                return gate;
            }

            auto ts = pos->getTimeInSamples();
            if (! ts)
                return gate;

            const auto start = regionStartSample.load();
            const auto end = start + (int64) std::llround (secs * sr);
            gate.from = (int) jlimit<int64> (0, numSamples, start - *ts);
            gate.to   = (int) jlimit<int64> (0, numSamples, end - *ts);
            gate.process = gate.from < gate.to;
            return gate;
        }

        /** Offline render owns the transport; returns the previous follow state for endOfflineRender. */
        bool beginOfflineRender()
        {
            const bool was = followOuterPlayHead.exchange (false);
            isPlaying.store (true);
            playHead.isPlaying.store (true);
            return was;
        }

        void setOfflinePosition (int64 samples) { playHead.timeInSamples.store (samples); }

        void endOfflineRender (bool wasFollowing)
        {
            isPlaying.store (false);
            playHead.isPlaying.store (false);
            followOuterPlayHead.store (wasFollowing);
        }

        /** Host-timeline sample where sample 0 of the bound file sits (may be negative). */
        std::atomic<int64> regionStartSample { 0 };
        /** Length of the bound file in seconds; 0 when nothing is bound. */
        std::atomic<double> audioSourceSeconds { 0.0 };

        Editor* createEditor() { return new Editor (*this); }

        AudioPluginInstance& getAudioPluginInstance() { return instance; }

        bool isDocumentReady() const { return documentController != nullptr; }
        bool hasBoundAudioSource() const { return context != nullptr; }

        void getStateInformation (juce::MemoryBlock& b)
        {
            std::lock_guard<std::mutex> configurationLock (instance.innerMutex);

            if (context != nullptr)
                context->getStateInformation (b);
        }

        void setStateInformation (const void* d, int s)
        {
            {
                std::lock_guard<std::mutex> lock { contextUpdateSourceMutex };
                contextUpdateSource = ContextUpdateSource { d, s };
            }

            synchronise();
        }

        ~ARATestHost()
        {
            if (playbackController != nullptr)
                playbackController->detach();

            instance.inner->releaseResources();
        }

    private:
        /**  Use this to put the plugin in an unprepared state for the duration of adding and removing PlaybackRegions
             to and from Renderers.
        */
        class ScopedPluginDeactivator
        {
        public:
            explicit ScopedPluginDeactivator (ARAPluginInstanceWrapper& inst) : instance (inst)
            {
                if (instance.prepareToPlayParams.isValid)
                    instance.inner->releaseResources();
            }

            ~ScopedPluginDeactivator()
            {
                if (instance.prepareToPlayParams.isValid)
                    instance.inner->prepareToPlay (instance.prepareToPlayParams.sampleRate,
                                                   instance.prepareToPlayParams.samplesPerBlock);
            }

        private:
            ARAPluginInstanceWrapper& instance;

            JUCE_DECLARE_NON_COPYABLE (ScopedPluginDeactivator)
        };

        class ContextUpdateSource
        {
        public:
            enum class Type
            {
                empty,
                audioSourceFile,
                stateInformation,
                reset
            };

            ContextUpdateSource() = default;

            explicit ContextUpdateSource (const File& file)
                : type (Type::audioSourceFile),
                  audioSourceFile (file)
            {
            }

            ContextUpdateSource (const void* d, int s)
                : type (Type::stateInformation),
                  stateInformation (d, (size_t) s)
            {
            }

            ContextUpdateSource (Type t) : type (t)
            {
                jassert (t == Type::reset);
            }

            Type getType() const { return type; }

            const File& getAudioSourceFile() const
            {
                jassert (type == Type::audioSourceFile);

                return audioSourceFile;
            }

            const MemoryBlock& getStateInformation() const
            {
                jassert (type == Type::stateInformation);

                return stateInformation;
            }

        private:
            Type type = Type::empty;

            File audioSourceFile;
            MemoryBlock stateInformation;
        };

        void synchronise()
        {
            const SpinLock::ScopedLockType scope (instance.innerProcessBlockFlag);
            std::lock_guard<std::mutex> configurationLock (instance.innerMutex);
            synchronizeStateWithDocumentController();
        }

        void synchronizeStateWithDocumentController()
        {
            if (documentController == nullptr)
                return;

            bool resetContext = false;

            auto newContext = [&]() -> std::unique_ptr<Context>
            {
                std::lock_guard<std::mutex> lock { contextUpdateSourceMutex };

                switch (contextUpdateSource.getType())
                {
                    case ContextUpdateSource::Type::empty:
                        return {};

                    case ContextUpdateSource::Type::audioSourceFile:
                        if (! (contextUpdateSource.getAudioSourceFile().existsAsFile()))
                            return {};

                        {
                            // Single edit cycle for the whole graph (TestHost beginEditing pattern).
                            const ARAEditGuard editGuard (documentController->getDocumentController());
                            try
                            {
                                return std::make_unique<Context> (documentController->getDocumentController(),
                                                                  contextUpdateSource.getAudioSourceFile());
                            }
                            catch (...)
                            {
                                return {};
                            }
                        }

                    case ContextUpdateSource::Type::stateInformation:
                        jassert (contextUpdateSource.getStateInformation().getSize() <= std::numeric_limits<int>::max());

                        return Context::createFromStateInformation (documentController->getDocumentController(),
                                                                    contextUpdateSource.getStateInformation().getData(),
                                                                    (int) contextUpdateSource.getStateInformation().getSize());

                    case ContextUpdateSource::Type::reset:
                        resetContext = true;
                        return {};
                }

                jassertfalse;
                return {};
            }();

            if (newContext != nullptr)
            {
                {
                    ScopedPluginDeactivator deactivator (instance);

                    // Destroy previous context first (ManagedARAHandle tears down bottom-up
                    // inside nested ARAEditGuards — same order as TestHost::destroyDocument).
                    context.reset();
                    context = std::move (newContext);
                    const auto& reader = context->fileAudioSource.getFormatReader();
                    audioSourceLength.store (reader.lengthInSamples);
                    audioSourceSeconds.store (reader.sampleRate > 0.0 ? (double) reader.lengthInSamples / reader.sampleRate
                                                                      : 0.0);

                    auto& region = context->playbackRegion.getPlaybackRegion();
                    playbackRenderer.add (region);
                    editorRenderer.add (region);
                }

                sendChangeMessage();
            }

            if (resetContext)
            {
                {
                    ScopedPluginDeactivator deactivator (instance);

                    context.reset();
                    audioSourceLength.store (0);
                    audioSourceSeconds.store (0.0);
                }

                sendChangeMessage();
            }
        }

        struct Context
        {
            Context (ARA::Host::DocumentController& dc, const File& audioFileIn)
                : audioFile (audioFileIn),
                  musicalContext    (dc),
                  regionSequence    (dc, musicalContext, "chouchou track 1"),
                  fileAudioSource   (dc, audioFile),
                  audioModification (dc, fileAudioSource),
                  playbackRegion    (dc, regionSequence, audioModification, fileAudioSource)
            {
                // Celemony TestHost: after building the graph, publish musical-context content
                // so the plug-in re-reads tempo / bar signatures via ContentAccessController.
                const auto scopes = ARA::ContentUpdateScopes::timelineIsAffected();
                dc.updateMusicalContextContent (musicalContext.getPluginRef(), nullptr, scopes);
                dc.updateAudioSourceContent (fileAudioSource.getPluginRef(), nullptr, scopes);
            }

            static std::unique_ptr<Context> createFromStateInformation (ARA::Host::DocumentController& dc, const void* d, int s)
            {
                if (auto xml = getXmlFromBinary (d, s))
                {
                    if (xml->hasTagName (xmlRootTag))
                    {
                        File file { xml->getStringAttribute (xmlAudioFileAttrib) };

                        if (file.existsAsFile())
                            return std::make_unique<Context> (dc, std::move (file));
                    }
                }

                return {};
            }

            void getStateInformation (juce::MemoryBlock& b)
            {
                XmlElement root { xmlRootTag };
                root.setAttribute (xmlAudioFileAttrib, audioFile.getFullPathName());
                copyXmlToBinary (root, b);
            }

            const static Identifier xmlRootTag;
            const static Identifier xmlAudioFileAttrib;

            File audioFile;

            MusicalContext musicalContext;
            RegionSequence regionSequence;
            FileAudioSource fileAudioSource;
            AudioModification audioModification;
            PlaybackRegion playbackRegion;
        };

        SimplePlayHead playHead;
        ARAPluginInstanceWrapper& instance;

        std::unique_ptr<ARAHostDocumentController> documentController;
        PlaybackController* playbackController = nullptr;
        ARAHostModel::PlaybackRendererInterface playbackRenderer;
        ARAHostModel::EditorRendererInterface editorRenderer;

        std::unique_ptr<Context> context;

        mutable std::mutex contextUpdateSourceMutex;
        ContextUpdateSource contextUpdateSource;

        std::atomic<bool> isPlaying { false };
        std::atomic<bool> goToStartSignal { false };
        /** When false, Audition/DAW playhead sync is ignored so local Play / Go to start stick. */
        std::atomic<bool> followOuterPlayHead { true };
        std::atomic<bool> lastOuterPlaying { false };
        std::atomic<int64> audioSourceLength { 0 };
        std::atomic<bool> cycleEnabled { false };
        std::atomic<double> cycleStartSeconds { 0.0 };
        std::atomic<double> cycleDurationSeconds { 0.0 };
    };

    explicit ARAPluginInstanceWrapper (std::unique_ptr<AudioPluginInstance> innerIn)
        : inner (std::move (innerIn)), araHost (*this)
    {
        jassert (inner != nullptr);

        for (auto isInput : { true, false })
            matchBuses (isInput);

        setBusesLayout (inner->getBusesLayout());
    }

    //==============================================================================
    AudioProcessorEditor* createARAHostEditor() { return araHost.createEditor(); }

    /** True once createARAFactoryAsync finished and DocumentController exists. */
    bool isARADocumentReady() const { return araHost.isDocumentReady(); }

    /** Assign a WAV (or other readable) file as the ARA AudioSource — used for live capture + file pick. */
    void assignAudioSourceFile (const File& file) { araHost.setAudioSource (file); }

    void clearAudioSourceFile() { araHost.clearAudioSource(); }

    File getAssignedAudioSourceFile() const { return araHost.getAudioSource(); }

    /** True if a PlaybackRegion Context is currently bound. */
    bool hasBoundAudioSource() const { return araHost.hasBoundAudioSource(); }

    /** Best-effort: follow outer DAW playhead when available; else advance locally while playing. */
    void syncFromOuterPlayHead (AudioPlayHead* outer, int numSamples)
    {
        araHost.syncFromOuterPlayHead (outer, numSamples);
    }

    using RegionGate = ARATestHost::RegionGate;
    RegionGate getRegionGate (AudioPlayHead* outer, int numSamples) const { return araHost.getRegionGate (outer, numSamples); }

    void setRegionStartSample (int64 hostSample) { araHost.regionStartSample.store (hostSample); }
    int64 getRegionStartSample() const { return araHost.regionStartSample.load(); }
    double getBoundSourceSeconds() const { return araHost.audioSourceSeconds.load(); }
    double getPreparedSampleRate() const { return prepareToPlayParams.isValid ? prepareToPlayParams.sampleRate : 0.0; }

    /** Pull the plug-in's playback output for the whole bound clip into a WAV. Call off the
        audio and message threads; the live audio path stays dry for this slot while it runs. */
    bool renderBoundSourceToFile (const File& outFile, String& error)
    {
        const double secs = araHost.audioSourceSeconds.load();
        if (secs <= 0.0)
        {
            error = "No audio is bound to this slot yet.";
            return false;
        }

        if (! prepareToPlayParams.isValid)
        {
            error = "Plug-in not prepared yet - press Play once in the DAW, then Render.";
            return false;
        }

        const double sr = prepareToPlayParams.sampleRate;
        const int bs = jmax (32, prepareToPlayParams.samplesPerBlock);
        const auto total = (int64) std::llround (secs * sr);
        const int numCh = jmax (1, inner->getTotalNumInputChannels(), inner->getTotalNumOutputChannels());
        const int mainOut = inner->getMainBusNumOutputChannels();
        const int outCh = jlimit (1, 2, mainOut > 0 ? mainOut : numCh);

        if (! outFile.getParentDirectory().createDirectory())
        {
            error = "Cannot create folder: " + outFile.getParentDirectory().getFullPathName();
            return false;
        }

        TemporaryFile temp (outFile);
        {
            std::unique_ptr<OutputStream> out (temp.getFile().createOutputStream());
            if (out == nullptr)
            {
                error = "Cannot write: " + outFile.getFullPathName();
                return false;
            }

            WavAudioFormat wav;
            std::unique_ptr<AudioFormatWriter> writer (wav.createWriterFor (out.get(), sr, (unsigned int) outCh, 24, {}, 0));
            if (writer == nullptr)
            {
                error = "Cannot create WAV writer.";
                return false;
            }
            out.release();

            AudioBuffer<float> buffer (numCh, bs);
            MidiBuffer midi;
            bool ok = true;

            {
                const SpinLock::ScopedLockType audioLock (innerProcessBlockFlag);
                const bool wasFollowing = araHost.beginOfflineRender();
                inner->setNonRealtime (true);

                for (int64 pos = 0; pos < total && ok; pos += bs)
                {
                    const int n = (int) jmin<int64> (bs, total - pos);
                    araHost.setOfflinePosition (pos);
                    buffer.setSize (numCh, n, false, false, true);
                    buffer.clear();
                    midi.clear();
                    inner->processBlock (buffer, midi);
                    ok = writer->writeFromAudioSampleBuffer (buffer, 0, n);
                }

                inner->setNonRealtime (false);
                araHost.endOfflineRender (wasFollowing);
            }

            if (! ok || ! writer->flush())
            {
                error = "Writing the rendered WAV failed.";
                return false;
            }
        }

        if (! temp.overwriteTargetFileWithTemporary())
        {
            error = "Cannot replace " + outFile.getFullPathName();
            return false;
        }

        error.clear();
        return true;
    }

    //==============================================================================
    const String getName() const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->getName();
    }

    StringArray getAlternateDisplayNames() const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->getAlternateDisplayNames();
    }

    double getTailLengthSeconds() const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->getTailLengthSeconds();
    }

    bool acceptsMidi() const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->acceptsMidi();
    }

    bool producesMidi() const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->producesMidi();
    }

    AudioProcessorEditor* createEditor() override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->createEditorAndMakeActive();
    }

    bool hasEditor() const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->hasEditor();
    }

    int getNumPrograms() override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->getNumPrograms();
    }

    int getCurrentProgram() override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->getCurrentProgram();
    }

    void setCurrentProgram (int i) override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->setCurrentProgram (i);
    }

    const String getProgramName (int i) override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->getProgramName (i);
    }

    void changeProgramName (int i, const String& n) override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->changeProgramName (i, n);
    }

    void getStateInformation (juce::MemoryBlock& b) override
    {
        XmlElement state ("ARAPluginInstanceWrapperState");
        state.setAttribute ("regionStart", String (araHost.regionStartSample.load()));

        {
            MemoryBlock m;
            araHost.getStateInformation (m);
            state.createNewChildElement ("host")->addTextElement (m.toBase64Encoding());
        }

        {
            std::lock_guard<std::mutex> lock (innerMutex);

            MemoryBlock m;
            inner->getStateInformation (m);
            state.createNewChildElement ("plugin")->addTextElement (m.toBase64Encoding());
        }

        copyXmlToBinary (state, b);
    }

    void setStateInformation (const void* d, int s) override
    {
        if (auto xml = getXmlFromBinary (d, s))
        {
            if (xml->hasTagName ("ARAPluginInstanceWrapperState"))
            {
                araHost.regionStartSample.store (xml->getStringAttribute ("regionStart").getLargeIntValue());

                if (auto* hostState = xml->getChildByName ("host"))
                {
                    MemoryBlock m;
                    m.fromBase64Encoding (hostState->getAllSubText());
                    jassert (m.getSize() <= std::numeric_limits<int>::max());
                    araHost.setStateInformation (m.getData(), (int) m.getSize());
                }

                if (auto* pluginState = xml->getChildByName ("plugin"))
                {
                    std::lock_guard<std::mutex> lock (innerMutex);

                    MemoryBlock m;
                    m.fromBase64Encoding (pluginState->getAllSubText());
                    jassert (m.getSize() <= std::numeric_limits<int>::max());
                    inner->setStateInformation (m.getData(), (int) m.getSize());
                }
            }
        }
    }

    void getCurrentProgramStateInformation (juce::MemoryBlock& b) override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->getCurrentProgramStateInformation (b);
    }

    void setCurrentProgramStateInformation (const void* d, int s) override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->setCurrentProgramStateInformation (d, s);
    }

    void prepareToPlay (double sr, int bs) override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->setRateAndBufferSizeDetails (sr, bs);
        inner->prepareToPlay (sr, bs);
        prepareToPlayParams = { sr, bs };
    }

    void releaseResources() override { inner->releaseResources(); }

    void memoryWarningReceived() override { inner->memoryWarningReceived(); }

    void processBlock (AudioBuffer<float>& a, MidiBuffer& m) override
    {
        const SpinLock::ScopedTryLockType scope (innerProcessBlockFlag);

        if (! scope.isLocked())
            return;

        inner->processBlock (a, m);
        araHost.afterProcessBlock (a.getNumSamples());
    }

    void processBlock (AudioBuffer<double>& a, MidiBuffer& m) override
    {
        const SpinLock::ScopedTryLockType scope (innerProcessBlockFlag);

        if (! scope.isLocked())
            return;

        inner->processBlock (a, m);
        araHost.afterProcessBlock (a.getNumSamples());
    }

    void processBlockBypassed (AudioBuffer<float>& a, MidiBuffer& m) override
    {
        const SpinLock::ScopedTryLockType scope (innerProcessBlockFlag);

        if (! scope.isLocked())
            return;

        inner->processBlockBypassed (a, m);
        araHost.afterProcessBlock (a.getNumSamples());
    }

    void processBlockBypassed (AudioBuffer<double>& a, MidiBuffer& m) override
    {
        const SpinLock::ScopedTryLockType scope (innerProcessBlockFlag);

        if (! scope.isLocked())
            return;

        inner->processBlockBypassed (a, m);
        araHost.afterProcessBlock (a.getNumSamples());
    }

    bool supportsDoublePrecisionProcessing() const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->supportsDoublePrecisionProcessing();
    }

    bool supportsMPE() const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->supportsMPE();
    }

    bool isMidiEffect() const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->isMidiEffect();
    }

    void reset() override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->reset();
    }

    void setNonRealtime (bool b) noexcept override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->setNonRealtime (b);
    }

    void refreshParameterList() override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->refreshParameterList();
    }

    void numChannelsChanged() override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->numChannelsChanged();
    }

    void numBusesChanged() override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->numBusesChanged();
    }

    void processorLayoutsChanged() override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->processorLayoutsChanged();
    }

    void setPlayHead (AudioPlayHead* p) override { ignoreUnused (p); }

    void updateTrackProperties (const TrackProperties& p) override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        inner->updateTrackProperties (p);
    }

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return inner->checkBusesLayoutSupported (layout);
    }

    bool canAddBus (bool) const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return true;
    }
    bool canRemoveBus (bool) const override
    {
        std::lock_guard<std::mutex> lock (innerMutex);
        return true;
    }

    //==============================================================================
    void fillInPluginDescription (PluginDescription& description) const override
    {
        return inner->fillInPluginDescription (description);
    }

private:
    void matchBuses (bool isInput)
    {
        const auto inBuses = inner->getBusCount (isInput);

        while (getBusCount (isInput) < inBuses)
            addBus (isInput);

        while (inBuses < getBusCount (isInput))
            removeBus (isInput);
    }

    // Used for mutual exclusion between the audio and other threads
    SpinLock innerProcessBlockFlag;

    // Used for mutual exclusion on non-audio threads
    mutable std::mutex innerMutex;

    std::unique_ptr<AudioPluginInstance> inner;

    ARATestHost araHost;

    struct PrepareToPlayParams
    {
        PrepareToPlayParams() : isValid (false) {}

        PrepareToPlayParams (double sampleRateIn, int samplesPerBlockIn)
            : isValid (true), sampleRate (sampleRateIn), samplesPerBlock (samplesPerBlockIn)
        {
        }

        bool isValid;
        double sampleRate;
        int samplesPerBlock;
    };

    PrepareToPlayParams prepareToPlayParams;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ARAPluginInstanceWrapper)
};
#endif
