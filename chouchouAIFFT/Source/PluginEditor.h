/*
  ==============================================================================
    PluginEditor.h — Spectral Lab IA UI
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>
#include <memory>

class NewProjectAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit NewProjectAudioProcessorEditor (NewProjectAudioProcessor&);
    ~NewProjectAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void updateHelpText();
    void rebuildActiveHints();

    NewProjectAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& apvts;

    juce::Label titleLabel, helpLabel;
    juce::ComboBox fftSizeBox, experimentBox;
    juce::Label fftSizeLabel, experimentLabel;

    juce::Viewport knobViewport;
    juce::Component knobPanel;

    struct KnobRow
    {
        juce::String paramId;
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        bool isBool = false;
        juce::ToggleButton toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> batt;
    };

    std::vector<std::unique_ptr<KnobRow>> rows;

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboAttachment> fftSizeAtt, experimentAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessorEditor)
};
