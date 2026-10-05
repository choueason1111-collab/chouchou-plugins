/*
  ==============================================================================

    ChouChou Compressor — editor (ASCII-only)

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg     { 0xff141618 };
    const juce::Colour kPanel  { 0xff22262a };
    const juce::Colour kAccent { 0xff7ec8a3 };
    const juce::Colour kLow    { 0xff6aa6d8 };
    const juce::Colour kHigh   { 0xffe08a6a };
    const juce::Colour kText   { 0xffe8e6e3 };
    const juce::Colour kMuted  { 0xff9a9690 };
    const juce::Colour kDim    { 0xff3a3e44 };
}

//==============================================================================
NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor (NewProjectAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    titleLabel.setText ("chouchouCompressor", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, kText);
    titleLabel.setFont (juce::FontOptions (24.0f).withStyle ("Bold"));
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("Flow: BOOST → COMP → MIX   |   Attack = wait before engage   |   Speed = ramp once engaged   |   Rel = release rate   |   Ratio 1 = bypass",
                           juce::dontSendNotification);
    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    subtitleLabel.setColour (juce::Label::textColourId, kMuted);
    subtitleLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (subtitleLabel);

    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setColour (juce::Label::textColourId, kMuted);
    infoLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (infoLabel);

    latencySectionLabel.setText ("LATENCY", juce::dontSendNotification);
    latencySectionLabel.setColour (juce::Label::textColourId, kAccent);
    latencySectionLabel.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    addAndMakeVisible (latencySectionLabel);

    auto styleLatencyLine = [this] (juce::Label& lab)
    {
        lab.setColour (juce::Label::textColourId, kText);
        lab.setFont (juce::FontOptions (13.0f));
        lab.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (lab);
    };

    styleLatencyLine (latencyValueLabel);
    styleLatencyLine (latencyFormulaLabel);
    styleLatencyLine (pdcValueLabel);
    styleLatencyLine (dryAlignLabel);
    latencyFormulaLabel.setColour (juce::Label::textColourId, kMuted);
    latencyFormulaLabel.setFont (juce::FontOptions (12.0f));
    pdcValueLabel.setColour (juce::Label::textColourId, kMuted);
    pdcValueLabel.setFont (juce::FontOptions (12.0f));
    dryAlignLabel.setColour (juce::Label::textColourId, kMuted);
    dryAlignLabel.setFont (juce::FontOptions (12.0f));

    boostTitle.setText ("BOOST (quiet up)", juce::dontSendNotification);
    boostTitle.setColour (juce::Label::textColourId, kLow);
    boostTitle.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    addAndMakeVisible (boostTitle);

    grTitle.setText ("GR (loud down)", juce::dontSendNotification);
    grTitle.setColour (juce::Label::textColourId, kHigh);
    grTitle.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    addAndMakeVisible (grTitle);

    inTitle.setText ("IN (detect)", juce::dontSendNotification);
    inTitle.setColour (juce::Label::textColourId, kAccent);
    inTitle.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    addAndMakeVisible (inTitle);

    rowBoostLabel.setText ("1  BOOST", juce::dontSendNotification);
    rowBoostLabel.setColour (juce::Label::textColourId, kLow);
    rowBoostLabel.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    addAndMakeVisible (rowBoostLabel);

    rowGrLabel.setText ("2  COMP", juce::dontSendNotification);
    rowGrLabel.setColour (juce::Label::textColourId, kHigh);
    rowGrLabel.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    addAndMakeVisible (rowGrLabel);

    rowMixLabel.setText ("3  MIX", juce::dontSendNotification);
    rowMixLabel.setColour (juce::Label::textColourId, kAccent);
    rowMixLabel.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    addAndMakeVisible (rowMixLabel);

    scTitle.setText ("SC IN", juce::dontSendNotification);
    scTitle.setColour (juce::Label::textColourId, kMuted);
    scTitle.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    addAndMakeVisible (scTitle);

    boostMeter.setColours (kLow, kDim);
    grMeter.setColours (kHigh, kDim);
    inMeter.setColours (kAccent, kDim);
    scMeter.setColours (kMuted, kDim);
    addAndMakeVisible (boostMeter);
    addAndMakeVisible (grMeter);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (scMeter);

    auto styleSc = [this] (juce::ToggleButton& b, juce::Colour c)
    {
        b.setColour (juce::ToggleButton::textColourId, c);
        b.setColour (juce::ToggleButton::tickColourId, c);
        b.setColour (juce::ToggleButton::tickDisabledColourId, kMuted);
        b.setTooltip ("Detect this stage from the sidechain input");
        addAndMakeVisible (b);
    };

    styleSc (boostScButton, kLow);
    styleSc (compScButton, kHigh);

    setupKnob (lowSlider,         lowLabel,         "Boost Thresh", kLow);
    setupKnob (upSlider,          upLabel,          "Boost Ratio",  kLow);
    setupKnob (boostAtkSlider,    boostAtkLabel,    "Boost Atk Delay", kLow);
    setupKnob (boostRelSlider,    boostRelLabel,    "Boost Rel",    kLow);
    setupKnob (boostSpeedSlider,  boostSpeedLabel,  "Boost Speed",  kLow);

    setupKnob (highSlider,        highLabel,        "Comp Thresh",  kHigh);
    setupKnob (downSlider,        downLabel,        "Comp Ratio",   kHigh);
    setupKnob (grAtkSlider,       grAtkLabel,       "Comp Atk Delay",  kHigh);
    setupKnob (grRelSlider,       grRelLabel,       "Comp Rel",     kHigh);
    setupKnob (compSpeedSlider,   compSpeedLabel,   "Comp Speed",   kHigh);

    setupKnob (mixSlider,         mixLabel,         "Mix",          kAccent);
    setupKnob (makeupSlider,      makeupLabel,      "Makeup",       kAccent);

    lowSlider.setTextValueSuffix (" dBFS");
    highSlider.setTextValueSuffix (" dBFS");
    boostAtkSlider.setTextValueSuffix (" ms");
    boostRelSlider.setTextValueSuffix (" ms");
    boostSpeedSlider.setTextValueSuffix (" %");
    grAtkSlider.setTextValueSuffix (" ms");
    grRelSlider.setTextValueSuffix (" ms");
    compSpeedSlider.setTextValueSuffix (" %");
    makeupSlider.setTextValueSuffix (" dB");

    mixSlider.textFromValueFunction = [] (double v)
    {
        return juce::String (juce::roundToInt (v * 100.0)) + " %";
    };
    mixSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return juce::jlimit (0.0, 1.0, t.getDoubleValue() / 100.0);
    };

    fftLabel.setText ("FFT Size", juce::dontSendNotification);
    fftLabel.setJustificationType (juce::Justification::centredLeft);
    fftLabel.setColour (juce::Label::textColourId, kMuted);
    fftLabel.setFont (juce::FontOptions (13.0f).withStyle ("Bold"));
    addAndMakeVisible (fftLabel);

    auto styleFft = [this] (juce::TextButton& b)
    {
        b.setClickingTogglesState (true);
        b.setRadioGroupId (90210);
        b.setColour (juce::TextButton::buttonColourId, kDim);
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setColour (juce::TextButton::textColourOffId, kText);
        b.setColour (juce::TextButton::textColourOnId, kBg);
        addAndMakeVisible (b);
    };

    styleFft (fft512);
    styleFft (fft1024);
    styleFft (fft2048);
    styleFft (fft4096);

    fft512.onClick  = [this] { setFftChoice (0); };
    fft1024.onClick = [this] { setFftChoice (1); };
    fft2048.onClick = [this] { setFftChoice (2); };
    fft4096.onClick = [this] { setFftChoice (3); };

    auto& apvts = audioProcessor.getAPVTS();
    lowAttachment        = std::make_unique<SliderAttachment> (apvts, "lowthresh",  lowSlider);
    highAttachment       = std::make_unique<SliderAttachment> (apvts, "highthresh", highSlider);
    upAttachment         = std::make_unique<SliderAttachment> (apvts, "upratio",    upSlider);
    downAttachment       = std::make_unique<SliderAttachment> (apvts, "downratio",  downSlider);
    boostAtkAttachment   = std::make_unique<SliderAttachment> (apvts, "boostatk",   boostAtkSlider);
    boostRelAttachment   = std::make_unique<SliderAttachment> (apvts, "boostrel",   boostRelSlider);
    boostSpeedAttachment = std::make_unique<SliderAttachment> (apvts, "boostspeed", boostSpeedSlider);
    grAtkAttachment      = std::make_unique<SliderAttachment> (apvts, "gratk",      grAtkSlider);
    grRelAttachment      = std::make_unique<SliderAttachment> (apvts, "grrel",      grRelSlider);
    compSpeedAttachment  = std::make_unique<SliderAttachment> (apvts, "compspeed",  compSpeedSlider);
    mixAttachment        = std::make_unique<SliderAttachment> (apvts, "mix",        mixSlider);
    makeupAttachment     = std::make_unique<SliderAttachment> (apvts, "makeup",     makeupSlider);
    boostScAttachment    = std::make_unique<ButtonAttachment> (apvts, "boostsc",    boostScButton);
    compScAttachment     = std::make_unique<ButtonAttachment> (apvts, "compsc",     compScButton);
    boostScButton.toFront (false);
    compScButton.toFront (false);

    updateFftButtons();
    updateInfoLabel();
    updateLatencyLabels();
    updateMeters();
    startTimerHz (30);

    setSize (1100, 780);
    setResizable (true, true);
    setResizeLimits (960, 680, 1500, 1200);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
    stopTimer();
}

void NewProjectAudioProcessorEditor::setFftChoice (int index)
{
    if (auto* p = audioProcessor.getAPVTS().getParameter ("fftsize"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, 3, index)));

    // Rebuild off the audio thread (callback lock), never inside processBlock.
    audioProcessor.applyFftSizeFromParameterLocked (false);

    updateFftButtons();
    updateInfoLabel();
    updateLatencyLabels();
}

void NewProjectAudioProcessorEditor::updateFftButtons()
{
    const int choice = (int) audioProcessor.getAPVTS().getRawParameterValue ("fftsize")->load();
    fft512.setToggleState  (choice == 0, juce::dontSendNotification);
    fft1024.setToggleState (choice == 1, juce::dontSendNotification);
    fft2048.setToggleState (choice == 2, juce::dontSendNotification);
    fft4096.setToggleState (choice == 3, juce::dontSendNotification);
}

void NewProjectAudioProcessorEditor::timerCallback()
{
    updateFftButtons();
    updateInfoLabel();
    updateLatencyLabels();
    updateMeters();
}

void NewProjectAudioProcessorEditor::updateInfoLabel()
{
    const double sr = audioProcessor.getCurrentSampleRate();
    const int fftN  = audioProcessor.getFftSize();
    const int hop   = audioProcessor.getHopSize();
    const float hopMs = audioProcessor.getHopMilliseconds();

    infoLabel.setText ("SR " + juce::String (sr, 0) + " Hz   ·   hop "
                       + juce::String (hop) + " (" + juce::String (hopMs, 2) + " ms)",
                       juce::dontSendNotification);
}

void NewProjectAudioProcessorEditor::updateLatencyLabels()
{
    const int samples = audioProcessor.getReportedLatencySamples();
    const float ms = audioProcessor.getReportedLatencyMilliseconds();
    const int fftN = audioProcessor.getFftSize();
    const int dryN = audioProcessor.getDryAlignSamples();
    const double sr = audioProcessor.getCurrentSampleRate();

    latencyValueLabel.setText (
        "Plugin delay:  " + juce::String (samples) + " samples   =   "
            + juce::String (ms, 2) + " ms",
        juce::dontSendNotification);

    latencyFormulaLabel.setText (
        "Calc:  latency_samples = FFT size (" + juce::String (fftN)
            + ")    ·    ms = samples / SR × 1000"
            + (sr > 0.0 ? ("  (" + juce::String (sr, 0) + " Hz)") : juce::String()),
        juce::dontSendNotification);

    pdcValueLabel.setText (
        "Host auto-compensate (PDC):  reports " + juce::String (samples)
            + " samples to the DAW  →  other tracks delayed to match",
        juce::dontSendNotification);

    dryAlignLabel.setText (
        "Internal Mix dry-align:  " + juce::String (dryN)
            + " samples (same as plugin delay, keeps dry/wet in phase)",
        juce::dontSendNotification);
}

void NewProjectAudioProcessorEditor::updateMeters()
{
    // Host pause often stops processBlock; freeze would leave stale BOOST/GR lit.
    const uint32_t counter = audioProcessor.getProcessCounter();
    if (counter == lastProcessCounter)
    {
        ++stalledTicks;
        if (stalledTicks >= 3) // ~100 ms at 30 Hz
        {
            smoothBoost = 0.0f;
            smoothGr = 0.0f;
            smoothIn = -120.0f;
            smoothSc = -120.0f;
            boostMeter.setValueDb (0.0f, 30.0f);
            grMeter.setValueDb (0.0f, 30.0f);
            inMeter.setValueDb (-120.0f, 60.0f);
            scMeter.setValueDb (-120.0f, 60.0f);
            return;
        }
    }
    else
    {
        lastProcessCounter = counter;
        stalledTicks = 0;
    }

    const float boost = audioProcessor.getMeterBoostDb();
    const float gr    = audioProcessor.getMeterGrDb();
    const float inDb  = audioProcessor.getMeterInputDb();

    if (boost < 0.05f)
        smoothBoost = 0.0f;
    else
        smoothBoost = (boost > smoothBoost) ? boost : (smoothBoost * 0.85f);

    if (gr < 0.05f)
        smoothGr = 0.0f;
    else
        smoothGr = (gr > smoothGr) ? gr : (smoothGr * 0.85f);

    smoothIn = (inDb > smoothIn) ? inDb : (smoothIn * 0.85f + inDb * 0.15f);

    const bool scConnected = audioProcessor.isSidechainConnected();
    const float scDb = scConnected ? audioProcessor.getMeterScDb() : -120.0f;
    smoothSc = (scDb > smoothSc) ? scDb : (smoothSc * 0.85f + scDb * 0.15f);
    scTitle.setText (scConnected ? "SC IN" : "SC IN (not connected)", juce::dontSendNotification);
    scMeter.setValueDb (smoothSc, 60.0f);

    if (smoothBoost < 0.05f) smoothBoost = 0.0f;
    if (smoothGr < 0.05f) smoothGr = 0.0f;

    boostMeter.setValueDb (smoothBoost, 30.0f);
    grMeter.setValueDb (smoothGr, 30.0f);
    inMeter.setValueDb (smoothIn, 60.0f);
}

void NewProjectAudioProcessorEditor::setupKnob (juce::Slider& s, juce::Label& l,
                                               const juce::String& text, juce::Colour fill)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 22);
    s.setColour (juce::Slider::rotarySliderFillColourId, fill);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, kMuted);
    s.setColour (juce::Slider::thumbColourId, kText);
    s.setColour (juce::Slider::textBoxTextColourId, kText);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s);

    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, kMuted);
    l.setFont (juce::FontOptions (12.0f));
    l.setInterceptsMouseClicks (false, false); // SC toggles overlap the label strip
    addAndMakeVisible (l);
}

//==============================================================================
void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // Same layout steps as resized() so panels track components when rescaling.
    auto content = getLocalBounds().reduced (20);
    content.removeFromTop (30); // title
    content.removeFromTop (22); // subtitle
    content.removeFromTop (8);
    content.removeFromTop (34); // FFT row
    content.removeFromTop (12);

    auto latencyPanel = content.removeFromTop (108);
    content.removeFromTop (12);

    auto meterPanel = content.removeFromTop (78);
    content.removeFromTop (14);
    auto knobPanel = content;

    g.setColour (kPanel);
    g.fillRoundedRectangle (latencyPanel.toFloat(), 10.0f);
    g.fillRoundedRectangle (meterPanel.toFloat(), 10.0f);
    g.fillRoundedRectangle (knobPanel.toFloat(), 10.0f);
}

void NewProjectAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (20);

    auto titleRow = bounds.removeFromTop (30);
    titleLabel.setBounds (titleRow.removeFromLeft (300));
    infoLabel.setBounds (titleRow);

    subtitleLabel.setBounds (bounds.removeFromTop (22));
    bounds.removeFromTop (8);

    auto fftRow = bounds.removeFromTop (34);
    fftLabel.setBounds (fftRow.removeFromLeft (80));
    const int bw = 78;
    fft512.setBounds  (fftRow.removeFromLeft (bw).reduced (3, 2));
    fft1024.setBounds (fftRow.removeFromLeft (bw).reduced (3, 2));
    fft2048.setBounds (fftRow.removeFromLeft (bw).reduced (3, 2));
    fft4096.setBounds (fftRow.removeFromLeft (bw).reduced (3, 2));

    bounds.removeFromTop (12);

    auto latencyArea = bounds.removeFromTop (108).reduced (14, 10);
    latencySectionLabel.setBounds (latencyArea.removeFromTop (18));
    latencyArea.removeFromTop (4);
    latencyValueLabel.setBounds (latencyArea.removeFromTop (20));
    latencyFormulaLabel.setBounds (latencyArea.removeFromTop (18));
    pdcValueLabel.setBounds (latencyArea.removeFromTop (18));
    dryAlignLabel.setBounds (latencyArea.removeFromTop (18));

    bounds.removeFromTop (12);

    auto meterArea = bounds.removeFromTop (78).reduced (10, 8);
    const int colW = meterArea.getWidth() / 4;
    auto inCol = meterArea.removeFromLeft (colW).reduced (6, 0);
    auto scCol = meterArea.removeFromLeft (colW).reduced (6, 0);
    auto boostCol = meterArea.removeFromLeft (colW).reduced (6, 0);
    auto grCol = meterArea.reduced (6, 0);
    inTitle.setBounds (inCol.removeFromTop (18));
    inMeter.setBounds (inCol);
    scTitle.setBounds (scCol.removeFromTop (18));
    scMeter.setBounds (scCol);
    boostTitle.setBounds (boostCol.removeFromTop (18));
    boostMeter.setBounds (boostCol);
    grTitle.setBounds (grCol.removeFromTop (18));
    grMeter.setBounds (grCol);

    bounds.removeFromTop (14);

    auto knobArea = bounds.reduced (8, 4);
    const int rowH = knobArea.getHeight() / 3;

    auto place = [] (juce::Rectangle<int> cell, juce::Label& lab, juce::Slider& s)
    {
        lab.setBounds (cell.removeFromTop (20));
        s.setBounds (cell.reduced (6, 2));
    };

    // Row 1: STAGE 1 BOOST
    {
        auto row = knobArea.removeFromTop (rowH).reduced (4, 2);
        rowBoostLabel.setBounds (row.getX(), row.getY() - 2, 90, 14);
        boostScButton.setBounds (row.getRight() - 56, row.getY() - 4, 56, 20);
        const int w = row.getWidth() / 5;
        place (row.removeFromLeft (w), lowLabel, lowSlider);
        place (row.removeFromLeft (w), upLabel, upSlider);
        place (row.removeFromLeft (w), boostAtkLabel, boostAtkSlider);
        place (row.removeFromLeft (w), boostRelLabel, boostRelSlider);
        place (row, boostSpeedLabel, boostSpeedSlider);
    }

    // Row 2: STAGE 2 COMP
    {
        auto row = knobArea.removeFromTop (rowH).reduced (4, 2);
        rowGrLabel.setBounds (row.getX(), row.getY() - 2, 90, 14);
        compScButton.setBounds (row.getRight() - 56, row.getY() - 4, 56, 20);
        const int w = row.getWidth() / 5;
        place (row.removeFromLeft (w), highLabel, highSlider);
        place (row.removeFromLeft (w), downLabel, downSlider);
        place (row.removeFromLeft (w), grAtkLabel, grAtkSlider);
        place (row.removeFromLeft (w), grRelLabel, grRelSlider);
        place (row, compSpeedLabel, compSpeedSlider);
    }

    // Row 3: STAGE 3 MIX
    {
        auto row = knobArea.removeFromTop (rowH).reduced (4, 2);
        rowMixLabel.setBounds (row.getX(), row.getY() - 2, 90, 14);
        const int w = row.getWidth() / 5;
        place (row.removeFromLeft (w), mixLabel, mixSlider);
        place (row.removeFromLeft (w), makeupLabel, makeupSlider);
    }
}
