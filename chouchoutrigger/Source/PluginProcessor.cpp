#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
    float dbToGainFloor (float db)
    {
        return db <= ChouchouTriggerAudioProcessor::kLevelFloorDb ? 0.0f : juce::Decibels::decibelsToGain (db);
    }

    juce::String dbText (float v, int)
    {
        if (v <= ChouchouTriggerAudioProcessor::kLevelFloorDb)
            return "-inf dB";
        return juce::String (v, 1) + " dB";
    }

    bool validSlot (int slot) { return slot >= 1 && slot <= ChouchouTriggerAudioProcessor::kNumSlots; }

    const juce::Identifier kSamplesTag { "SAMPLES" };
    const juce::Identifier kSlotTag    { "SLOT" };
}

//==============================================================================
ChouchouTriggerAudioProcessor::ChouchouTriggerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "chouchouTrigger", createLayout())
{
    formatManager.registerBasicFormats();

    pThreshold = apvts.getRawParameterValue ("threshold");
    pRetrigger = apvts.getRawParameterValue ("retrigger");
    pRelease   = apvts.getRawParameterValue ("release");
    pDry       = apvts.getRawParameterValue ("dry");
    pWet       = apvts.getRawParameterValue ("wet");
    pOutMode   = apvts.getRawParameterValue ("outMode");

    for (int s = 1; s <= kNumSlots; ++s)
    {
        const auto i = (size_t) (s - 1);
        pOn[i]    = apvts.getRawParameterValue (idOn (s));
        pLevel[i] = apvts.getRawParameterValue (idLevel (s));
        pPitch[i] = apvts.getRawParameterValue (idPitch (s));
        pFine[i]  = apvts.getRawParameterValue (idFine (s));
    }

    for (auto& p : published) p.store (nullptr);
    for (auto& p : audioAck)  p.store (nullptr);
    for (auto& t : triggerLog) t.store (0);

    startTimer (500);
}

ChouchouTriggerAudioProcessor::~ChouchouTriggerAudioProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout ChouchouTriggerAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dbAttr = AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText);

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "threshold", 1 }, "Threshold",
                                                       NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -20.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));

    NormalisableRange<float> retrigRange (5.0f, 2000.0f, 1.0f);
    retrigRange.setSkewForCentre (120.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "retrigger", 1 }, "Retrigger", retrigRange, 50.0f,
                                                       AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release",
                                                       NormalisableRange<float> (0.0f, 24.0f, 0.1f), 3.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dry", 1 }, "Dry",
                                                       NormalisableRange<float> (kLevelFloorDb, 6.0f, 0.1f), 0.0f, dbAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "wet", 1 }, "Wet",
                                                       NormalisableRange<float> (kLevelFloorDb, 6.0f, 0.1f), 0.0f, dbAttr));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "outMode", 1 }, "Output Mode",
                                                        StringArray { "Mono", "Stereo" }, 1));

    for (int s = 1; s <= kNumSlots; ++s)
    {
        const String n (s);
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { idOn (s), 1 }, "Slot " + n + " On", true));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { idLevel (s), 1 }, "Slot " + n + " Level",
                                                           NormalisableRange<float> (kLevelFloorDb, 6.0f, 0.1f), 0.0f, dbAttr));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { idPitch (s), 1 }, "Slot " + n + " Pitch",
                                                           NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { idFine (s), 1 }, "Slot " + n + " Fine",
                                                           NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ct")));
    }

    return layout;
}

const juce::String ChouchouTriggerAudioProcessor::getName() const { return JucePlugin_Name; }

float  ChouchouTriggerAudioProcessor::getThresholdDb() const      { return pThreshold->load(); }
double ChouchouTriggerAudioProcessor::getRetriggerSeconds() const { return (double) pRetrigger->load() / 1000.0; }

//==============================================================================
void ChouchouTriggerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
    scopeSamplesPerColumn.store (juce::jmax (1, (int) std::lround (sampleRate / 1000.0)));
    stealFadeSamples = juce::jmax (1, (int) std::lround (kStealFadeSeconds * sampleRate));

    for (auto& pool : voices)
        for (auto& v : pool)
            v = Voice {};

    env = 0.0f;
    envHold = 0;
    armed = true;
    samplesSinceTrigger = std::numeric_limits<juce::int64>::max() / 2;
    totalSamples = 0;
    scopeAccum = ScopeColumn {};
    scopeCount = 0;
    gainsInitialised = false;
    previewMask.store (0);
    triggerCount.store (0);
    triggerLogWrite.store (0);

    for (size_t s = 0; s < (size_t) kNumSlots; ++s)
    {
        audioData[s] = published[s].load (std::memory_order_acquire);
        audioAck[s].store (const_cast<SampleData*> (audioData[s]), std::memory_order_release);
    }

    audioRunning.store (true);
}

