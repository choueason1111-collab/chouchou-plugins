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
    titleLabel.setFont (juce::FontOptions (16.0f).withStyle ("Bold"));
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("The 10 ranges choose which frequencies are processed. Grey = ignored. Blue = expansion, orange = compression.",
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
    boostScButton.toFront (false);
    compScButton.toFront (false);

    static const char* tessLabels[NewProjectAudioProcessor::kTessituras] = {
        "20-60 Hz", "60-120 Hz", "120-250 Hz", "250-500 Hz", "0.5-1 kHz",
        "1-2 kHz", "2-4 kHz", "4-8 kHz", "8-12 kHz", "12-20 kHz"
    };
    for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
    {
        auto& b = tessButtons[(size_t) i];
        b.setButtonText (tessLabels[i]);
        b.setClickingTogglesState (true);
        b.setTooltip ("Turns the expander and the compressor on or off for this band.");
        b.setColour (juce::TextButton::buttonColourId, kDim);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff2d56b));
        b.setColour (juce::TextButton::textColourOffId, kMuted);
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c1f24));
        addAndMakeVisible (b);
        tessAttachments[(size_t) i] = std::make_unique<ButtonAttachment> (
            apvts, "tess" + juce::String (i), b);
    }

    viewButton.onClick = [this]
    {
        showSpectrum = ! showSpectrum;
        viewButton.setButtonText (showSpectrum ? "Wave" : "Spectrum");
        repaint();
    };
    viewButton.setColour (juce::TextButton::buttonColourId, kDim);
    viewButton.setColour (juce::TextButton::textColourOffId, kText);
    addAndMakeVisible (viewButton);

    updateFftButtons();
    updateInfoLabel();
    updateLatencyLabels();
    updateMeters();
    startTimerHz (30);

    setSize (900, 664);
    setResizable (true, true);
    setResizeLimits (820, 580, 1100, 780);

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
    audioProcessor.copyWaveform (waveIn, waveOut, waveGr);
    audioProcessor.copySpectrum (specMag, specGr, specBoost, specSr, specFft);
    const auto summary = compressedSummary();
    const bool active = summary.startsWith ("Expand") || summary.startsWith ("Compress");
    subtitleLabel.setText (active
                               ? summary
                               : "The 10 ranges choose which frequencies are processed. Grey = ignored. Blue = expansion, orange = compression.",
                           juce::dontSendNotification);
    for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
    {
        auto& b = tessButtons[(size_t) i];
        const bool on = b.getToggleState();
        const float expansion = audioProcessor.getTessituraBoostDb (i);
        const float reduction = audioProcessor.getTessituraGrDb (i);
        juce::Colour face = on ? juce::Colour (0xff4a4530) : kDim;
        if (on && expansion > 0.4f)
        {
            const float t = juce::jlimit (0.0f, 1.0f, expansion / 12.0f);
            face = kLow.interpolatedWith (juce::Colours::white, 0.55f * (1.0f - t));
        }
        if (on && reduction > expansion && reduction > 0.4f)
        {
            const float t = juce::jlimit (0.0f, 1.0f, reduction / 12.0f);
            face = kHigh.interpolatedWith (juce::Colours::white, 0.55f * (1.0f - t));
        }
        b.setColour (juce::TextButton::buttonColourId, face);
        b.setColour (juce::TextButton::buttonOnColourId, face);
        b.setColour (juce::TextButton::textColourOnId, on ? juce::Colour (0xff1c1f24) : kMuted);
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
    auto plotArea = area.reduced (10, 16);
    g.setColour (kMuted);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Input", area.getX() + 8, area.getY() + 2, 52, 14, juce::Justification::centredLeft);
    g.setColour (kAccent);
    g.drawText ("Output", area.getX() + 62, area.getY() + 2, 60, 14, juce::Justification::centredLeft);
    g.setColour (kHigh);
    g.drawText ("Compression", area.getX() + 118, area.getY() + 2, 100, 14, juce::Justification::centredLeft);

    if (waveIn.size() < 2 || plotArea.getWidth() < 2)
        return;

    const int n = (int) waveIn.size();
    float peak = 1.0e-4f;
    for (int i = 0; i < n; ++i)
        peak = juce::jmax (peak, std::abs (waveIn[(size_t) i]), std::abs (waveOut[(size_t) i]));

    for (int x = 0; x < plotArea.getWidth(); ++x)
    {
        const int i = juce::jlimit (0, n - 1, x * n / plotArea.getWidth());
        if (waveGr[(size_t) i] > 0.5f)
        {
            g.setColour (kHigh.withAlpha (0.35f));
            g.fillRect (plotArea.getX() + x, plotArea.getY(), 1, plotArea.getHeight());
        }
    }

    auto stroke = [&] (const std::vector<float>& wave, juce::Colour colour)
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
        g.strokePath (path, juce::PathStrokeType (1.3f));
    };

    stroke (waveIn, kMuted);
    stroke (waveOut, kAccent);
}

