/*
  ==============================================================================
    PluginProcessor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout NewProjectAudioProcessor::makeParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "fftSize", 1 }, "fftSize",
        juce::StringArray { "512", "1024", "2048", "4096" }, 1)); // 預設 1024

    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "experiment", 1 }, "experiment",
        juce::StringArray { "0_mul", "1_pitch", "2_split", "3_add", "4_write" }, 0));

    auto f = [] (const char* id, const char* name, float minV, float maxV, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (minV, maxV, 0.001f), def);
    };

    // shift 類：讀寫 bin 倍率。太大→嚴重摺疊/金屬/空洞；太小→擠在低頻附近
    p.push_back (f ("shift",      "shift",      0.01f, 16.0f, 0.9f));
    p.push_back (f ("shiftreel",  "shiftreel",  0.01f, 16.0f, 0.9f));
    p.push_back (f ("shiftimage", "shiftimage", 0.01f, 16.0f, 1.1f));
    // mul 類：振幅/相位倍率。mulreel 太大→爆音削波；mulimage 太大→相位亂轉、音色碎
    p.push_back (f ("mulreel",    "mulreel",    0.0f,  16.0f, 1.0f));
    p.push_back (f ("mulimage",   "mulimage",   0.0f,  16.0f, 1.0f));
    // offsetBins：實驗 3 平移用（單位 = FFT bin）
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "offsetBins", 1 }, "offsetBins",
        juce::NormalisableRange<float> (-2048.0f, 2048.0f, 1.0f), 20.0f));

    // 0 = dry only, 1 = full wet. Dry is delayed by the FFT size so the mix stays in phase.
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return { p.begin(), p.end() };
}

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
#endif
      apvts (*this, nullptr, "PARAMS", makeParams())
{
    allocateMaxBuffers (channels[0]);
    allocateMaxBuffers (channels[1]);
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
    return currentSampleRate > 0.0 ? ((double) fftSize * 1.5 / currentSampleRate) : 0.0;
}
int NewProjectAudioProcessor::getNumPrograms() { return 1; }
int NewProjectAudioProcessor::getCurrentProgram() { return 0; }
void NewProjectAudioProcessor::setCurrentProgram (int) {}
const juce::String NewProjectAudioProcessor::getProgramName (int) { return {}; }
void NewProjectAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
int NewProjectAudioProcessor::choiceToOrder (int choiceIndex) noexcept
{
    choiceIndex = juce::jlimit (0, 3, choiceIndex);
    return minFftOrder + choiceIndex; // 9..12
}

void NewProjectAudioProcessor::allocateMaxBuffers (Chan& c)
{
    c.inRing.assign ((size_t) maxFftSize, 0.0f);
    c.outRing.assign ((size_t) maxFftSize, 0.0f);
    c.frame.assign ((size_t) maxFftSize, 0.0f);
    c.spectrum.assign ((size_t) (maxFftSize * 2), 0.0f);
    c.reeldata.assign ((size_t) maxNBins, 0.0f);
    c.imagedata.assign ((size_t) maxNBins, 0.0f);
    c.outReel.assign ((size_t) maxNBins, 0.0f);
    c.outImage.assign ((size_t) maxNBins, 0.0f);
    c.dryDelay.assign ((size_t) maxFftSize, 0.0f);
    c.writePos = 0;
    c.hopCounter = 0;
    c.dryPos = 0;
}

void NewProjectAudioProcessor::clearChanState (Chan& c)
{
    std::fill (c.inRing.begin(), c.inRing.end(), 0.0f);
    std::fill (c.outRing.begin(), c.outRing.end(), 0.0f);
    std::fill (c.frame.begin(), c.frame.end(), 0.0f);
    std::fill (c.spectrum.begin(), c.spectrum.end(), 0.0f);
    std::fill (c.reeldata.begin(), c.reeldata.end(), 0.0f);
    std::fill (c.imagedata.begin(), c.imagedata.end(), 0.0f);
    std::fill (c.outReel.begin(), c.outReel.end(), 0.0f);
    std::fill (c.outImage.begin(), c.outImage.end(), 0.0f);
    std::fill (c.dryDelay.begin(), c.dryDelay.end(), 0.0f);
    c.writePos = 0;
    c.hopCounter = 0;
    c.dryPos = 0;
}

void NewProjectAudioProcessor::applyFftSizeFromChoice (int choiceIndex, bool forceReset)
{
    const int newOrder = choiceToOrder (choiceIndex);
    if (! forceReset && newOrder == fftOrder)
        return;

    fftOrder = newOrder;
    fftSize  = 1 << fftOrder;
    hopSize  = fftSize / 4;
    nBins    = fftSize / 2 + 1;

    clearChanState (channels[0]);
    clearChanState (channels[1]);
    setLatencySamples (fftSize);
}

juce::dsp::FFT& NewProjectAudioProcessor::currentFft() noexcept
{
    switch (fftOrder)
    {
        case 9:  return fft512;
        case 11: return fft2048;
        case 12: return fft4096;
        default: return fft1024;
    }
}

juce::dsp::WindowingFunction<float>& NewProjectAudioProcessor::currentHann() noexcept
{
    switch (fftOrder)
    {
        case 9:  return hann512;
        case 11: return hann2048;
        case 12: return hann4096;
        default: return hann1024;
    }
}

void NewProjectAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int choice = (int) apvts.getRawParameterValue ("fftSize")->load();
    applyFftSizeFromChoice (choice, true);
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

int NewProjectAudioProcessor::wrapi (int i, int n)
{
    if (n <= 0) return 0;
    i %= n;
    if (i < 0) i += n;
    return i;
}

//==============================================================================
// MAX gen 對應的五種實驗（0_mul ~ 4_write）
void NewProjectAudioProcessor::runExperiment (Chan& c,
                                              int experiment,
                                              float shift,
                                              float shiftreel,
                                              float shiftimage,
                                              float mulreel,
                                              float mulimage,
                                              float offsetBins)
{
    const int bins = nBins;

    // JUCE real-FFT：spectrum[2*i], spectrum[2*i+1] = bin i 的 re/im（含 DC、Nyquist）
    auto readPolar = [&] (int idx, float& reel, float& image)
    {
        const float re = c.spectrum[(size_t) (2 * idx)];
        const float im = c.spectrum[(size_t) (2 * idx + 1)];
        reel  = std::sqrt (re * re + im * im);
        image = std::atan2 (im, re);
    };

    auto writePolar = [&] (int idx, float reel, float image)
    {
        c.spectrum[(size_t) (2 * idx)]     = reel * std::cos (image);
        c.spectrum[(size_t) (2 * idx + 1)] = reel * std::sin (image);
    };

    // 對齊 MAX：wrap(pos, 0, 4095) + peek(..., interp="linear")
    // Data(4096) 裡只有 0..bins-1 有本幀資料；超過的格子視為 0（不要折回 bins）
    auto wrapMax4095 = [] (float pos) noexcept
    {
        constexpr float hi = 4095.0f;
        pos = std::fmod (pos, hi);
        if (pos < 0.0f)
            pos += hi;
        return pos;
    };

    auto peekPolar = [&] (float pos, float& reel, float& image)
    {
        const float x = wrapMax4095 (pos);
        const int i0 = (int) x;
        int i1 = i0 + 1;
        if (i1 >= 4095)
            i1 = 0;
        const float frac = x - (float) i0;

        auto sample = [&] (int i) noexcept -> std::pair<float, float>
        {
            if (i < 0 || i >= bins)
                return { 0.0f, 0.0f };
            return { c.reeldata[(size_t) i], c.imagedata[(size_t) i] };
        };

        const auto a = sample (i0);
        const auto b = sample (i1);
        reel  = a.first  * (1.0f - frac) + b.first  * frac;
        image = a.second * (1.0f - frac) + b.second * frac;
    };

    auto& outReel  = c.outReel;
    auto& outImage = c.outImage;

    // 4_write：寫入重映射（複數累加，避免覆蓋）
    if (experiment == 4)
    {
        std::fill_n (outReel.begin(),  (size_t) bins, 0.0f);
        std::fill_n (outImage.begin(), (size_t) bins, 0.0f);

        for (int idx = 0; idx < bins; ++idx)
        {
            float reel, image;
            readPolar (idx, reel, image);
            const int writeidx = wrapi ((int) std::lround ((double) idx * (double) shift), bins);
            outReel[(size_t) writeidx]  += reel * std::cos (image);
            outImage[(size_t) writeidx] += reel * std::sin (image);
        }

        for (int idx = 0; idx < bins; ++idx)
        {
            const float re = outReel[(size_t) idx];
            const float im = outImage[(size_t) idx];
            outReel[(size_t) idx]  = std::sqrt (re * re + im * im);
            outImage[(size_t) idx] = std::atan2 (im, re);
        }

        for (int idx = 0; idx < bins; ++idx)
            writePolar (idx, outReel[(size_t) idx], outImage[(size_t) idx]);
        return;
    }

    for (int idx = 0; idx < bins; ++idx)
    {
        float reel, image;
        readPolar (idx, reel, image);
        c.reeldata[(size_t) idx]  = reel;
        c.imagedata[(size_t) idx] = image;
    }

    for (int idx = 0; idx < bins; ++idx)
    {
        float reel = c.reeldata[(size_t) idx];
        float image = c.imagedata[(size_t) idx];

        switch (experiment)
        {
            case 0: // mul
                reel  *= mulreel;
                image *= mulimage;
                break;

            case 1: // pitch
                peekPolar ((float) idx * shift, reel, image);
                break;

            case 2: // split：振幅讀 shiftreel、相位讀 shiftimage（MAX 實驗 2）
            {
                float magFromR = 0.0f, phaseUnused = 0.0f;
                float magUnused = 0.0f, phaseFromI = 0.0f;
                peekPolar ((float) idx * shiftreel,  magFromR,  phaseUnused);
                peekPolar ((float) idx * shiftimage, magUnused, phaseFromI);
                reel  = magFromR;
                image = phaseFromI;
                break;
            }

            case 3: // add / frequency shift by bins
                peekPolar ((float) idx + offsetBins, reel, image);
                break;

            default:
                break;
        }

        outReel[(size_t) idx]  = reel;
        outImage[(size_t) idx] = image;
    }

    for (int idx = 0; idx < bins; ++idx)
        writePolar (idx, outReel[(size_t) idx], outImage[(size_t) idx]);
}

void NewProjectAudioProcessor::processOneFrame (Chan& c,
                                                int experiment, float shift, float shiftreel,
                                                float shiftimage, float mulreel, float mulimage, float offsetBins)
{
    const int size = fftSize;
    auto& fft  = currentFft();
    auto& hann = currentHann();

    for (int i = 0; i < size; ++i)
        c.frame[(size_t) i] = c.inRing[(size_t) wrapi (c.writePos + i, size)];

    hann.multiplyWithWindowingTable (c.frame.data(), (size_t) size);

    std::fill_n (c.spectrum.begin(), (size_t) (size * 2), 0.0f);
    std::copy_n (c.frame.begin(), (size_t) size, c.spectrum.begin());
    fft.performRealOnlyForwardTransform (c.spectrum.data(), true);

    runExperiment (c, experiment, shift, shiftreel, shiftimage, mulreel, mulimage, offsetBins);

    fft.performRealOnlyInverseTransform (c.spectrum.data());

    for (int i = 0; i < size; ++i)
        c.frame[(size_t) i] = c.spectrum[(size_t) i];

    hann.multiplyWithWindowingTable (c.frame.data(), (size_t) size);

    for (int i = 0; i < size; ++i)
        c.outRing[(size_t) wrapi (c.writePos + i, size)] += c.frame[(size_t) i] * olaGain;
}

void NewProjectAudioProcessor::pushSample (Chan& c, float inputSample, float& outputSample,
                                           int experiment, float shift, float shiftreel,
                                           float shiftimage, float mulreel, float mulimage, float offsetBins,
                                           float mix, float outputGain)
{
    const int size = fftSize;

    const float dry = c.dryDelay[(size_t) c.dryPos];
    c.dryDelay[(size_t) c.dryPos] = inputSample;
    c.dryPos = wrapi (c.dryPos + 1, size);

    const float wet = c.outRing[(size_t) c.writePos];
    c.outRing[(size_t) c.writePos] = 0.0f;

    c.inRing[(size_t) c.writePos] = inputSample;
    c.writePos = wrapi (c.writePos + 1, size);

    if (++c.hopCounter >= hopSize)
    {
        c.hopCounter = 0;
        processOneFrame (c, experiment, shift, shiftreel, shiftimage, mulreel, mulimage, offsetBins);
    }

    const float blended = dry + mix * (wet - dry);
    outputSample = blended * outputGain;
}

//==============================================================================
void NewProjectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int fftChoice = (int) apvts.getRawParameterValue ("fftSize")->load();
    applyFftSizeFromChoice (fftChoice, false);

    const int experiment = (int) *apvts.getRawParameterValue ("experiment");
    const float shift      = *apvts.getRawParameterValue ("shift");
    const float shiftreel  = *apvts.getRawParameterValue ("shiftreel");
    const float shiftimage = *apvts.getRawParameterValue ("shiftimage");
    const float mulreel    = *apvts.getRawParameterValue ("mulreel");
    const float mulimage   = *apvts.getRawParameterValue ("mulimage");
    const float offsetBins = *apvts.getRawParameterValue ("offsetBins");
    const float mix = apvts.getRawParameterValue ("mix")->load();
    const float outputDb = apvts.getRawParameterValue ("output")->load();
    const float outputGain = juce::Decibels::decibelsToGain (outputDb);

    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer (ch);

        for (int i = 0; i < n; ++i)
        {
            float out = 0.0f;
            pushSample (channels[(size_t) ch], data[i], out,
                        experiment, shift, shiftreel, shiftimage, mulreel, mulimage, offsetBins,
                        mix, outputGain);
            data[i] = out;
        }
    }
}

//==============================================================================
bool NewProjectAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* NewProjectAudioProcessor::createEditor()
{
    return new NewProjectAudioProcessorEditor (*this);
}

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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}
