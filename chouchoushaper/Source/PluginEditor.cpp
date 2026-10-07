/*
  ==============================================================================

    chouchouShaper — editor

  ==============================================================================
*/

#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg       { 0xff1e2126 };
    const juce::Colour kPanel    { 0xff292d33 };
    const juce::Colour kGrid     { 0xff3a3f46 };
    const juce::Colour kText     { 0xffe6e6e6 };
    const juce::Colour kDim      { 0xff8a9099 };
    const juce::Colour kTime     { 0xfff0a35e };
    const juce::Colour kSpectral { 0xff6fb7e8 };
    const juce::Colour kSide     { 0xffb58ce0 };
    const juce::Colour kOut      { 0xff7ec8a3 };

    constexpr float kDisplayDb = 24.0f;

    float dbToY (float db, juce::Rectangle<float> r, float rangeDb = kDisplayDb)
    {
        const float t = juce::jlimit (-1.0f, 1.0f, db / rangeDb);
        return r.getCentreY() - t * r.getHeight() * 0.5f;
    }

    void drawDbGrid (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setFont (juce::FontOptions (10.0f));
        for (float db : { -24.0f, -12.0f, 0.0f, 12.0f, 24.0f })
        {
            const float y = dbToY (db, r);
            g.setColour (db == 0.0f ? kDim.withAlpha (0.6f) : kGrid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (kDim);
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db),
                        juce::Rectangle<float> (r.getX() + 2.0f, y - 11.0f, 30.0f, 10.0f),
                        juce::Justification::left);
        }
    }
}

//==============================================================================
void TimeTraceDisplay::push (float minDb, float maxDb, float inputDb)
{
    mins[(size_t) head] = minDb;
    maxs[(size_t) head] = maxDb;
    inputs[(size_t) head] = inputDb;
    head = (head + 1) % kHistory;
    repaint();
}

void TimeTraceDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kBg);
    g.fillRoundedRectangle (r, 4.0f);
    r = r.reduced (2.0f);

    const float colW = r.getWidth() / (float) kHistory;

    // Input level (-60..0 dBFS) as faint bars from the bottom.
    g.setColour (kDim.withAlpha (0.18f));
    for (int i = 0; i < kHistory; ++i)
    {
        const int idx = (head + i) % kHistory;
        const float t = juce::jlimit (0.0f, 1.0f, (inputs[(size_t) idx] + 60.0f) / 60.0f);
        const float h = t * r.getHeight();
        g.fillRect (r.getX() + (float) i * colW, r.getBottom() - h, colW + 0.5f, h);
    }

    drawDbGrid (g, r);

    // Gain range per column: band between min and max applied gain.
    g.setColour (kTime.withAlpha (0.85f));
    for (int i = 0; i < kHistory; ++i)
    {
        const int idx = (head + i) % kHistory;
        const float y0 = dbToY (maxs[(size_t) idx], r);
        const float y1 = dbToY (mins[(size_t) idx], r);
        g.fillRect (r.getX() + (float) i * colW, y0, colW + 0.5f, juce::jmax (1.5f, y1 - y0));
    }

    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("gain dB (orange)  /  input level (grey)", r.reduced (4.0f), juce::Justification::bottomRight);
}