namespace
{
juce::String hzLabel (float hz)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
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
        static const char* names[] {
            "20-60 Hz", "60-120 Hz", "120-250 Hz", "250-500 Hz", "0.5-1 kHz",
            "1-2 kHz", "2-4 kHz", "4-8 kHz", "8-12 kHz", "12-20 kHz"
        };
        juce::String text;
        for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
        {
            const float gr = audioProcessor.getTessituraGrDb (i);
            if (gr <= 0.5f)
                continue;
            if (text.isNotEmpty())
                text += ", ";
            text += juce::String (names[i]) + " -" + juce::String (gr, 1) + " dB";
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
    auto plot = area.reduced (8, 16);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (kLow);
    g.fillRoundedRectangle ((float) area.getX() + 8.0f, (float) area.getY() + 3.0f, 10.0f, 10.0f, 2.0f);
    g.drawText ("expansion", area.getX() + 22, area.getY() + 1, 70, 14, juce::Justification::centredLeft);
    g.setColour (kHigh);
    g.fillRoundedRectangle ((float) area.getX() + 96.0f, (float) area.getY() + 3.0f, 10.0f, 10.0f, 2.0f);
    g.drawText ("compression", area.getX() + 110, area.getY() + 1, 90, 14, juce::Justification::centredLeft);

    if (specMag.size() < 2 || specFft <= 0 || plot.getWidth() < 2)
        return;

    const double sr = juce::jmax (1.0, specSr);
    auto xOf = [&] (float hz)
    {
        const float t = std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f);
        return (float) plot.getX() + t * (float) plot.getWidth();
    };
    auto hzOfX = [&] (float x)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - (float) plot.getX()) / (float) plot.getWidth());
        return 20.0f * std::pow (1000.0f, t);
    };

    auto shade = [] (juce::Colour deep, float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, db / 12.0f);
        const auto pale = deep.interpolatedWith (juce::Colours::white, 0.78f);
        return pale.interpolatedWith (deep, t).withAlpha (0.35f + 0.6f * t);
    };
    const bool hasBoost = specBoost.size() == specGr.size();
    for (int x = 0; x < plot.getWidth(); ++x)
    {
        const float hz = hzOfX ((float) (plot.getX() + x));
        const int bin = juce::jlimit (0, (int) specGr.size() - 1, (int) std::lround (hz / sr * (double) specFft));
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

    static const float tessEdges[] = {
        20.0f, 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 12000.0f, 20000.0f
    };
    for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
    {
        if (tessButtons[(size_t) i].getToggleState())
            continue;
        const float x0 = xOf (tessEdges[i]);
        const float x1 = xOf (tessEdges[i + 1]);
        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.fillRect (x0, (float) plot.getY(), juce::jmax (0.0f, x1 - x0), (float) plot.getHeight());
    }

    juce::Path path;
    for (int x = 0; x < plot.getWidth(); ++x)
    {
        const float hz = hzOfX ((float) (plot.getX() + x));
        const int bin = juce::jlimit (0, (int) specMag.size() - 1, (int) std::lround (hz / sr * (double) specFft));
        const float db = specMag[(size_t) bin];
        const float t = juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f);
        const float y = (float) plot.getBottom() - t * (float) plot.getHeight();
        if (x == 0)
            path.startNewSubPath ((float) (plot.getX() + x), y);
        else
            path.lineTo ((float) (plot.getX() + x), y);
    }
    g.setColour (kAccent);
    g.strokePath (path, juce::PathStrokeType (1.3f));

    g.setColour (kMuted);
    g.setFont (juce::FontOptions (10.0f));
    for (float mark : { 100.0f, 1000.0f, 10000.0f })
    {
        const int x = (int) xOf (mark);
        g.drawText (hzLabel (mark), x - 24, plot.getBottom() - 12, 48, 12, juce::Justification::centred);
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
            const int x = (int) xOf (peaks[i].hz);
            g.drawText (hzLabel (peaks[i].hz), x - 28, y, 56, 12, juce::Justification::centred);
        }
    };
    drawPeaks (specBoost, kLow, plot.getBottom() - 26);
    drawPeaks (specGr, kHigh, plot.getY());

    if (cursorOn && waveBounds.contains (cursorPos.toInt()))
    {
        const float hz = hzOfX (cursorPos.x);
        const int bin = juce::jlimit (0, (int) specGr.size() - 1, (int) std::lround (hz / sr * (double) specFft));
        const float expansion = hasBoost ? specBoost[(size_t) bin] : 0.0f;
        g.setColour (kText);
        g.drawText (juce::String (hz, 0) + " Hz   expansion +" + juce::String (expansion, 1)
                        + " dB   reduction -" + juce::String (specGr[(size_t) bin], 1) + " dB",
                    plot.getRight() - 320, plot.getY() + 12, 310, 14, juce::Justification::right);
    }
}

