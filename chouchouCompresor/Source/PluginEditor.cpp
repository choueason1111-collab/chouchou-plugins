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

    static const char* kTessLabels[NewProjectAudioProcessor::kTessituras] = {
        "1-60 Hz", "60-120 Hz", "120-250 Hz", "250-500 Hz", "0.5-1 kHz",
        "1-2 kHz", "2-4 kHz", "4-8 kHz", "8-20 kHz", "20-96 kHz"
    };

    // Band edges for spectrum shading (1 Hz … 96 kHz).
    static constexpr float kTessEdges[NewProjectAudioProcessor::kTessituras + 1] = {
        1.0f, 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 20000.0f, 96000.0f
    };

    juce::String hzLabel (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz";
        if (hz >= 100.0f)
            return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz, hz < 10.0f ? 1 : 0) + " Hz";
    }

    struct CompPeak { float hz = 0.0f, gr = 0.0f; };

    void compressedPeaks (const std::vector<float>& gr, double sr, int fftN, CompPeak* out, int& count, int maxOut)
    {
        count = 0;
        if (fftN <= 0 || sr <= 0.0 || gr.size() < 4 || out == nullptr || maxOut <= 0)
            return;
        CompPeak found[24];
        int n = 0;
        for (int i = 2; i < (int) gr.size() - 1 && n < 24; ++i)
        {
            const float g = gr[(size_t) i];
            if (g > 0.5f && g >= gr[(size_t) (i - 1)] && g > gr[(size_t) (i + 1)])
                found[n++] = { (float) i * (float) sr / (float) fftN, g };
        }
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b)
                if (found[b].gr > found[a].gr)
                    std::swap (found[a], found[b]);
        for (int i = 0; i < n && count < maxOut; ++i)
        {
            bool close = false;
            for (int k = 0; k < count; ++k)
                if (found[i].hz > 0.0f && out[k].hz > 0.0f
                    && std::abs (std::log (found[i].hz / out[k].hz)) < 0.18f)
                    close = true;
            if (! close)
                out[count++] = found[i];
        }
    }

    void styleTessButton (juce::TextButton& b, juce::Colour onColour, const juce::String& tip)
    {
        b.setClickingTogglesState (true);
        b.setTooltip (tip);
        b.setColour (juce::TextButton::buttonColourId, kDim);
        b.setColour (juce::TextButton::buttonOnColourId, onColour);
        b.setColour (juce::TextButton::textColourOffId, kMuted);
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c1f24));
    }
}