//==============================================================================
void SpectralGainDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kBg);
    g.fillRoundedRectangle (r, 4.0f);
    r = r.reduced (2.0f);

    const auto& stage = proc.getSpectralStage();
    const double sr = juce::jmax (1.0, proc.getCurrentSampleRate());
    const int nb = stage.numBins();
    const float binHz = (float) (sr / (double) stage.getFftSize());
    const float fMin = 20.0f, fMax = (float) juce::jmin (20000.0, sr * 0.5);

    auto freqToX = [&] (float f)
    {
        return r.getX() + r.getWidth() * std::log (f / fMin) / std::log (fMax / fMin);
    };
    auto xToBin = [&] (float x)
    {
        const float f = fMin * std::pow (fMax / fMin, (x - r.getX()) / r.getWidth());
        return juce::jlimit (0, nb - 1, juce::roundToInt (f / binHz));
    };

    // Processed range highlight.
    {
        const float x0 = freqToX (juce::jlimit (fMin, fMax, loHz));
        const float x1 = freqToX (juce::jlimit (fMin, fMax, hiHz));
        g.setColour (kSpectral.withAlpha (0.06f));
        g.fillRect (x0, r.getY(), juce::jmax (0.0f, x1 - x0), r.getHeight());
    }

    // Frequency grid.
    g.setFont (juce::FontOptions (10.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        if (f >= fMax) continue;
        const float x = freqToX (f);
        g.setColour (kGrid);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (kDim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x + 2.0f, r.getBottom() - 12.0f, 30.0f, 10.0f),
                    juce::Justification::left);
    }

    // Slow-envelope spectrum (-100..0 dB) as a faint fill.
    {
        juce::Path level;
        level.startNewSubPath (r.getX(), r.getBottom());
        for (float x = r.getX(); x <= r.getRight(); x += 2.0f)
        {
            const float db = stage.getMeterLevelDb (xToBin (x));
            const float t = juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 100.0f);
            level.lineTo (x, r.getBottom() - t * r.getHeight());
        }
        level.lineTo (r.getRight(), r.getBottom());
        level.closeSubPath();
        g.setColour (kDim.withAlpha (0.18f));
        g.fillPath (level);
    }

    drawDbGrid (g, r);

    // Gain curve.
    juce::Path curve;
    bool started = false;
    for (float x = r.getX(); x <= r.getRight(); x += 1.0f)
    {
        const float y = dbToY (stage.getMeterGainDb (xToBin (x)), r);
        if (! started) { curve.startNewSubPath (x, y); started = true; }
        else           curve.lineTo (x, y);
    }
    g.setColour (kSpectral);
    g.strokePath (curve, juce::PathStrokeType (1.6f));
}

//==============================================================================
void LevelBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kGrid);
    g.fillRoundedRectangle (r, 3.0f);

    const float t = juce::jlimit (0.0f, 1.0f, (valueDb + 60.0f) / 60.0f);
    g.setColour ((isActive ? colour : kDim).withAlpha (isActive ? 0.9f : 0.4f));
    g.fillRoundedRectangle (r.withWidth (r.getWidth() * t), 3.0f);

    g.setColour (kText);
    g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    const juce::String value = valueDb <= -119.0f ? juce::String ("-inf") : juce::String (valueDb, 1);
    g.drawText (caption + "  " + value + " dB", r.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
}

//==============================================================================
ChouchouShaperAudioProcessorEditor::ChouchouShaperAudioProcessorEditor (ChouchouShaperAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), spectralDisplay (p), scopeSection (p)
{
    tOn   = &addToggle ("t_on", "On");
    tLink = &addToggle ("t_stereo_link", "Link");
    tSc   = &addToggle ("t_sc", "SC");
    sOn   = &addToggle ("s_on", "On");
    sSc   = &addToggle ("s_sc", "SC");

    tRatio   = &addKnob ("t_ratio", "Ratio", kTime);
    tFast    = &addKnob ("t_fast_ms", "Fast", kTime);
    tSlow    = &addKnob ("t_slow_ms", "Slow", kTime);
    tAttack  = &addKnob ("t_attack_db", "Attack", kTime);
    tSustain = &addKnob ("t_sustain_db", "Sustain", kTime);
    tMix     = &addKnob ("t_mix", "Dry/Wet", kTime);

    sRatio   = &addKnob ("s_ratio", "Ratio", kSpectral);
    sFast    = &addKnob ("s_fast_ms", "Fast", kSpectral);
    sSlow    = &addKnob ("s_slow_ms", "Slow", kSpectral);
    sSmooth  = &addKnob ("s_freq_smooth", "Freq Smooth", kSpectral);
    sAttack  = &addKnob ("s_attack_db", "Attack", kSpectral);
    sSustain = &addKnob ("s_sustain_db", "Sustain", kSpectral);
    sLo      = &addKnob ("s_lo_hz", "Low", kSpectral);
    sHi      = &addKnob ("s_hi_hz", "High", kSpectral);
    sMix     = &addKnob ("s_mix", "Dry/Wet", kSpectral);

    scGain  = &addKnob ("sc_gain_db", "SC Gain", kSide);
    scHpf   = &addKnob ("sc_hpf_hz", "SC HPF", kSide);
    scLpf   = &addKnob ("sc_lpf_hz", "SC LPF", kSide);
    outKnob = &addKnob ("out_db", "Output", kOut);

    fftBox.addItemList ({ "FFT 512", "FFT 1024", "FFT 2048", "FFT 4096" }, 1);
    addAndMakeVisible (fftBox);
    fftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        audioProcessor.getAPVTS(), "s_fft_size", fftBox);

    addAndMakeVisible (timeTrace);
    addAndMakeVisible (spectralDisplay);

    inputBar.setCaption ("IN", kOut);
    scBar.setCaption ("SC", kSide);
    addAndMakeVisible (inputBar);
    addAndMakeVisible (scBar);

    infoLabel.setColour (juce::Label::textColourId, kDim);
    infoLabel.setFont (juce::FontOptions (12.0f));
    infoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (infoLabel);

    scopeButton.setClickingTogglesState (true);
    scopeButton.setColour (juce::TextButton::buttonColourId, kPanel);
    scopeButton.setColour (juce::TextButton::buttonOnColourId, kOut.darker (0.4f));
    scopeButton.setColour (juce::TextButton::textColourOffId, kText);
    scopeButton.setColour (juce::TextButton::textColourOnId, kText);
    scopeButton.onClick = [this] { setScopeOpen (scopeButton.getToggleState()); };
    addAndMakeVisible (scopeButton);
    addChildComponent (scopeSection);

    setScopeOpen (audioProcessor.isScopeOpen());
    startTimerHz (30);
}

