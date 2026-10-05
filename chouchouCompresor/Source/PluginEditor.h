/*
  ==============================================================================

    ChouChou Compressor — editor
    (ASCII-only UI strings)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include "PluginProcessor.h"

//==============================================================================
class MeterBar  : public juce::Component
{
public:
    void setValueDb (float db, float maxDb)
    {
        const float span = juce::jmax (1.0f, maxDb);
        // Positive = gain-reduction / boost amount; negative = input peak (dBFS).
        if (db < 0.0f)
            level = juce::jlimit (0.0f, 1.0f, (db + span) / span);
        else
            level = juce::jlimit (0.0f, 1.0f, db / span);
        readout = db;
        repaint();
    }

    void setColours (juce::Colour fillIn, juce::Colour trackIn)
    {
        fill = fillIn;
        track = trackIn;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (track);
        g.fillRoundedRectangle (r, 4.0f);

        auto filled = r.withWidth (juce::jmax (0.0f, r.getWidth() * level));
        g.setColour (fill);
        g.fillRoundedRectangle (filled, 4.0f);

        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::FontOptions (13.0f).withStyle ("Bold"));
        g.drawText (juce::String (readout, 1) + " dB", r, juce::Justification::centred);
    }

private:
    float level = 0.0f;
    float readout = 0.0f;
    juce::Colour fill { 0xff7ec8a3 };
    juce::Colour track { 0xff3a3e44 };
};

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
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void updateInfoLabel();
    void updateLatencyLabels();
    void updateMeters();
    void updateFftButtons();
    void setFftChoice (int index);
    void setupKnob (juce::Slider& s, juce::Label& l, const juce::String& text, juce::Colour fill);

    NewProjectAudioProcessor& audioProcessor;

    juce::Slider lowSlider, highSlider, upSlider, downSlider;
    juce::Slider boostAtkSlider, boostRelSlider, boostSpeedSlider;
    juce::Slider grAtkSlider, grRelSlider, compSpeedSlider;
    juce::Slider mixSlider, makeupSlider;

    juce::Label lowLabel, highLabel, upLabel, downLabel;
    juce::Label boostAtkLabel, boostRelLabel, boostSpeedLabel;
    juce::Label grAtkLabel, grRelLabel, compSpeedLabel;
    juce::Label mixLabel, makeupLabel;
    juce::Label titleLabel, subtitleLabel, infoLabel, fftLabel;
    juce::Label boostTitle, grTitle, inTitle, rowBoostLabel, rowGrLabel, rowMixLabel;

    juce::Label latencySectionLabel;
    juce::Label latencyValueLabel;
    juce::Label latencyFormulaLabel;
    juce::Label pdcValueLabel;
    juce::Label dryAlignLabel;

    juce::TextButton fft512 { "512" }, fft1024 { "1024" }, fft2048 { "2048" }, fft4096 { "4096" };

    juce::ToggleButton boostScButton { "SC" }, compScButton { "SC" };
    juce::Label scTitle;

    MeterBar boostMeter, grMeter, inMeter, scMeter;
    float smoothBoost = 0.0f, smoothGr = 0.0f, smoothIn = -120.0f, smoothSc = -120.0f;
    uint32_t lastProcessCounter = 0;
    int stalledTicks = 0;

    std::unique_ptr<SliderAttachment> lowAttachment, highAttachment,
                                      upAttachment, downAttachment,
                                      boostAtkAttachment, boostRelAttachment, boostSpeedAttachment,
                                      grAtkAttachment, grRelAttachment, compSpeedAttachment,
                                      mixAttachment, makeupAttachment;

    std::unique_ptr<ButtonAttachment> boostScAttachment, compScAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessorEditor)
};