//==============================================================================
NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor (NewProjectAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    titleLabel.setText ("chouchouCompressor", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, kText);
    titleLabel.setFont (juce::FontOptions (16.0f).withStyle ("Bold"));
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("Expand and Compress each have their own 1 Hz-96 kHz bands. Blue = expand, orange = compress.",
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

    boostTitle.setText ("Expander", juce::dontSendNotification);
    boostTitle.setColour (juce::Label::textColourId, kLow);
    boostTitle.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    addAndMakeVisible (boostTitle);

    grTitle.setText ("Reduction", juce::dontSendNotification);
    grTitle.setColour (juce::Label::textColourId, kHigh);
    grTitle.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    addAndMakeVisible (grTitle);

    inTitle.setText ("Input", juce::dontSendNotification);
    inTitle.setColour (juce::Label::textColourId, kAccent);
    inTitle.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    addAndMakeVisible (inTitle);

    rowBoostLabel.setText ("1  Expander", juce::dontSendNotification);
    rowBoostLabel.setColour (juce::Label::textColourId, kLow);
    rowBoostLabel.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    addAndMakeVisible (rowBoostLabel);

    rowGrLabel.setText ("2  Compression", juce::dontSendNotification);
    rowGrLabel.setColour (juce::Label::textColourId, kHigh);
    rowGrLabel.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    addAndMakeVisible (rowGrLabel);

    rowMixLabel.setText ("3  Mix", juce::dontSendNotification);
    rowMixLabel.setColour (juce::Label::textColourId, kAccent);
    rowMixLabel.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    addAndMakeVisible (rowMixLabel);

    tessExpLabel.setText ("Expand bands", juce::dontSendNotification);
    tessExpLabel.setColour (juce::Label::textColourId, kLow);
    tessExpLabel.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    addAndMakeVisible (tessExpLabel);

    tessCompLabel.setText ("Compress bands", juce::dontSendNotification);
    tessCompLabel.setColour (juce::Label::textColourId, kHigh);
    tessCompLabel.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    addAndMakeVisible (tessCompLabel);

    scTitle.setText ("Sidechain", juce::dontSendNotification);
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
        b.setTooltip ("This stage listens to the sidechain bus");
        addAndMakeVisible (b);
    };

    styleSc (boostScButton, kLow);
    styleSc (compScButton, kHigh);

    autoGainButton.setColour (juce::ToggleButton::textColourId, kAccent);
    autoGainButton.setColour (juce::ToggleButton::tickColourId, kAccent);
    autoGainButton.setColour (juce::ToggleButton::tickDisabledColourId, kMuted);
    autoGainButton.setTooltip ("Matches wet level to the latency-aligned dry signal");
    addAndMakeVisible (autoGainButton);

    setupKnob (lowSlider,         lowLabel,         "Threshold", kLow);
    setupKnob (upSlider,          upLabel,          "Ratio",  kLow);
    setupKnob (boostAtkSlider,    boostAtkLabel,    "Attack", kLow);
    setupKnob (boostRelSlider,    boostRelLabel,    "Release",    kLow);
    setupKnob (boostSpeedSlider,  boostSpeedLabel,  "Speed",  kLow);

    setupKnob (highSlider,        highLabel,        "Threshold",  kHigh);
    setupKnob (downSlider,        downLabel,        "Ratio",   kHigh);
    setupKnob (grAtkSlider,       grAtkLabel,       "Attack",  kHigh);
    setupKnob (grRelSlider,       grRelLabel,       "Release",     kHigh);
    setupKnob (compSpeedSlider,   compSpeedLabel,   "Speed",   kHigh);

    setupKnob (mixSlider,         mixLabel,         "Mix",          kAccent);
    setupKnob (makeupSlider,      makeupLabel,      "Output",       kAccent);

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

    fftLabel.setText ("Window size", juce::dontSendNotification);
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
    autoGainAttachment   = std::make_unique<ButtonAttachment> (apvts, "autoGain",   autoGainButton);
    boostScButton.toFront (false);
    compScButton.toFront (false);

    for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
    {
        auto& expB = tessExpButtons[(size_t) i];
        expB.setButtonText (kTessLabels[i]);
        styleTessButton (expB, kLow.withBrightness (0.85f),
                         "Turns the expander on or off for this band only.");
        addAndMakeVisible (expB);
        tessExpAttachments[(size_t) i] = std::make_unique<ButtonAttachment> (
            apvts, "tessExp" + juce::String (i), expB);

        auto& compB = tessCompButtons[(size_t) i];
        compB.setButtonText (kTessLabels[i]);
        styleTessButton (compB, kHigh.withBrightness (0.85f),
                         "Turns the compressor on or off for this band only.");
        addAndMakeVisible (compB);
        tessCompAttachments[(size_t) i] = std::make_unique<ButtonAttachment> (
            apvts, "tessComp" + juce::String (i), compB);
    }

    viewButton.onClick = [this] { cycleViewMode(); };
    viewButton.setColour (juce::TextButton::buttonColourId, kDim);
    viewButton.setColour (juce::TextButton::textColourOffId, kText);
    viewButton.setTooltip ("Cycle Spectrum / Wave / Both");
    addAndMakeVisible (viewButton);

    updateFftButtons();
    updateInfoLabel();
    updateLatencyLabels();
    updateMeters();
    startTimerHz (30);

    setSize (960, 780);
    setResizable (true, true);
    setResizeLimits (860, 700, 1280, 980);

    latencySectionLabel.setVisible (false);
    latencyValueLabel.setVisible (false);
    latencyFormulaLabel.setVisible (false);
    pdcValueLabel.setVisible (false);
    dryAlignLabel.setVisible (false);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
    stopTimer();
}

