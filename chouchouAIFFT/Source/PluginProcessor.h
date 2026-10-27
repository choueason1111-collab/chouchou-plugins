/*
  ==============================================================================
    PluginProcessor.h — Spectral Lab IA (expanded playground)
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

class NewProjectAudioProcessor : public juce::AudioProcessor
{
public:
    NewProjectAudioProcessor();
    ~NewProjectAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    int getFftSize() const noexcept { return fftSize; }
    int getHopSize() const noexcept { return hopSize; }
    int getNumBins() const noexcept { return nBins; }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }
    double getHzPerBin() const noexcept
    {
        return currentSampleRate > 0.0 ? (currentSampleRate / (double) fftSize) : 0.0;
    }

    static juce::StringArray experimentNames();
    static int numExperiments() { return experimentNames().size(); }

private:
    static constexpr int minFftOrder = 9;
    static constexpr int maxFftOrder = 12;
    static constexpr int maxFftSize  = 1 << maxFftOrder;
    static constexpr int maxNBins    = maxFftSize / 2 + 1;
    static constexpr float olaGain   = 2.0f / 3.0f;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParams();

    juce::dsp::FFT fft512  { 9 };
    juce::dsp::FFT fft1024 { 10 };
    juce::dsp::FFT fft2048 { 11 };
    juce::dsp::FFT fft4096 { 12 };

    juce::dsp::WindowingFunction<float> hann512  { 512,  juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> hann1024 { 1024, juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> hann2048 { 2048, juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> hann4096 { 4096, juce::dsp::WindowingFunction<float>::hann };

    int fftOrder = 10;
    int fftSize  = 1024;
    int hopSize  = 256;
    int nBins    = 513;

    struct Chan
    {
        std::vector<float> inRing, outRing, frame, spectrum;
        std::vector<float> reeldata, imagedata, outReel, outImage;
        std::vector<float> freezeReel, freezeImage, dryDelay, smoothTmp;
        int writePos = 0, hopCounter = 0, dryPos = 0;
        float fbState = 0.0f;
        bool hasFreeze = false;
        juce::Random rng { 0xC0FFEEu };
    };

    struct Globals
    {
        float wet = 1.0f, feedback = 0.0f, tilt = 0.0f, softClip = 0.0f;
        float stereoSpread = 0.0f, phaseRandom = 0.0f, postGain = 1.0f;
        float contrast = 1.0f, peakBoost = 0.0f, gateFloor = 0.0f, binSmooth = 0.0f;
        bool freeze = false;
    };

    std::array<Chan, 2> channels;
    double currentSampleRate = 44100.0;

    static int choiceToOrder (int choiceIndex) noexcept;
    void applyFftSizeFromChoice (int choiceIndex, bool forceReset);
    void allocateMaxBuffers (Chan& c);
    void clearChanState (Chan& c);
    juce::dsp::FFT& currentFft() noexcept;
    juce::dsp::WindowingFunction<float>& currentHann() noexcept;

    void pushSample (Chan& c, int chIndex, float inputSample, float& outputSample,
                     int experiment, float shift, float shiftreel, float shiftimage,
                     float mulreel, float mulimage, float offsetBins, const Globals& g);

    void processOneFrame (Chan& c, int chIndex,
                          int experiment, float shift, float shiftreel, float shiftimage,
                          float mulreel, float mulimage, float offsetBins, const Globals& g);

    void runExperiment (Chan& c, int chIndex,
                        int experiment, float shift, float shiftreel, float shiftimage,
                        float mulreel, float mulimage, float offsetBins, const Globals& g);

    void applyPostSpectral (Chan& c, const Globals& g);
    static int wrapi (int i, int n);
    static float softClipSample (float x, float amount);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessor)
};
