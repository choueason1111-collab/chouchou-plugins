/*
  ==============================================================================

    chouchouShaper — editor (ASCII-only UI strings)

      Left panel   TIME envelope: knobs + scrolling gain trace
      Right panel  SPECTRAL envelope: knobs + per-frequency gain curve
      Bottom       sidechain (shared), meters, output

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ScopeView.h"
#include <array>
#include <memory>
#include <vector>

//==============================================================================
/** Scrolling min / max gain trace of the time stage. */
class TimeTraceDisplay  : public juce::Component
{
public:
    void push (float minDb, float maxDb, float inputDb);
    void paint (juce::Graphics&) override;

private:
    static constexpr int kHistory = 240;
    std::array<float, kHistory> mins {}, maxs {}, inputs {};
    int head = 0;
};

//==============================================================================
/** Per-frequency gain curve of the spectral stage over its slow-envelope spectrum. */
class SpectralGainDisplay  : public juce::Component
{
public:
    explicit SpectralGainDisplay (ChouchouShaperAudioProcessor& p) : proc (p) {}
    void setRange (float lo, float hi) { loHz = lo; hiHz = hi; }
    void paint (juce::Graphics&) override;

private:
    ChouchouShaperAudioProcessor& proc;
    float loHz = 20.0f, hiHz = 20000.0f;
};

//==============================================================================
class LevelBar  : public juce::Component
{
public:
    void set (float db, bool active) { valueDb = db; isActive = active; repaint(); }
    void setCaption (juce::String c, juce::Colour fill) { caption = std::move (c); colour = fill; }
    void paint (juce::Graphics&) override;

private:
    juce::Colour colour;
    float valueDb = -120.0f;
    bool isActive = true;
    juce::String caption;
};

//==============================================================================
class ChouchouShaperAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit ChouchouShaperAudioProcessorEditor (ChouchouShaperAudioProcessor&);
    ~ChouchouShaperAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 1000, kMainHeight = 680, kScopeHeight = 280;

    void setScopeOpen (bool open);

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    Knob& addKnob (const char* paramId, const juce::String& text, juce::Colour colour);
    Toggle& addToggle (const char* paramId, const juce::String& text);
    void layoutKnobRow (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs);
    void timerCallback() override;

    ChouchouShaperAudioProcessor& audioProcessor;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Toggle>> toggles;

    Knob *tRatio {}, *tFast {}, *tSlow {}, *tAttack {}, *tSustain {}, *tMix {};
    Knob *sRatio {}, *sFast {}, *sSlow {}, *sAttack {}, *sSustain {}, *sMix {};
    Knob *sSmooth {}, *sLo {}, *sHi {};
    Knob *scGain {}, *scHpf {}, *scLpf {}, *outKnob {};
    Toggle *tOn {}, *tLink {}, *tSc {}, *sOn {}, *sSc {};

    juce::ComboBox fftBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fftAttachment;

    TimeTraceDisplay timeTrace;
    SpectralGainDisplay spectralDisplay;
    LevelBar inputBar, scBar;
    juce::Label infoLabel;

    juce::TextButton scopeButton { "SCOPE" };
    ScopeSection scopeSection;

    juce::Rectangle<int> leftPanel, rightPanel, bottomPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouShaperAudioProcessorEditor)
};
