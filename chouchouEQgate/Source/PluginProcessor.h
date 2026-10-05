/*
  ==============================================================================

    ChouChou EQ Gate — spectral expander
    Strong bins get stronger; weak bins get weaker.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

//==============================================================================
class NewProjectAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    NewProjectAudioProcessor();
    ~NewProjectAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    int getFftSize() const noexcept { return fftSize; }
    int getHopSize() const noexcept { return hopSize; }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    /** Hop duration in ms at the current sample rate (envelope update period). */
    float getHopMilliseconds() const noexcept;

private:
    //==============================================================================
    static constexpr int kMinFftOrder = 9;  // 512
    static constexpr int kMaxFftOrder = 12; // 4096
    static constexpr int kDefaultFftOrder = 11; // 2048

    struct ChannelState
    {
        std::vector<float> inputFifo;
        std::vector<float> ola;
        std::vector<float> outFifo;
        std::vector<float> fftData;
        std::vector<float> window;
        std::vector<float> smoothedMag;
        int writePos = 0;
        int outRead = 0;
        int outAvailable = 0;
    };

    void applyFftSizeFromParameter (bool forceRebuild);
    void configureTransform (int order);
    void resetChannel (ChannelState& ch);
    void processHop (ChannelState& ch,
                     float amount,
                     float thresholdLin,
                     float mix,
                     float makeupLin,
                     float attackCoeff,
                     float releaseCoeff,
                     int mode);

    static float timeMsToCoeff (float timeMs, float hopSeconds) noexcept;
    static int fftSizeChoiceToOrder (int choiceIndex) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<ChannelState> channels;

    int fftOrder = kDefaultFftOrder;
    int fftSize  = 1 << kDefaultFftOrder;
    int hopSize  = (1 << kDefaultFftOrder) / 4;
    double currentSampleRate = 44100.0;
    int preparedBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessor)
};
