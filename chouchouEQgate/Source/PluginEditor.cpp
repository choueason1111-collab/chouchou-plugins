/*
  ==============================================================================

    ChouChou EQ Gate — editor

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg     { 0xff1a1c1e };
    const juce::Colour kPanel  { 0xff25282c };
    const juce::Colour kAccent { 0xffe8a87c };
    const juce::Colour kText   { 0xffe8e6e3 };
    const juce::Colour kMuted  { 0xff9a9690 };
    const juce::Colour kDim    { 0xff3a3e44 };
}

//==============================================================================
NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor (NewProjectAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    titleLabel.setText (JucePlugin_Name, juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, kText);
    titleLabel.setFont (juce::FontOptions (22.0f).withStyle ("Bold"));
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("Spectral gate — weak bins cut, strong bins stay",
                           juce::dontSendNotification);
    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    subtitleLabel.setColour (juce::Label::textColourId, kMuted);
    subtitleLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (subtitleLabel);

    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setColour (juce::Label::textColourId, kMuted);
    infoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (infoLabel);

    autoGainButton.setClickingTogglesState (true);
    autoGainButton.setColour (juce::ToggleButton::textColourId, kText);
    autoGainButton.setColour (juce::ToggleButton::tickColourId, kAccent);
    autoGainButton.setColour (juce::ToggleButton::tickDisabledColourId, kMuted);
    autoGainButton.setTooltip ("Matches wet level to the latency-aligned dry signal");
    addAndMakeVisible (autoGainButton);

    setupSlider (amountSlider,    amountLabel,    "Amount");
    setupSlider (thresholdSlider, thresholdLabel, "Threshold");
    setupSlider (attackSlider,    attackLabel,    "Attack");
    setupSlider (releaseSlider,   releaseLabel,   "Release");
    setupSlider (mixSlider,       mixLabel,       "Mix");
    setupSlider (makeupSlider,    makeupLabel,    "Makeup");

    thresholdSlider.setTextValueSuffix (" dBFS");
    attackSlider.setTextValueSuffix (" ms");
    releaseSlider.setTextValueSuffix (" ms");
    makeupSlider.setTextValueSuffix (" dB");

    fftLabel.setText ("FFT", juce::dontSendNotification);
    fftLabel.setJustificationType (juce::Justification::centredLeft);
    fftLabel.setColour (juce::Label::textColourId, kMuted);
    addAndMakeVisible (fftLabel);

    fftCombo.addItem ("512", 1);
    fftCombo.addItem ("1024", 2);
    fftCombo.addItem ("2048", 3);
    fftCombo.addItem ("4096", 4);
    fftCombo.setColour (juce::ComboBox::backgroundColourId, kDim);
    fftCombo.setColour (juce::ComboBox::outlineColourId, kMuted);
    fftCombo.setColour (juce::ComboBox::textColourId, kText);
    fftCombo.setColour (juce::ComboBox::arrowColourId, kAccent);
    addAndMakeVisible (fftCombo);

    auto& apvts = audioProcessor.getAPVTS();
    amountAttachment    = std::make_unique<SliderAttachment> (apvts, "amt",       amountSlider);
    thresholdAttachment = std::make_unique<SliderAttachment> (apvts, "threshold", thresholdSlider);
    attackAttachment    = std::make_unique<SliderAttachment> (apvts, "attack",    attackSlider);
    releaseAttachment   = std::make_unique<SliderAttachment> (apvts, "release",   releaseSlider);
    mixAttachment       = std::make_unique<SliderAttachment> (apvts, "mix",       mixSlider);
    makeupAttachment    = std::make_unique<SliderAttachment> (apvts, "makeup",    makeupSlider);
    fftAttachment       = std::make_unique<ComboBoxAttachment> (apvts, "fftsize",  fftCombo);
    autoGainAttachment  = std::make_unique<ButtonAttachment> (apvts, "autoGain",  autoGainButton);

    updateInfoLabel();
    startTimerHz (8);

    setSize (760, 360);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
    stopTimer();
}

void NewProjectAudioProcessorEditor::timerCallback()
{
    updateInfoLabel();
}

void NewProjectAudioProcessorEditor::updateInfoLabel()
{
    const double sr = audioProcessor.getCurrentSampleRate();
    const int fftN  = audioProcessor.getFftSize();
    const int hop   = audioProcessor.getHopSize();
    const float hopMs = audioProcessor.getHopMilliseconds();

    infoLabel.setText (juce::String::formatted ("SR %.0f Hz  ·  FFT %d  ·  hop %d (%.2f ms)",
                                                sr, fftN, hop, hopMs),
                       juce::dontSendNotification);
}

void NewProjectAudioProcessorEditor::setupSlider (juce::Slider& s, juce::Label& l, const juce::String& text)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 20);
    s.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, kMuted);
    s.setColour (juce::Slider::thumbColourId, kText);
    s.setColour (juce::Slider::textBoxTextColourId, kText);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s);

    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, kMuted);
    l.attachToComponent (&s, false);
    addAndMakeVisible (l);
}

//==============================================================================
void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    auto panel = getLocalBounds().reduced (16).withTrimmedTop (96);
    g.setColour (kPanel);
    g.fillRoundedRectangle (panel.toFloat(), 10.0f);
}

void NewProjectAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);

    auto titleRow = bounds.removeFromTop (32);
    titleLabel.setBounds (titleRow.removeFromLeft (280));
    autoGainButton.setBounds (titleRow.removeFromLeft (110).reduced (4, 2));

    auto subRow = bounds.removeFromTop (22);
    subtitleLabel.setBounds (subRow);

    auto infoRow = bounds.removeFromTop (28);
    fftLabel.setBounds (infoRow.removeFromLeft (32));
    fftCombo.setBounds (infoRow.removeFromLeft (110).reduced (0, 2));
    infoLabel.setBounds (infoRow);

    bounds.removeFromTop (10);

    auto row = bounds.reduced (4, 12);
    const int w = row.getWidth() / 6;

    amountSlider.setBounds    (row.removeFromLeft (w).reduced (6, 8));
    thresholdSlider.setBounds (row.removeFromLeft (w).reduced (6, 8));
    attackSlider.setBounds    (row.removeFromLeft (w).reduced (6, 8));
    releaseSlider.setBounds   (row.removeFromLeft (w).reduced (6, 8));
    mixSlider.setBounds       (row.removeFromLeft (w).reduced (6, 8));
    makeupSlider.setBounds    (row.reduced (6, 8));
}
