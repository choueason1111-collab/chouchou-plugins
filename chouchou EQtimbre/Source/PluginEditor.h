#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <functional>
#include <memory>
#include <vector>

namespace chouchou_timbre_ui
{
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    namespace Palette
    {
        const juce::Colour background { 0xff15171c };
        const juce::Colour panel      { 0xff1f232b };
        const juce::Colour outline    { 0xff343a46 };
        const juce::Colour text       { 0xffd9dde5 };
        const juce::Colour dimText    { 0xff8a93a3 };
        const juce::Colour even       { 0xfff0a24f };
        const juce::Colour odd        { 0xff5aa7f0 };
        const juce::Colour accent     { 0xffc9d36a };
        const juce::Colour random     { 0xffd57cf0 };
        const juce::Colour comb       { 0xff6fd3b8 };
    }

    // Where the random stage may land for one engine parameter, in normalised 0..1 space.
    struct RandomRange
    {
        float base = 0.0f, lo = 0.0f, hi = 0.0f, value = 0.0f, amount = 0.0f;
    };

    // Fills the range of parameter i and returns whether the random stage is on (not bypassed);
    // shared by the knobs, switches and the random page.
    struct RandomMarks
    {
        std::function<bool (int index, RandomRange&)> query;
        bool get (int index, RandomRange& r) const { return index >= 0 && query && query (index, r); }
    };

    // The knob whose effect the display explains: an engine parameter index or a
    // TimbreModel::ExtraFocus value. Set by hovering or touching a control.
    struct KnobFocus
    {
        int index = chouchou::epEven;
    };

    struct FocusTag
    {
        KnobFocus* focus = nullptr;
        int index = -1;
        void claim() const { if (focus != nullptr && index >= 0) focus->index = index; }
    };

    class TimbreLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        TimbreLookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
        const RandomMarks* marks = nullptr;
    };

    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name,
              const juce::String& tooltip, juce::Colour colour = Palette::accent);
        void resized() override;
        void setRandomIndex (int index) { slider.getProperties().set ("rndIndex", index); }
        void refreshMarks() { slider.repaint(); }
        void setFocus (KnobFocus* f, int index) { focusTag = { f, index }; }
        void mouseEnter (const juce::MouseEvent&) override { focusTag.claim(); }
        void mouseDown (const juce::MouseEvent&) override  { focusTag.claim(); }
        void mouseDrag (const juce::MouseEvent&) override  { focusTag.claim(); }

    private:
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
        FocusTag focusTag;
    };

    class Choice : public juce::Component
    {
    public:
        Choice (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name,
                const juce::String& tooltip);
        void resized() override;
        void paintOverChildren (juce::Graphics&) override;
        void setRandomMarks (const RandomMarks* m, int index) { marks = m; randomIndex = index; }
        void setFocus (KnobFocus* f, int index) { focusTag = { f, index }; }
        void mouseEnter (const juce::MouseEvent&) override { focusTag.claim(); }
        void mouseDown (const juce::MouseEvent&) override  { focusTag.claim(); }

    private:
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAttachment> attachment;
        const RandomMarks* marks = nullptr;
        int randomIndex = -1;
        FocusTag focusTag;
    };

    class Toggle : public juce::Component
    {
    public:
        Toggle (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name,
                const juce::String& tooltip);
        void resized() override;
        void paintOverChildren (juce::Graphics&) override;
        void setRandomMarks (const RandomMarks* m, int index) { marks = m; randomIndex = index; }
        void setFocus (KnobFocus* f, int index) { focusTag = { f, index }; }
        void mouseEnter (const juce::MouseEvent&) override { focusTag.claim(); }
        void mouseDown (const juce::MouseEvent&) override  { focusTag.claim(); }

    private:
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> attachment;
        const RandomMarks* marks = nullptr;
        int randomIndex = -1;
        FocusTag focusTag;
    };

    class ClipLight : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit ClipLight (ChouchouEQtimbreAudioProcessor& p) : proc (p)
        {
            setTooltip (juce::String::fromUTF8 ("La sortie a dépassé 0 dBFS. Cliquer pour réinitialiser."));
        }
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override { proc.resetClip(); repaint(); }
        void refresh();

    private:
        ChouchouEQtimbreAudioProcessor& proc;
        bool lit = false;
    };

    class HarmonicDisplay : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        explicit HarmonicDisplay (ChouchouEQtimbreAudioProcessor&);
        ~HarmonicDisplay() override;
        void paint (juce::Graphics&) override;
        void setSources (const KnobFocus* f, const RandomMarks* m) { focus = f; marks = m; }

    private:
        struct VoiceView
        {
            chouchou::EngineVoice::Settings settings;
            float inputRms = 0.0f, autoGain = 1.0f, f0 = 0.0f;
            bool pitchActive = false;
        };

        void timerCallback() override;
        void paintSpectrum (juce::Graphics&, juce::Rectangle<float>);
        void paintCombCurve (juce::Graphics&, juce::Rectangle<float>, const VoiceView&, bool dashed, bool emphasis);
        void paintFilterCurves (juce::Graphics&, juce::Rectangle<float>);
        void paintFocusPanel (juce::Graphics&, juce::Rectangle<float>);
        void paintEffectBody (juce::Graphics&, juce::Rectangle<float>, int bars);
        void paintGlideDiagram (juce::Graphics&, juce::Rectangle<float>);
        void paintConfidence (juce::Graphics&, juce::Rectangle<float>);
        void paintTextBody (juce::Graphics&, juce::Rectangle<float>, const juce::StringArray& lines);
        void paintBars (juce::Graphics&, juce::Rectangle<float>);
        int focusIndex() const;
        juce::RangedAudioParameter* focusParameter() const;
        float maxDisplayHz() const;
        juce::String statusText() const;

        ChouchouEQtimbreAudioProcessor& proc;
        chouchou::HarmonicAnalyzer analyzer;
        std::vector<float> pullBuffer;
        float f0 = 0.0f;
        bool f0Valid = false;
        VoiceView mainView, randomView;
        bool randomOn = false;
        float outputGainLin = 1.0f, inputGainLin = 1.0f, mainMix = 1.0f;
        chouchou::EngineValues mainValues {};
        const KnobFocus* focus = nullptr;
        const RandomMarks* marks = nullptr;
    };

    // One random amount: name, slider and a bar showing base, reachable range and current value.
    class AmountRow : public juce::Component
    {
    public:
        AmountRow (ChouchouEQtimbreAudioProcessor&, const RandomMarks&, int index);
        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        const RandomMarks& marks;
        int index;
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
        juce::Rectangle<int> barArea;
    };

    class RandomPage : public juce::Component, private juce::Timer
    {
    public:
        RandomPage (ChouchouEQtimbreAudioProcessor&, const RandomMarks&);
        ~RandomPage() override;
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;

        struct LabelledSlider : public juce::Component
        {
            LabelledSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
                            const juce::String& name, const juce::String& tooltip);
            void resized() override;
            juce::Label label;
            juce::Slider slider;
            std::unique_ptr<SliderAttachment> attachment;
        };

        ChouchouEQtimbreAudioProcessor& proc;
        Toggle bypass, autoRoll, sync;
        juce::TextButton rollButton, adoptButton;
        LabelledSlider mix, interval, morph;
        Choice division;
        juce::Label rollInfo;
        std::vector<std::unique_ptr<AmountRow>> rows;
        int lastRollCount = -1;
    };
}

class ChouchouEQtimbreAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ChouchouEQtimbreAudioProcessorEditor (ChouchouEQtimbreAudioProcessor&);
    ~ChouchouEQtimbreAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Shows the random page under the spectrum, growing the window (also used by the snapshot tool).
    void showRandomPage (bool show);
    // Which knob the display explains (also used by the snapshot tool).
    void setFocus (int index) { knobFocus.index = index; }

    // With the random page the spectrum shrinks from 300 to 210 px, keeping the window at 840 px.
    static constexpr int kWidth = 1060, kBaseHeight = 640, kRandomPageHeight = 280;
    static constexpr int kHeightWithRandom = kBaseHeight + kRandomPageHeight + 10 - 90;

private:
    void timerCallback() override;

    using Knob   = chouchou_timbre_ui::Knob;
    using Choice = chouchou_timbre_ui::Choice;
    using Toggle = chouchou_timbre_ui::Toggle;

    ChouchouEQtimbreAudioProcessor& proc;
    chouchou_timbre_ui::TimbreLookAndFeel lookAndFeel;
    chouchou_timbre_ui::RandomMarks marks;
    chouchou_timbre_ui::KnobFocus knobFocus;
    juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow;

    Choice mode, oversampling;
    Toggle autoGain, autoDrive;
    juce::TextButton randomButton;
    chouchou_timbre_ui::ClipLight clipLight;

    Knob drive, driveComp, bias, even, odd, tone, dcCut, genLevel;

    Knob balance, rebLevel, manualHz, confidence, glide, jumpFade;
    Choice pitchSource, midiPriority;
    Toggle midiHold, protect;
    juce::Label autoNote;

    Knob inputGain, outputGain, mix;

    chouchou_timbre_ui::HarmonicDisplay display;
    chouchou_timbre_ui::RandomPage randomPage;

    juce::Rectangle<int> genArea, rebArea, globalArea;
    bool genActive = true, rebActive = true;
    bool marksVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouEQtimbreAudioProcessorEditor)
};
