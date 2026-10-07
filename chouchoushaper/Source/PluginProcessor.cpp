/*
  ==============================================================================

    chouchouShaper — serial signal flow

      input -> TIME stage (Dry/Wet) -> SPECTRAL stage (Dry/Wet, dry delayed by N)
            -> Output gain

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::NormalisableRange<float> ratioRange()
    {
        // 0.25 .. 8, logarithmic; 1:1 sits at 40 % of the travel.
        // Values are rounded to 0.01 so that 1:1 is exactly 1 (true unity).
        return { 0.25f, 8.0f,
                 [] (float lo, float hi, float x) { return std::round (lo * std::pow (hi / lo, x) * 100.0f) / 100.0f; },
                 [] (float lo, float hi, float v) { return std::log (v / lo) / std::log (hi / lo); },
                 [] (float, float, float v) { return std::round (v * 100.0f) / 100.0f; } };
    }

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float l, float h, float x) { return l * std::pow (h / l, x); },
                 [] (float l, float h, float v) { return std::log (v / l) / std::log (h / l); } };
    }

    auto msAttr()
    {
        return juce::AudioParameterFloatAttributes().withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; });
    }

    auto dbAttr()
    {
        return juce::AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; });
    }

    auto hzAttr()
    {
        return juce::AudioParameterFloatAttributes().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
            })
            .withValueFromStringFunction ([] (const juce::String& s)
            {
                const float v = s.getFloatValue();
                return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
            });
    }

    auto ratioAttr()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " : 1"; });
    }

    auto pctAttr()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ChouchouShaperAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    auto id = [] (const char* s) { return juce::ParameterID { s, 1 }; };

    // --- Time stage ---
    p.push_back (std::make_unique<B> (id ("t_on"), "Time On", true));
    p.push_back (std::make_unique<F> (id ("t_ratio"), "Time Ratio", ratioRange(), 1.0f, ratioAttr()));
    p.push_back (std::make_unique<F> (id ("t_fast_ms"), "Time Fast", logRange (0.5f, 100.0f), 10.0f, msAttr()));
    p.push_back (std::make_unique<F> (id ("t_slow_ms"), "Time Slow", logRange (10.0f, 2000.0f), 150.0f, msAttr()));
    p.push_back (std::make_unique<F> (id ("t_attack_db"), "Time Attack",
                                      juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f, dbAttr()));
    p.push_back (std::make_unique<F> (id ("t_sustain_db"), "Time Sustain",
                                      juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f, dbAttr()));
    p.push_back (std::make_unique<B> (id ("t_stereo_link"), "Time Stereo Link", true));
    p.push_back (std::make_unique<F> (id ("t_mix"), "Time Mix",
                                      juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 1.0f, pctAttr()));

    // --- Spectral stage ---
    p.push_back (std::make_unique<B> (id ("s_on"), "Spectral On", true));
    p.push_back (std::make_unique<F> (id ("s_ratio"), "Spectral Ratio", ratioRange(), 1.0f, ratioAttr()));
    p.push_back (std::make_unique<F> (id ("s_fast_ms"), "Spectral Fast", logRange (1.0f, 300.0f), 20.0f, msAttr()));
    p.push_back (std::make_unique<F> (id ("s_slow_ms"), "Spectral Slow", logRange (20.0f, 3000.0f), 250.0f, msAttr()));
    p.push_back (std::make_unique<F> (id ("s_attack_db"), "Spectral Attack",
                                      juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f, dbAttr()));
    p.push_back (std::make_unique<F> (id ("s_sustain_db"), "Spectral Sustain",
                                      juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f, dbAttr()));
    p.push_back (std::make_unique<F> (id ("s_freq_smooth"), "Spectral Freq Smooth",
                                      juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.3f, pctAttr()));
    p.push_back (std::make_unique<F> (id ("s_lo_hz"), "Spectral Low", logRange (20.0f, 20000.0f), 20.0f, hzAttr()));
    p.push_back (std::make_unique<F> (id ("s_hi_hz"), "Spectral High", logRange (20.0f, 20000.0f), 20000.0f, hzAttr()));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        id ("s_fft_size"), "Spectral FFT Size", juce::StringArray { "512", "1024", "2048", "4096" }, 2));
    p.push_back (std::make_unique<F> (id ("s_mix"), "Spectral Mix",
                                      juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 1.0f, pctAttr()));

    // --- Sidechain ---
    p.push_back (std::make_unique<B> (id ("t_sc"), "Time Sidechain", false));
    p.push_back (std::make_unique<B> (id ("s_sc"), "Spectral Sidechain", false));
    p.push_back (std::make_unique<F> (id ("sc_gain_db"), "SC Gain",
                                      juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f, dbAttr()));
    p.push_back (std::make_unique<F> (id ("sc_hpf_hz"), "SC HPF", logRange (20.0f, 2000.0f), 20.0f, hzAttr()));
    p.push_back (std::make_unique<F> (id ("sc_lpf_hz"), "SC LPF", logRange (200.0f, 20000.0f), 20000.0f, hzAttr()));

    // --- Global ---
    p.push_back (std::make_unique<F> (id ("out_db"), "Output",
                                      juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f, dbAttr()));

    return { p.begin(), p.end() };
}

//==============================================================================
ChouchouShaperAudioProcessor::ChouchouShaperAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    spectral.prepare (currentSampleRate, 2, fftChoiceToOrder ((int) param ("s_fft_size")));
    setLatencySamples (spectral.getFftSize());
    apvts.addParameterListener ("s_fft_size", this);
}

ChouchouShaperAudioProcessor::~ChouchouShaperAudioProcessor()
{
    apvts.removeParameterListener ("s_fft_size", this);
    cancelPendingUpdate();
}

double ChouchouShaperAudioProcessor::getTailLengthSeconds() const
{
    const double slowSec = (double) juce::jmax (param ("t_slow_ms"), param ("s_slow_ms")) * 0.001;
    return (double) spectral.getFftSize() / juce::jmax (1.0, currentSampleRate) + slowSec;
}

//==============================================================================
void ChouchouShaperAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;

    // Keep stereo state available even if a nested host briefly reports 0 channels.
    const int numChannels = juce::jmax (2, getMainBusNumInputChannels(), getMainBusNumOutputChannels());

    timeStage.prepare (sampleRate, numChannels);
    spectral.prepare (sampleRate, numChannels, fftChoiceToOrder ((int) param ("s_fft_size")));
    setLatencySamples (spectral.getFftSize());

    scWork.setSize (2, juce::jmax (1, samplesPerBlock), false, true, false);
    scHpf.reset();
    scLpf.reset();
    lastHpfHz = lastLpfHz = -1.0f;

    outGain.reset (sampleRate, 0.02);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param ("out_db")));

    ensureScratch (juce::jmax (1, samplesPerBlock));
    scopeAcc = {};
    accRatio = accAttack = accSustain = accTotal = 0.0;
    accCount = 0;
    scopeFifo.clear();
}

void ChouchouShaperAudioProcessor::ensureScratch (int numSamples)
{
    if ((int) scopeIn.size() >= numSamples)
        return;

    for (auto* v : { &scopeIn, &traceRatio, &traceAttack, &traceSustain, &traceApplied })
        v->assign ((size_t) numSamples, 0.0f);
}

void ChouchouShaperAudioProcessor::pushScopeColumns (const float* out, const float* sc, int numSamples,
                                                     bool timeOn, bool scActive)
{
    const auto hop = spectral.getHopSummary();

    for (int i = 0; i < numSamples; ++i)
    {
        if (accCount == 0)
        {
            scopeAcc = {};
            scopeAcc.inMin = scopeAcc.inMax = scopeIn[(size_t) i];
            scopeAcc.outMin = scopeAcc.outMax = out[i];
            if (sc != nullptr)
                scopeAcc.scMin = scopeAcc.scMax = sc[i];
        }

        scopeAcc.inMin  = juce::jmin (scopeAcc.inMin, scopeIn[(size_t) i]);
        scopeAcc.inMax  = juce::jmax (scopeAcc.inMax, scopeIn[(size_t) i]);
        scopeAcc.outMin = juce::jmin (scopeAcc.outMin, out[i]);
        scopeAcc.outMax = juce::jmax (scopeAcc.outMax, out[i]);
        if (sc != nullptr)
        {
            scopeAcc.scMin = juce::jmin (scopeAcc.scMin, sc[i]);
            scopeAcc.scMax = juce::jmax (scopeAcc.scMax, sc[i]);
        }

        if (timeOn)
        {
            accRatio   += traceRatio[(size_t) i];
            accAttack  += traceAttack[(size_t) i];
            accSustain += traceSustain[(size_t) i];
            accTotal   += traceApplied[(size_t) i];
        }

        if (++accCount == kScopeColumnSamples)
        {
            constexpr double inv = 1.0 / kScopeColumnSamples;
            scopeAcc.scActive = scActive;
            scopeAcc.tRatio   = (float) (accRatio * inv);
            scopeAcc.tAttack  = (float) (accAttack * inv);
            scopeAcc.tSustain = (float) (accSustain * inv);
            scopeAcc.tTotal   = (float) (accTotal * inv);
            scopeAcc.sRatio   = hop.ratioDb;
            scopeAcc.sAttack  = hop.attackDb;
            scopeAcc.sSustain = hop.sustainDb;
            scopeAcc.sTotal   = hop.totalDb;
            scopeFifo.push (scopeAcc);

            accRatio = accAttack = accSustain = accTotal = 0.0;
            accCount = 0;
        }
    }
}

void ChouchouShaperAudioProcessor::reset()
{
    const juce::ScopedLock sl (getCallbackLock());
    timeStage.reset();
    spectral.reset();
    scHpf.reset();
    scLpf.reset();
}

void ChouchouShaperAudioProcessor::numChannelsChanged()
{
    if (currentSampleRate > 0.0)
        prepareToPlay (currentSampleRate, preparedBlockSize);
}

void ChouchouShaperAudioProcessor::processorLayoutsChanged()
{
    if (currentSampleRate > 0.0)
        prepareToPlay (currentSampleRate, preparedBlockSize);
}

bool ChouchouShaperAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (out != layouts.getMainInputChannelSet())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
void ChouchouShaperAudioProcessor::applyFftSizeFromParameterLocked (bool force)
{
    const int order = fftChoiceToOrder ((int) param ("s_fft_size"));
    if (! force && order == spectral.getOrder())
        return;

    {
        const juce::ScopedLock sl (getCallbackLock());
        spectral.configure (order);
    }
    setLatencySamples (spectral.getFftSize());
}

void ChouchouShaperAudioProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "s_fft_size")
        triggerAsyncUpdate();
}

void ChouchouShaperAudioProcessor::handleAsyncUpdate()
{
    applyFftSizeFromParameterLocked (false);
}

void ChouchouShaperAudioProcessor::updateScFilters (float hpfHz, float lpfHz)
{
    const double nyq = currentSampleRate * 0.5;
    if (! juce::exactlyEqual (hpfHz, lastHpfHz))
    {
        scHpf.setup (true, juce::jlimit (10.0, nyq * 0.9, (double) hpfHz), currentSampleRate);
        lastHpfHz = hpfHz;
    }
    if (! juce::exactlyEqual (lpfHz, lastLpfHz))
    {
        scLpf.setup (false, juce::jlimit (10.0, nyq * 0.9, (double) lpfHz), currentSampleRate);
        lastLpfHz = lpfHz;
    }
}

//==============================================================================
void ChouchouShaperAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    processCounter.fetch_add (1, std::memory_order_relaxed);

    const int numSamples = buffer.getNumSamples();
    const int bufCh = buffer.getNumChannels();
    if (numSamples <= 0 || bufCh <= 0)
        return;

    // Main-bus counts only (the total would include the sidechain bus).
    int numIn  = getMainBusNumInputChannels();
    int numOut = getMainBusNumOutputChannels();
    if (numOut <= 0) numOut = getTotalNumOutputChannels();
    if (numIn <= 0)  numIn = juce::jmin (bufCh, juce::jmax (1, numOut));
    if (numOut <= 0) numOut = juce::jmax (numIn, bufCh);

    for (int i = numIn; i < numOut && i < bufCh; ++i)
        buffer.clear (i, 0, numSamples);

    const int numCh = juce::jmin (bufCh, juce::jmax (1, numIn), spectral.getNumChannels());

    //--------------------------------------------------------------------------
    // Sidechain: copy, gain, HPF / LPF (detection only, never heard).
    int scCh = 0;
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
    {
        const int scOffset = scBus->getChannelIndexInProcessBlockBuffer (0);
        scCh = juce::jlimit (0, 2, juce::jmin (scBus->getNumberOfChannels(), bufCh - scOffset));

        if (scCh > 0)
        {
            if (scWork.getNumSamples() < numSamples)
                scWork.setSize (2, numSamples, false, false, true);

            const float scGain = juce::Decibels::decibelsToGain (param ("sc_gain_db"));
            const float hpfHz = param ("sc_hpf_hz");
            const float lpfHz = param ("sc_lpf_hz");
            updateScFilters (hpfHz, lpfHz);
            const bool useHpf = hpfHz > 20.5f;
            const bool useLpf = lpfHz < 19999.0f;

            float scPeak = 0.0f;
            for (int c = 0; c < scCh; ++c)
            {
                const float* src = buffer.getReadPointer (scOffset + c);
                float* dst = scWork.getWritePointer (c);
                scPeak = juce::jmax (scPeak, buffer.getMagnitude (scOffset + c, 0, numSamples));

                for (int i = 0; i < numSamples; ++i)
                {
                    float x = src[i] * scGain;
                    if (useHpf) x = scHpf.process (c, x);
                    if (useLpf) x = scLpf.process (c, x);
                    dst[i] = x;
                }
            }
            meterScDb.store (juce::Decibels::gainToDecibels (scPeak, -120.0f), std::memory_order_relaxed);
        }
    }

    const bool hasSc = scCh > 0;
    sidechainConnected.store (hasSc, std::memory_order_relaxed);
    if (! hasSc)
        meterScDb.store (-120.0f, std::memory_order_relaxed);

    float inputPeak = 0.0f;
    for (int c = 0; c < numCh; ++c)
        inputPeak = juce::jmax (inputPeak, buffer.getMagnitude (c, 0, numSamples));
    meterInputDb.store (juce::Decibels::gainToDecibels (inputPeak, -120.0f), std::memory_order_relaxed);

    ensureScratch (numSamples);
    std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + numSamples, scopeIn.begin());

    float* mainPtrs[2] {};
    const float* scPtrs[2] {};
    for (int c = 0; c < juce::jmin (numCh, 2); ++c)
        mainPtrs[c] = buffer.getWritePointer (c);
    for (int c = 0; c < scCh; ++c)
        scPtrs[c] = scWork.getReadPointer (c);

    //--------------------------------------------------------------------------
    // Stage 1: TIME
    TimeStageParams tp;
    tp.shape.ratio     = param ("t_ratio");
    tp.shape.attackDb  = paramDb ("t_attack_db");
    tp.shape.sustainDb = paramDb ("t_sustain_db");
    tp.fastMs = param ("t_fast_ms");
    tp.slowMs = param ("t_slow_ms");
    tp.mix    = param ("t_mix");
    tp.link   = param ("t_stereo_link") > 0.5f;

    const bool timeSc = hasSc && param ("t_sc") > 0.5f;
    const int stageCh = juce::jmin (numCh, 2);
    const float* const* timeDet = timeSc ? scPtrs : (const float* const*) mainPtrs;
    const int timeDetCh = timeSc ? scCh : stageCh;

    const bool timeOn = param ("t_on") > 0.5f;
    if (timeOn)
    {
        TimeStageTrace trace { traceRatio.data(), traceAttack.data(), traceSustain.data(), traceApplied.data() };
        timeStage.process (mainPtrs, stageCh, timeDet, timeDetCh, numSamples, tp, &trace);
    }
    else
        timeStage.track (timeDet, timeDetCh, numSamples, tp, stageCh);

    meterTimeMinDb.store (timeStage.getLastMinGainDb(), std::memory_order_relaxed);
    meterTimeMaxDb.store (timeStage.getLastMaxGainDb(), std::memory_order_relaxed);

    //--------------------------------------------------------------------------
    // Stage 2: SPECTRAL
    SpectralStageParams sp;
    sp.shape.ratio     = param ("s_ratio");
    sp.shape.attackDb  = paramDb ("s_attack_db");
    sp.shape.sustainDb = paramDb ("s_sustain_db");
    sp.fastMs     = param ("s_fast_ms");
    sp.slowMs     = param ("s_slow_ms");
    sp.freqSmooth = param ("s_freq_smooth");
    sp.loHz       = param ("s_lo_hz");
    sp.hiHz       = juce::jmax (sp.loHz, param ("s_hi_hz"));
    sp.mix        = param ("s_mix");
    sp.useSidechain = hasSc && param ("s_sc") > 0.5f;

    const bool specOn = param ("s_on") > 0.5f;
    for (int c = 0; c < numCh; ++c)
    {
        const float* sc = sp.useSidechain ? scPtrs[juce::jmin (c, scCh - 1)] : nullptr;
        spectral.processChannel (c, buffer.getWritePointer (c), sc, numSamples, sp, specOn);
    }

    //--------------------------------------------------------------------------
    // Output
    outGain.setTargetValue (juce::Decibels::decibelsToGain (param ("out_db")));
    if (outGain.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = outGain.getNextValue();
            for (int c = 0; c < numCh; ++c)
                buffer.getWritePointer (c)[i] *= g;
        }
    }
    else if (outGain.getTargetValue() != 1.0f)
    {
        for (int c = 0; c < numCh; ++c)
            buffer.applyGain (c, 0, numSamples, outGain.getTargetValue());
    }

    const bool scActive = timeSc || sp.useSidechain;
    pushScopeColumns (buffer.getReadPointer (0), scActive ? scPtrs[0] : nullptr, numSamples, timeOn, scActive);
}

//==============================================================================
juce::AudioProcessorEditor* ChouchouShaperAudioProcessor::createEditor()
{
    return new ChouchouShaperAudioProcessorEditor (*this);
}

void ChouchouShaperAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ChouchouShaperAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            applyFftSizeFromParameterLocked (false);
        }
}

#ifndef CHOUCHOU_SHAPER_SELFTEST
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChouchouShaperAudioProcessor();
}
#endif