void ChouchouTriggerAudioProcessor::releaseResources()
{
    audioRunning.store (false);
    collectGarbage();
}

bool ChouchouTriggerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void ChouchouTriggerAudioProcessor::startVoice (int slotIndex, const SampleData& d)
{
    auto& pool = voices[(size_t) slotIndex];

    int playing = 0;
    Voice* oldest = nullptr;
    for (auto& v : pool)
    {
        if (v.active && v.fade < 0)
        {
            ++playing;
            if (oldest == nullptr || v.age < oldest->age)
                oldest = &v;
        }
    }

    if (playing >= kVoicesPerSlot && oldest != nullptr)
        oldest->fade = stealFadeSamples;

    Voice* target = nullptr;
    for (auto& v : pool)
        if (! v.active) { target = &v; break; }

    if (target == nullptr)
        for (auto& v : pool)
            if (v.fade >= 0 && (target == nullptr || v.fade < target->fade))
                target = &v;

    if (target == nullptr)
        target = &pool[0];

    const double semis = (double) pPitch[(size_t) slotIndex]->load() + (double) pFine[(size_t) slotIndex]->load() / 100.0;
    const double sr = currentSampleRate.load();

    target->data   = &d;
    target->pos    = 0.0;
    target->inc    = std::pow (2.0, semis / 12.0) * d.sampleRate / sr;
    target->gain   = 1.0f;
    target->fade   = -1;
    target->age    = ++voiceAgeCounter;
    target->active = d.buffer.getNumSamples() > 0;
}

void ChouchouTriggerAudioProcessor::renderSegment (juce::AudioBuffer<float>& out, int start, int end, int blockLen,
                                                   const std::array<float, kNumSlots>& g0,
                                                   const std::array<float, kNumSlots>& g1, bool monoOut)
{
    if (end <= start)
        return;

    const int numOut = out.getNumChannels();
    float* o0 = out.getWritePointer (0);
    float* o1 = numOut > 1 ? out.getWritePointer (1) : nullptr;
    const bool mix = monoOut || numOut < 2;
    const float fadeNorm = 1.0f / (float) stealFadeSamples;

    for (size_t s = 0; s < (size_t) kNumSlots; ++s)
    {
        const auto* d = audioData[s];
        if (d == nullptr)
            continue;

        const int len = d->buffer.getNumSamples();
        const float* L = d->buffer.getReadPointer (0);
        const float* R = d->buffer.getNumChannels() > 1 ? d->buffer.getReadPointer (1) : L;
        const float gA = g0[s];
        const float gD = (g1[s] - g0[s]) / (float) blockLen;

        for (auto& v : voices[s])
        {
            if (! v.active)
                continue;

            for (int i = start; i < end; ++i)
            {
                const int idx = (int) v.pos;
                if (idx >= len)
                {
                    v.active = false;
                    break;
                }

                const float frac = (float) (v.pos - (double) idx);
                const float lA = L[idx], rA = R[idx];
                const float lB = idx + 1 < len ? L[idx + 1] : 0.0f;
                const float rB = idx + 1 < len ? R[idx + 1] : 0.0f;
                const float l = lA + frac * (lB - lA);
                const float r = rA + frac * (rB - rA);

                float g = (gA + gD * (float) i) * v.gain;
                if (v.fade >= 0)
                    g *= (float) v.fade * fadeNorm;

                if (mix)
                {
                    const float m = (l + r) * 0.5f * g;
                    o0[i] += m;
                    if (o1 != nullptr)
                        o1[i] += m;
                }
                else
                {
                    o0[i] += l * g;
                    o1[i] += r * g;
                }

                v.pos += v.inc;

                if (v.fade >= 0 && --v.fade < 0)
                {
                    v.active = false;
                    break;
                }
            }
        }
    }
}

void ChouchouTriggerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());
    if (n <= 0 || numCh <= 0)
        return;

    for (size_t s = 0; s < (size_t) kNumSlots; ++s)
    {
        auto* p = published[s].load (std::memory_order_acquire);
        if (p != audioData[s])
        {
            for (auto& v : voices[s])
                v.active = false;
            audioData[s] = p;
        }
        audioAck[s].store (p, std::memory_order_release);
    }

    const double sr = currentSampleRate.load();
    const float thrDb = pThreshold->load();
    const float thrLin = juce::Decibels::decibelsToGain (thrDb);
    const float rearmLin = juce::Decibels::decibelsToGain (thrDb - pRelease->load());
    const juce::int64 retrigSamples = juce::jmax<juce::int64> (1, (juce::int64) std::llround ((double) pRetrigger->load() * sr / 1000.0));
    const float relCoef = (float) std::exp (-1.0 / (kEnvReleaseSeconds * sr));
    const int holdSamples = juce::jmax (1, (int) std::lround (kEnvHoldSeconds * sr));
    const int spc = scopeSamplesPerColumn.load();

    // Detection runs on the untouched input before the dry gain is applied.
    int numTriggers = 0;
    float blockPeak = 0.0f;
    const float* in0 = buffer.getReadPointer (0);
    const float* in1 = numCh > 1 ? buffer.getReadPointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        float v = in0[i];
        float peak = std::abs (v);
        if (in1 != nullptr && std::abs (in1[i]) > peak)
        {
            v = in1[i];
            peak = std::abs (v);
        }
        blockPeak = juce::jmax (blockPeak, peak);

        if (peak >= env)
        {
            env = peak;
            envHold = holdSamples;
        }
        else if (peak >= env * 0.7f)
        {
            envHold = holdSamples;
        }
        else if (envHold > 0)
        {
            --envHold;
        }
        else
        {
            env = juce::jmax (peak, env * relCoef);
        }
        if (! armed && env < rearmLin)
            armed = true;

        ++samplesSinceTrigger;
        bool fired = false;
        if (armed && peak > 0.0f && peak >= thrLin && samplesSinceTrigger >= retrigSamples)
        {
            fired = true;
            armed = false;
            samplesSinceTrigger = 0;
            if (numTriggers < kMaxTriggersPerBlock)
                triggerOffsets[(size_t) numTriggers++] = i;

            const int w = triggerLogWrite.load (std::memory_order_relaxed);
            triggerLog[(size_t) (w % kTriggerLogSize)].store (totalSamples + i, std::memory_order_relaxed);
            triggerLogWrite.store (w + 1, std::memory_order_release);
            triggerCount.fetch_add (1, std::memory_order_relaxed);
        }

        if (scopeCount == 0)
            scopeAccum = ScopeColumn { v, v, 0 };
        scopeAccum.lo = juce::jmin (scopeAccum.lo, v);
        scopeAccum.hi = juce::jmax (scopeAccum.hi, v);
        if (fired)
            scopeAccum.flags |= scopeTrigger;
        if (samplesSinceTrigger < retrigSamples)
            scopeAccum.flags |= scopeHoldoff;

        if (++scopeCount >= spc)
        {
            const auto scope = scopeFifo.write (1);
            if (scope.blockSize1 > 0)
                scopeData[(size_t) scope.startIndex1] = scopeAccum;
            scopeCount = 0;
        }
    }

    if (blockPeak > meterPeak.load (std::memory_order_relaxed))
        meterPeak.store (blockPeak, std::memory_order_relaxed);

    const float wetGain = dbToGainFloor (pWet->load());
    const float dryTarget = dbToGainFloor (pDry->load());
    const bool monoOut = pOutMode->load() < 0.5f;

    std::array<float, kNumSlots> g1 {};
    for (size_t s = 0; s < (size_t) kNumSlots; ++s)
        g1[s] = pOn[s]->load() >= 0.5f ? dbToGainFloor (pLevel[s]->load()) * wetGain : 0.0f;

    if (! gainsInitialised)
    {
        lastSlotGain = g1;
        lastDryGain = dryTarget;
        gainsInitialised = true;
    }
    const auto g0 = lastSlotGain;

    buffer.applyGainRamp (0, n, lastDryGain, dryTarget);
    lastDryGain = dryTarget;

    const auto mask = previewMask.exchange (0);
    for (size_t s = 0; s < (size_t) kNumSlots; ++s)
        if ((mask & (1u << s)) != 0 && audioData[s] != nullptr)
            startVoice ((int) s, *audioData[s]);

    int segStart = 0;
    for (int t = 0; t < numTriggers; ++t)
    {
        const int off = triggerOffsets[(size_t) t];
        renderSegment (buffer, segStart, off, n, g0, g1, monoOut);
        for (size_t s = 0; s < (size_t) kNumSlots; ++s)
            if (audioData[s] != nullptr && pOn[s]->load() >= 0.5f)
                startVoice ((int) s, *audioData[s]);
        segStart = off;
    }
    renderSegment (buffer, segStart, n, n, g0, g1, monoOut);

    lastSlotGain = g1;
    totalSamples += n;
}