void NewProjectAudioProcessorEditor::cycleViewMode()
{
    switch (viewMode)
    {
        case ViewMode::spectrum: viewMode = ViewMode::wave;     viewButton.setButtonText ("Both");     break;
        case ViewMode::wave:     viewMode = ViewMode::both;     viewButton.setButtonText ("Spectrum"); break;
        case ViewMode::both:     viewMode = ViewMode::spectrum; viewButton.setButtonText ("Wave");     break;
    }
    resized();
    repaint();
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

float NewProjectAudioProcessorEditor::spectrumMinHz() const noexcept
{
    return NewProjectAudioProcessor::kSpectrumMinHz;
}

float NewProjectAudioProcessorEditor::spectrumMaxHz() const noexcept
{
    // Always show the full 1 Hz … 96 kHz axis. Content above the host Nyquist
    // is drawn empty / shaded so the tessitura layout stays consistent.
    return NewProjectAudioProcessor::kSpectrumMaxHz;
}

void NewProjectAudioProcessorEditor::timerCallback()
{
    updateFftButtons();
    updateInfoLabel();
    updateLatencyLabels();
    updateMeters();
    audioProcessor.copyWaveform (waveIn, waveOut, waveGr);
    audioProcessor.copySpectrum (specMag, specGr, specBoost, specSr, specFft);
    const auto summary = compressedSummary();
    const bool active = summary.startsWith ("Expand") || summary.startsWith ("Compress");
    subtitleLabel.setText (active
                               ? summary
                               : "Expand and Compress each have their own 1 Hz-96 kHz bands. Blue = expand, orange = compress.",
                           juce::dontSendNotification);

    auto colourBand = [] (juce::TextButton& b, bool on, float activityDb, juce::Colour base, juce::Colour idleOn)
    {
        juce::Colour face = on ? idleOn : kDim;
        if (on && activityDb > 0.4f)
        {
            const float t = juce::jlimit (0.0f, 1.0f, activityDb / 12.0f);
            face = base.interpolatedWith (juce::Colours::white, 0.55f * (1.0f - t));
        }
        b.setColour (juce::TextButton::buttonColourId, face);
        b.setColour (juce::TextButton::buttonOnColourId, face);
        b.setColour (juce::TextButton::textColourOnId, on ? juce::Colour (0xff1c1f24) : kMuted);
    };

    for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
    {
        colourBand (tessExpButtons[(size_t) i],
                    tessExpButtons[(size_t) i].getToggleState(),
                    audioProcessor.getTessituraBoostDb (i),
                    kLow, juce::Colour (0xff2f4050));
        colourBand (tessCompButtons[(size_t) i],
                    tessCompButtons[(size_t) i].getToggleState(),
                    audioProcessor.getTessituraGrDb (i),
                    kHigh, juce::Colour (0xff4a3530));
    }
    repaint();
}

void NewProjectAudioProcessorEditor::updateInfoLabel()
{
    const double sr = audioProcessor.getCurrentSampleRate();
    const int hop   = audioProcessor.getHopSize();
    const float hopMs = audioProcessor.getHopMilliseconds();

    const int latency = audioProcessor.getReportedLatencySamples();
    infoLabel.setText ("Sample rate " + juce::String (sr, 0) + " Hz    hop "
                       + juce::String (hop) + " (" + juce::String (hopMs, 1) + " ms)    latency "
                       + juce::String (latency) + " smp",
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
    scTitle.setText (scConnected ? "Sidechain" : "Sidechain off", juce::dontSendNotification);
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
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 18);
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

void NewProjectAudioProcessorEditor::paintWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    auto plotArea = area.reduced (10, 18);

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (kMuted);
    g.drawText ("Input", area.getX() + 8, area.getY() + 2, 46, 14, juce::Justification::centredLeft);
    g.setColour (kAccent);
    g.drawText ("Output", area.getX() + 56, area.getY() + 2, 54, 14, juce::Justification::centredLeft);
    g.setColour (kHigh);
    g.drawText ("Compress", area.getX() + 114, area.getY() + 2, 70, 14, juce::Justification::centredLeft);
    g.setColour (kLow);
    g.drawText ("Expand zone", area.getX() + 188, area.getY() + 2, 90, 14, juce::Justification::centredLeft);

    // Grid + zero line
    g.setColour (kDim.withAlpha (0.65f));
    for (int i = 1; i < 4; ++i)
    {
        const float y = (float) plotArea.getY() + (float) plotArea.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine ((int) y, (float) plotArea.getX(), (float) plotArea.getRight());
    }
    g.setColour (kMuted.withAlpha (0.55f));
    g.drawHorizontalLine (plotArea.getCentreY(), (float) plotArea.getX(), (float) plotArea.getRight());

    if (waveIn.size() < 2 || plotArea.getWidth() < 2)
        return;

    const int n = (int) waveIn.size();
    float peak = 1.0e-4f;
    for (int i = 0; i < n; ++i)
        peak = juce::jmax (peak, std::abs (waveIn[(size_t) i]), std::abs (waveOut[(size_t) i]));

    // Expansion hint: where output > input envelope
    for (int x = 0; x < plotArea.getWidth(); ++x)
    {
        const int i = juce::jlimit (0, n - 1, x * n / plotArea.getWidth());
        const float inAbs = std::abs (waveIn[(size_t) i]);
        const float outAbs = std::abs (waveOut[(size_t) i]);
        if (outAbs > inAbs * 1.08f && waveGr[(size_t) i] < 0.3f)
        {
            g.setColour (kLow.withAlpha (0.22f));
            g.fillRect (plotArea.getX() + x, plotArea.getY(), 1, plotArea.getHeight());
        }
        if (waveGr[(size_t) i] > 0.5f)
        {
            const float t = juce::jlimit (0.0f, 1.0f, waveGr[(size_t) i] / 12.0f);
            g.setColour (kHigh.withAlpha (0.18f + 0.35f * t));
            g.fillRect (plotArea.getX() + x, plotArea.getY(), 1, plotArea.getHeight());
        }
    }

    auto stroke = [&] (const std::vector<float>& wave, juce::Colour colour, float thickness)
    {
        juce::Path path;
        for (int x = 0; x < plotArea.getWidth(); ++x)
        {
            const int i = juce::jlimit (0, n - 1, x * n / plotArea.getWidth());
            const float y = (float) plotArea.getCentreY()
                            - wave[(size_t) i] / peak * (float) plotArea.getHeight() * 0.45f;
            const float px = (float) (plotArea.getX() + x);
            if (x == 0)
                path.startNewSubPath (px, y);
            else
                path.lineTo (px, y);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness));
    };

    stroke (waveIn, kMuted.withAlpha (0.85f), 1.1f);
    stroke (waveOut, kAccent, 1.6f);

    if (cursorOn && waveBounds.contains (cursorPos.toInt()) && viewMode != ViewMode::spectrum)
    {
        const int i = juce::jlimit (0, n - 1,
                                    (int) ((cursorPos.x - (float) plotArea.getX())
                                           / (float) juce::jmax (1, plotArea.getWidth()) * (float) n));
        g.setColour (kText.withAlpha (0.7f));
        g.drawVerticalLine ((int) cursorPos.x, (float) plotArea.getY(), (float) plotArea.getBottom());
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("in " + juce::String (waveIn[(size_t) i], 3)
                        + "  out " + juce::String (waveOut[(size_t) i], 3)
                        + "  GR " + juce::String (waveGr[(size_t) i], 1) + " dB",
                    plotArea.getRight() - 280, plotArea.getY() + 2, 270, 12, juce::Justification::right);
    }
}

