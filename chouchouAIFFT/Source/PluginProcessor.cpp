/*
  ==============================================================================
    PluginProcessor.cpp — Spectral Lab IA playground
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::StringArray NewProjectAudioProcessor::experimentNames()
{
    return {
        "0_mul", "1_pitch", "2_split", "3_fshift", "4_scatter",
        "5_formant", "6_pitchForm", "7_mirror", "8_gate", "9_lpf",
        "10_hpf", "11_bpf", "12_notch", "13_blur", "14_robot",
        "15_whisper", "16_harmonic", "17_pitchGain", "18_fshiftGain", "19_magShift",
        "20_phaseShift", "21_comb", "22_binQuant", "23_octDown", "24_octUp",
        "25_contrast", "26_peaks", "27_valleys", "28_shepard", "29_glitch",
        "30_stereoDiff", "31_phaseCancel", "32_envelopeFollow", "33_brickwall", "34_tiltEQ",
        "35_hybrid"
    };
}

juce::AudioProcessorValueTreeState::ParameterLayout NewProjectAudioProcessor::makeParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "fftSize", 1 }, "fftSize",
        juce::StringArray { "512", "1024", "2048", "4096" }, 1));

    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "experiment", 1 }, "experiment", experimentNames(), 0));

    auto f = [] (const char* id, const char* name, float minV, float maxV, float def, float step = 0.001f)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (minV, maxV, step), def);
    };

    // core remappers
    p.push_back (f ("shift", "shift", 0.01f, 16.0f, 0.9f));
    p.push_back (f ("shiftreel", "shiftreel", 0.01f, 16.0f, 0.9f));
    p.push_back (f ("shiftimage", "shiftimage", 0.01f, 16.0f, 1.1f));
    p.push_back (f ("mulreel", "mulreel", 0.0f, 16.0f, 1.0f));
    p.push_back (f ("mulimage", "mulimage", 0.0f, 16.0f, 1.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "offsetBins", 1 }, "offsetBins",
        juce::NormalisableRange<float> (-2048.0f, 2048.0f, 1.0f), 20.0f));

    // global / always-on
    // wet: 0 = delayed dry only, 1 = full processed signal. The dry delay equals the FFT size.
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "wet", 1 }, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    p.push_back (f ("feedback", "feedback", 0.0f, 0.95f, 0.0f));
    p.push_back (f ("tilt", "tilt", -1.0f, 1.0f, 0.0f));
    p.push_back (f ("softClip", "softClip", 0.0f, 1.0f, 0.0f));
    p.push_back (f ("stereoSpread", "stereoSpread", 0.0f, 1.0f, 0.0f));
    p.push_back (f ("phaseRandom", "phaseRandom", 0.0f, 1.0f, 0.0f));
    // Version 2: decibels, 0 dB = unity. The old 0..4 linear knob is not reused.
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "postGain", 2 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (f ("contrast", "contrast", 0.0f, 4.0f, 1.0f));
    p.push_back (f ("peakBoost", "peakBoost", 0.0f, 4.0f, 0.0f));
    p.push_back (f ("gateFloor", "gateFloor", 0.0f, 1.0f, 0.0f));
    p.push_back (f ("binSmooth", "binSmooth", 0.0f, 64.0f, 0.0f, 1.0f));
    p.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "freeze", 1 }, "freeze", false));

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
    channels[0].rng.setSeed (0xA11CE);
    channels[1].rng.setSeed (0xBEEF);
}

NewProjectAudioProcessor::~NewProjectAudioProcessor() = default;

const juce::String NewProjectAudioProcessor::getName() const { return JucePlugin_Name; }
bool NewProjectAudioProcessor::acceptsMidi() const { return false; }
bool NewProjectAudioProcessor::producesMidi() const { return false; }
bool NewProjectAudioProcessor::isMidiEffect() const { return false; }
double NewProjectAudioProcessor::getTailLengthSeconds() const
{
    return currentSampleRate > 0.0 ? ((double) fftSize * 1.5 / currentSampleRate) : 0.0;
}
int NewProjectAudioProcessor::getNumPrograms() { return 1; }
int NewProjectAudioProcessor::getCurrentProgram() { return 0; }
void NewProjectAudioProcessor::setCurrentProgram (int) {}
const juce::String NewProjectAudioProcessor::getProgramName (int) { return {}; }
void NewProjectAudioProcessor::changeProgramName (int, const juce::String&) {}

int NewProjectAudioProcessor::choiceToOrder (int choiceIndex) noexcept
{
    return minFftOrder + juce::jlimit (0, 3, choiceIndex);
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
    c.freezeReel.assign ((size_t) maxNBins, 0.0f);
    c.freezeImage.assign ((size_t) maxNBins, 0.0f);
    c.dryDelay.assign ((size_t) maxFftSize, 0.0f);
    c.smoothTmp.assign ((size_t) maxNBins, 0.0f);
    c.writePos = c.hopCounter = c.dryPos = 0;
    c.fbState = 0.0f;
    c.hasFreeze = false;
}

void NewProjectAudioProcessor::clearChanState (Chan& c)
{
    std::fill (c.inRing.begin(), c.inRing.end(), 0.0f);
    std::fill (c.outRing.begin(), c.outRing.end(), 0.0f);
    std::fill (c.frame.begin(), c.frame.end(), 0.0f);
    std::fill (c.spectrum.begin(), c.spectrum.end(), 0.0f);
    std::fill (c.dryDelay.begin(), c.dryDelay.end(), 0.0f);
    c.writePos = c.hopCounter = c.dryPos = 0;
    c.fbState = 0.0f;
    c.hasFreeze = false;
}

void NewProjectAudioProcessor::applyFftSizeFromChoice (int choiceIndex, bool forceReset)
{
    const int newOrder = choiceToOrder (choiceIndex);
    if (! forceReset && newOrder == fftOrder) return;
    fftOrder = newOrder;
    fftSize = 1 << fftOrder;
    hopSize = fftSize / 4;
    nBins = fftSize / 2 + 1;
    clearChanState (channels[0]);
    clearChanState (channels[1]);
    setLatencySamples (fftSize);
}

juce::dsp::FFT& NewProjectAudioProcessor::currentFft() noexcept
{
    switch (fftOrder) { case 9: return fft512; case 11: return fft2048; case 12: return fft4096; default: return fft1024; }
}

juce::dsp::WindowingFunction<float>& NewProjectAudioProcessor::currentHann() noexcept
{
    switch (fftOrder) { case 9: return hann512; case 11: return hann2048; case 12: return hann4096; default: return hann1024; }
}

void NewProjectAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    applyFftSizeFromChoice ((int) apvts.getRawParameterValue ("fftSize")->load(), true);
}

void NewProjectAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NewProjectAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return true;
}
#endif

int NewProjectAudioProcessor::wrapi (int i, int n)
{
    if (n <= 0) return 0;
    i %= n;
    if (i < 0) i += n;
    return i;
}

float NewProjectAudioProcessor::softClipSample (float x, float amount)
{
    if (amount <= 0.0001f) return x;
    const float drive = 1.0f + amount * 8.0f;
    const float y = std::tanh (x * drive);
    return y * (1.0f - amount) + y * amount;
}

void NewProjectAudioProcessor::applyPostSpectral (Chan& c, const Globals& g)
{
    const int bins = nBins;
    // gateFloor
    if (g.gateFloor > 0.0f)
    {
        for (int i = 0; i < bins; ++i)
            if (c.outReel[(size_t) i] < g.gateFloor)
                c.outReel[(size_t) i] = 0.0f;
    }
    // contrast around mean
    if (std::abs (g.contrast - 1.0f) > 0.001f)
    {
        float mean = 0.0f;
        for (int i = 0; i < bins; ++i) mean += c.outReel[(size_t) i];
        mean /= (float) juce::jmax (1, bins);
        for (int i = 0; i < bins; ++i)
        {
            float d = c.outReel[(size_t) i] - mean;
            c.outReel[(size_t) i] = juce::jmax (0.0f, mean + d * g.contrast);
        }
    }
    // peakBoost: boost bins above mean
    if (g.peakBoost > 0.0f)
    {
        float mean = 0.0f;
        for (int i = 0; i < bins; ++i) mean += c.outReel[(size_t) i];
        mean /= (float) juce::jmax (1, bins);
        for (int i = 0; i < bins; ++i)
            if (c.outReel[(size_t) i] > mean)
                c.outReel[(size_t) i] *= (1.0f + g.peakBoost);
    }
    // tilt
    if (std::abs (g.tilt) > 0.0001f)
    {
        for (int i = 0; i < bins; ++i)
        {
            const float t = (bins <= 1) ? 0.0f : (float) i / (float) (bins - 1);
            const float gain = std::pow (2.0f, g.tilt * (t - 0.5f) * 2.0f);
            c.outReel[(size_t) i] *= gain;
        }
    }
    // binSmooth
    const int R = juce::jlimit (0, 64, (int) std::lround (g.binSmooth));
    if (R > 0)
    {
        for (int i = 0; i < bins; ++i)
        {
            float s = 0.0f; int n = 0;
            for (int d = -R; d <= R; ++d) { s += c.outReel[(size_t) wrapi (i + d, bins)]; ++n; }
            c.smoothTmp[(size_t) i] = s / (float) n;
        }
        for (int i = 0; i < bins; ++i) c.outReel[(size_t) i] = c.smoothTmp[(size_t) i];
    }
    // phaseRandom
    if (g.phaseRandom > 0.0f)
    {
        for (int i = 0; i < bins; ++i)
        {
            const float r = c.rng.nextFloat() * 6.2831853f - 3.14159265f;
            c.outImage[(size_t) i] += r * g.phaseRandom;
        }
    }
}

void NewProjectAudioProcessor::runExperiment (Chan& c, int chIndex,
                                              int experiment, float shift, float shiftreel, float shiftimage,
                                              float mulreel, float mulimage, float offsetBins, const Globals& g)
{
    const int size = fftSize;
    const int bins = nBins;
    constexpr float twoPi = 6.28318530718f;

    // stereo spread modifies shift-like params per channel
    float sh = shift, shr = shiftreel, shi = shiftimage;
    if (g.stereoSpread > 0.0f)
    {
        const float side = (chIndex == 0 ? -1.0f : 1.0f) * g.stereoSpread;
        sh  *= (1.0f + 0.5f * side);
        shr *= (1.0f + 0.35f * side);
        shi *= (1.0f - 0.35f * side);
    }

    auto readPolar = [&] (int idx, float& reel, float& image)
    {
        float re, im;
        if (idx == 0) { re = c.spectrum[0]; im = 0.0f; }
        else if (idx == size / 2) { re = c.spectrum[1]; im = 0.0f; }
        else { re = c.spectrum[(size_t) (2 * idx)]; im = c.spectrum[(size_t) (2 * idx + 1)]; }
        reel = std::sqrt (re * re + im * im);
        image = std::atan2 (im, re);
    };
    auto writePolar = [&] (int idx, float reel, float image)
    {
        const float re = reel * std::cos (image);
        const float im = reel * std::sin (image);
        if (idx == 0) c.spectrum[0] = re;
        else if (idx == size / 2) c.spectrum[1] = re;
        else { c.spectrum[(size_t) (2 * idx)] = re; c.spectrum[(size_t) (2 * idx + 1)] = im; }
    };

    auto& outReel = c.outReel;
    auto& outImage = c.outImage;

    if (experiment == 4) // scatter write
    {
        std::fill_n (outReel.begin(), (size_t) bins, 0.0f);
        std::fill_n (outImage.begin(), (size_t) bins, 0.0f);
        for (int idx = 0; idx < bins; ++idx)
        {
            float reel, image; readPolar (idx, reel, image);
            const int w = wrapi ((int) std::lround ((double) idx * (double) sh), bins);
            outReel[(size_t) w] += reel * std::cos (image);
            outImage[(size_t) w] += reel * std::sin (image);
        }
        for (int idx = 0; idx < bins; ++idx)
        {
            const float re = outReel[(size_t) idx], im = outImage[(size_t) idx];
            outReel[(size_t) idx] = std::sqrt (re * re + im * im);
            outImage[(size_t) idx] = std::atan2 (im, re);
        }
        applyPostSpectral (c, g);
        for (int idx = 0; idx < bins; ++idx) writePolar (idx, outReel[(size_t) idx], outImage[(size_t) idx]);
        return;
    }

    for (int idx = 0; idx < bins; ++idx)
    {
        float reel, image; readPolar (idx, reel, image);
        c.reeldata[(size_t) idx] = reel;
        c.imagedata[(size_t) idx] = image;
    }

    if (g.freeze)
    {
        if (! c.hasFreeze)
        {
            std::copy_n (c.reeldata.begin(), (size_t) bins, c.freezeReel.begin());
            std::copy_n (c.imagedata.begin(), (size_t) bins, c.freezeImage.begin());
            c.hasFreeze = true;
        }
        std::copy_n (c.freezeReel.begin(), (size_t) bins, c.reeldata.begin());
        std::copy_n (c.freezeImage.begin(), (size_t) bins, c.imagedata.begin());
    }
    else c.hasFreeze = false;

    const int cutoff = juce::jlimit (0, bins - 1, (int) std::lround (std::abs (offsetBins)));
    const int halfW = juce::jmax (1, (int) std::lround ((double) sh * (double) bins * 0.25));
    const int blurR = juce::jlimit (0, 64, (int) std::lround (std::abs (offsetBins)));
    const int harmStep = juce::jmax (1, (int) std::lround (std::abs (offsetBins)));
    const int quantStep = harmStep;
    const float combPeriod = juce::jmax (1.0f, std::abs (offsetBins));

    if (experiment == 13)
    {
        for (int idx = 0; idx < bins; ++idx)
        {
            float sum = 0.0f; int count = 0;
            for (int d = -blurR; d <= blurR; ++d) { sum += c.reeldata[(size_t) wrapi (idx + d, bins)]; ++count; }
            outReel[(size_t) idx] = (sum / (float) count) * mulreel;
            outImage[(size_t) idx] = c.imagedata[(size_t) idx] * mulimage;
        }
        applyPostSpectral (c, g);
        for (int idx = 0; idx < bins; ++idx) writePolar (idx, outReel[(size_t) idx], outImage[(size_t) idx]);
        return;
    }

    for (int idx = 0; idx < bins; ++idx)
    {
        float reel = c.reeldata[(size_t) idx];
        float image = c.imagedata[(size_t) idx];

        switch (experiment)
        {
            case 0: reel *= mulreel; image *= mulimage; break;
            case 1: {
                const int r = wrapi ((int) std::lround ((double) idx * (double) sh), bins);
                reel = c.reeldata[(size_t) r]; image = c.imagedata[(size_t) r]; break; }
            case 2: {
                const int rR = wrapi ((int) std::lround ((double) idx * (double) shr), bins);
                const int rI = wrapi ((int) std::lround ((double) idx * (double) shi), bins);
                reel = c.reeldata[(size_t) rR]; image = c.imagedata[(size_t) rI]; break; }
            case 3: {
                const int r = wrapi (idx + (int) std::lround (offsetBins), bins);
                reel = c.reeldata[(size_t) r]; image = c.imagedata[(size_t) r]; break; }
            case 5: {
                const int r = wrapi ((int) std::lround ((double) idx * (double) shr), bins);
                reel = c.reeldata[(size_t) r] * mulreel; image = c.imagedata[(size_t) idx]; break; }
            case 6: {
                const int rP = wrapi ((int) std::lround ((double) idx * (double) sh), bins);
                const int rF = wrapi ((int) std::lround ((double) idx * (double) shr), bins);
                reel = c.reeldata[(size_t) rF] * mulreel; image = c.imagedata[(size_t) rP]; break; }
            case 7: {
                const int r = wrapi ((bins - 1 - idx) + (int) std::lround (offsetBins), bins);
                reel = c.reeldata[(size_t) r] * mulreel; image = c.imagedata[(size_t) r] * mulimage; break; }
            case 8:
                if (reel < mulreel) reel = 0.0f;
                image *= mulimage; break;
            case 9:
                reel = (idx > cutoff) ? 0.0f : reel * mulreel; image *= mulimage; break;
            case 10:
                reel = (idx < cutoff) ? 0.0f : reel * mulreel; image *= mulimage; break;
            case 11:
                reel = (std::abs (idx - cutoff) > halfW) ? 0.0f : reel * mulreel; image *= mulimage; break;
            case 12:
                reel = (std::abs (idx - cutoff) <= halfW) ? 0.0f : reel * mulreel; image *= mulimage; break;
            case 14:
                reel *= mulreel; image = (float) idx * mulimage; break;
            case 15:
                reel = mulreel * 0.05f; image = c.imagedata[(size_t) idx] * mulimage; break;
            case 16:
                reel = ((idx % harmStep) == 0) ? reel * mulreel : 0.0f; image *= mulimage; break;
            case 17: {
                const int r = wrapi ((int) std::lround ((double) idx * (double) sh), bins);
                reel = c.reeldata[(size_t) r] * mulreel; image = c.imagedata[(size_t) r] * mulimage; break; }
            case 18: {
                const int r = wrapi (idx + (int) std::lround (offsetBins), bins);
                reel = c.reeldata[(size_t) r] * mulreel; image = c.imagedata[(size_t) r] * mulimage; break; }
            case 19: {
                const int r = wrapi ((int) std::lround ((double) idx * (double) sh), bins);
                reel = c.reeldata[(size_t) r] * mulreel; image = c.imagedata[(size_t) idx]; break; }
            case 20: {
                const int r = wrapi ((int) std::lround ((double) idx * (double) shi), bins);
                reel = c.reeldata[(size_t) idx] * mulreel; image = c.imagedata[(size_t) r]; break; }
            case 21: {
                const float gg = 0.5f + 0.5f * std::cos (twoPi * (float) idx / combPeriod);
                reel *= gg * mulreel; image *= mulimage; break; }
            case 22: {
                const int q = (idx / quantStep) * quantStep;
                reel = c.reeldata[(size_t) q] * mulreel; image = c.imagedata[(size_t) q] * mulimage; break; }
            case 23: { // oct down ~ *2 read
                const int r = wrapi (idx * 2, bins);
                reel = c.reeldata[(size_t) r] * mulreel; image = c.imagedata[(size_t) r] * mulimage; break; }
            case 24: { // oct up ~ /2 read
                const int r = wrapi (idx / 2, bins);
                reel = c.reeldata[(size_t) r] * mulreel; image = c.imagedata[(size_t) r] * mulimage; break; }
            case 25: { // contrast in-experiment
                // handled mostly in post; here scale by distance from mean later — simple pow
                reel = std::pow (juce::jmax (0.0f, reel), juce::jmax (0.05f, shr)) * mulreel;
                image *= mulimage; break; }
            case 26: { // keep peaks only vs local
                float local = 0.0f; int n = 0;
                for (int d = -3; d <= 3; ++d) { local += c.reeldata[(size_t) wrapi (idx + d, bins)]; ++n; }
                local /= (float) n;
                reel = (reel >= local * (1.0f + sh * 0.25f)) ? reel * mulreel : 0.0f;
                image *= mulimage; break; }
            case 27: { // valleys
                float local = 0.0f; int n = 0;
                for (int d = -3; d <= 3; ++d) { local += c.reeldata[(size_t) wrapi (idx + d, bins)]; ++n; }
                local /= (float) n;
                reel = (reel <= local) ? reel * mulreel : 0.0f;
                image *= mulimage; break; }
            case 28: { // shepard-ish rotate by offset
                const int r = wrapi (idx + (int) std::lround (offsetBins + (double) idx * (double) (sh - 1.0f)), bins);
                reel = c.reeldata[(size_t) r] * mulreel; image = c.imagedata[(size_t) r] * mulimage; break; }
            case 29: { // glitch random jump
                const int span = juce::jmax (1, (int) std::lround (std::abs (offsetBins) + sh * 10.0f));
                const int r = wrapi (idx + c.rng.nextInt (span * 2 + 1) - span, bins);
                reel = c.reeldata[(size_t) r] * mulreel; image = c.imagedata[(size_t) r] * mulimage; break; }
            case 30: { // stereoDiff: L uses shift, R uses shiftreel (also uses chIndex)
                const float use = (chIndex == 0 ? sh : shr);
                const int r = wrapi ((int) std::lround ((double) idx * (double) use), bins);
                reel = c.reeldata[(size_t) r] * mulreel; image = c.imagedata[(size_t) r] * mulimage; break; }
            case 31: // phase invert odd bins
                reel *= mulreel;
                image = ((idx & 1) ? image + 3.14159265f : image) * mulimage;
                break;
            case 32: { // envelope: mag from smoothed, phase original
                float sum = 0.0f; int n = 0;
                const int R = juce::jmax (1, (int) std::lround (sh * 8.0f));
                for (int d = -R; d <= R; ++d) { sum += c.reeldata[(size_t) wrapi (idx + d, bins)]; ++n; }
                reel = (sum / (float) n) * mulreel; image = c.imagedata[(size_t) idx] * mulimage; break; }
            case 33: // brickwall below/above using mulreel as ceiling
                reel = juce::jmin (reel, mulreel); image *= mulimage; break;
            case 34: { // tilt inside
                const float t = (bins <= 1) ? 0.0f : (float) idx / (float) (bins - 1);
                const float gain = std::pow (2.0f, sh * (t - 0.5f) * 2.0f);
                reel *= gain * mulreel; image *= mulimage; break; }
            case 35: { // hybrid pitch+fshift+mul
                const int r1 = wrapi ((int) std::lround ((double) idx * (double) sh), bins);
                const int r2 = wrapi (r1 + (int) std::lround (offsetBins), bins);
                reel = c.reeldata[(size_t) r2] * mulreel;
                image = c.imagedata[(size_t) r2] * mulimage;
                break; }
            default: break;
        }

        outReel[(size_t) idx] = reel;
        outImage[(size_t) idx] = image;
    }

    applyPostSpectral (c, g);
    for (int idx = 0; idx < bins; ++idx)
        writePolar (idx, outReel[(size_t) idx], outImage[(size_t) idx]);
}

void NewProjectAudioProcessor::processOneFrame (Chan& c, int chIndex,
                                                int experiment, float shift, float shiftreel, float shiftimage,
                                                float mulreel, float mulimage, float offsetBins, const Globals& g)
{
    const int size = fftSize;
    auto& fft = currentFft();
    auto& hann = currentHann();

    for (int i = 0; i < size; ++i)
        c.frame[(size_t) i] = c.inRing[(size_t) wrapi (c.writePos + i, size)];

    hann.multiplyWithWindowingTable (c.frame.data(), (size_t) size);
    std::fill_n (c.spectrum.begin(), (size_t) (size * 2), 0.0f);
    std::copy_n (c.frame.begin(), (size_t) size, c.spectrum.begin());
    fft.performRealOnlyForwardTransform (c.spectrum.data(), true);

    runExperiment (c, chIndex, experiment, shift, shiftreel, shiftimage, mulreel, mulimage, offsetBins, g);

    fft.performRealOnlyInverseTransform (c.spectrum.data());
    for (int i = 0; i < size; ++i) c.frame[(size_t) i] = c.spectrum[(size_t) i];
    hann.multiplyWithWindowingTable (c.frame.data(), (size_t) size);

    for (int i = 0; i < size; ++i)
        c.outRing[(size_t) wrapi (c.writePos + i, size)] += c.frame[(size_t) i] * olaGain;
}

void NewProjectAudioProcessor::pushSample (Chan& c, int chIndex, float inputSample, float& outputSample,
                                           int experiment, float shift, float shiftreel, float shiftimage,
                                           float mulreel, float mulimage, float offsetBins, const Globals& g)
{
    const int size = fftSize;
    const float dry = c.dryDelay[(size_t) c.dryPos];
    c.dryDelay[(size_t) c.dryPos] = inputSample;
    c.dryPos = wrapi (c.dryPos + 1, size);

    float x = inputSample + c.fbState * g.feedback;
    float proc = c.outRing[(size_t) c.writePos];
    c.outRing[(size_t) c.writePos] = 0.0f;

    c.inRing[(size_t) c.writePos] = x;
    c.writePos = wrapi (c.writePos + 1, size);

    if (++c.hopCounter >= hopSize)
    {
        c.hopCounter = 0;
        processOneFrame (c, chIndex, experiment, shift, shiftreel, shiftimage, mulreel, mulimage, offsetBins, g);
    }

    float y = proc * g.wet + dry * (1.0f - g.wet);
    y *= g.postGain;
    y = softClipSample (y, g.softClip);
    c.fbState = y;
    outputSample = y;
}

void NewProjectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    applyFftSizeFromChoice ((int) apvts.getRawParameterValue ("fftSize")->load(), false);

    Globals g;
    g.wet = *apvts.getRawParameterValue ("wet");
    g.feedback = *apvts.getRawParameterValue ("feedback");
    g.tilt = *apvts.getRawParameterValue ("tilt");
    g.softClip = *apvts.getRawParameterValue ("softClip");
    g.stereoSpread = *apvts.getRawParameterValue ("stereoSpread");
    g.phaseRandom = *apvts.getRawParameterValue ("phaseRandom");
    g.postGain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("postGain")->load());
    g.contrast = *apvts.getRawParameterValue ("contrast");
    g.peakBoost = *apvts.getRawParameterValue ("peakBoost");
    g.gateFloor = *apvts.getRawParameterValue ("gateFloor");
    g.binSmooth = *apvts.getRawParameterValue ("binSmooth");
    g.freeze = *apvts.getRawParameterValue ("freeze") > 0.5f;

    const int experiment = (int) *apvts.getRawParameterValue ("experiment");
    const float shift = *apvts.getRawParameterValue ("shift");
    const float shiftreel = *apvts.getRawParameterValue ("shiftreel");
    const float shiftimage = *apvts.getRawParameterValue ("shiftimage");
    const float mulreel = *apvts.getRawParameterValue ("mulreel");
    const float mulimage = *apvts.getRawParameterValue ("mulimage");
    const float offsetBins = *apvts.getRawParameterValue ("offsetBins");

    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            float out = 0.0f;
            pushSample (channels[(size_t) ch], ch, data[i], out,
                        experiment, shift, shiftreel, shiftimage, mulreel, mulimage, offsetBins, g);
            data[i] = out;
        }
    }
}

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
