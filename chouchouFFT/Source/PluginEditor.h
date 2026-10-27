/*
  ==============================================================================
    PluginEditor.h
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

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
    void updatePipelineText();

    NewProjectAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& apvts;

    juce::Label titleLabel;
    juce::Label pipelineLabel;
    juce::Label helpLabel;

    juce::ComboBox fftSizeBox, experimentBox;
    juce::Label fftSizeLabel, experimentLabel;

    juce::Slider shiftSlider, shiftreelSlider, shiftimageSlider;
    juce::Slider mulreelSlider, mulimageSlider, offsetBinsSlider;
    juce::Slider mixSlider, outputSlider;
    juce::Label shiftLabel, shiftreelLabel, shiftimageLabel;
    juce::Label mulreelLabel, mulimageLabel, offsetBinsLabel;
    juce::Label mixLabel, outputLabel;

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<ComboAttachment> fftSizeAtt, experimentAtt;
    std::unique_ptr<SliderAttachment> shiftAtt, shiftreelAtt, shiftimageAtt;
    std::unique_ptr<SliderAttachment> mulreelAtt, mulimageAtt, offsetBinsAtt;
    std::unique_ptr<SliderAttachment> mixAtt, outputAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessorEditor)
};