juce::String NewProjectAudioProcessorEditor::compressedSummary() const
{
    auto peaksText = [this] (const std::vector<float>& src, const char* title, const char* sign)
    {
        CompPeak peaks[4];
        int count = 0;
        compressedPeaks (src, specSr, specFft, peaks, count, 4);
        juce::String text;
        for (int i = 0; i < count; ++i)
        {
            if (text.isNotEmpty())
                text += ", ";
            text += hzLabel (peaks[i].hz) + " " + sign + juce::String (peaks[i].gr, 1) + " dB";
        }
        return text.isEmpty() ? juce::String() : juce::String (title) + text;
    };

    const auto expansion = peaksText (specBoost, "Expand: ", "+");
    juce::String compression = peaksText (specGr, "Compress: ", "-");
    if (compression.isEmpty())
    {
        juce::String text;
        for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
        {
            const float gr = audioProcessor.getTessituraGrDb (i);
            if (gr <= 0.5f)
                continue;
            if (text.isNotEmpty())
                text += ", ";
            text += juce::String (kTessLabels[i]) + " -" + juce::String (gr, 1) + " dB";
        }
        if (text.isNotEmpty())
            compression = "Compress: " + text;
    }
    if (expansion.isEmpty() && compression.isEmpty())
        return {};
    if (expansion.isNotEmpty() && compression.isNotEmpty())
        return expansion + "    " + compression;
    return expansion.isNotEmpty() ? expansion : compression;
}

