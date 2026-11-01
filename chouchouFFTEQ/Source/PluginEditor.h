#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "AnalyserPanels.h"
#include <array>

class ChouchouFFTEQEditor : public juce::AudioProcessorEditor,
                            private juce::Timer,
                            private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit ChouchouFFTEQEditor (ChouchouFFTEQAudioProcessor&);
    ~ChouchouFFTEQEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void drawSpectrum (juce::Graphics& g, juce::Rectangle<int> area);
    void layoutBands();
    void syncCutRows();
    int shownBands() const;
    int hitNode (juce::Point<float> p) const;
    void setPlain (const juce::String& id, float plain);
    void addBandAt (float hz, float db);
    void removeSelectedBand();
    juce::Point<float> nodeCentre (int band) const;
    float xToHz (float x) const;
    float yToDb (float y) const;
    float displayT (float hz) const;
    float displayHz (float t) const;
    float levelAt (float hz, const std::vector<float>& db) const;
    void drawPhase (juce::Graphics& g);
    void showPage (int page);
    void updateFftCode();
    void paintFftCode (juce::Graphics& g);

    ChouchouFFTEQAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& apvts;

    juce::Label title, eqCaption, fftCaption, mixCaption, amountCaption, engineLabel, windowLabel;
    juce::Rectangle<int> bandHeader;
    std::array<int, 7> colX {}, colW {};
    int selectedBand = 0;
    bool draggingNode = false;
    bool dragChangesQ = false;
    float dragStartQ = 0.707f;
    float dragStartY = 0.0f;
    juce::Rectangle<int> spectrumArea, fftCodeArea;
    juce::String fftFormula;
    juce::ToggleButton eqOn, autoGain, scMatch, scFollow, scCapture, freeze, scUseBands;
    juce::TextButton fftOn { "FFT" };
    juce::Slider mix, scAmount, scBandN;
    juce::Label scBandCaption;
    juce::ComboBox scMode, fftSize, eqEngine, experimentBox;
    juce::TextButton addBand { "Add" };
    juce::TextButton removeBand { "Remove" };
    juce::Viewport bandViewport;
    juce::Component bandHolder;

    struct BandRow
    {
        juce::TextButton index;
        juce::ToggleButton on { "On" };
        juce::ComboBox type;
        juce::Slider freq, gain, q, slope, delay, pan;
        juce::ComboBox calc;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt, calcAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAtt, gainAtt, qAtt, slopeAtt, delayAtt, panAtt;
    };
    std::array<BandRow, ChouchouFFTEQAudioProcessor::kMaxBands> bands;

    struct Knob
    {
        juce::Slider slider;
        juce::Label name, help;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    std::array<Knob, 12> knobs;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqAtt, fftAtt, agAtt, matchAtt, followAtt, capAtt, freezeAtt, scUseBandsAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAtt, amountAtt, scBandAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scModeAtt, fftSizeAtt, engineAtt, experimentAtt;

    std::vector<float> mainMag, scMag, refMag, matchDb;
    std::vector<ChouchouFFTEQAudioProcessor::ScBand> scNodes;
    int matchBands = 0;
    std::vector<float> refCurve, slotA, slotB, corrHistory;
    AnalyserControls analyserUi;
    PhaseControls phaseUi;
    juce::TextButton pageEq { "EQ" }, pageSpec { "Spectrum" }, pagePhase { "Phase" };
    int viewPage = 0;
    int timerHz = 30;
    DisplayAnalyser::Frame anFrame;
    PhaseCapture::Frame phFrame;
    struct TrailDot { float x = 0, y = 0; juce::uint32 born = 0; };
    std::vector<TrailDot> trail;
    bool cursorOn = false;
    juce::Point<float> cursorPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouFFTEQEditor)
};