//==============================================================================
int ChouchouTriggerAudioProcessor::popScopeColumns (ScopeColumn* dest, int maxColumns)
{
    const auto scope = scopeFifo.read (juce::jmin (maxColumns, scopeFifo.getNumReady()));
    int k = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[k++] = scopeData[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[k++] = scopeData[(size_t) (scope.startIndex2 + i)];
    return k;
}

std::vector<juce::int64> ChouchouTriggerAudioProcessor::getTriggerLog() const
{
    const int w = triggerLogWrite.load (std::memory_order_acquire);
    const int count = juce::jmin (w, kTriggerLogSize);
    std::vector<juce::int64> out;
    out.reserve ((size_t) count);
    for (int k = w - count; k < w; ++k)
        out.push_back (triggerLog[(size_t) (k % kTriggerLogSize)].load (std::memory_order_relaxed));
    return out;
}

void ChouchouTriggerAudioProcessor::triggerPreview (int slot)
{
    if (validSlot (slot))
        previewMask.fetch_or (1u << (unsigned) (slot - 1));
}

//==============================================================================
bool ChouchouTriggerAudioProcessor::loadSlot (int slot, const juce::File& file)
{
    if (! validSlot (slot))
        return false;

    const juce::ScopedLock sl (slotLock);
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    const auto maxLen = (juce::int64) (reader->sampleRate * 600.0);
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
    const int numCh = juce::jlimit (1, 2, (int) reader->numChannels);

    auto d = std::make_shared<SampleData>();
    d->buffer.setSize (numCh, len);
    d->buffer.clear();
    reader->read (&d->buffer, 0, len, 0, true, numCh > 1);
    d->sampleRate = reader->sampleRate;
    d->path = file.getFullPathName();

    publishSlot (slot - 1, d, d->path, false);
    return true;
}

void ChouchouTriggerAudioProcessor::clearSlot (int slot)
{
    if (validSlot (slot))
        publishSlot (slot - 1, nullptr, {}, false);
}

void ChouchouTriggerAudioProcessor::publishSlot (int slotIndex, std::shared_ptr<SampleData> data,
                                                 const juce::String& path, bool missing)
{
    const juce::ScopedLock sl (slotLock);
    auto& store = slots[(size_t) slotIndex];

    if (store.data != nullptr)
        graveyard.emplace_back (slotIndex, store.data);

    store.data = std::move (data);
    store.path = path;
    store.missing = missing;
    published[(size_t) slotIndex].store (store.data.get(), std::memory_order_release);

    updateTail();
    collectGarbage();
    ++slotsVersion;
}

void ChouchouTriggerAudioProcessor::collectGarbage()
{
    const juce::ScopedLock sl (slotLock);
    const bool running = audioRunning.load();

    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(), [&] (const auto& e)
    {
        const auto s = (size_t) e.first;
        return ! running
            || audioAck[s].load (std::memory_order_acquire) == published[s].load (std::memory_order_acquire);
    }), graveyard.end());
}

void ChouchouTriggerAudioProcessor::updateTail()
{
    double longest = 0.0;
    for (auto& s : slots)
        if (s.data != nullptr)
            longest = juce::jmax (longest, (double) s.data->buffer.getNumSamples() / s.data->sampleRate);

    // Lowest pitch (-24 st) plays four times longer.
    tailSeconds.store (longest * 4.0);
}

juce::String ChouchouTriggerAudioProcessor::getSlotPath (int slot) const
{
    const juce::ScopedLock sl (slotLock);
    return validSlot (slot) ? slots[(size_t) (slot - 1)].path : juce::String();
}

bool ChouchouTriggerAudioProcessor::isSlotMissing (int slot) const
{
    const juce::ScopedLock sl (slotLock);
    return validSlot (slot) && slots[(size_t) (slot - 1)].missing;
}

std::shared_ptr<const ChouchouTriggerAudioProcessor::SampleData> ChouchouTriggerAudioProcessor::getSlotData (int slot) const
{
    const juce::ScopedLock sl (slotLock);
    return validSlot (slot) ? slots[(size_t) (slot - 1)].data : nullptr;
}

//==============================================================================
void ChouchouTriggerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return;

    auto* samples = xml->createNewChildElement (kSamplesTag.toString());
    for (int s = 1; s <= kNumSlots; ++s)
    {
        auto* e = samples->createNewChildElement (kSlotTag.toString());
        e->setAttribute ("index", s);
        e->setAttribute ("path", getSlotPath (s));
    }

    copyXmlToBinary (*xml, destData);
}

void ChouchouTriggerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    std::unique_ptr<juce::XmlElement> samples;
    if (auto* e = xml->getChildByName (kSamplesTag.toString()))
    {
        xml->removeChildElement (e, false);
        samples.reset (e);
    }

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    for (int s = 1; s <= kNumSlots; ++s)
    {
        juce::String path;
        if (samples != nullptr)
            for (auto* e : samples->getChildWithTagNameIterator (kSlotTag.toString()))
                if (e->getIntAttribute ("index") == s)
                    path = e->getStringAttribute ("path");

        if (path.isEmpty())
            clearSlot (s);
        else if (! juce::File::isAbsolutePath (path) || ! loadSlot (s, juce::File (path)))
            publishSlot (s - 1, nullptr, path, true);
    }
}

//==============================================================================
juce::AudioProcessorEditor* ChouchouTriggerAudioProcessor::createEditor()
{
    return new ChouchouTriggerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChouchouTriggerAudioProcessor();
}