void NewProjectAudioProcessorEditor::paintSpectrum (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    auto plot = area.reduced (8, 18);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (kLow);
    g.fillRoundedRectangle ((float) area.getX() + 8.0f, (float) area.getY() + 3.0f, 10.0f, 10.0f, 2.0f);
    g.drawText ("expansion", area.getX() + 22, area.getY() + 1, 70, 14, juce::Justification::centredLeft);
    g.setColour (kHigh);
    g.fillRoundedRectangle ((float) area.getX() + 96.0f, (float) area.getY() + 3.0f, 10.0f, 10.0f, 2.0f);
    g.drawText ("compression", area.getX() + 110, area.getY() + 1, 90, 14, juce::Justification::centredLeft);
    g.setColour (kAccent);
    g.drawText ("magnitude", area.getX() + 210, area.getY() + 1, 80, 14, juce::Justification::centredLeft);
    g.setColour (kMuted);
    g.drawText ("1 Hz - 96 kHz", area.getRight() - 110, area.getY() + 1, 100, 14, juce::Justification::centredRight);

    if (specMag.size() < 2 || specFft <= 0 || plot.getWidth() < 2)
        return;

    const double sr = juce::jmax (1.0, specSr);
    const float fMin = spectrumMinHz();
    const float fMax = juce::jmax (fMin * 2.0f, spectrumMaxHz());
    const float nyquist = (float) (0.5 * sr);
    const float logSpan = std::log (fMax / fMin);

    auto xOf = [&] (float hz)
    {
        const float clamped = juce::jlimit (fMin, fMax, hz);
        const float t = std::log (clamped / fMin) / logSpan;
        return (float) plot.getX() + t * (float) plot.getWidth();
    };
    auto hzOfX = [&] (float x)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - (float) plot.getX()) / (float) plot.getWidth());
        return fMin * std::pow (fMax / fMin, t);
    };

    // dB grid
    g.setColour (kDim.withAlpha (0.55f));
    for (float db : { -60.0f, -40.0f, -20.0f })
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f);
        const float y = (float) plot.getBottom() - t * (float) plot.getHeight();
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
    }

    auto shade = [] (juce::Colour deep, float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, db / 12.0f);
        const auto pale = deep.interpolatedWith (juce::Colours::white, 0.78f);
        return pale.interpolatedWith (deep, t).withAlpha (0.35f + 0.6f * t);
    };
    const bool hasBoost = specBoost.size() == specGr.size();
    const int maxBin = juce::jmax (0, (int) specGr.size() - 1);
    for (int x = 0; x < plot.getWidth(); ++x)
    {
        const float hz = hzOfX ((float) (plot.getX() + x));
        // Do not mirror the Nyquist bin across the 24 kHz…96 kHz axis.
        if (hz > nyquist + 0.5f)
            continue;

        const int bin = juce::jlimit (0, maxBin, (int) std::lround (hz / sr * (double) specFft));
        const float expansion = hasBoost ? specBoost[(size_t) bin] : 0.0f;
        const float reduction = specGr[(size_t) bin];
        if (expansion > 0.4f)
        {
            const float t = juce::jlimit (0.0f, 1.0f, expansion / 12.0f);
            const int h = juce::jmax (3, (int) std::round ((0.2f + 0.8f * t) * (float) plot.getHeight()));
            g.setColour (shade (kLow, expansion));
            g.fillRect (plot.getX() + x, plot.getBottom() - h, 1, h);
        }
        if (reduction > 0.4f)
        {
            const float t = juce::jlimit (0.0f, 1.0f, reduction / 12.0f);
            const int h = juce::jmax (3, (int) std::round ((0.2f + 0.8f * t) * (float) plot.getHeight()));
            g.setColour (shade (kHigh, reduction));
            g.fillRect (plot.getX() + x, plot.getY(), 1, h);
        }
    }

    // Per-band mute overlays: expand-off (top strip) / compress-off (bottom strip)
    for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
    {
        const float lo = juce::jmax (fMin, kTessEdges[i]);
        const float hi = juce::jmin (fMax, kTessEdges[i + 1]);
        if (hi <= lo)
            continue;
        const float x0 = xOf (lo);
        const float x1 = xOf (hi);
        const float w = juce::jmax (0.0f, x1 - x0);
        if (! tessExpButtons[(size_t) i].getToggleState())
        {
            g.setColour (kLow.withAlpha (0.12f));
            g.fillRect (x0, (float) plot.getY(), w, (float) plot.getHeight() * 0.5f);
        }
        if (! tessCompButtons[(size_t) i].getToggleState())
        {
            g.setColour (kHigh.withAlpha (0.12f));
            g.fillRect (x0, (float) plot.getCentreY(), w, (float) plot.getHeight() * 0.5f);
        }
        if (! tessExpButtons[(size_t) i].getToggleState()
            && ! tessCompButtons[(size_t) i].getToggleState())
        {
            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.fillRect (x0, (float) plot.getY(), w, (float) plot.getHeight());
        }
        g.setColour (kDim.withAlpha (0.8f));
        g.drawVerticalLine ((int) x0, (float) plot.getY(), (float) plot.getBottom());
    }

    juce::Path fillPath, linePath;
    bool started = false;
    float lastAudibleX = (float) plot.getX();
    for (int x = 0; x < plot.getWidth(); ++x)
    {
        const float hz = hzOfX ((float) (plot.getX() + x));
        if (hz > nyquist + 0.5f)
            break;

        const int bin = juce::jlimit (0, (int) specMag.size() - 1, (int) std::lround (hz / sr * (double) specFft));
        const float db = specMag[(size_t) bin];
        const float t = juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f);
        const float px = (float) (plot.getX() + x);
        const float y = (float) plot.getBottom() - t * (float) plot.getHeight();
        lastAudibleX = px;
        if (! started)
        {
            fillPath.startNewSubPath (px, (float) plot.getBottom());
            fillPath.lineTo (px, y);
            linePath.startNewSubPath (px, y);
            started = true;
        }
        else
        {
            fillPath.lineTo (px, y);
            linePath.lineTo (px, y);
        }
    }
    if (started)
    {
        fillPath.lineTo (lastAudibleX, (float) plot.getBottom());
        fillPath.closeSubPath();
        g.setColour (kAccent.withAlpha (0.18f));
        g.fillPath (fillPath);
        g.setColour (kAccent);
        g.strokePath (linePath, juce::PathStrokeType (1.5f));
    }

    // Region above host Nyquist stays on the 1…96 kHz axis but is inactive.
    if (nyquist < fMax - 1.0f)
    {
        const float xN = xOf (nyquist);
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRect (xN, (float) plot.getY(), (float) plot.getRight() - xN, (float) plot.getHeight());
        g.setColour (kMuted.withAlpha (0.9f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("above Nyquist (" + hzLabel (nyquist) + ")",
                    (int) xN + 4, plot.getY() + 2, 160, 12, juce::Justification::centredLeft);
        g.setColour (kMuted.withAlpha (0.7f));
        g.drawVerticalLine ((int) xN, (float) plot.getY(), (float) plot.getBottom());
    }

    g.setColour (kMuted);
    g.setFont (juce::FontOptions (10.0f));
    for (float mark : { 1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f, 96000.0f })
    {
        if (mark < fMin * 0.99f || mark > fMax * 1.01f)
            continue;
        const int x = (int) xOf (mark);
        g.drawVerticalLine (x, (float) plot.getBottom() - 4.0f, (float) plot.getBottom());
        g.drawText (hzLabel (mark), x - 28, plot.getBottom() - 14, 56, 12, juce::Justification::centred);
    }

    auto drawPeaks = [&] (const std::vector<float>& src, juce::Colour colour, int y)
    {
        if (src.size() < 4)
            return;
        CompPeak peaks[4];
        int peakCount = 0;
        compressedPeaks (src, sr, specFft, peaks, peakCount, 4);
        g.setColour (colour);
        g.setFont (juce::FontOptions (10.0f));
        for (int i = 0; i < peakCount && i < 3; ++i)
        {
            if (peaks[i].hz < fMin || peaks[i].hz > juce::jmin (fMax, nyquist))
                continue;
            const int x = (int) xOf (peaks[i].hz);
            g.drawText (hzLabel (peaks[i].hz), x - 28, y, 56, 12, juce::Justification::centred);
        }
    };
    drawPeaks (specBoost, kLow, plot.getBottom() - 28);
    drawPeaks (specGr, kHigh, plot.getY());

    if (cursorOn && spectrumBounds.contains (cursorPos.toInt()))
    {
        const float hz = hzOfX (cursorPos.x);
        g.setColour (kText.withAlpha (0.55f));
        g.drawVerticalLine ((int) cursorPos.x, (float) plot.getY(), (float) plot.getBottom());
        g.setColour (kText);
        g.setFont (juce::FontOptions (10.0f));
        if (hz > nyquist + 0.5f)
        {
            g.drawText (hzLabel (hz) + "  (above Nyquist " + hzLabel (nyquist) + ")",
                        plot.getRight() - 360, plot.getY() + 12, 350, 14, juce::Justification::right);
        }
        else
        {
            const int bin = juce::jlimit (0, maxBin, (int) std::lround (hz / sr * (double) specFft));
            const float expansion = hasBoost ? specBoost[(size_t) bin] : 0.0f;
            const float magDb = bin < (int) specMag.size() ? specMag[(size_t) bin] : -120.0f;
            g.drawText (hzLabel (hz) + "   mag " + juce::String (magDb, 1) + " dB   exp +"
                            + juce::String (expansion, 1) + " dB   GR -"
                            + juce::String (specGr[(size_t) bin], 1) + " dB",
                        plot.getRight() - 360, plot.getY() + 12, 350, 14, juce::Justification::right);
        }
    }
}

void NewProjectAudioProcessorEditor::mouseMove (const juce::MouseEvent& e)
{
    cursorOn = spectrumBounds.contains (e.getPosition()) || waveBounds.contains (e.getPosition());
    cursorPos = e.position;
    if (cursorOn)
        repaint();
}

void NewProjectAudioProcessorEditor::mouseExit (const juce::MouseEvent&)
{
    cursorOn = false;
    repaint();
}

//==============================================================================
void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    auto content = getLocalBounds().reduced (12);
    content.removeFromTop (22); // title
    content.removeFromTop (16); // subtitle
    content.removeFromTop (4);
    content.removeFromTop (28); // window size
    content.removeFromTop (4);

    const int vizH = viewMode == ViewMode::both ? 220 : 120;
    auto vizPanel = content.removeFromTop (vizH);
    content.removeFromTop (4);
    content.removeFromTop (56); // tessituras (2 rows + labels)
    content.removeFromTop (6);

    auto meterPanel = content.removeFromTop (52);
    content.removeFromTop (6);
    auto knobPanel = content;

    g.setColour (kPanel);
    g.fillRoundedRectangle (meterPanel.toFloat(), 8.0f);
    g.fillRoundedRectangle (knobPanel.toFloat(), 8.0f);

    if (viewMode == ViewMode::spectrum)
    {
        paintSpectrum (g, vizPanel);
    }
    else if (viewMode == ViewMode::wave)
    {
        paintWaveform (g, vizPanel);
    }
    else
    {
        auto top = vizPanel.removeFromTop (vizPanel.getHeight() / 2).reduced (0, 1);
        auto bottom = vizPanel.reduced (0, 1);
        paintSpectrum (g, top);
        paintWaveform (g, bottom);
    }
}

void NewProjectAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (12);

    auto titleRow = bounds.removeFromTop (22);
    titleLabel.setBounds (titleRow.removeFromLeft (240));
    infoLabel.setBounds (titleRow);

    subtitleLabel.setBounds (bounds.removeFromTop (16));
    bounds.removeFromTop (4);

    auto fftRow = bounds.removeFromTop (28);
    fftLabel.setBounds (fftRow.removeFromLeft (140));
    const int bw = 72;
    fft512.setBounds  (fftRow.removeFromLeft (bw).reduced (3, 2));
    fft1024.setBounds (fftRow.removeFromLeft (bw).reduced (3, 2));
    fft2048.setBounds (fftRow.removeFromLeft (bw).reduced (3, 2));
    fft4096.setBounds (fftRow.removeFromLeft (bw).reduced (3, 2));
    viewButton.setBounds (fftRow.removeFromLeft (96).reduced (3, 2));

    bounds.removeFromTop (4);
    const int vizH = viewMode == ViewMode::both ? 220 : 120;
    auto vizArea = bounds.removeFromTop (vizH);
    if (viewMode == ViewMode::spectrum)
    {
        spectrumBounds = vizArea;
        waveBounds = {};
    }
    else if (viewMode == ViewMode::wave)
    {
        waveBounds = vizArea;
        spectrumBounds = {};
    }
    else
    {
        spectrumBounds = vizArea.removeFromTop (vizArea.getHeight() / 2).reduced (0, 1);
        waveBounds = vizArea.reduced (0, 1);
    }
    bounds.removeFromTop (4);

    auto tessArea = bounds.removeFromTop (56);
    {
        auto expRow = tessArea.removeFromTop (28);
        tessExpLabel.setBounds (expRow.removeFromLeft (108));
        const int tessW = expRow.getWidth() / NewProjectAudioProcessor::kTessituras;
        for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
            tessExpButtons[(size_t) i].setBounds (expRow.removeFromLeft (tessW).reduced (1, 2));
    }
    {
        auto compRow = tessArea;
        tessCompLabel.setBounds (compRow.removeFromLeft (108));
        const int tessW = compRow.getWidth() / NewProjectAudioProcessor::kTessituras;
        for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
            tessCompButtons[(size_t) i].setBounds (compRow.removeFromLeft (tessW).reduced (1, 2));
    }
    bounds.removeFromTop (6);

    auto meterArea = bounds.removeFromTop (52).reduced (8, 4);
    const int colW = meterArea.getWidth() / 4;
    auto inCol = meterArea.removeFromLeft (colW).reduced (6, 0);
    auto scCol = meterArea.removeFromLeft (colW).reduced (6, 0);
    auto boostCol = meterArea.removeFromLeft (colW).reduced (6, 0);
    auto grCol = meterArea.reduced (6, 0);
    inTitle.setBounds (inCol.removeFromTop (16));
    inMeter.setBounds (inCol);
    scTitle.setBounds (scCol.removeFromTop (16));
    scMeter.setBounds (scCol);
    boostTitle.setBounds (boostCol.removeFromTop (16));
    boostMeter.setBounds (boostCol);
    grTitle.setBounds (grCol.removeFromTop (16));
    grMeter.setBounds (grCol);

    bounds.removeFromTop (6);

    auto knobArea = bounds.reduced (6, 2);
    const int rowH = knobArea.getHeight() / 3;

    auto place = [] (juce::Rectangle<int> cell, juce::Label& lab, juce::Slider& s)
    {
        lab.setBounds (cell.removeFromTop (16));
        s.setBounds (cell.reduced (2, 0));
    };

    // Row 1: expander. Title and sidechain stay above the knobs.
    {
        auto row = knobArea.removeFromTop (rowH).reduced (4, 2);
        auto head = row.removeFromTop (18);
        rowBoostLabel.setBounds (head.removeFromLeft (160));
        boostScButton.setBounds (head.removeFromRight (100).reduced (0, 1));
        const int w = row.getWidth() / 5;
        place (row.removeFromLeft (w), lowLabel, lowSlider);
        place (row.removeFromLeft (w), upLabel, upSlider);
        place (row.removeFromLeft (w), boostAtkLabel, boostAtkSlider);
        place (row.removeFromLeft (w), boostRelLabel, boostRelSlider);
        place (row, boostSpeedLabel, boostSpeedSlider);
    }

    // Row 2: compression
    {
        auto row = knobArea.removeFromTop (rowH).reduced (4, 2);
        auto head = row.removeFromTop (18);
        rowGrLabel.setBounds (head.removeFromLeft (160));
        compScButton.setBounds (head.removeFromRight (100).reduced (0, 1));
        const int w = row.getWidth() / 5;
        place (row.removeFromLeft (w), highLabel, highSlider);
        place (row.removeFromLeft (w), downLabel, downSlider);
        place (row.removeFromLeft (w), grAtkLabel, grAtkSlider);
        place (row.removeFromLeft (w), grRelLabel, grRelSlider);
        place (row, compSpeedLabel, compSpeedSlider);
    }

    // Row 3: mix. Same knob width as the rows above.
    {
        auto row = knobArea.removeFromTop (rowH).reduced (4, 2);
        auto head = row.removeFromTop (18);
        rowMixLabel.setBounds (head.removeFromLeft (160));
        autoGainButton.setBounds (head.removeFromRight (100).reduced (0, 1));
        const int w = row.getWidth() / 5;
        place (row.removeFromLeft (w), mixLabel, mixSlider);
        place (row.removeFromLeft (w), makeupLabel, makeupSlider);
    }
}
