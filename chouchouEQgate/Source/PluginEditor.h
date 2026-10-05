/*
  ==============================================================================

    ChouChou EQ Gate — editor

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class NewProjectAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit NewProjectAudioProcessorEditor (NewProjectAudioProcessor&);
    ~NewProjectAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void updateModeButtons();
    void updateInfoLabel();
    void setupSlider (juce::Slider& s, juce::Label& l, const juce::String& text);

    NewProjectAudioProcessor& audioProcessor;

    juce::Slider amountSlider, thresholdSlider, attackSlider, releaseSlider, mixSlider, makeupSlider;
    juce::Label  amountLabel,  thresholdLabel,  attackLabel,  releaseLabel,  mixLabel,  makeupLabel;
    juce::Label  titleLabel, subtitleLabel, infoLabel, fftLabel;

    juce::TextButton expandButton { "Expand" };
    juce::TextButton gateButton   { "Gate" };

    juce::ComboBox fftCombo;

    std::unique_ptr<SliderAttachment> amountAttachment, thresholdAttachment,
                                      attackAttachment, releaseAttachment,
                                      mixAttachment, makeupAttachment;
    std::unique_ptr<ComboBoxAttachment> fftAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessorEditor)
};
