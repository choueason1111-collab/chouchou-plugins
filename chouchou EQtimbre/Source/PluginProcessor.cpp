#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

namespace
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    juce::AudioParameterFloatAttributes withUnit (const juce::String& unit, int decimals)
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel (unit)
            .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); });
    }

    void addFloat (Layout& layout, const juce::String& id, const juce::String& name,
                   juce::NormalisableRange<float> range, float defaultValue,
                   const juce::String& unit, int decimals)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range,
                                                                 defaultValue, withUnit (unit, decimals)));
    }

    // Non-ASCII literals must go through fromUTF8, otherwise JUCE reads them as Latin-1.
    juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    const juce::Identifier kDrawsTag ("RandomDraws");

    // Draws are stored as the float's bit pattern so a reload reproduces them exactly.
    juce::String drawToString (float v)
    {
        uint32_t bits;
        std::memcpy (&bits, &v, sizeof (bits));
        return juce::String::toHexString ((int) bits);
    }

    float drawFromVar (const juce::var& v)
    {
        const auto bits = (uint32_t) v.toString().getHexValue32();
        float f;
        std::memcpy (&f, &bits, sizeof (f));
        return std::isfinite (f) ? juce::jlimit (0.0f, 0.99999994f, f) : 0.5f;
    }
}

