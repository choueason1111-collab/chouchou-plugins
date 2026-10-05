/*
  ==============================================================================

    ChouChou EQ Gate — spectral expander

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kEps       = 1.0e-8f;
    constexpr float kGainFloor = 1.0e-4f;
    constexpr float kGainCeil  = 32.0f;
    constexpr float kColaNorm  = 2.0f / 3.0f; // Hann + hop N/4
}

//==============================================================================
int NewProjectAudioProcessor::fftSizeChoiceToOrder (int choiceIndex) noexcept
{
    // UI: 0=512, 1=1024, 2=2048, 3=4096
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

    // version 5: ParameterID "amt" (was "amount") so hosts drop cached 0–32 range
    // amt 0 = no effect (x^0 = 1); high = extreme sculpting (up to 100)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amt", 5 }, "Amount",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f, 0.25f }, 0.8f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "threshold", 5 }, "Threshold",
        juce::NormalisableRange<float> { -120.0f, 12.0f, 0.1f }, -40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 5 }, "Mode",
        juce::StringArray { "Expand", "Gate" },
        0));

    // 0 ms = instant (coeff = 1). Skew keeps fine control near zero.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "attack", 5 }, "Attack",
        juce::NormalisableRange<float> { 0.0f, 500.0f, 0.01f, 0.35f }, 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "release", 5 }, "Release",
        juce::NormalisableRange<float> { 0.0f, 2000.0f, 0.01f, 0.3f }, 80.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 5 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "makeup", 5 }, "Makeup",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // Smaller FFT = faster hop = shorter usable attack/release
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "fftsize", 5 }, "FFT Size",
        juce::StringArray { "512", "1024", "2048", "4096" },
        2)); // default 2048

    return { params.begin(), params.end() };
}

float NewProjectAudioProcessor::timeMsToCoeff (float timeMs, float hopSeconds) noexcept
{
    // 0 ms → instant follow (coeff = 1)
    if (timeMs <= 0.0f)
        return 1.0f;

    const float t = timeMs * 0.001f;
    return 1.0f - std::exp (-hopSeconds / t);
}

float NewProjectAudioProcessor::getHopMilliseconds() const noexcept
{
    return (float) (1000.0 * (double) hopSize / juce::jmax (1.0, currentSampleRate));
}

//==============================================================================
NewProjectAudioProcessor::NewProjectAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
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
}

NewProjectAudioProcessor::~NewProjectAudioProcessor() = default;

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
    return (double) fftSize / juce::jmax (1.0, currentSampleRate);
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
    ch.inputFifo.assign ((size_t) fftSize, 0.0f);
    ch.ola.assign ((size_t) fftSize, 0.0f);
    ch.outFifo.assign ((size_t) hopSize, 0.0f);
    ch.fftData.assign ((size_t) fftSize * 2, 0.0f);
    ch.smoothedMag.assign ((size_t) (fftSize / 2 + 1), 0.0f);
    ch.window.resize ((size_t) fftSize);

    for (int i = 0; i < fftSize; ++i)
        ch.window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) fftSize));

    ch.writePos = 0;
    ch.outRead = 0;
    ch.outAvailable = 0;
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

void NewProjectAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;

    const int numChannels = juce::jmax (1, juce::jmax (getTotalNumInputChannels(),
                                                       getTotalNumOutputChannels()));
    channels.resize ((size_t) numChannels);

    applyFftSizeFromParameter (true);
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
   #endif

    return true;
  #endif
}
#endif

void NewProjectAudioProcessor::processHop (ChannelState& ch,
                                           float amount,
                                           float thresholdLin,
                                           float mix,
                                           float makeupLin,
                                           float attackCoeff,
                                           float releaseCoeff,
                                           int mode)
{
    if (fft == nullptr)
        return;

    for (int i = 0; i < fftSize; ++i)
        ch.fftData[(size_t) i] = ch.inputFifo[(size_t) i] * ch.window[(size_t) i];

    std::fill (ch.fftData.begin() + fftSize, ch.fftData.end(), 0.0f);
    fft->performRealOnlyForwardTransform (ch.fftData.data(), true);

    const int numBins = fftSize / 2;
    const bool gateOnly = (mode == 1); // Gate: only weaken below threshold

    auto expandBin = [&] (int magIndex, float& re, float& im)
    {
        const float mag = std::sqrt (re * re + im * im);
        float& env = ch.smoothedMag[(size_t) magIndex];
        const float coeff = mag > env ? attackCoeff : releaseCoeff;
        env += coeff * (mag - env);

        const float relative = env / (thresholdLin + kEps);
        float gain = std::pow (juce::jmax (relative, kEps), amount);

        // Gate mode: never boost above unity — only cut weak bins
        if (gateOnly)
            gain = juce::jmin (gain, 1.0f);

        gain = juce::jlimit (kGainFloor, kGainCeil, gain);
        gain = (1.0f - mix) + mix * gain;
        gain *= makeupLin;

        re *= gain;
        im *= gain;
    };

    {
        float re = ch.fftData[0], im = 0.0f;
        expandBin (0, re, im);
        ch.fftData[0] = re;
    }
    {
        float re = ch.fftData[1], im = 0.0f;
        expandBin (numBins, re, im);
        ch.fftData[1] = re;
    }

    for (int bin = 1; bin < numBins; ++bin)
    {
        const int reIndex = 2 * bin;
        float re = ch.fftData[(size_t) reIndex];
        float im = ch.fftData[(size_t) (reIndex + 1)];
        expandBin (bin, re, im);
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

    ch.writePos = fftSize - hopSize;
    ch.outRead = 0;
    ch.outAvailable = hopSize;
}

void NewProjectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (channels.empty())
        return;

    // Hot-swap FFT size if the user changed it (safe rebuild between blocks)
    applyFftSizeFromParameter (false);

    const float amount       = apvts.getRawParameterValue ("amt")->load();
    const float thresholdDb  = apvts.getRawParameterValue ("threshold")->load();
    const int   mode         = (int) apvts.getRawParameterValue ("mode")->load();
    const float attackMs     = apvts.getRawParameterValue ("attack")->load();
    const float releaseMs    = apvts.getRawParameterValue ("release")->load();
    const float mix          = apvts.getRawParameterValue ("mix")->load();
    const float makeupDb     = apvts.getRawParameterValue ("makeup")->load();
    const float thresholdLin = juce::Decibels::decibelsToGain (thresholdDb);
    const float makeupLin    = juce::Decibels::decibelsToGain (makeupDb);

    const float hopSeconds   = (float) hopSize / (float) juce::jmax (1.0, currentSampleRate);
    const float attackCoeff  = timeMsToCoeff (attackMs, hopSeconds);
    const float releaseCoeff = timeMsToCoeff (releaseMs, hopSeconds);

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (totalNumInputChannels, (int) channels.size());

    for (int channel = 0; channel < numCh; ++channel)
    {
        auto& ch = channels[(size_t) channel];
        auto* data = buffer.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
        {
            ch.inputFifo[(size_t) ch.writePos] = data[i];
            ++ch.writePos;

            float out = 0.0f;

            if (ch.outAvailable > 0)
            {
                out = ch.outFifo[(size_t) ch.outRead];
                ++ch.outRead;
                --ch.outAvailable;
            }

            data[i] = out;

            if (ch.writePos == fftSize)
                processHop (ch, amount, thresholdLin, mix, makeupLin,
                            attackCoeff, releaseCoeff, mode);
        }
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
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}
