#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace chouchou_delay_ui
{
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    class DelayLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        DelayLookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
    };

    class CloseButton : public juce::Button
    {
    public:
        CloseButton() : juce::Button ("Close") { setTooltip ("Close (Esc)"); }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    class TimelineView : public juce::Component
    {
    public:
        explicit TimelineView (ChouchouDelayAudioProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
    private:
        ChouchouDelayAudioProcessor& proc;
    };

    class EchoCell : public juce::Component
    {
    public:
        EchoCell (ChouchouDelayAudioProcessor& p, int echoIndex);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        void refresh (bool selected);

        std::function<void (int)> onOpen;
    private:
        ChouchouDelayAudioProcessor& proc;
        const int echo;
        bool isSelected = false;
        juce::ToggleButton onButton;
        std::unique_ptr<ButtonAttachment> onAttach;
    };

    class EchoPage : public juce::Component
    {
    public:
        explicit EchoPage (ChouchouDelayAudioProcessor& p);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        void showEcho (int echoIndex);
        void close();
        void refresh();
        int getEcho() const { return echo; }

        std::function<void()> onClosed;
    private:
        juce::Rectangle<int> panelBounds() const;
        void setupKnob (juce::Slider&, juce::Label&, const juce::String& name);

        ChouchouDelayAudioProcessor& proc;
        int echo = 1;

        juce::Label title;
        CloseButton closeButton;
        juce::TextButton prevButton { "<" }, nextButton { ">" };
        juce::ToggleButton onButton { "On" };
        juce::Slider levelKnob, pitchKnob, fineKnob, offsetKnob, panKnob, countKnob;
        juce::Label levelName, pitchName, fineName, offsetName, panName, countName, rhythmName, tupletName;
        juce::ComboBox rhythmBox, tupletBox;
        juce::Label offsetReadout, infoReadout;

        juce::ToggleButton gateOnButton { "Gate" }, compOnButton { "Comp" };
        juce::Slider gateAmtKnob, gateThreshKnob, compThreshKnob, compRatioKnob;
        juce::Label gateAmtName, gateThreshName, compThreshName, compRatioName;
        juce::ToggleButton gateAutoButton { "Gate AG" }, compAutoButton { "Comp AG" };
        juce::TextButton insertLoadButton { "Insert…" }, insertClearButton { "Clear" }, insertOpenButton { "Open UI" };
        juce::ToggleButton insertBypassButton { "Ins Bypass" };
        juce::Label insertNameLabel, fxStatusLabel;

        std::unique_ptr<ButtonAttachment> onAttach, gateOnAttach, compOnAttach, insertBypassAttach;
        std::unique_ptr<ButtonAttachment> gateAutoAttach, compAutoAttach;
        std::unique_ptr<SliderAttachment> levelAttach, pitchAttach, fineAttach, offsetAttach, panAttach, countAttach;
        std::unique_ptr<SliderAttachment> gateAmtAttach, gateThreshAttach, compThreshAttach, compRatioAttach;
        std::unique_ptr<ComboAttachment> rhythmAttach, tupletAttach;

        std::unique_ptr<juce::AudioProcessorEditor> insertEditor;
        std::shared_ptr<juce::AudioPluginInstance> insertKeepAlive;
        std::unique_ptr<juce::DocumentWindow> insertWindow;
    };
}

class ChouchouDelayAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit ChouchouDelayAudioProcessorEditor (ChouchouDelayAudioProcessor&);
    ~ChouchouDelayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& name);
    void applyIntervalUnit();
    bool intervalInSamples() const;
    void openEcho (int echo);

    ChouchouDelayAudioProcessor& proc;
    chouchou_delay_ui::DelayLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::Label title, subtitle;
    juce::Slider countKnob, intervalKnob, decayKnob, mixKnob;
    juce::Label countName, intervalName, decayName, mixName;
    juce::TextButton unitButton;
    juce::Label intervalReadout;
    juce::ComboBox tempoModeBox;
    juce::TextButton tapButton { "Tap" };
    juce::Label bpmReadout, tempoHint;
    juce::Label gridTitle;
    juce::Label footer;

    std::unique_ptr<chouchou_delay_ui::SliderAttachment> countAttach, intervalAttach, decayAttach, mixAttach;
    std::unique_ptr<chouchou_delay_ui::ComboAttachment> tempoModeAttach;

    chouchou_delay_ui::TimelineView timeline;
    std::array<std::unique_ptr<chouchou_delay_ui::EchoCell>, ChouchouDelayAudioProcessor::kMaxEchoes> cells;
    chouchou_delay_ui::EchoPage page;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouDelayAudioProcessorEditor)
};