void ChouchouShaperAudioProcessorEditor::setScopeOpen (bool open)
{
    audioProcessor.setScopeOpen (open);
    scopeButton.setToggleState (open, juce::dontSendNotification);
    scopeSection.setVisible (open);
    setSize (kWidth, kMainHeight + (open ? kScopeHeight : 0));
}

ChouchouShaperAudioProcessorEditor::~ChouchouShaperAudioProcessorEditor()
{
    stopTimer();
}

ChouchouShaperAudioProcessorEditor::Knob& ChouchouShaperAudioProcessorEditor::addKnob (
    const char* paramId, const juce::String& text, juce::Colour colour)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    k->slider.setColour (juce::Slider::rotarySliderOutlineColourId, kGrid);
    k->slider.setColour (juce::Slider::thumbColourId, colour.brighter (0.3f));
    k->slider.setColour (juce::Slider::textBoxTextColourId, kText);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k->slider);

    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setColour (juce::Label::textColourId, kText);
    k->label.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    addAndMakeVisible (k->label);

    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), paramId, k->slider);

    knobs.push_back (std::move (k));
    return *knobs.back();
}

ChouchouShaperAudioProcessorEditor::Toggle& ChouchouShaperAudioProcessorEditor::addToggle (
    const char* paramId, const juce::String& text)
{
    auto t = std::make_unique<Toggle>();
    t->button.setButtonText (text);
    t->button.setColour (juce::ToggleButton::textColourId, kText);
    t->button.setColour (juce::ToggleButton::tickColourId, kText);
    addAndMakeVisible (t->button);
    t->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.getAPVTS(), paramId, t->button);
    toggles.push_back (std::move (t));
    return *toggles.back();
}

//==============================================================================
void ChouchouShaperAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kText);
    g.setFont (juce::FontOptions (20.0f).withStyle ("Bold"));
    g.drawText ("chouchouShaper", 14, 8, 300, 26, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("envelope compressor  -  time  >  spectral", 190, 8, 400, 26, juce::Justification::centredLeft);

    auto drawPanel = [&] (juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (r.toFloat().withHeight (3.0f), 1.5f);
        g.setFont (juce::FontOptions (14.0f).withStyle ("Bold"));
        g.drawText (title, r.reduced (12, 0).withHeight (32).translated (0, 4), juce::Justification::centredLeft);
    };

    drawPanel (leftPanel, "1  TIME ENVELOPE", kTime);
    drawPanel (rightPanel, "2  SPECTRAL ENVELOPE", kSpectral);
    drawPanel (bottomPanel, "SIDECHAIN", kSide);
}

void ChouchouShaperAudioProcessorEditor::layoutKnobRow (juce::Rectangle<int> area, std::initializer_list<Knob*> row)
{
    const int w = juce::jmin (92, area.getWidth() / (int) row.size());
    auto r = area.withSizeKeepingCentre (w * (int) row.size(), area.getHeight());
    for (auto* k : row)
    {
        auto cell = r.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell.reduced (2, 0));
    }
}

