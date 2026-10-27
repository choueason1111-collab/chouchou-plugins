/*
  ==============================================================================
    PluginProcessor.h  — STFT + 五種頻譜實驗（可選 FFT 解析度）
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

private:
    static constexpr int minFftOrder = 9;  // 512
    static constexpr int maxFftOrder = 12; // 4096
    static constexpr int maxFftSize  = 1 << maxFftOrder;
    static constexpr int maxNBins    = maxFftSize / 2 + 1;
    static constexpr float olaGain   = 2.0f / 3.0f;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParams();

    // 四種解析度各備一份，切換時不配置記憶體
    juce::dsp::FFT fft512  { 9 };
    juce::dsp::FFT fft1024 { 10 };
    juce::dsp::FFT fft2048 { 11 };
    juce::dsp::FFT fft4096 { 12 };

    juce::dsp::WindowingFunction<float> hann512  { 512,  juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> hann1024 { 1024, juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> hann2048 { 2048, juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> hann4096 { 4096, juce::dsp::WindowingFunction<float>::hann };

    int fftOrder = 10; // 預設 1024
    int fftSize  = 1024;
    int hopSize  = 256;
    int nBins    = 513;

    struct Chan
    {
        std::vector<float> inRing;
        std::vector<float> outRing;
        std::vector<float> frame;
        std::vector<float> spectrum;
        std::vector<float> reeldata;
        std::vector<float> imagedata;
        std::vector<float> outReel;
        std::vector<float> outImage;
        std::vector<float> dryDelay; // latency-aligned dry, length = current FFT size
        int writePos = 0;
        int hopCounter = 0;
        int dryPos = 0;
    };

    std::array<Chan, 2> channels;
    double currentSampleRate = 44100.0;

    static int choiceToOrder (int choiceIndex) noexcept;
    void applyFftSizeFromChoice (int choiceIndex, bool forceReset);
    void allocateMaxBuffers (Chan& c);
    void clearChanState (Chan& c);

    juce::dsp::FFT& currentFft() noexcept;
    juce::dsp::WindowingFunction<float>& currentHann() noexcept;

    void pushSample (Chan& c, float inputSample, float& outputSample,
                     int experiment, float shift, float shiftreel,
                     float shiftimage, float mulreel, float mulimage, float offsetBins,
                     float mix, float outputGain);

    void processOneFrame (Chan& c,
                          int experiment, float shift, float shiftreel,
                          float shiftimage, float mulreel, float mulimage, float offsetBins);

    void runExperiment (Chan& c,
                        int experiment, float shift, float shiftreel,
                        float shiftimage, float mulreel, float mulimage, float offsetBins);

    static int wrapi (int i, int n);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessor)
};