//==============================================================================
ChouchouEQtimbreAudioProcessor::ChouchouEQtimbreAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "chouchouEQtimbre", createParameterLayout())
{
    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    params.oversampling = raw (ParamIDs::oversampling);
    params.inputGain    = raw (ParamIDs::inputGain);
    params.outputGain   = raw (ParamIDs::outputGain);
    params.mix          = raw (ParamIDs::mix);
    params.autoGain     = raw (ParamIDs::autoGain);
    params.rndBypass    = raw (ParamIDs::rndBypass);
    params.rndMix       = raw (ParamIDs::rndMix);
    params.rndTrigger   = raw (ParamIDs::rndTrigger);
    params.rndAuto      = raw (ParamIDs::rndAuto);
    params.rndInterval  = raw (ParamIDs::rndInterval);
    params.rndSync      = raw (ParamIDs::rndSync);
    params.rndDivision  = raw (ParamIDs::rndDivision);
    params.rndMorph     = raw (ParamIDs::rndMorph);

    for (int i = 0; i < kNumRandom; ++i)
    {
        const juce::String id (chouchou::engineParamInfo()[(size_t) i].id);
        params.engine[(size_t) i] = raw (id);
        params.amount[(size_t) i] = raw (ParamIDs::rndAmount (i));
        engineParams[(size_t) i] = apvts.getParameter (id);
        jassert (params.engine[(size_t) i] != nullptr && engineParams[(size_t) i] != nullptr);
    }

    for (int i = 0; i < kNumRandom; ++i)
    {
        savedU[(size_t) i].store (randomizer.getTargetU()[(size_t) i]);
        savedV[(size_t) i].store (randomizer.getTargetV()[(size_t) i]);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout ChouchouEQtimbreAudioProcessor::createParameterLayout()
{
    Layout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::mode, 1 }, "Mode",
                                                              juce::StringArray { fr ("Générer"), fr ("Rééquilibrer"), "Les deux" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::oversampling, 1 }, fr ("Suréchantillonnage"),
                                                              juce::StringArray { "2x", "4x", "8x" }, 1));
    addFloat (layout, ParamIDs::inputGain,  fr ("Entrée"), { -24.0f, 24.0f }, 0.0f, "dB", 1);
    addFloat (layout, ParamIDs::outputGain, "Sortie", { -24.0f, 24.0f }, 0.0f, "dB", 1);
    addFloat (layout, ParamIDs::mix,        "Mix",    { 0.0f, 100.0f }, 100.0f, "%", 0);
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::autoGain, 1 }, "Gain auto", false));

    addFloat (layout, ParamIDs::drive,    "Drive",    skewed (0.0f, 10.0f, 2.0f), 2.0f, "", 2);
    addFloat (layout, ParamIDs::driveComp, "Comp. Drive", { 0.0f, 100.0f }, 0.0f, "%", 0);
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::autoDrive, 1 }, "Drive auto", false));
    addFloat (layout, ParamIDs::bias,     "Biais",    { -1.0f, 1.0f }, 0.3f, "", 2);
    addFloat (layout, ParamIDs::even,     "Pairs",    { 0.0f, 200.0f }, 50.0f, "%", 0);
    addFloat (layout, ParamIDs::odd,      "Impairs",  { 0.0f, 200.0f }, 0.0f, "%", 0);
    addFloat (layout, ParamIDs::tone,     fr ("Tonalité"), skewed (1000.0f, 20000.0f, 5000.0f), 20000.0f, "Hz", 0);
    addFloat (layout, ParamIDs::dcCut,    "Coupe DC", { 2.0f, 30.0f }, 7.0f, "Hz", 1);
    addFloat (layout, ParamIDs::genLevel, fr ("Niveau Génération"), { 0.0f, 100.0f }, 100.0f, "%", 0);

    addFloat (layout, ParamIDs::balance,  "Balance",  { -100.0f, 100.0f }, 0.0f, "%", 0);
    addFloat (layout, ParamIDs::rebLevel, fr ("Niveau Rééquilibrage"), { 0.0f, 100.0f }, 100.0f, "%", 0);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::pitchSource, 1 }, "Source de hauteur",
                                                              juce::StringArray { "Manuel", "MIDI", "Auto" }, 0));
    addFloat (layout, ParamIDs::manualHz, "Hauteur",  skewed (30.0f, 2000.0f, 220.0f), 220.0f, "Hz", 1);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::midiPriority, 1 }, fr ("Priorité MIDI"),
                                                              juce::StringArray { fr ("Dernière"), fr ("Plus aiguë"), "Plus grave" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::midiHold, 1 }, "Maintien MIDI", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::protect, 1 }, "Protection fondamentale", true));
    addFloat (layout, ParamIDs::confidence, "Confiance", { 0.0f, 100.0f }, 70.0f, "%", 0);
    addFloat (layout, ParamIDs::glide,    "Glissement", skewed (2.0f, 100.0f, 20.0f), 20.0f, "ms/oct", 0);
    addFloat (layout, ParamIDs::jumpFade, "Fondu de saut", { 5.0f, 50.0f }, 15.0f, "ms", 0);

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::rndBypass, 1 }, fr ("Bypass aléatoire"), true));
    addFloat (layout, ParamIDs::rndMix, fr ("Mix aléatoire"), { 0.0f, 100.0f }, 100.0f, "%", 0);
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::rndTrigger, 1 }, "Lancer", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::rndAuto, 1 }, fr ("Aléatoire auto"), false));
    addFloat (layout, ParamIDs::rndInterval, "Intervalle", skewed (0.25f, 30.0f, 4.0f), 4.0f, "s", 2);
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::rndSync, 1 }, "Sync tempo", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::rndDivision, 1 }, "Division",
                                                              juce::StringArray { "1/4 temps", "1/2 temps", "1 temps", "2 temps",
                                                                                  "1 mesure", "2 mesures", "4 mesures", "8 mesures" }, 4));
    addFloat (layout, ParamIDs::rndMorph, "Morph", skewed (0.0f, 10.0f, 1.0f), 0.5f, "s", 2);

    for (int i = 0; i < kNumRandom; ++i)
        addFloat (layout, ParamIDs::rndAmount (i), fr ("Aléa ") + fr (chouchou::engineParamInfo()[(size_t) i].shortName),
                  { 0.0f, 100.0f }, 30.0f, "%", 0);

    return layout;
}

bool ChouchouEQtimbreAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
chouchou::EngineValues ChouchouEQtimbreAudioProcessor::getMainValues() const noexcept
{
    chouchou::EngineValues v {};
    for (int i = 0; i < kNumRandom; ++i)
        v[(size_t) i] = params.engine[(size_t) i]->load();
    return v;
}

chouchou::EngineValues ChouchouEQtimbreAudioProcessor::getRandomValues() const noexcept
{
    chouchou::EngineValues v {};
    for (int i = 0; i < kNumRandom; ++i)
        v[(size_t) i] = getRandomPlain (i);
    return v;
}