void ChouchouShaperAudioProcessorEditor::resized()
{
    // The original controls always occupy the top kMainHeight pixels; the scope goes below.
    scopeButton.setBounds (kWidth - 12 - 90, 10, 90, 24);
    scopeSection.setBounds (juce::Rectangle<int> (0, kMainHeight, kWidth, kScopeHeight).reduced (12, 0).withTrimmedBottom (12));

    auto area = juce::Rectangle<int> (0, 0, kWidth, kMainHeight).reduced (12);
    area.removeFromTop (34);

    bottomPanel = area.removeFromBottom (150);
    area.removeFromBottom (10);
    leftPanel = area.removeFromLeft ((area.getWidth() - 10) / 2);
    area.removeFromLeft (10);
    rightPanel = area;

    auto layoutPanelTop = [] (juce::Rectangle<int> panel, std::initializer_list<juce::Component*> right)
    {
        auto bar = panel.reduced (10, 0).withHeight (32).translated (0, 4);
        for (auto* c : right)
            c->setBounds (bar.removeFromRight (dynamic_cast<juce::ComboBox*> (c) != nullptr ? 110 : 64)
                             .reduced (2, 4));
    };

    // Left panel
    {
        layoutPanelTop (leftPanel, { &tSc->button, &tLink->button, &tOn->button });
        auto r = leftPanel.reduced (10).withTrimmedTop (32);
        timeTrace.setBounds (r.removeFromTop (160));
        r.removeFromTop (8);
        const int rowH = (r.getHeight() - 6) / 2;
        layoutKnobRow (r.removeFromTop (rowH), { tRatio, tFast, tSlow });
        r.removeFromTop (6);
        layoutKnobRow (r.removeFromTop (rowH), { tAttack, tSustain, tMix });
    }

    // Right panel
    {
        layoutPanelTop (rightPanel, { &fftBox, &sSc->button, &sOn->button });
        auto r = rightPanel.reduced (10).withTrimmedTop (32);
        spectralDisplay.setBounds (r.removeFromTop (160));
        r.removeFromTop (8);
        const int rowH = (r.getHeight() - 6) / 2;
        layoutKnobRow (r.removeFromTop (rowH), { sRatio, sFast, sSlow, sSmooth });
        r.removeFromTop (6);
        layoutKnobRow (r.removeFromTop (rowH), { sAttack, sSustain, sLo, sHi, sMix });
    }

    // Bottom panel: sidechain knobs, meters, info, output
    {
        auto r = bottomPanel.reduced (10, 8).withTrimmedTop (22);
        auto knobsArea = r.removeFromLeft (290);
        layoutKnobRow (knobsArea, { scGain, scHpf, scLpf });

        auto outArea = r.removeFromRight (100);
        layoutKnobRow (outArea, { outKnob });

        r.reduce (12, 0);
        auto meters = r.removeFromTop (r.getHeight() - 22);
        const int barH = 20;
        inputBar.setBounds (meters.withSizeKeepingCentre (meters.getWidth(), barH).translated (0, -14));
        scBar.setBounds (meters.withSizeKeepingCentre (meters.getWidth(), barH).translated (0, 14));
        infoLabel.setBounds (r);
    }
}

//==============================================================================
void ChouchouShaperAudioProcessorEditor::timerCallback()
{
    scopeSection.tick();

    auto& apvts = audioProcessor.getAPVTS();
    const bool timeOn = apvts.getRawParameterValue ("t_on")->load() > 0.5f;

    timeTrace.push (timeOn ? audioProcessor.getMeterTimeMinDb() : 0.0f,
                    timeOn ? audioProcessor.getMeterTimeMaxDb() : 0.0f,
                    audioProcessor.getMeterInputDb());

    spectralDisplay.setRange (apvts.getRawParameterValue ("s_lo_hz")->load(),
                              apvts.getRawParameterValue ("s_hi_hz")->load());
    spectralDisplay.repaint();

    const bool scConnected = audioProcessor.isSidechainConnected();
    inputBar.set (audioProcessor.getMeterInputDb(), true);
    scBar.set (audioProcessor.getMeterScDb(), scConnected);

    const int fft = audioProcessor.getFftSize();
    const double sr = juce::jmax (1.0, audioProcessor.getCurrentSampleRate());
    infoLabel.setText (juce::String (scConnected ? "sidechain connected" : "no sidechain (SC uses main input)")
                           + "   |   latency " + juce::String (fft) + " smp ("
                           + juce::String (1000.0 * fft / sr, 1) + " ms)",
                       juce::dontSendNotification);
}
