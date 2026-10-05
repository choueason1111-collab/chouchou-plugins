/*
  ==============================================================================

    ChouChou Compressor — serial signal flow

      Stage 1  BOOST  → Stage 2  COMP (+ Makeup)  → Stage 3  MIX (dry/wet)

    Each stage has its own parameters. Stages do not rewrite each other's knobs.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kEps       = 1.0e-8f;
    constexpr float kGainFloor = 1.0e-4f;   // -80 dB
    constexpr float kGainCeil  = 32.0f;     // +30.1 dB
    constexpr float kColaNorm  = 2.0f / 3.0f; // Hann + hop N/4

    constexpr float kSilencePeak = 1.0e-5f; // ~-100 dBFS

    // MaxMSP pfft~ feel across FFT sizes (raw cartopol-like amp at N=2048).
    constexpr float kMagRefFftSize = 2048.0f;

    // Hann + bin-centered full-scale sine → |X|≈N/4; after *(2048/N) → 512.
    constexpr float kSpectralFullScale = 512.0f;

    // Boost ignores empty FFT bins so the noise floor is not expanded to +30 dB.
    constexpr float kBoostNoiseFloorDb = -90.0f;
}

//==============================================================================
int NewProjectAudioProcessor::fftSizeChoiceToOrder (int choiceIndex) noexcept
{
    switch (juce::jlimit (0, 3, choiceIndex))
    {
        case 0:  return 9;
        case 1:  return 10;
        case 3:  return 12;
        default: return 11;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout NewProjectAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // --- Stage 1: BOOST ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lowthresh", 6 }, "Boost Thresh",
        juce::NormalisableRange<float> { -120.0f, 0.0f, 0.1f }, -50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dBFS")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "upratio", 5 }, "Boost Ratio",
        juce::NormalisableRange<float> { 1.0f, 20.0f, 0.01f, 0.4f }, 2.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "boostatk", 5 }, "Boost Attack",
        juce::NormalisableRange<float> { 0.0f, 500.0f, 0.01f, 0.35f }, 20.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "boostrel", 5 }, "Boost Release",
        juce::NormalisableRange<float> { 0.0f, 2000.0f, 0.01f, 0.3f }, 200.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "boostspeed", 5 }, "Boost Speed",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f));

    // --- Stage 2: COMP ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "highthresh", 6 }, "Comp Thresh",
        juce::NormalisableRange<float> { -80.0f, 12.0f, 0.1f }, -18.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dBFS")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "downratio", 5 }, "Comp Ratio",
        juce::NormalisableRange<float> { 1.0f, 20.0f, 0.01f, 0.4f }, 4.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gratk", 5 }, "Comp Attack",
        juce::NormalisableRange<float> { 0.0f, 500.0f, 0.01f, 0.35f }, 10.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "grrel", 5 }, "Comp Release",
        juce::NormalisableRange<float> { 0.0f, 2000.0f, 0.01f, 0.3f }, 120.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compspeed", 5 }, "Comp Speed",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f));

    // --- Stage 3: MIX / output ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 5 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "makeup", 5 }, "Makeup",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "fftsize", 5 }, "FFT Size",
        juce::StringArray { "512", "1024", "2048", "4096" },
        2));

    // --- Sidechain detection (per stage) ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "boostsc", 7 }, "Boost Sidechain", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "compsc", 7 }, "Comp Sidechain", false));

    return { params.begin(), params.end() };
}

float NewProjectAudioProcessor::timeMsToCoeff (float timeMs, float hopSeconds) noexcept
{
    if (timeMs <= 0.0f || hopSeconds <= 0.0f)
        return 1.0f;

    const float t = juce::jmax (timeMs * 0.001f, hopSeconds * 0.25f);
    return 1.0f - std::exp (-hopSeconds / t);
}

float NewProjectAudioProcessor::speedToEngageCoeff (float speed0to100, float hopSeconds) noexcept
{
    // Speed = how fast to reach target AFTER Attack delay.
    // 0 → ~400 ms, 50 → ~40 ms, 100 → ~2 ms (near-instant).
    const float s = juce::jlimit (0.0f, 100.0f, speed0to100) * 0.01f;
    const float engageMs = 2.0f + 398.0f * std::pow (0.05f, s);
    return timeMsToCoeff (engageMs, hopSeconds);
}

int NewProjectAudioProcessor::attackMsToDelayHops (float attackMs, float hopSeconds) noexcept
{
    // Attack = how long to WAIT before gain starts moving toward target.
    if (attackMs <= 0.0f || hopSeconds <= 0.0f)
        return 0;

    return (int) std::ceil ((double) (attackMs * 0.001f) / (double) hopSeconds);
}

float NewProjectAudioProcessor::getHopMilliseconds() const noexcept
{
    return (float) (1000.0 * (double) hopSize / juce::jmax (1.0, currentSampleRate));
}

float NewProjectAudioProcessor::boostTargetGain (float magLin, float lowLin, float upRatio) noexcept
{
    const float noiseFloorLin = juce::Decibels::decibelsToGain (kBoostNoiseFloorDb)
                                * kSpectralFullScale;

    // Ratio 1 = Stage 1 bypass.
    if (upRatio <= 1.0f + 1.0e-4f)
        return 1.0f;

    if (magLin < noiseFloorLin || magLin >= lowLin)
        return 1.0f;

    const float envDb = juce::Decibels::gainToDecibels (magLin / kSpectralFullScale, -160.0f);
    const float lowDb = juce::Decibels::gainToDecibels (lowLin / kSpectralFullScale, -160.0f);
    const float slope = 1.0f - 1.0f / upRatio;
    return juce::Decibels::decibelsToGain ((lowDb - envDb) * slope);
}

float NewProjectAudioProcessor::grTargetGain (float magLin, float highLin, float downRatio) noexcept
{
    // Ratio 1 = Stage 2 bypass.
    if (downRatio <= 1.0f + 1.0e-4f)
        return 1.0f;

    if (magLin <= highLin)
        return 1.0f;

    const float envDb  = juce::Decibels::gainToDecibels (magLin / kSpectralFullScale, -160.0f);
    const float highDb = juce::Decibels::gainToDecibels (highLin / kSpectralFullScale, -160.0f);
    const float slope  = 1.0f - 1.0f / downRatio;
    return juce::Decibels::decibelsToGain ((highDb - envDb) * slope);
}

void NewProjectAudioProcessor::resetDynamicsToUnity() noexcept
{
    for (auto& ch : channels)
    {
        std::fill (ch.boostGain.begin(), ch.boostGain.end(), 1.0f);
        std::fill (ch.grGain.begin(), ch.grGain.end(), 1.0f);
        std::fill (ch.boostDelayHops.begin(), ch.boostDelayHops.end(), -1);
        std::fill (ch.grDelayHops.begin(), ch.grDelayHops.end(), -1);
    }

    meterBoostDb.store (0.0f, std::memory_order_relaxed);
    meterGrDb.store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
NewProjectAudioProcessor::NewProjectAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#else
     :
#endif
       apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    configureTransform (kDefaultFftOrder);
    apvts.addParameterListener ("fftsize", this);
}

NewProjectAudioProcessor::~NewProjectAudioProcessor()
{
    apvts.removeParameterListener ("fftsize", this);
    cancelPendingUpdate();
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
    const float boostRelMs = apvts.getRawParameterValue ("boostrel")->load();
    const float grRelMs = apvts.getRawParameterValue ("grrel")->load();
    const double releaseSec = (double) juce::jmax (boostRelMs, grRelMs) * 0.001;
    return (double) fftSize / juce::jmax (1.0, currentSampleRate) + releaseSec;
}

int NewProjectAudioProcessor::getNumPrograms()                     { return 1; }
int NewProjectAudioProcessor::getCurrentProgram()                 { return 0; }
void NewProjectAudioProcessor::setCurrentProgram (int)            {}
const juce::String NewProjectAudioProcessor::getProgramName (int) { return {}; }
void NewProjectAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void NewProjectAudioProcessor::configureTransform (int order)
{
    order = juce::jlimit (kMinFftOrder, kMaxFftOrder, order);
    fftOrder = order;
    fftSize  = 1 << fftOrder;
    hopSize  = fftSize / 4;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    setLatencySamples (fftSize);
}

void NewProjectAudioProcessor::resetChannel (ChannelState& ch)
{
    const int numBins = fftSize / 2 + 1;

    ch.inputFifo.assign ((size_t) fftSize, 0.0f);
    ch.ola.assign ((size_t) fftSize, 0.0f);
    ch.outFifo.assign ((size_t) hopSize, 0.0f);
    ch.fftData.assign ((size_t) fftSize * 2, 0.0f);
    ch.scInputFifo.assign ((size_t) fftSize, 0.0f);
    ch.scFftData.assign ((size_t) fftSize * 2, 0.0f);
    ch.boostGain.assign ((size_t) numBins, 1.0f);
    ch.grGain.assign ((size_t) numBins, 1.0f);
    ch.boostDelayHops.assign ((size_t) numBins, -1);
    ch.grDelayHops.assign ((size_t) numBins, -1);
    ch.dryDelay.assign ((size_t) fftSize, 0.0f);
    ch.window.resize ((size_t) fftSize);

    for (int i = 0; i < fftSize; ++i)
        ch.window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) fftSize));

    ch.writePos = 0;
    ch.outRead = 0;
    ch.outAvailable = 0;
    ch.dryWrite = 0;
}

void NewProjectAudioProcessor::reset()
{
    const juce::ScopedLock sl (getCallbackLock());
    for (auto& ch : channels)
        resetChannel (ch);
    resetDynamicsToUnity();
}

void NewProjectAudioProcessor::applyFftSizeFromParameter (bool forceRebuild)
{
    const int choice = (int) apvts.getRawParameterValue ("fftsize")->load();
    const int order  = fftSizeChoiceToOrder (choice);

    if (! forceRebuild && order == fftOrder)
        return;

    configureTransform (order);

    for (auto& ch : channels)
        resetChannel (ch);
}

void NewProjectAudioProcessor::applyFftSizeFromParameterLocked (bool forceRebuild)
{
    const int choice = (int) apvts.getRawParameterValue ("fftsize")->load();
    const int order  = fftSizeChoiceToOrder (choice);

    if (! forceRebuild && order == fftOrder)
        return;

    const juce::ScopedLock sl (getCallbackLock());
    applyFftSizeFromParameter (forceRebuild);
}

void NewProjectAudioProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "fftsize")
        triggerAsyncUpdate();
}

void NewProjectAudioProcessor::handleAsyncUpdate()
{
    applyFftSizeFromParameterLocked (false);
}

void NewProjectAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;

    // Always keep stereo channel state available — nested hosts (Plugplugin / Insert)
    // can report 0 inputs briefly while still delivering a stereo in-place buffer.
    const int reported = juce::jmax (getMainBusNumInputChannels(),
                                     getMainBusNumOutputChannels(),
                                     getTotalNumOutputChannels());
    const int numChannels = juce::jmax (2, reported);
    channels.resize ((size_t) numChannels);

    applyFftSizeFromParameter (true);
}

void NewProjectAudioProcessor::numChannelsChanged()
{
    if (currentSampleRate > 0.0)
        prepareToPlay (currentSampleRate, preparedBlockSize);
}

void NewProjectAudioProcessor::processorLayoutsChanged()
{
    if (currentSampleRate > 0.0)
        prepareToPlay (currentSampleRate, preparedBlockSize);
}

void NewProjectAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NewProjectAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled()
         && sc != juce::AudioChannelSet::mono()
         && sc != juce::AudioChannelSet::stereo())
            return false;
    }
   #endif

    return true;
  #endif
}
#endif

//==============================================================================
void NewProjectAudioProcessor::processHop (ChannelState& ch,
                                           const HopParams& hp,
                                           float& hopBoostDb,
                                           float& hopGrDb)
{
    if (fft == nullptr)
        return;

    float framePeak = 0.0f;
    for (int i = 0; i < fftSize; ++i)
        framePeak = juce::jmax (framePeak, std::abs (ch.inputFifo[(size_t) i]));

    const bool forceUnity = framePeak < kSilencePeak;

    for (int i = 0; i < fftSize; ++i)
        ch.fftData[(size_t) i] = ch.inputFifo[(size_t) i] * ch.window[(size_t) i];

    std::fill (ch.fftData.begin() + fftSize, ch.fftData.end(), 0.0f);
    fft->performRealOnlyForwardTransform (ch.fftData.data(), true);

    const bool needSc = hp.useBoostSc || hp.useCompSc;
    bool scSilent = true;

    if (needSc)
    {
        float scFramePeak = 0.0f;
        for (int i = 0; i < fftSize; ++i)
        {
            const float s = ch.scInputFifo[(size_t) i];
            scFramePeak = juce::jmax (scFramePeak, std::abs (s));
            ch.scFftData[(size_t) i] = s * ch.window[(size_t) i];
        }

        scSilent = scFramePeak < kSilencePeak;
        std::fill (ch.scFftData.begin() + fftSize, ch.scFftData.end(), 0.0f);
        fft->performRealOnlyForwardTransform (ch.scFftData.data(), true);
    }

    const bool boostForceUnity = hp.useBoostSc ? scSilent : forceUnity;
    const bool compForceUnity  = hp.useCompSc  ? scSilent : forceUnity;

    const int numBins = fftSize / 2;
    const float noiseFloorLin = juce::Decibels::decibelsToGain (kBoostNoiseFloorDb)
                                * kSpectralFullScale;

    // First pass: peak spectral mag for this hop (for meter gating).
    float frameMagPeak = kEps;
    auto binMag = [&] (float re, float im) -> float
    {
        return std::sqrt (re * re + im * im) * (kMagRefFftSize / (float) fftSize) + kEps;
    };

    {
        frameMagPeak = juce::jmax (frameMagPeak, binMag (ch.fftData[0], 0.0f));
        frameMagPeak = juce::jmax (frameMagPeak, binMag (ch.fftData[1], 0.0f));
        for (int bin = 1; bin < numBins; ++bin)
        {
            const int reIndex = 2 * bin;
            frameMagPeak = juce::jmax (frameMagPeak,
                                       binMag (ch.fftData[(size_t) reIndex],
                                               ch.fftData[(size_t) (reIndex + 1)]));
        }
    }

    // Ignore FFT leakage when reporting BOOST — only meter bins near the hop peak
    // (≈ main lobe). Otherwise side-bins below Boost Thresh peg the meter forever.
    const float meterFloorLin = juce::jmax (noiseFloorLin, frameMagPeak * 0.25f); // ~-12 dB re peak

    auto processBin = [&] (int magIndex, float& re, float& im, float scMag)
    {
        const float mag = binMag (re, im);

        float& b = ch.boostGain[(size_t) magIndex];
        float& g = ch.grGain[(size_t) magIndex];
        int& bDelay = ch.boostDelayHops[(size_t) magIndex];
        int& gDelay = ch.grDelayHops[(size_t) magIndex];

        //------------------------------------------------------------------
        // STAGE 1 — BOOST (input or sidechain magnitude; Comp params ignored here)
        // Attack = wait N hops before moving; Speed = rate once engaged.
        //------------------------------------------------------------------
        const float boostDetect = hp.useBoostSc ? scMag : mag;

        float boostTarget = 1.0f;
        if (! boostForceUnity)
            boostTarget = juce::jlimit (1.0f, kGainCeil,
                                        boostTargetGain (boostDetect, hp.boostThreshLin, hp.boostRatio));

        const bool boostWants = boostTarget > 1.001f;

        if (! boostWants)
        {
            bDelay = -1;
            b += hp.boostRelCoeff * (1.0f - b);
        }
        else
        {
            if (bDelay < 0)
                bDelay = hp.boostAttackDelayHops;

            if (bDelay > 0)
                --bDelay;
            else
                b += hp.boostSpeedCoeff * (boostTarget - b);
        }

        if (std::abs (b - 1.0f) < 1.0e-4f) b = 1.0f;

        if (b > 1.001f && mag >= meterFloorLin)
            hopBoostDb = juce::jmax (hopBoostDb, juce::Decibels::gainToDecibels (b, 0.0f));

        //------------------------------------------------------------------
        // STAGE 2 — COMP (detects AFTER boost — true serial signal flow,
        // or directly from the sidechain spectrum)
        //------------------------------------------------------------------
        const float compDetect = hp.useCompSc ? scMag : mag * b;

        float grTarget = 1.0f;
        if (! compForceUnity)
            grTarget = juce::jlimit (kGainFloor, 1.0f,
                                     grTargetGain (compDetect, hp.compThreshLin, hp.compRatio));

        const bool grWants = grTarget < 0.999f;

        if (! grWants)
        {
            gDelay = -1;
            g += hp.compRelCoeff * (1.0f - g);
        }
        else
        {
            if (gDelay < 0)
                gDelay = hp.compAttackDelayHops;

            if (gDelay > 0)
                --gDelay;
            else
                g += hp.compSpeedCoeff * (grTarget - g);
        }

        if (std::abs (g - 1.0f) < 1.0e-4f) g = 1.0f;

        if (g < 0.999f)
            hopGrDb = juce::jmax (hopGrDb, -juce::Decibels::gainToDecibels (g, -80.0f));

        // Apply Stage 1 then Stage 2, then Makeup (still wet-only; Mix is Stage 3).
        const float gain = b * g * hp.makeupLin;
        re *= gain;
        im *= gain;
    };

    auto scBinMag = [&] (int reIndex, bool realOnly) -> float
    {
        if (! needSc)
            return 0.0f;
        return binMag (ch.scFftData[(size_t) reIndex],
                       realOnly ? 0.0f : ch.scFftData[(size_t) (reIndex + 1)]);
    };

    {
        float re = ch.fftData[0], im = 0.0f;
        processBin (0, re, im, scBinMag (0, true));
        ch.fftData[0] = re;
    }
    {
        float re = ch.fftData[1], im = 0.0f;
        processBin (numBins, re, im, scBinMag (1, true));
        ch.fftData[1] = re;
    }

    for (int bin = 1; bin < numBins; ++bin)
    {
        const int reIndex = 2 * bin;
        float re = ch.fftData[(size_t) reIndex];
        float im = ch.fftData[(size_t) (reIndex + 1)];
        processBin (bin, re, im, scBinMag (reIndex, false));
        ch.fftData[(size_t) reIndex]       = re;
        ch.fftData[(size_t) (reIndex + 1)] = im;
    }

    fft->performRealOnlyInverseTransform (ch.fftData.data());

    for (int i = 0; i < fftSize; ++i)
        ch.ola[(size_t) i] += ch.fftData[(size_t) i] * ch.window[(size_t) i] * kColaNorm;

    for (int i = 0; i < hopSize; ++i)
        ch.outFifo[(size_t) i] = ch.ola[(size_t) i];

    std::copy (ch.ola.begin() + hopSize, ch.ola.end(), ch.ola.begin());
    std::fill (ch.ola.end() - hopSize, ch.ola.end(), 0.0f);

    std::copy (ch.inputFifo.begin() + hopSize, ch.inputFifo.end(), ch.inputFifo.begin());
    std::copy (ch.scInputFifo.begin() + hopSize, ch.scInputFifo.end(), ch.scInputFifo.begin());

    ch.writePos = fftSize - hopSize;
    ch.outRead = 0;
    ch.outAvailable = hopSize;
}

void NewProjectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    processCounter.fetch_add (1, std::memory_order_relaxed);

    if (channels.empty())
        return;

    const int numSamples = buffer.getNumSamples();
    const int bufCh = buffer.getNumChannels();

    // Prefer main-bus counts. Nested AU hosts sometimes report totalIn == 0 while
    // still passing an in-place stereo buffer — never wipe that audio.
    // Total input count would include the sidechain bus, so only use the main bus.
    int numIn  = getMainBusNumInputChannels();
    int numOut = getMainBusNumOutputChannels();

    if (numOut <= 0)
        numOut = getTotalNumOutputChannels();

    if (numIn <= 0 && bufCh > 0)
        numIn = juce::jmin (bufCh, juce::jmax (1, numOut));
    if (numOut <= 0)
        numOut = juce::jmax (numIn, bufCh);

    for (int i = numIn; i < numOut && i < bufCh; ++i)
        buffer.clear (i, 0, numSamples);

    // Stage params — fully independent (no cross-clamping).
    const float boostThreshDb = apvts.getRawParameterValue ("lowthresh")->load();
    const float boostRatio    = apvts.getRawParameterValue ("upratio")->load();
    const float boostAtkMs    = apvts.getRawParameterValue ("boostatk")->load();
    const float boostRelMs    = apvts.getRawParameterValue ("boostrel")->load();
    const float boostSpeed    = apvts.getRawParameterValue ("boostspeed")->load();

    const float compThreshDb  = apvts.getRawParameterValue ("highthresh")->load();
    const float compRatio     = apvts.getRawParameterValue ("downratio")->load();
    const float compAtkMs     = apvts.getRawParameterValue ("gratk")->load();
    const float compRelMs     = apvts.getRawParameterValue ("grrel")->load();
    const float compSpeed     = apvts.getRawParameterValue ("compspeed")->load();

    const float mix  = apvts.getRawParameterValue ("mix")->load();
    const float mkDb = apvts.getRawParameterValue ("makeup")->load();

    const float hopSeconds = (float) hopSize / (float) juce::jmax (1.0, currentSampleRate);

    HopParams hp;
    hp.boostThreshLin       = juce::Decibels::decibelsToGain (boostThreshDb) * kSpectralFullScale;
    hp.boostRatio           = boostRatio;
    hp.boostAttackDelayHops = attackMsToDelayHops (boostAtkMs, hopSeconds);
    hp.boostSpeedCoeff      = speedToEngageCoeff (boostSpeed, hopSeconds);
    hp.boostRelCoeff        = timeMsToCoeff (boostRelMs, hopSeconds);

    hp.compThreshLin        = juce::Decibels::decibelsToGain (compThreshDb) * kSpectralFullScale;
    hp.compRatio            = compRatio;
    hp.compAttackDelayHops  = attackMsToDelayHops (compAtkMs, hopSeconds);
    hp.compSpeedCoeff       = speedToEngageCoeff (compSpeed, hopSeconds);
    hp.compRelCoeff         = timeMsToCoeff (compRelMs, hopSeconds);

    hp.makeupLin            = juce::Decibels::decibelsToGain (mkDb);

    const int numCh = juce::jmin (bufCh,
                                  juce::jmax (numIn, 1),
                                  (int) channels.size());

    // Sidechain bus lives after the main input channels in the buffer.
    int scCh = 0;
    juce::AudioBuffer<float> scBuffer;
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
    {
        const int scOffset = scBus->getChannelIndexInProcessBlockBuffer (0);
        scCh = juce::jmin (scBus->getNumberOfChannels(), bufCh - scOffset);
        if (scCh > 0)
            scBuffer = getBusBuffer (buffer, true, 1);
        else
            scCh = 0;
    }

    const bool hasSidechain = scCh > 0;
    sidechainConnected.store (hasSidechain, std::memory_order_relaxed);

    hp.useBoostSc = hasSidechain && apvts.getRawParameterValue ("boostsc")->load() > 0.5f;
    hp.useCompSc  = hasSidechain && apvts.getRawParameterValue ("compsc")->load()  > 0.5f;

    float inputPeak = 0.0f;
    for (int channel = 0; channel < numCh; ++channel)
        inputPeak = juce::jmax (inputPeak, buffer.getMagnitude (channel, 0, numSamples));

    meterInputDb.store (juce::Decibels::gainToDecibels (inputPeak, -120.0f),
                        std::memory_order_relaxed);

    float scPeak = 0.0f;
    for (int channel = 0; channel < scCh; ++channel)
        scPeak = juce::jmax (scPeak, scBuffer.getMagnitude (channel, 0, numSamples));

    meterScDb.store (juce::Decibels::gainToDecibels (scPeak, -120.0f),
                     std::memory_order_relaxed);

    const bool silentBlock = inputPeak < kSilencePeak;

    float blockBoostDb = 0.0f;
    float blockGrDb = 0.0f;
    bool didHop = false;

    for (int channel = 0; channel < numCh; ++channel)
    {
        auto& ch = channels[(size_t) channel];
        auto* data = buffer.getWritePointer (channel);
        const float* scData = hasSidechain
                                ? scBuffer.getReadPointer (juce::jmin (channel, scCh - 1))
                                : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryIn = data[i];

            //------------------------------------------------------------------
            // STAGE 3 — MIX (latency-aligned dry vs wet from Stages 1+2)
            //------------------------------------------------------------------
            const float delayedDry = ch.dryDelay[(size_t) ch.dryWrite];
            ch.dryDelay[(size_t) ch.dryWrite] = dryIn;
            ch.dryWrite = (ch.dryWrite + 1) % fftSize;

            ch.inputFifo[(size_t) ch.writePos] = dryIn;
            ch.scInputFifo[(size_t) ch.writePos] = scData != nullptr ? scData[i] : 0.0f;
            ++ch.writePos;

            float wet = 0.0f;
            if (ch.outAvailable > 0)
            {
                wet = ch.outFifo[(size_t) ch.outRead];
                ++ch.outRead;
                --ch.outAvailable;
            }

            data[i] = (1.0f - mix) * delayedDry + mix * wet;

            if (ch.writePos == fftSize)
            {
                float hopBoost = 0.0f, hopGr = 0.0f;
                processHop (ch, hp, hopBoost, hopGr);
                blockBoostDb = juce::jmax (blockBoostDb, hopBoost);
                blockGrDb = juce::jmax (blockGrDb, hopGr);
                didHop = true;
            }
        }
    }

    if (silentBlock)
    {
        meterBoostDb.store (0.0f, std::memory_order_relaxed);
        meterGrDb.store (0.0f, std::memory_order_relaxed);

        bool nearUnity = true;
        for (auto& ch : channels)
        {
            for (auto b : ch.boostGain)
                if (std::abs (b - 1.0f) > 1.0e-3f) { nearUnity = false; break; }
            if (! nearUnity) break;
            for (auto g : ch.grGain)
                if (std::abs (g - 1.0f) > 1.0e-3f) { nearUnity = false; break; }
            if (! nearUnity) break;
        }

        if (nearUnity)
            resetDynamicsToUnity();
    }
    else if (didHop)
    {
        if (blockBoostDb < 0.05f) blockBoostDb = 0.0f;
        if (blockGrDb < 0.05f) blockGrDb = 0.0f;

        meterBoostDb.store (blockBoostDb, std::memory_order_relaxed);
        meterGrDb.store (blockGrDb, std::memory_order_relaxed);
    }
}

//==============================================================================
bool NewProjectAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* NewProjectAudioProcessor::createEditor()
{
    return new NewProjectAudioProcessorEditor (*this);
}

//==============================================================================
void NewProjectAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void NewProjectAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            applyFftSizeFromParameterLocked (false);
        }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}
