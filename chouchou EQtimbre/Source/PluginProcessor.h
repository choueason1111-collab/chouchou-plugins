#pragma once

#include <JuceHeader.h>
#include "EngineVoice.h"
#include "HarmonicAnalyzer.h"
#include "Randomizer.h"
#include <array>
#include <atomic>
#include <limits>
#include <vector>

namespace ParamIDs
{
    inline constexpr const char* mode         = "mode";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* inputGain    = "inputGain";
    inline constexpr const char* outputGain   = "outputGain";
    inline constexpr const char* mix          = "mix";
    inline constexpr const char* autoGain     = "autoGain";

    inline constexpr const char* drive        = "drive";
    inline constexpr const char* driveComp    = "driveComp";
    inline constexpr const char* bias         = "bias";
    inline constexpr const char* even         = "even";
    inline constexpr const char* odd          = "odd";
    inline constexpr const char* tone         = "tone";
    inline constexpr const char* dcCut        = "dcCut";
    inline constexpr const char* genLevel     = "genLevel";

    inline constexpr const char* balance      = "balance";
    inline constexpr const char* rebLevel     = "rebLevel";
    inline constexpr const char* pitchSource  = "pitchSource";
    inline constexpr const char* manualHz     = "manualHz";
    inline constexpr const char* midiPriority = "midiPriority";
    inline constexpr const char* midiHold     = "midiHold";
    inline constexpr const char* protect      = "protect";
    inline constexpr const char* confidence   = "confidence";
    inline constexpr const char* glide        = "glide";
    inline constexpr const char* jumpFade     = "jumpFade";
    inline constexpr const char* autoDrive    = "autoDrive";

    inline constexpr const char* rndBypass    = "rndBypass";
    inline constexpr const char* rndMix       = "rndMix";
    inline constexpr const char* rndTrigger   = "rndTrigger";
    inline constexpr const char* rndAuto      = "rndAuto";
    inline constexpr const char* rndInterval  = "rndInterval";
    inline constexpr const char* rndSync      = "rndSync";
    inline constexpr const char* rndDivision  = "rndDivision";
    inline constexpr const char* rndMorph     = "rndMorph";

    // Random amount of engine parameter i: "rndAmt_" + engineParamInfo()[i].id
    inline juce::String rndAmount (int i) { return juce::String ("rndAmt_") + chouchou::engineParamInfo()[(size_t) i].id; }
}

class ChouchouEQtimbreAudioProcessor : public juce::AudioProcessor
{
public:
    enum class Mode { generate = 0, rebalance, both };

    static constexpr int    kNumRandom        = chouchou::kNumEngineParams;
    static constexpr double kGainRampSeconds  = 0.03;
    static constexpr double kAutoGainSeconds  = 0.3;
    static constexpr float  kAutoGainLimit    = 4.0f;   // +-12 dB
    static constexpr double kOversamplingFadeSeconds = 0.01;
    static constexpr double kBypassFadeSeconds = 0.02;
    static constexpr double kDuckSeconds      = 0.01;
    static constexpr int    kSwitchChunk = 64;

    // Longest half-period delay plus the two cascaded fundamental-protect band-passes ringing
    // down to -60 dB (about 9.2 time constants for a doubled pole); checked by an impulse test.
    static constexpr double kVoiceTailSeconds = 0.5 / chouchou::HarmonicRebalancer::kMinF0
                                              + 9.5 * chouchou::HarmonicRebalancer::kProtectQ
                                                    / (3.14159265358979323846 * chouchou::HarmonicRebalancer::kMinF0);
    // Two voices in series.
    static constexpr double kTailSeconds = 2.0 * kVoiceTailSeconds;