void NewProjectAudioProcessorEditor::mouseMove (const juce::MouseEvent& e)
{
    cursorOn = showSpectrum && waveBounds.contains (e.getPosition());
    cursorPos = e.position;
    if (cursorOn)
        repaint();
}

//==============================================================================
void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // Same layout steps as resized() so panels track components when rescaling.
    auto content = getLocalBounds().reduced (12);
    content.removeFromTop (22); // title
    content.removeFromTop (16); // subtitle
    content.removeFromTop (4);
    content.removeFromTop (28); // window size
    content.removeFromTop (4);
    auto wavePanel = content.removeFromTop (96);
    content.removeFromTop (4);
    content.removeFromTop (48); // tessituras
    content.removeFromTop (6);

    auto meterPanel = content.removeFromTop (52);
    content.removeFromTop (6);
    auto knobPanel = content;

    g.setColour (kPanel);
    g.fillRoundedRectangle (meterPanel.toFloat(), 8.0f);
    g.fillRoundedRectangle (knobPanel.toFloat(), 8.0f);
    if (showSpectrum)
        paintSpectrum (g, wavePanel);
    else
        paintWaveform (g, wavePanel);
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
    viewButton.setBounds (fftRow.removeFromLeft (88).reduced (3, 2));

    bounds.removeFromTop (4);
    waveBounds = bounds.removeFromTop (96);
    bounds.removeFromTop (4);
    auto tessArea = bounds.removeFromTop (48);
    auto tessTop = tessArea.removeFromTop (24);
    const int tessW = tessTop.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
        tessButtons[(size_t) i].setBounds (tessTop.removeFromLeft (tessW).reduced (2, 1));
    for (int i = 5; i < NewProjectAudioProcessor::kTessituras; ++i)
        tessButtons[(size_t) i].setBounds (tessArea.removeFromLeft (tessW).reduced (2, 1));
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
        const int w = row.getWidth() / 5;
        place (row.removeFromLeft (w), mixLabel, mixSlider);
        place (row.removeFromLeft (w), makeupLabel, makeupSlider);
    }
}