float ChouchouEQtimbreAudioProcessor::plainFromNormalised (int i, float norm) const noexcept
{
    const int choices = chouchou::engineParamInfo()[(size_t) i].numChoices;
    if (choices > 0)
        return (float) juce::roundToInt (juce::jlimit (0.0f, 1.0f, norm) * (float) (choices - 1));
    return engineParams[(size_t) i]->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm));
}

float ChouchouEQtimbreAudioProcessor::getRandomPlain (int i) const noexcept
{
    return plainFromNormalised (i, randomNorm[(size_t) i].load());
}

chouchou::EngineVoice::Settings ChouchouEQtimbreAudioProcessor::mainSettings() const noexcept
{
    return chouchou::EngineVoice::Settings::fromValues (getMainValues(), params.mix->load() * 0.01f);
}

void ChouchouEQtimbreAudioProcessor::evaluateRandom() noexcept
{
    chouchou::Randomizer::Draws base {}, amount {};
    for (int i = 0; i < kNumRandom; ++i)
    {
        base[(size_t) i] = engineParams[(size_t) i]->convertTo0to1 (params.engine[(size_t) i]->load());
        amount[(size_t) i] = juce::jlimit (0.0f, 1.0f, params.amount[(size_t) i]->load() * 0.01f);
    }
    randomizer.evaluate (base, amount, evaluated);

    for (int i = 0; i < kNumRandom; ++i)
        if (! chouchou::Randomizer::isSwitch (i))
            applied[(size_t) i] = evaluated[(size_t) i];
}

chouchou::EngineVoice::Settings ChouchouEQtimbreAudioProcessor::randomSettings() noexcept
{
    chouchou::EngineValues v {};
    for (int i = 0; i < kNumRandom; ++i)
        v[(size_t) i] = plainFromNormalised (i, applied[(size_t) i]);
    return chouchou::EngineVoice::Settings::fromValues (v, params.rndMix->load() * 0.01f);
}

int ChouchouEQtimbreAudioProcessor::readOversamplingChoice() const noexcept
{
    return juce::jlimit (0, chouchou::HarmonicGenerator::kNumFactors - 1, (int) params.oversampling->load());
}

void ChouchouEQtimbreAudioProcessor::applyOversampling (int index)
{
    mainVoice.setFactorIndex (index);
    randomVoice.setFactorIndex (index);
    latency = mainVoice.getLatencySamples();
    setLatencySamples (2 * latency);
}

// A factor change fades the whole output out, switches while silent, then fades back in.
void ChouchouEQtimbreAudioProcessor::updateOversampling() noexcept
{
    const int desired = readOversamplingChoice();
    if (desired == mainVoice.getFactorIndex())
    {
        oversamplingGate.setTargetValue (1.0f);
        return;
    }

    if (oversamplingGate.getCurrentValue() <= 0.0f)
    {
        applyOversampling (desired);
        oversamplingGate.setTargetValue (1.0f);
    }
    else
    {
        oversamplingGate.setTargetValue (0.0f);
    }
}

void ChouchouEQtimbreAudioProcessor::updateSmoothingTargets() noexcept
{
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (params.inputGain->load()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (params.outputGain->load()));
    autoGainBlend.setTargetValue (params.autoGain->load() > 0.5f ? 1.0f : 0.0f);
    bypassGain.setTargetValue (params.rndBypass->load() > 0.5f ? 0.0f : 1.0f);
}

void ChouchouEQtimbreAudioProcessor::applyPendingDraws() noexcept
{
    if (! drawsPending.exchange (false))
        return;

    chouchou::Randomizer::Draws u {}, v {};
    for (int i = 0; i < kNumRandom; ++i)
    {
        u[(size_t) i] = pendingU[(size_t) i].load();
        v[(size_t) i] = pendingV[(size_t) i].load();
    }
    randomizer.setDraws (u, v);
}

void ChouchouEQtimbreAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);
    numChannels = juce::jmax (1, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    maxBlock = juce::jmax (1, samplesPerBlock);

    applyPendingDraws();
    randomizer.finishMorph();
    evaluateRandom();
    applied = evaluated;

    const int factor = readOversamplingChoice();
    mainVoice.prepare (sampleRate, numChannels, maxBlock, factor, mainSettings());
    randomVoice.prepare (sampleRate, numChannels, maxBlock, factor, randomSettings());
    monoScratch.assign ((size_t) maxBlock, 0.0f);
    wetScratch.assign ((size_t) maxBlock, 0.0f);

    applyOversampling (factor);
    oversamplingGate.reset (sampleRate, kOversamplingFadeSeconds);
    oversamplingGate.setCurrentAndTargetValue (1.0f);
    clipped.store (false);

    inputGain.reset (sampleRate, kGainRampSeconds);
    outputGain.reset (sampleRate, kGainRampSeconds);
    autoGainBlend.reset (sampleRate, kGainRampSeconds);
    bypassGain.reset (sampleRate, kBypassFadeSeconds);
    duckGain.reset (sampleRate, kDuckSeconds);

    updateSmoothingTargets();
    for (auto* s : { &autoGainBlend, &bypassGain })
        s->setCurrentAndTargetValue (s->getTargetValue());
    inputGain.setCurrentAndTargetValue (inputGain.getTargetValue());
    outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
    duckGain.setCurrentAndTargetValue (1.0f);
    randomSkipped = bypassGain.getTargetValue() <= 0.0f;

    lastTrigger = params.rndTrigger->load() > 0.5f;
    autoCounter = freeBeats = 0.0;
    lastSyncStep = std::numeric_limits<juce::int64>::min();

    inPower = outPower = 0.0f;
    powerCoef = (float) (1.0 - std::exp (-1.0 / (kAutoGainSeconds * sampleRate)));
}

// Collects every reason to draw new random values at the start of a block.
void ChouchouEQtimbreAudioProcessor::handleTriggers (int numSamples) noexcept
{
    const double sr = currentSampleRate.load();
    int requests = rollRequests.exchange (0);

    const bool trigger = params.rndTrigger->load() > 0.5f;
    if (trigger && ! lastTrigger)
        ++requests;
    lastTrigger = trigger;

    const bool autoOn = params.rndAuto->load() > 0.5f && params.rndBypass->load() <= 0.5f;
    if (! autoOn)
    {
        autoCounter = freeBeats = 0.0;
        lastSyncStep = std::numeric_limits<juce::int64>::min();
    }
    else if (params.rndSync->load() > 0.5f)
    {
        double bpm = kFallbackBpm, beatsPerBar = 4.0;
        juce::Optional<double> ppq;
        if (auto* playHead = getPlayHead())
        {
            if (auto pos = playHead->getPosition())
            {
                if (auto b = pos->getBpm())
                    bpm = juce::jmax (1.0, *b);
                if (auto sig = pos->getTimeSignature())
                    beatsPerBar = juce::jmax (1.0, 4.0 * sig->numerator / juce::jmax (1, sig->denominator));
                if (pos->getIsPlaying())
                    ppq = pos->getPpqPosition();
            }
        }

        const int division = juce::jlimit (0, (int) kDivisionBeats.size() - 1, (int) params.rndDivision->load());
        const double beats = division < 4 ? kDivisionBeats[(size_t) division]
                                          : kDivisionBeats[(size_t) division] * 0.25 * beatsPerBar;
        if (ppq.hasValue())
        {
            const auto stepIndex = (juce::int64) std::floor (*ppq / beats + 1.0e-9);
            if (lastSyncStep != std::numeric_limits<juce::int64>::min() && stepIndex != lastSyncStep)
                ++requests;
            lastSyncStep = stepIndex;
            freeBeats = 0.0;
        }
        else
        {
            lastSyncStep = std::numeric_limits<juce::int64>::min();
            if (freeBeats >= beats)
            {
                ++requests;
                freeBeats = std::fmod (freeBeats, beats);
            }
            freeBeats += numSamples / sr * bpm / 60.0;
        }
    }
    else
    {
        const double period = juce::jmax (0.01, (double) params.rndInterval->load()) * sr;
        if (autoCounter >= period)
        {
            ++requests;
            autoCounter = std::fmod (autoCounter, period);
        }
        autoCounter += numSamples;
    }

    if (requests <= 0)
        return;

    randomizer.roll (juce::jmax (0.0f, params.rndMorph->load()), sr);
    rollCount.fetch_add (1);
    for (int i = 0; i < kNumRandom; ++i)
    {
        savedU[(size_t) i].store (randomizer.getTargetU()[(size_t) i]);
        savedV[(size_t) i].store (randomizer.getTargetV()[(size_t) i]);
    }
}

void ChouchouEQtimbreAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    applyPendingDraws();
    updateSmoothingTargets();

    const int total = buffer.getNumSamples();
    handleTriggers (total);
    int position = 0;

    for (const auto meta : midi)
    {
        const int eventPos = juce::jlimit (0, total, meta.samplePosition);
        if (eventPos > position)
        {
            processRange (buffer, position, eventPos - position);
            position = eventPos;
        }
        const auto message = meta.getMessage();
        mainVoice.handleMidiMessage (message);
        randomVoice.handleMidiMessage (message);
    }

    if (position < total)
        processRange (buffer, position, total - position);
}

bool ChouchouEQtimbreAudioProcessor::randomNeedsShortChunks() const noexcept
{
    if (randomSkipped)
        return false;
    if (duckGain.isSmoothing() || duckGain.getTargetValue() < 1.0f)
        return true;
    for (int i = 0; i < kNumRandom; ++i)
        if (chouchou::Randomizer::isSwitch (i) && ! juce::exactlyEqual (applied[(size_t) i], evaluated[(size_t) i]))
            return true;
    return false;
}

void ChouchouEQtimbreAudioProcessor::processRange (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept
{
    for (int done = 0; done < numSamples;)
    {
        const bool switchPending = readOversamplingChoice() != mainVoice.getFactorIndex();
        const bool shortChunks = switchPending || randomNeedsShortChunks();
        const int n = juce::jmin (shortChunks ? kSwitchChunk : maxBlock, numSamples - done);
        processChunk (buffer, start + done, n);
        done += n;
    }
}

void ChouchouEQtimbreAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int n) noexcept
{
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());
    updateOversampling();

    for (int i = 0; i < n; ++i)
    {
        const float g = inputGain.getNextValue();
        for (int ch = 0; ch < chans; ++ch)
            buffer.getWritePointer (ch, start)[i] *= g;
    }

    mainVoice.process (buffer, start, n, mainSettings(), nullptr);

    // Random stage, in series after the main voice. Bypass and the switch duck only scale its
    // processed part, so a fully bypassed stage is exactly the main output delayed once more.
    evaluateRandom();
    const bool skip = ! bypassGain.isSmoothing() && bypassGain.getTargetValue() <= 0.0f;
    if (skip)
    {
        applied = evaluated;
        duckGain.setCurrentAndTargetValue (1.0f);
        randomVoice.processDelayOnly (buffer, start, n);
        randomSkipped = true;
    }
    else
    {
        if (randomSkipped)
        {
            applied = evaluated;
            duckGain.setCurrentAndTargetValue (1.0f);
            randomVoice.resetDsp (randomSettings());
            randomSkipped = false;
        }

        bool switchPending = false;
        for (int i = 0; i < kNumRandom; ++i)
            switchPending = switchPending || (chouchou::Randomizer::isSwitch (i) && ! juce::exactlyEqual (applied[(size_t) i], evaluated[(size_t) i]));

        if (switchPending && duckGain.getCurrentValue() <= 0.0f)
        {
            applied = evaluated;
            duckGain.setTargetValue (1.0f);
        }
        else if (switchPending)
        {
            duckGain.setTargetValue (0.0f);
        }

        for (int i = 0; i < n; ++i)
            wetScratch[(size_t) i] = bypassGain.getNextValue() * duckGain.getNextValue();
        randomVoice.process (buffer, start, n, randomSettings(), wetScratch.data());
    }
    randomizer.advance (n);

    const auto& dryRef = mainVoice.getAligned();
    float chunkPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float og = outputGain.getNextValue() * oversamplingGate.getNextValue();
        const float blend = autoGainBlend.getNextValue();

        float sumIn = 0.0f, sumOut = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float dry = dryRef.getReadPointer (ch)[i];
            const float wet = buffer.getReadPointer (ch, start)[i];
            sumIn += dry * dry;
            sumOut += wet * wet;
        }

        inPower += powerCoef * (sumIn - inPower);
        outPower += powerCoef * (sumOut - outPower);
        const float agc = juce::jlimit (1.0f / kAutoGainLimit, kAutoGainLimit,
                                        std::sqrt ((inPower + 1.0e-10f) / (outPower + 1.0e-10f)));
        const float gain = (1.0f + blend * (agc - 1.0f)) * og;

        float mono = 0.0f, peak = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
        {
            float& s = buffer.getWritePointer (ch, start)[i];
            s *= gain;
            mono += s;
            peak = juce::jmax (peak, std::abs (s));
        }
        monoScratch[(size_t) i] = mono / (float) juce::jmax (1, chans);
        chunkPeak = juce::jmax (chunkPeak, peak);
    }

    if (chunkPeak > 1.0f)
        clipped.store (true);

    analyzerFifo.push (monoScratch.data(), n);

    displayF0.store (mainVoice.getF0());
    displayActive.store (mainVoice.isPitchActive());
    displayConfidence.store (mainVoice.getAutoConfidence());
    displayStatus.store ((int) mainVoice.getPitchStatus());

    for (int i = 0; i < kNumRandom; ++i)
        randomNorm[(size_t) i].store (applied[(size_t) i]);
    randomAudible.store (! randomSkipped && bypassGain.getTargetValue() > 0.0f && params.rndMix->load() > 0.0f);
    mainInputRms.store (mainVoice.getInputRms());
    randomInputRms.store (randomVoice.getInputRms());
    mainAutoGain.store (mainVoice.getAutoGain());
    randomAutoGain.store (randomVoice.getAutoGain());
    randomF0.store (randomVoice.getF0());
    randomPitchActive.store (! randomSkipped && randomVoice.isPitchActive());
}

