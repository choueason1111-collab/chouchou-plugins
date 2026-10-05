#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace chouchou_trigger_ui
{
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    class TriggerLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        TriggerLookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
    };

    // Live scrolling input waveform with threshold line, trigger markers and retrigger hold-off shading.
    class ScopeView : public juce::Component
    {
    public:
        explicit ScopeView (ChouchouTriggerAudioProcessor& p);

        void pull();
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        int  getTotalColumns() const { return (int) totalColumns; }
        int  countTriggerMarkers() const;
        void setDisplaySeconds (double s);
        void setDbScale (bool shouldUseDb);

        static constexpr int kHistory = 12000;

    private:
        juce::Rectangle<float> plotArea() const;
        float valueToY (float v, const juce::Rectangle<float>& plot) const;
        float thresholdFromY (float y) const;

        ChouchouTriggerAudioProcessor& proc;
        std::vector<ChouchouTriggerAudioProcessor::ScopeColumn> history;
        std::vector<ChouchouTriggerAudioProcessor::ScopeColumn> scratch;
        int writePos = 0;
        juce::int64 totalColumns = 0;
        double displaySeconds = 2.0;
        bool dbScale = true;
        bool dragging = false;

        juce::TextButton lengthButton, scaleButton;
    };

    class InputMeter : public juce::Component
    {
    public:
        explicit InputMeter (ChouchouTriggerAudioProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void update (float peak, bool triggered);
    private:
        ChouchouTriggerAudioProcessor& proc;
        float level = 0.0f;
        float flash = 0.0f;
    };

    class SlotStrip : public juce::Component,
                      public juce::FileDragAndDropTarget
    {
    public:
        SlotStrip (ChouchouTriggerAudioProcessor& p, int slotIndex);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();
        void setFlash (float amount);

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
        void filesDropped (const juce::StringArray& files, int, int) override;

        juce::String getDisplayName() const { return nameLabel.getText(); }

    private:
        void openChooser();
        void setupKnob (juce::Slider&, juce::Label&, const juce::String& name);

        ChouchouTriggerAudioProcessor& proc;
        const int slot;
        bool dragOver = false;
        float flash = 0.0f;
        std::vector<float> peaks;

        juce::Label numberLabel, nameLabel;
        juce::TextButton loadButton { "Load" }, clearButton { "Clear" }, previewButton { "Preview" };
        juce::ToggleButton onButton { "On" };
        juce::Slider levelKnob, pitchKnob, fineKnob;
        juce::Label levelName, pitchName, fineName;
        std::unique_ptr<ButtonAttachment> onAttach;
        std::unique_ptr<SliderAttachment> levelAttach, pitchAttach, fineAttach;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}

class ChouchouTriggerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit ChouchouTriggerAudioProcessorEditor (ChouchouTriggerAudioProcessor&);
    ~ChouchouTriggerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Pulls pending scope data and refreshes monitors; normally driven by the editor timer.
    void refreshNow();

private:
    void timerCallback() override { refreshNow(); }
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& name);
    void updateModeButton();

    ChouchouTriggerAudioProcessor& proc;
    chouchou_trigger_ui::TriggerLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::Label title, subtitle;
    chouchou_trigger_ui::ScopeView scope;
    chouchou_trigger_ui::InputMeter meter;

    juce::Slider thresholdKnob, retriggerKnob, releaseKnob, dryKnob, wetKnob;
    juce::Label thresholdName, retriggerName, releaseName, dryName, wetName;
    juce::TextButton modeButton;
    juce::Label modeName;
    std::unique_ptr<chouchou_trigger_ui::SliderAttachment> thresholdAttach, retriggerAttach, releaseAttach, dryAttach, wetAttach;
    std::unique_ptr<chouchou_trigger_ui::ButtonAttachment> modeAttach;

    std::array<std::unique_ptr<chouchou_trigger_ui::SlotStrip>, ChouchouTriggerAudioProcessor::kNumSlots> strips;

    int lastSlotsVersion = -1;
    int lastTriggerCount = 0;
    float stripFlash = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChouchouTriggerAudioProcessorEditor)
};