    // Beats per step of the tempo-synced auto trigger (4/4 bars).
    static constexpr std::array<double, 8> kDivisionBeats { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
    static constexpr double kFallbackBpm = 120.0;

    ChouchouEQtimbreAudioProcessor();
    ~ChouchouEQtimbreAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "chouchouEQtimbre"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return kTailSeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    // Read from the GUI thread.
    chouchou::AnalyzerFifo& getAnalyzerFifo() noexcept { return analyzerFifo; }
    double getSampleRateSafe() const noexcept          { return currentSampleRate.load(); }
    float getCurrentF0() const noexcept                { return displayF0.load(); }
    bool isPitchActive() const noexcept                { return displayActive.load(); }
    float getAutoConfidence() const noexcept           { return displayConfidence.load(); }
    chouchou::PitchSource::Status getPitchStatus() const noexcept
    {
        return (chouchou::PitchSource::Status) displayStatus.load();
    }
    bool hasClipped() const noexcept { return clipped.load(); }
    void resetClip() noexcept        { clipped.store (false); }

    // Random stage, GUI side.
    void requestRoll() noexcept                        { rollRequests.fetch_add (1); }
    int getRollCount() const noexcept                  { return rollCount.load(); }
    bool isRandomAudible() const noexcept              { return randomAudible.load(); }
    // Normalised value the random stage currently uses for engine parameter i.
    float getRandomNormalised (int i) const noexcept   { return randomNorm[(size_t) i].load(); }
    float getRandomPlain (int i) const noexcept;
    float getMainInputRms() const noexcept             { return mainInputRms.load(); }
    float getRandomInputRms() const noexcept           { return randomInputRms.load(); }
    float getMainAutoGain() const noexcept             { return mainAutoGain.load(); }
    float getRandomAutoGain() const noexcept           { return randomAutoGain.load(); }
    float getRandomF0() const noexcept                 { return randomF0.load(); }
    bool isRandomPitchActive() const noexcept          { return randomPitchActive.load(); }
    chouchou::EngineValues getMainValues() const noexcept;
    chouchou::EngineValues getRandomValues() const noexcept;
    // Writes the random stage's current values into the main knobs and bypasses the random
    // stage. Message thread only.
    void adoptRandom();
    // Fixes the random sequence. Only call while audio is stopped (tests).
    void setRandomSeed (juce::int64 seed) noexcept     { randomizer.setSeed (seed); }

private:
    struct RawParams
    {
        std::atomic<float>* oversampling = nullptr;
        std::atomic<float>* inputGain = nullptr;
        std::atomic<float>* outputGain = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* autoGain = nullptr;
        std::atomic<float>* rndBypass = nullptr;
        std::atomic<float>* rndMix = nullptr;
        std::atomic<float>* rndTrigger = nullptr;
        std::atomic<float>* rndAuto = nullptr;
        std::atomic<float>* rndInterval = nullptr;
        std::atomic<float>* rndSync = nullptr;
        std::atomic<float>* rndDivision = nullptr;
        std::atomic<float>* rndMorph = nullptr;
        std::array<std::atomic<float>*, kNumRandom> engine {};
        std::array<std::atomic<float>*, kNumRandom> amount {};
    };

    chouchou::EngineVoice::Settings mainSettings() const noexcept;
    chouchou::EngineVoice::Settings randomSettings() noexcept;
    void evaluateRandom() noexcept;
    float plainFromNormalised (int i, float norm) const noexcept;
    int readOversamplingChoice() const noexcept;
    void applyOversampling (int index);
    void updateOversampling() noexcept;
    void updateSmoothingTargets() noexcept;
    void handleTriggers (int numSamples) noexcept;
    void applyPendingDraws() noexcept;
    bool randomNeedsShortChunks() const noexcept;
    void processRange (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept;
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    RawParams params;
    std::array<juce::RangedAudioParameter*, kNumRandom> engineParams {};

    chouchou::EngineVoice mainVoice, randomVoice;
    chouchou::Randomizer randomizer;
    chouchou::AnalyzerFifo analyzerFifo;

    std::vector<float> monoScratch, wetScratch;
    int latency = 0;
    int numChannels = 2;
    int maxBlock = 512;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain, outputGain;
    juce::SmoothedValue<float> autoGainBlend, oversamplingGate, bypassGain, duckGain;
    float inPower = 0.0f, outPower = 0.0f, powerCoef = 0.0f;

    // Random stage state, audio thread.
    chouchou::Randomizer::Draws evaluated {}, applied {};
    bool randomSkipped = true;
    bool lastTrigger = false;
    double autoCounter = 0.0, freeBeats = 0.0;
    juce::int64 lastSyncStep = std::numeric_limits<juce::int64>::min();

    // Random draws handed between the audio thread and state save/load.
    std::array<std::atomic<float>, kNumRandom> savedU {}, savedV {}, pendingU {}, pendingV {};
    std::atomic<bool> drawsPending { false };

    std::atomic<int> rollRequests { 0 }, rollCount { 0 };
    std::array<std::atomic<float>, kNumRandom> randomNorm {};
    std::atomic<bool> randomAudible { false }, randomPitchActive { false };
    std::atomic<float> mainInputRms { 0.0f }, randomInputRms { 0.0f }, randomF0 { 220.0f };
    std::atomic<float> mainAutoGain { 1.0f }, randomAutoGain { 1.0f };

    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<float> displayF0 { 220.0f };
    std::atomic<bool> displayActive { false };
    std::atomic<float> displayConfidence { 0.0f };
    std::atomic<int> displayStatus { 0 };
    std::atomic<bool> clipped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouEQtimbreAudioProcessor)
};