//==============================================================================
void ChouchouEQtimbreAudioProcessor::adoptRandom()
{
    std::array<float, kNumRandom> values {};
    for (int i = 0; i < kNumRandom; ++i)
        values[(size_t) i] = randomNorm[(size_t) i].load();

    for (int i = 0; i < kNumRandom; ++i)
    {
        auto* p = engineParams[(size_t) i];
        p->beginChangeGesture();
        p->setValueNotifyingHost (values[(size_t) i]);
        p->endChangeGesture();
    }

    if (auto* bypass = apvts.getParameter (ParamIDs::rndBypass))
    {
        bypass->beginChangeGesture();
        bypass->setValueNotifyingHost (1.0f);
        bypass->endChangeGesture();
    }
}

juce::AudioProcessorEditor* ChouchouEQtimbreAudioProcessor::createEditor()
{
    return new ChouchouEQtimbreAudioProcessorEditor (*this);
}

void ChouchouEQtimbreAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (auto old = state.getChildWithName (kDrawsTag); old.isValid(); old = state.getChildWithName (kDrawsTag))
        state.removeChild (old, nullptr);

    juce::ValueTree draws (kDrawsTag);
    for (int i = 0; i < kNumRandom; ++i)
    {
        draws.setProperty ("u" + juce::String (i), drawToString (savedU[(size_t) i].load()), nullptr);
        draws.setProperty ("v" + juce::String (i), drawToString (savedV[(size_t) i].load()), nullptr);
    }
    state.appendChild (draws, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChouchouEQtimbreAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto draws = tree.getChildWithName (kDrawsTag);
    if (draws.isValid())
    {
        for (int i = 0; i < kNumRandom; ++i)
        {
            const float u = drawFromVar (draws.getProperty ("u" + juce::String (i), drawToString (0.5f)));
            const float v = drawFromVar (draws.getProperty ("v" + juce::String (i), drawToString (0.5f)));
            pendingU[(size_t) i].store (u);
            pendingV[(size_t) i].store (v);
            savedU[(size_t) i].store (u);
            savedV[(size_t) i].store (v);
        }
        drawsPending.store (true);
        tree.removeChild (draws, nullptr);
    }
    apvts.replaceState (tree);
}

#ifndef CHOUCHOU_TIMBRE_SELFTEST
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChouchouEQtimbreAudioProcessor();
}
#endif
