#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include "EqFilters.h"
#include "DisplayAnalyser.h"

class ChouchouFFTEQAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxBands = chouchoueq::kMaxBands;
    static constexpr int kMaxFftOrder = 12;
    static constexpr int kMaxFftSize = 1 << kMaxFftOrder;
    static constexpr int kMaxBins = kMaxFftSize / 2 + 1;
    static constexpr float kAbsMaxHz = (float) chouchoueq::kAbsMaxHz;
    static constexpr float kDisplayLoHz = 1000.0f;   // 1 kHz
    static constexpr float kDisplayHiHz = 60000.0f;  // 60 kHz

    ChouchouFFTEQAudioProcessor();
    ~ChouchouFFTEQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    int getNumBins() const noexcept { return nBins; }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }
    float responseDb (double hz) const noexcept { return eq.responseDb (hz); }

    void copySpectra (std::vector<float>& mainMag,
                      std::vector<float>& sidechainMag,
                      std::vector<float>& referenceMag) const;

    struct ScBand
    {
        float hz = 1000.0f;
        float db = 0.0f;
        float q = 0.71f;
    };

    float getMatchPeakHz() const noexcept { return matchPeakHz.load (std::memory_order_relaxed); }
    void copyMatch (std::vector<float>& gainDb, int& bandCount) const;
    void copyScBands (std::vector<ScBand>& bands) const;
    int getAnalysisHops() const noexcept { return analysisHops.load (std::memory_order_relaxed); }
    void pulseCapture() noexcept { capturePulse.store (true, std::memory_order_release); }

    void setDisplayOpen (bool open) noexcept { displayOpen.store (open, std::memory_order_relaxed); }
    void pumpDisplays();
    DisplayAnalyser::Frame copyAnalyser() const { return displayAnalyser.copy(); }
    PhaseCapture::Frame copyPhase() const { return phaseCapture.copy(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParams();

    struct Chan
    {
        std::vector<float> inRing, outRing, frame, spectrum;
        std::vector<float> mag, phase, frozenMag, frozenPhase;
        std::vector<float> dryDelay, bypassDelay;
        int writePos = 0, hop = 0, dryPos = 0, bypassPos = 0;
        bool hasFreeze = false;
    };

    struct Analyser
    {
        std::vector<float> ring, spectrum, mag;
        int writePos = 0, hop = 0;
    };

    void applyFftSize (int choice, bool force);
    void clearChan (Chan& c);
    void ensureSizes();
    float readDelay (std::vector<float>& d, int& pos, float x, int length) const noexcept;
    void readPolar (const std::vector<float>& spectrum, int idx, float& mag, float& phase) const noexcept;
    void writePolar (std::vector<float>& spectrum, int idx, float mag, float phase) const noexcept;
    void processEffectFrame (Chan& c, const float* scMag);
    void applyUltrasound (Chan& c, int bins) noexcept;
    void processAnalysis (Analyser& a, std::vector<float>& dest);
    void clearSidechainMatch() noexcept;
    void dropSidechainBands() noexcept;
    void computeMatchTarget (const std::vector<float>& mainMag);
    void designSidechainBands();
    bool sidechainMatchArmed() const noexcept;

    juce::AudioProcessorValueTreeState apvts;
    chouchoueq::GraphicEq eq;
    DisplayAnalyser displayAnalyser;
    PhaseCapture phaseCapture;
    std::atomic<bool> displayOpen { false };
    std::vector<float> displayL, displayR, displaySc;

    juce::dsp::FFT fft512  { 9 };
    juce::dsp::FFT fft1024 { 10 };
    juce::dsp::FFT fft2048 { 11 };
    juce::dsp::FFT fft4096 { 12 };
    juce::dsp::WindowingFunction<float> hann512  { 512,  juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> hann1024 { 1024, juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> hann2048 { 2048, juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> hann4096 { 4096, juce::dsp::WindowingFunction<float>::hann };

    int fftOrder = 10, fftSize = 1024, hopSize = 256, nBins = 513;
    int dryDelayLength = 1024;
    double currentSampleRate = 48000.0;
    float agCoeff = 0.01f;
    float inPower = 1.0e-8f, wetPower = 1.0e-8f;

    std::array<Chan, 2> channels;
    Analyser mainAnalyser, scAnalyser;
    std::vector<float> reference, scLive, mainLive;
    std::vector<float> envA, envB, tmpMag, tmpPhase;
    std::vector<float> matchTarget, matchDb, scBandGain;
    std::array<std::vector<chouchoueq::Biquad>, 2> scFilt;
    std::vector<ScBand> scNodes, pubScBands;
    int scFiltCount = 0;
    bool lastCapture = false;
    bool referenceValid = false;
    std::atomic<bool> capturePulse { false };

    mutable std::mutex spectrumMutex;
    std::vector<float> pubMain, pubSc, pubRef, pubMatch;
    int matchBandCount = 0;
    int pubMatchBands = 0;
    std::atomic<float> matchPeakHz { 0.0f };
    std::atomic<int> analysisHops { 0 };

    juce::dsp::FFT& currentFft() noexcept;
    juce::dsp::WindowingFunction<float>& currentHann() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouFFTEQAudioProcessor)
};
