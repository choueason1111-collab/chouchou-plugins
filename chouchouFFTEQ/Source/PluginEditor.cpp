#include "PluginEditor.h"
#include <algorithm>

namespace
{
juce::String bandId (int i, const char* field)
{
    return "b" + juce::String (i) + field;
}

const juce::Colour kText { 0xffe6e1d6 };
const juce::Colour kBox  { 0xff2a2e33 };

void styleRotary (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    s.setColour (juce::Slider::textBoxTextColourId, kText);
    s.setColour (juce::Slider::textBoxBackgroundColourId, kBox);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xfff2d56b));
    s.setColour (juce::Slider::thumbColourId, kText);
}

void styleLinear (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 16);
    s.setColour (juce::Slider::textBoxTextColourId, kText);
    s.setColour (juce::Slider::textBoxBackgroundColourId, kBox);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::trackColourId, juce::Colour (0xfff2d56b));
    s.setColour (juce::Slider::thumbColourId, kText);
}

void styleLabel (juce::Label& l, const juce::String& text, float size = 12.0f)
{
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centredLeft);
    l.setFont (juce::FontOptions (size));
    l.setColour (juce::Label::textColourId, kText);
}

void styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::textColourId, kText);
    c.setColour (juce::ComboBox::backgroundColourId, kBox);
    c.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a4048));
}

void styleToggle (juce::ToggleButton& b)
{
    b.setColour (juce::ToggleButton::textColourId, kText);
    b.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xfff2d56b));
}
}

ChouchouFFTEQEditor::ChouchouFFTEQEditor (ChouchouFFTEQAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), apvts (p.getAPVTS()),
      analyserUi (apvts), phaseUi (apvts)
{
    setSize (1000, 700);
    setResizable (true, true);
    setResizeLimits (820, 600, 1280, 860);

    styleLabel (title, "ChouchouFFTEQ", 16.0f);
    addAndMakeVisible (title);

    eqOn.setButtonText ("EQ");
    autoGain.setButtonText ("Auto Gain");
    usOn.setButtonText ("Ultrasound");
    usOn.setTooltip ("Opens Ultra: display/edit 1-60 kHz (or Full Nyquist) and ultrasound DSP.");
    scMatch.setButtonText ("Copy spectrum");
    scFollow.setButtonText ("Pitch follow");
    freeze.setButtonText ("Freeze");
    scUseBands.setButtonText ("Bands");
    scUseBands.setTooltip ("On: real bell filters (1 to 1000) shape the sound without forcing the FFT chain. Off: full spectral copy inside the FFT. Real-time follows the sidechain. Snapshot needs one Capture.");
    for (auto* b : { &eqOn, &autoGain, &usOn, &scMatch, &scFollow, &freeze, &scUseBands })
    {
        styleToggle (*b);
        addAndMakeVisible (b);
    }
    scCapture.setColour (juce::TextButton::buttonColourId, kBox);
    scCapture.setColour (juce::TextButton::textColourOffId, kText);
    scCapture.setTooltip ("Snapshot only: take one sidechain picture. Press again to replace it.");
    scCapture.onClick = [this] { audioProcessor.pulseCapture(); };
    addAndMakeVisible (scCapture);
    fftOn.setClickingTogglesState (true);
    fftOn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2e33));
    fftOn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff2d56b));
    fftOn.setColour (juce::TextButton::textColourOffId, kText);
    fftOn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c1f24));
    fftOn.setTooltip ("Master switch for the whole FFT chain. Off: pitch, mulreel, experiments and Freeze do not change the sound.");
    addAndMakeVisible (fftOn);

    styleLinear (mix);
    addAndMakeVisible (mix);
    styleLinear (scAmount);
    addAndMakeVisible (scAmount);
    styleLinear (scBandN);
    scBandN.setNumDecimalPlacesToDisplay (0);
    scBandN.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 16);
    scBandN.setTooltip ("Band count, from 1 to 1000.");
    addAndMakeVisible (scBandN);
    styleLabel (scBandCaption, "Count", 12.0f);
    addAndMakeVisible (scBandCaption);

    styleCombo (scMode);
    styleCombo (fftSize);
    styleCombo (eqEngine);
    scMode.addItemList (juce::StringArray { "Off", "Snapshot", "Real-time" }, 1);
    fftSize.addItemList (juce::StringArray { "512", "1024", "2048", "4096" }, 1);
    eqEngine.addItemList (juce::StringArray { "Original", "IIR", "FIR" }, 1);
    addAndMakeVisible (scMode);
    addAndMakeVisible (fftSize);
    addAndMakeVisible (eqEngine);

    addBand.setButtonText ("Add");
    addBand.onClick = [this]
    {
        const int n = shownBands();
        if (n < ChouchouFFTEQAudioProcessor::kMaxBands)
            addBandAt (1000.0f, 0.0f);
    };
    addAndMakeVisible (addBand);
    removeBand.setButtonText ("Remove");
    removeBand.onClick = [this] { removeSelectedBand(); };
    removeBand.setTooltip ("Removes the selected band. The bands after it move up.");
    addAndMakeVisible (removeBand);

    styleLabel (engineLabel, "Engine", 12.0f);
    addAndMakeVisible (engineLabel);
    styleLabel (windowLabel, "Window size", 12.0f);
    addAndMakeVisible (windowLabel);
    styleLabel (mixCaption, "Dry/Wet", 12.0f);
    addAndMakeVisible (mixCaption);
    styleLabel (amountCaption, "Amount", 12.0f);
    addAndMakeVisible (amountCaption);
    styleLabel (fftCaption, "code", 13.0f);
    addAndMakeVisible (fftCaption);
    styleCombo (experimentBox);
    experimentBox.addItemList (juce::StringArray { "0_chain", "1_shift", "2_split", "3_offset", "4_write", "5_ultra" }, 1);
    experimentBox.setTooltip ("0_chain keeps pitch, then formant. 1_shift, 2_split, 3_offset, 4_write, 5_ultra fold ultrasound bins with shift.");
    addAndMakeVisible (experimentBox);

    const struct { const char* id; const char* name; const char* help; } ultraDefs[] = {
        { "usFloor", "Floor", "Bins at or above this Hz are ultrasound." },
        { "usGain", "Gain", "Gain in dB applied only above Floor." },
        { "usTilt", "Tilt", "Tilts only the ultrasound region." },
        { "usGate", "Gate", "Relative gate inside the ultrasound region." }
    };
    auto hzText = [] (double v)
    {
        if (v >= 1000.0)
            return juce::String (v / 1000.0, v >= 10000.0 ? 1 : 2) + " kHz";
        return juce::String (v, 0) + " Hz";
    };
    auto hzFromText = [] (const juce::String& text)
    {
        auto t = text.trim().toLowerCase();
        const double n = t.getDoubleValue();
        if (t.contains ("k"))
            return n * 1000.0;
        return n;
    };
    auto bindHzSlider = [&] (juce::Slider& s)
    {
        s.textFromValueFunction = hzText;
        s.valueFromTextFunction = hzFromText;
        s.setNumDecimalPlacesToDisplay (1);
        s.updateText();
    };
    for (size_t i = 0; i < ultraKnobs.size(); ++i)
    {
        auto& k = ultraKnobs[i];
        styleLinear (k.slider);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 16);
        k.slider.setTooltip (ultraDefs[i].help);
        addAndMakeVisible (k.slider);
        styleLabel (k.name, ultraDefs[i].name, 12.0f);
        addAndMakeVisible (k.name);
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ultraDefs[i].id, k.slider);
        if (i == 0)
            bindHzSlider (k.slider);
        else if (i == 1)
            k.slider.setTextValueSuffix (" dB");
    }
    for (auto* cell : { &ultraMin, &ultraMax })
    {
        styleLinear (cell->slider);
        cell->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 16);
        addAndMakeVisible (cell->slider);
        addAndMakeVisible (cell->name);
    }
    styleLabel (ultraMin.name, "Min Hz", 12.0f);
    styleLabel (ultraMax.name, "Max Hz", 12.0f);
    ultraMin.slider.setTooltip ("Lowest frequency shown on the Ultra spectrum (Hz).");
    ultraMax.slider.setTooltip ("Highest frequency shown on the Ultra spectrum (Hz, clamped by Nyquist).");
    ultraMin.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "anMin", ultraMin.slider);
    ultraMax.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "anMax", ultraMax.slider);
    bindHzSlider (ultraMin.slider);
    bindHzSlider (ultraMax.slider);

    styleToggle (usSolo);
    usSolo.setTooltip ("Mute bins below Floor; keep ultrasound only.");
    addAndMakeVisible (usSolo);
    styleToggle (ultraLinear);
    ultraLinear.setTooltip ("Linear frequency axis (better for 1-60 kHz). Off = log.");
    ultraLinear.onClick = [this]
    {
        setPlain ("anFreqScale", ultraLinear.getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible (ultraLinear);

    styleLabel (ultraHint, "Drag / dbl-click / wheel Q", 11.0f);
    styleLabel (ultraRangeCaption, "Range", 12.0f);
    styleLabel (ultraDspCaption, "US DSP", 12.0f);
    addAndMakeVisible (ultraHint);
    addAndMakeVisible (ultraRangeCaption);
    addAndMakeVisible (ultraDspCaption);

    auto styleRangeBtn = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2e33));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff2d56b));
        b.setColour (juce::TextButton::textColourOffId, kText);
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c1f24));
    };
    for (auto* b : { &rangeAudible, &range1to60, &rangeFull, &floor18, &floor20, &floor30, &ultraAddBand, &ultraRemoveBand })
    {
        styleRangeBtn (*b);
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    for (auto* b : { &rangeAudible, &range1to60, &rangeFull })
        b->setClickingTogglesState (true);
    rangeAudible.setButtonText ("20-20k");
    range1to60.setButtonText ("1-60k");
    rangeFull.setButtonText ("Full");
    rangeAudible.setTooltip ("Show 20 Hz - 20 kHz.");
    range1to60.setTooltip ("Show 1 kHz - 60 kHz (clamped to Nyquist). Best for ultrasound editing.");
    rangeFull.setTooltip ("Show 20 Hz to Nyquist of the current sample rate.");
    rangeAudible.onClick = [this] { applyDisplayRange (20.0f, 20000.0f, false); };
    range1to60.onClick = [this]
    {
        applyDisplayRange (ChouchouFFTEQAudioProcessor::kDisplayLoHz,
                           ChouchouFFTEQAudioProcessor::kDisplayHiHz,
                           true);
    };
    rangeFull.onClick = [this] { applyDisplayRange (20.0f, displayNyquist(), true); };
    floor18.onClick = [this] { setPlain ("usFloor", 18000.0f); };
    floor20.onClick = [this] { setPlain ("usFloor", 20000.0f); };
    floor30.onClick = [this] { setPlain ("usFloor", 30000.0f); };
    ultraAddBand.onClick = [this]
    {
        const float mid = 0.5f * (apvts.getRawParameterValue ("anMin")->load()
                                  + apvts.getRawParameterValue ("anMax")->load());
        setPlain ("eqOn", 1.0f);
        addBandAt (mid, 0.0f);
    };
    ultraRemoveBand.onClick = [this] { removeSelectedBand(); };

    eqCaption.setVisible (false);
    bandViewport.setScrollBarsShown (true, false);
    bandViewport.setOpaque (false);
    addAndMakeVisible (bandViewport);
    bandViewport.setViewedComponent (&bandHolder, false);

    for (int i = 0; i < ChouchouFFTEQAudioProcessor::kMaxBands; ++i)
    {
        auto& row = bands[(size_t) i];
        const auto id = [i] (const char* part) { return bandId (i, part); };
        row.index.setButtonText (juce::String (i + 1));
        row.index.setColour (juce::TextButton::buttonColourId, kBox);
        row.index.setColour (juce::TextButton::textColourOffId, kText);
        row.index.onClick = [this, i]
        {
            selectedBand = i;
            layoutBands();
            repaint();
        };
        bandHolder.addAndMakeVisible (row.index);
        row.on.setButtonText ("On");
        styleToggle (row.on);
        bandHolder.addAndMakeVisible (row.on);
        styleCombo (row.type);
        row.type.addItemList (juce::StringArray { "Peak", "Low shelf", "High shelf", "Low cut", "High cut" }, 1);
        bandHolder.addAndMakeVisible (row.type);
        for (auto* s : { &row.freq, &row.gain, &row.q, &row.slope, &row.delay, &row.pan })
        {
            styleLinear (*s);
            bandHolder.addAndMakeVisible (s);
        }
        row.freq.setTextValueSuffix ({});
        row.freq.setNumDecimalPlacesToDisplay (1);
        row.freq.textFromValueFunction = [] (double v)
        {
            if (v >= 1000.0)
                return juce::String (v / 1000.0, v >= 10000.0 ? 1 : 2) + " kHz";
            return juce::String (v, 1) + " Hz";
        };
        row.freq.valueFromTextFunction = [] (const juce::String& text)
        {
            const double n = text.getDoubleValue();
            return text.containsIgnoreCase ("k") ? n * 1000.0 : n;
        };
        row.freq.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 18);
        row.gain.setTextValueSuffix (" dB");
        row.gain.setNumDecimalPlacesToDisplay (1);
        row.gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 68, 18);
        row.q.setNumDecimalPlacesToDisplay (2);
        row.q.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        row.slope.setTextValueSuffix (" dB/oct");
        row.slope.setNumDecimalPlacesToDisplay (0);
        row.slope.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
        row.slope.setTooltip ("Cut slope. 6 dB/oct = 1st order, 12 = 2nd, 24 = 4th, 48 = 8th.");
        styleCombo (row.calc);
        row.calc.addItemList (juce::StringArray { "Butterworth", "Q" }, 1);
        row.calc.setTooltip ("Butterworth: a smooth slope. Q: each 12 dB/oct stage uses this Q.");
        bandHolder.addAndMakeVisible (row.calc);
        row.delay.setTextValueSuffix (" ms");
        row.delay.setNumDecimalPlacesToDisplay (2);
        row.delay.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
        row.pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 18);
        row.delay.setTooltip ("Delay of this band. 0 ms keeps the original phase.");
        row.pan.setDoubleClickReturnValue (true, 0.0);
        row.pan.setTooltip ("Pan of this band. Double-click: centre.");
        row.pan.textFromValueFunction = [] (double v)
        {
            if (std::abs (v) < 0.02)
                return juce::String ("C");
            return (v < 0.0 ? "L " : "R ") + juce::String (juce::roundToInt (std::abs (v) * 100.0));
        };
        row.pan.valueFromTextFunction = [] (const juce::String& text)
        {
            if (text.trim().equalsIgnoreCase ("C"))
                return 0.0;
            const double n = text.getDoubleValue() / (text.containsIgnoreCase ("L") ? -100.0 : 100.0);
            return juce::jlimit (-1.0, 1.0, text.containsIgnoreCase ("L") || text.containsIgnoreCase ("R") ? n : text.getDoubleValue());
        };
        row.onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, id ("on"), row.on);
        row.typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id ("type"), row.type);
        row.freqAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id ("freq"), row.freq);
        row.gainAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id ("gain"), row.gain);
        row.qAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id ("q"), row.q);
        row.slopeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id ("slope"), row.slope);
        row.calcAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id ("calc"), row.calc);
        row.delayAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id ("dly"), row.delay);
        row.panAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id ("pan"), row.pan);
    }

    const struct { const char* id; const char* name; const char* help; } defs[] = {
        { "shift", "shift", "peekPolar(idx * shift, reel, image). 1 = the bin reads its own place." },
        { "shiftreel", "shiftreel", "reel = peek(idx * shiftreel). Amplitude comes from another bin." },
        { "shiftimage", "shiftimage", "image = peek(idx * shiftimage). Phase comes from another bin." },
        { "mulReel", "mulreel", "re = reel * cos(image) * mulreel. 1 = no change." },
        { "mulImage", "mulimage", "im = reel * sin(image) * mulimage. 1 = no change." },
        { "offsetBins", "offsetBins", "peekPolar(idx + offsetBins, reel, image). 0 = no change." },
        { "pitch", "pitch", "0_chain: src = idx * pitch. Above 1, pitch goes down." },
        { "formant", "formant", "Moves the timbre envelope. 1 = no change." },
        { "tilt", "tilt", "Tilts the spectrum. 0 = flat." },
        { "contrast", "contrast", "Emphasises peaks. 1 = no change." },
        { "gate", "gate", "if (reel < gate * peak) reel = 0. 0 = everything passes." },
        { "blur", "blur", "Averages re/im across neighbouring bins. 0 = sharp." }
    };

    int knobIndex = 0;
    const auto codeFont = juce::FontOptions (12.0f).withName (juce::Font::getDefaultMonospacedFontName());
    for (auto& d : defs)
    {
        auto& k = knobs[(size_t) knobIndex++];
        styleLinear (k.slider);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 16);
        k.slider.setTooltip (d.help);
        addAndMakeVisible (k.slider);
        styleLabel (k.name, d.name, 12.0f);
        k.name.setFont (codeFont);
        k.name.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (k.name);
        k.help.setVisible (false);
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, d.id, k.slider);
    }

    eqAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "eqOn", eqOn);
    fftAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "fftOn", fftOn);
    agAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "autoGain", autoGain);
    usAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "usOn", usOn);
    usSoloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "usSolo", usSolo);
    matchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "scMatch", scMatch);
    followAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "scFollow", scFollow);
    scUseBandsAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "scUseBands", scUseBands);
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "freeze", freeze);
    mixAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "mix", mix);
    amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "scAmount", scAmount);
    scBandAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "scBandN", scBandN);
    scModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "scMode", scMode);
    fftSizeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "fftSize", fftSize);
    engineAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "eqEngine", eqEngine);
    experimentAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "experiment", experimentBox);

    for (auto* b : { &pageEq, &pageSpec, &pagePhase, &pageUltra })
    {
        b->setRadioGroupId (4401);
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2e33));
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff2d56b));
        b->setColour (juce::TextButton::textColourOffId, kText);
        b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c1f24));
        addAndMakeVisible (b);
    }
    pageEq.setToggleState (true, juce::dontSendNotification);
    pageEq.onClick = [this] { showPage (0); };
    pageSpec.onClick = [this] { showPage (1); };
    pagePhase.onClick = [this] { showPage (2); };
    pageUltra.onClick = [this] { showPage (3); };

    addAndMakeVisible (analyserUi);
    addAndMakeVisible (phaseUi);
    analyserUi.capRef.onClick = [this] { refCurve = anFrame.db; };
    analyserUi.capA.onClick = [this] { slotA = anFrame.db; };
    analyserUi.capB.onClick = [this] { slotB = anFrame.db; };

    apvts.addParameterListener ("bandCount", this);
    apvts.addParameterListener ("usOn", this);
    lastUsOn = apvts.getRawParameterValue ("usOn")->load() > 0.5f;
    audioProcessor.setDisplayOpen (true);
    startTimerHz (timerHz);
}

ChouchouFFTEQEditor::~ChouchouFFTEQEditor()
{
    audioProcessor.setDisplayOpen (false);
    apvts.removeParameterListener ("bandCount", this);
    apvts.removeParameterListener ("usOn", this);
}

float ChouchouFFTEQEditor::displayNyquist() const noexcept
{
    return (float) juce::jmin ((double) ChouchouFFTEQAudioProcessor::kAbsMaxHz,
                               juce::jmax (1000.0, audioProcessor.getCurrentSampleRate() * 0.49));
}

float ChouchouFFTEQEditor::clampEditableHz (float hz) const noexcept
{
    return juce::jlimit (1.0f, displayNyquist(), hz);
}

void ChouchouFFTEQEditor::applyDisplayRange (float minHz, float maxHz, bool linearScale)
{
    const float nyquist = displayNyquist();
    float lo = juce::jlimit (1.0f, nyquist - 10.0f, minHz);
    float hi = juce::jlimit (lo + 10.0f, nyquist, maxHz);
    setPlain ("anMin", lo);
    setPlain ("anMax", hi);
    setPlain ("anFreqScale", linearScale ? 1.0f : 0.0f);
    setPlain ("anOn", 1.0f);
    ultraLinear.setToggleState (linearScale, juce::dontSendNotification);
    updateUltraReadout();
    repaint();
}

void ChouchouFFTEQEditor::updateUltraReadout()
{
    const float lo = apvts.getRawParameterValue ("anMin")->load();
    const float hi = apvts.getRawParameterValue ("anMax")->load();
    const float floorHz = apvts.getRawParameterValue ("usFloor")->load();
    const bool linear = apvts.getRawParameterValue ("anFreqScale")->load() > 0.5f;
    auto fmt = [] (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    };
    ultraHint.setText (fmt (lo) + " -> " + fmt (hi)
                           + " | floor " + fmt (floorHz)
                           + " | " + (linear ? "linear" : "log")
                           + " | Nyquist " + fmt (displayNyquist()),
                       juce::dontSendNotification);
    ultraLinear.setToggleState (linear, juce::dontSendNotification);

    const bool nearAudible = std::abs (lo - 20.0f) < 5.0f && std::abs (hi - 20000.0f) < 50.0f;
    const bool near160 = std::abs (lo - 1000.0f) < 50.0f
                         && std::abs (hi - juce::jmin (60000.0f, displayNyquist())) < 100.0f;
    const bool nearFull = std::abs (lo - 20.0f) < 5.0f && std::abs (hi - displayNyquist()) < 100.0f;
    rangeAudible.setToggleState (nearAudible, juce::dontSendNotification);
    range1to60.setToggleState (near160, juce::dontSendNotification);
    rangeFull.setToggleState (nearFull, juce::dontSendNotification);
}

void ChouchouFFTEQEditor::applyUltrasoundView()
{
    applyDisplayRange (ChouchouFFTEQAudioProcessor::kDisplayLoHz,
                       ChouchouFFTEQAudioProcessor::kDisplayHiHz,
                       true);
    showPage (3);
}

void ChouchouFFTEQEditor::parameterChanged (const juce::String& parameterID, float)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ChouchouFFTEQEditor> (this), parameterID]
    {
        if (safe == nullptr)
            return;
        if (parameterID == "usOn")
        {
            const bool on = safe->apvts.getRawParameterValue ("usOn")->load() > 0.5f;
            if (on && ! safe->lastUsOn)
                safe->applyUltrasoundView();
            safe->lastUsOn = on;
        }
        safe->selectedBand = juce::jlimit (0, juce::jmax (0, safe->shownBands() - 1), safe->selectedBand);
        safe->resized();
    });
}

void ChouchouFFTEQEditor::showPage (int page)
{
    viewPage = juce::jlimit (0, 3, page);
    pageEq.setToggleState (viewPage == 0, juce::dontSendNotification);
    pageSpec.setToggleState (viewPage == 1, juce::dontSendNotification);
    pagePhase.setToggleState (viewPage == 2, juce::dontSendNotification);
    pageUltra.setToggleState (viewPage == 3, juce::dontSendNotification);
    if (viewPage == 1 || viewPage == 3)
        setPlain ("anOn", 1.0f);
    if (viewPage == 2)
        setPlain ("phOn", 1.0f);
    resized();
}

void ChouchouFFTEQEditor::timerCallback()
{
    const int rate = juce::jlimit (5, 60, (int) std::lround (apvts.getRawParameterValue ("anRate")->load()));
    if (rate != timerHz)
    {
        timerHz = rate;
        startTimerHz (timerHz);
    }
    audioProcessor.pumpDisplays();
    audioProcessor.copySpectra (mainMag, scMag, refMag);
    audioProcessor.copyMatch (matchDb, matchBands);
    audioProcessor.copyScBands (scNodes);
    const float bandAlpha = apvts.getRawParameterValue ("scUseBands")->load() > 0.5f ? 1.0f : 0.35f;
    scBandN.setAlpha (bandAlpha);
    scBandCaption.setAlpha (bandAlpha);
    anFrame = audioProcessor.copyAnalyser();
    phFrame = audioProcessor.copyPhase();
    if (viewPage == 2 && apvts.getRawParameterValue ("phFreeze")->load() < 0.5f)
    {
        const auto now = juce::Time::getMillisecondCounter();
        const int n = juce::jmin (phFrame.x.size(), phFrame.y.size());
        for (int i = 0; i < n; i += 4)
            trail.push_back ({ phFrame.x[(size_t) i], phFrame.y[(size_t) i], now });
        const float persist = apvts.getRawParameterValue ("phPersist")->load();
        const auto cutoff = now - (juce::uint32) persist;
        trail.erase (std::remove_if (trail.begin(), trail.end(),
                                     [cutoff] (const TrailDot& d) { return d.born < cutoff; }),
                     trail.end());
        if (trail.size() > 4000)
            trail.erase (trail.begin(), trail.begin() + (std::ptrdiff_t) (trail.size() - 4000));
        corrHistory.push_back (phFrame.correlation);
        if (corrHistory.size() > 160)
            corrHistory.erase (corrHistory.begin());
    }
    updateFftCode();
    syncCutRows();
    if (viewPage == 3)
        updateUltraReadout();
    repaint();
}

void ChouchouFFTEQEditor::updateFftCode()
{
    const int experiment = (int) apvts.getRawParameterValue ("experiment")->load();
    const bool live = apvts.getRawParameterValue ("fftOn")->load() > 0.5f;
    juce::String line;
    switch (experiment)
    {
        case 1:
            line = "peekPolar(idx * shift, reel, image);\nre = reel * cos(image) * mulreel;   im = reel * sin(image) * mulimage;";
            break;
        case 2:
            line = "reel = peek(idx * shiftreel);   image = peek(idx * shiftimage);\nre = reel * cos(image) * mulreel;   im = reel * sin(image) * mulimage;";
            break;
        case 3:
            line = "peekPolar(idx + offsetBins, reel, image);\nre = reel * cos(image) * mulreel;   im = reel * sin(image) * mulimage;";
            break;
        case 4:
            line = "writeidx = wrap(round(idx * shift), bins);\noutReel[writeidx] += reel * cos(image) * mulreel;   outImage[writeidx] += reel * sin(image) * mulimage;";
            break;
        case 5:
            line = "if (idx >= floorBin) peekPolar(idx * shift, reel, image);\nre = reel * cos(image) * mulreel;   im = reel * sin(image) * mulimage;";
            break;
        default:
            line = "src = idx * pitch;   reel, image = peek(src);\nre = reel * cos(image) * mulreel;   im = reel * sin(image) * mulimage;";
            break;
    }
    if (! live)
        line = "bypass  //  tick FFT to run the chain\n" + line;
    else if (apvts.getRawParameterValue ("autoGain")->load() > 0.5f)
        line += "\nautogain matches the level and hides mulreel";
    fftFormula = line;

    bool used[12] {};
    const auto mark = [&used] (std::initializer_list<int> ids)
    {
        for (int id : ids)
            used[id] = true;
    };
    mark ({ 3, 4, 7, 8, 9, 10, 11 });
    if (experiment == 0)
        mark ({ 5, 6 });
    if (experiment == 1 || experiment == 4 || experiment == 5)
        mark ({ 0 });
    if (experiment == 2)
        mark ({ 1, 2 });
    if (experiment == 3)
        mark ({ 5 });
    for (int i = 0; i < 12; ++i)
    {
        const float alpha = live && used[i] ? 1.0f : 0.35f;
        knobs[(size_t) i].slider.setAlpha (alpha);
        knobs[(size_t) i].name.setAlpha (alpha);
    }
    experimentBox.setAlpha (live ? 1.0f : 0.35f);
}

int ChouchouFFTEQEditor::shownBands() const
{
    return juce::jlimit (0, ChouchouFFTEQAudioProcessor::kMaxBands,
                         (int) std::lround (apvts.getRawParameterValue ("bandCount")->load()));
}

void ChouchouFFTEQEditor::setPlain (const juce::String& id, float plain)
{
    if (auto* param = apvts.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

float ChouchouFFTEQEditor::displayT (float hz) const
{
    float lo = apvts.getRawParameterValue ("anMin")->load();
    float hi = apvts.getRawParameterValue ("anMax")->load();
    if (hi < lo + 10.0f)
        hi = lo + 10.0f;
    hz = juce::jlimit (lo, hi, hz);
    if (apvts.getRawParameterValue ("anFreqScale")->load() > 0.5f)
        return (hz - lo) / (hi - lo);
    return std::log (hz / lo) / std::log (hi / lo);
}

float ChouchouFFTEQEditor::displayHz (float t) const
{
    float lo = apvts.getRawParameterValue ("anMin")->load();
    float hi = apvts.getRawParameterValue ("anMax")->load();
    if (hi < lo + 10.0f)
        hi = lo + 10.0f;
    t = juce::jlimit (0.0f, 1.0f, t);
    if (apvts.getRawParameterValue ("anFreqScale")->load() > 0.5f)
        return lo + t * (hi - lo);
    return lo * std::pow (hi / lo, t);
}

float ChouchouFFTEQEditor::levelAt (float hz, const std::vector<float>& db) const
{
    if (db.empty() || anFrame.sampleRate <= 0.0 || anFrame.fftSize <= 0)
        return -120.0f;
    const float idx = juce::jlimit (0.0f, (float) db.size() - 1.0f,
                                    hz / (float) anFrame.sampleRate * (float) anFrame.fftSize);
    const int i0 = (int) idx;
    const int i1 = juce::jmin ((int) db.size() - 1, i0 + 1);
    const float f = idx - (float) i0;
    return db[(size_t) i0] * (1.0f - f) + db[(size_t) i1] * f;
}

float ChouchouFFTEQEditor::xToHz (float x) const
{
    const float t = juce::jlimit (0.0f, 1.0f,
                                  (x - (float) spectrumArea.getX()) / (float) juce::jmax (1, spectrumArea.getWidth()));
    return displayHz (t);
}

float ChouchouFFTEQEditor::yToDb (float y) const
{
    const float mid = (float) spectrumArea.getCentreY();
    const float span = (float) spectrumArea.getHeight() * 0.45f;
    if (span <= 1.0f)
        return 0.0f;
    return juce::jlimit (-24.0f, 24.0f, (mid - y) / span * 24.0f);
}

juce::Point<float> ChouchouFFTEQEditor::nodeCentre (int band) const
{
    const auto id = [band] (const char* part) { return bandId (band, part); };
    const float hz = apvts.getRawParameterValue (id ("freq"))->load();
    const int type = (int) std::lround (apvts.getRawParameterValue (id ("type"))->load());
    const float gain = apvts.getRawParameterValue (id ("gain"))->load();
    const float t = displayT (hz);
    const float x = (float) spectrumArea.getX() + t * (float) spectrumArea.getWidth();
    const float db = (type >= 3) ? audioProcessor.responseDb (hz) : gain;
    const float y = (float) spectrumArea.getCentreY() - juce::jlimit (-24.0f, 24.0f, db) / 24.0f * (float) spectrumArea.getHeight() * 0.45f;
    return { x, y };
}

int ChouchouFFTEQEditor::hitNode (juce::Point<float> p) const
{
    const int n = shownBands();
    int best = -1;
    float bestD = 14.0f;
    for (int i = 0; i < n; ++i)
    {
        const float d = p.getDistanceFrom (nodeCentre (i));
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void ChouchouFFTEQEditor::addBandAt (float hz, float db)
{
    const int n = shownBands();
    if (n >= ChouchouFFTEQAudioProcessor::kMaxBands)
        return;
    const auto id = [n] (const char* part) { return bandId (n, part); };
    setPlain ("bandCount", (float) (n + 1));
    setPlain (id ("on"), 1.0f);
    setPlain (id ("type"), 0.0f);
    setPlain (id ("freq"), clampEditableHz (hz));
    setPlain (id ("gain"), db);
    setPlain (id ("q"), 0.707f);
    setPlain (id ("dly"), 0.0f);
    selectedBand = n;
}

void ChouchouFFTEQEditor::removeSelectedBand()
{
    const int n = shownBands();
    if (n <= 0 || selectedBand < 0 || selectedBand >= n)
        return;

    const char* fields[] { "on", "type", "freq", "gain", "q", "slope", "calc", "dly", "pan" };
    for (int i = selectedBand; i < n - 1; ++i)
        for (const char* field : fields)
            setPlain (bandId (i, field), apvts.getRawParameterValue (bandId (i + 1, field))->load());

    setPlain ("bandCount", (float) (n - 1));
    selectedBand = juce::jlimit (0, juce::jmax (0, n - 2), selectedBand);
    resized();
}

void ChouchouFFTEQEditor::mouseMove (const juce::MouseEvent& e)
{
    cursorOn = viewPage != 2 && spectrumArea.contains (e.getPosition());
    cursorPos = e.position;
    if (cursorOn)
        repaint();
}

void ChouchouFFTEQEditor::mouseDown (const juce::MouseEvent& e)
{
    if (viewPage == 2 || ! spectrumArea.contains (e.getPosition()))
        return;
    const int hit = hitNode (e.position);
    if (hit < 0)
        return;
    selectedBand = hit;
    layoutBands();
    draggingNode = true;
    dragChangesQ = e.mods.isAltDown() || e.mods.isRightButtonDown();
    dragStartY = e.position.y;
    dragStartQ = apvts.getRawParameterValue (bandId (hit, "q"))->load();
}

void ChouchouFFTEQEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingNode || selectedBand < 0 || selectedBand >= shownBands())
        return;
    const auto id = [this] (const char* part) { return bandId (selectedBand, part); };
    if (dragChangesQ)
    {
        const float q = juce::jlimit (0.1f, 10.0f, dragStartQ * std::pow (1.06f, (dragStartY - e.position.y) * 0.12f));
        setPlain (id ("q"), q);
        return;
    }
    setPlain (id ("freq"), clampEditableHz (xToHz (e.position.x)));
    const int type = (int) std::lround (apvts.getRawParameterValue (id ("type"))->load());
    if (type <= 2)
        setPlain (id ("gain"), yToDb (e.position.y));
}

void ChouchouFFTEQEditor::mouseUp (const juce::MouseEvent&)
{
    draggingNode = false;
}

void ChouchouFFTEQEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (viewPage == 2 || ! spectrumArea.contains (e.getPosition()))
        return;
    const int hit = hitNode (e.position);
    if (hit >= 0)
    {
        const auto id = bandId (hit, "on");
        const bool on = apvts.getRawParameterValue (id)->load() > 0.5f;
        setPlain (id, on ? 0.0f : 1.0f);
        selectedBand = hit;
        return;
    }
    addBandAt (xToHz (e.position.x), yToDb (e.position.y));
}

void ChouchouFFTEQEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! spectrumArea.contains (e.getPosition()) || shownBands() <= 0)
        return;
    const int hit = hitNode (e.position);
    if (hit >= 0)
        selectedBand = hit;
    if (selectedBand < 0 || selectedBand >= shownBands())
        return;
    const auto id = bandId (selectedBand, "q");
    const float q = apvts.getRawParameterValue (id)->load();
    setPlain (id, juce::jlimit (0.1f, 10.0f, q * std::pow (1.15f, wheel.deltaY * 4.0f)));
}

void ChouchouFFTEQEditor::paintFftCode (juce::Graphics& g)
{
    if (fftCodeArea.isEmpty())
        return;
    g.setColour (juce::Colour (0xfff2d56b));
    g.setFont (juce::FontOptions (12.0f).withName (juce::Font::getDefaultMonospacedFontName()));
    auto row = fftCodeArea;
    for (auto line : juce::StringArray::fromLines (fftFormula))
        g.drawText (line, row.removeFromTop (16), juce::Justification::centredLeft, true);
}

void ChouchouFFTEQEditor::drawSpectrum (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (juce::Colour (0xff14181c));
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    const float lo = apvts.getRawParameterValue ("anMin")->load();
    const float hi = apvts.getRawParameterValue ("anMax")->load();
    auto axisHz = [] (float hz) -> juce::String
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    };
    const float ticks[] { 1.0f, 100.0f, 1000.0f, 5000.0f, 10000.0f, 15000.0f, 20000.0f,
                          30000.0f, 40000.0f, 48000.0f, 60000.0f, 80000.0f, 96000.0f };
    g.setColour (juce::Colour (0xff2c333a));
    g.setFont (juce::FontOptions (10.0f));
    float lastLabelX = -1000.0f;
    for (float hz : ticks)
    {
        if (hz <= lo + 1.0f || hz >= hi - 1.0f)
            continue;
        const float x = (float) area.getX() + displayT (hz) * (float) area.getWidth();
        g.drawVerticalLine ((int) x, (float) area.getY() + 18.0f, (float) area.getBottom() - 18.0f);
        if (x - lastLabelX >= 52.0f)
        {
            g.setColour (juce::Colour (0xff9aa3ad));
            g.drawText (axisHz (hz), (int) x - 28, area.getBottom() - 16, 56, 14, juce::Justification::centred);
            lastLabelX = x;
        }
        g.setColour (juce::Colour (0xff2c333a));
    }
    const float nyquist = displayNyquist();
    if (nyquist > lo && nyquist < hi)
    {
        const float nx = (float) area.getX() + displayT (nyquist) * (float) area.getWidth();
        g.setColour (juce::Colour (0xffe29a4a).withAlpha (0.85f));
        const float dashY0 = (float) area.getY() + 18.0f;
        const float dashY1 = (float) area.getBottom() - 18.0f;
        for (float y = dashY0; y < dashY1; y += 8.0f)
            g.drawLine (nx, y, nx, juce::jmin (y + 4.0f, dashY1), 1.2f);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("Nyquist", (int) nx - 28, area.getY() + 4, 56, 12, juce::Justification::centred);
    }
    g.setColour (juce::Colour (0xffc8cdd3));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (axisHz (lo), area.getX() + 6, area.getBottom() - 16, 72, 14, juce::Justification::left);
    g.drawText (axisHz (hi), area.getRight() - 78, area.getBottom() - 16, 72, 14, juce::Justification::right);
    if (apvts.getRawParameterValue ("usOn")->load() > 0.5f)
    {
        const float floorHz = apvts.getRawParameterValue ("usFloor")->load();
        if (floorHz > lo && floorHz < hi)
        {
            const float x = (float) area.getX() + displayT (floorHz) * (float) area.getWidth();
            g.setColour (juce::Colour (0xff7ec8e3));
            g.drawVerticalLine ((int) x, (float) area.getY() + 16.0f, (float) area.getBottom() - 18.0f);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("US", (int) x - 14, area.getY() + 18, 28, 12, juce::Justification::centred);
        }
    }
    const float y0 = (float) area.getCentreY();
    g.setColour (juce::Colour (0xff2c333a));
    g.drawHorizontalLine ((int) y0, (float) area.getX() + 4.0f, (float) area.getRight() - 4.0f);

    const float refDb = apvts.getRawParameterValue ("anRef")->load();
    const float dbSpan = juce::jmax (6.0f, apvts.getRawParameterValue ("anDb")->load());
    const float ampSpan = juce::jmax (0.05f, apvts.getRawParameterValue ("anAmp")->load());
    const bool linearAmp = apvts.getRawParameterValue ("anAmpScale")->load() > 0.5f;
    auto dbY = [&] (float db)
    {
        float t = 0.0f;
        if (linearAmp)
            t = juce::Decibels::decibelsToGain (db, -120.0f) / ampSpan;
        else
            t = (db - (refDb - dbSpan)) / dbSpan;
        t = juce::jlimit (0.0f, 1.0f, t);
        return (float) area.getBottom() - 18.0f - t * (float) (area.getHeight() - 36);
    };
    auto strokeDb = [&] (const std::vector<float>& db, juce::Colour colour)
    {
        if (db.size() < 2)
            return;
        juce::Path path;
        for (int x = 0; x < area.getWidth(); ++x)
        {
            const float t = (float) x / (float) juce::jmax (1, area.getWidth() - 1);
            const float y = dbY (levelAt (displayHz (t), db));
            if (x == 0)
                path.startNewSubPath ((float) (area.getX() + x), y);
            else
                path.lineTo ((float) (area.getX() + x), y);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.3f));
    };

    const int mode = (int) std::lround (apvts.getRawParameterValue ("anMode")->load());
    if (mode == 4 && anFrame.waterfallRows > 1 && (int) anFrame.waterfall.size() >= anFrame.waterfallRows * DisplayAnalyser::kWaterCols)
    {
        const int rows = anFrame.waterfallRows;
        const float rh = (float) area.getHeight() / (float) rows;
        for (int row = 0; row < rows; ++row)
        {
            for (int col = 0; col < DisplayAnalyser::kWaterCols; ++col)
            {
                const float db = anFrame.waterfall[(size_t) row * DisplayAnalyser::kWaterCols + (size_t) col];
                const float t = juce::jlimit (0.0f, 1.0f, (db - (refDb - dbSpan)) / dbSpan);
                g.setColour (juce::Colour (0xff14202a).interpolatedWith (juce::Colour (0xfff2d56b), t));
                const int x0 = area.getX() + col * area.getWidth() / DisplayAnalyser::kWaterCols;
                const int x1 = area.getX() + (col + 1) * area.getWidth() / DisplayAnalyser::kWaterCols;
                g.fillRect (x0, area.getY() + (int) (row * rh), juce::jmax (1, x1 - x0), juce::jmax (1, (int) rh));
            }
        }
    }
    else if (! anFrame.db.empty())
    {
        strokeDb (anFrame.db, juce::Colour (0xffd7efe8));
        strokeDb (anFrame.dbR, juce::Colour (0xff8fd0c4));
        if (apvts.getRawParameterValue ("anSc")->load() > 0.5f)
            strokeDb (anFrame.dbSc, juce::Colour (0xffe29a4a));
        if (mode == 1)
            strokeDb (anFrame.peakDb, juce::Colour (0xfff2d56b).withAlpha (0.7f));
        strokeDb (refCurve, juce::Colour (0xffe7a0c4));
        if (apvts.getRawParameterValue ("anDiff")->load() > 0.5f && slotA.size() == slotB.size() && slotA.size() > 2)
        {
            std::vector<float> diff (slotA.size());
            for (size_t i = 0; i < slotA.size(); ++i)
                diff[i] = slotA[i] - slotB[i];
            strokeDb (diff, juce::Colour (0xffffb4a2));
        }
    }
    else
    {
        auto plot = [&] (const std::vector<float>& mag, juce::Colour colour)
        {
            if (mag.size() < 2)
                return;
            float peak = 1.0e-6f;
            for (float v : mag)
                peak = juce::jmax (peak, v);
            juce::Path path;
            const int n = (int) mag.size();
            for (int x = 0; x < area.getWidth(); ++x)
            {
                const float t = (float) x / (float) juce::jmax (1, area.getWidth() - 1);
                const int bin = juce::jlimit (0, n - 1, (int) std::round (std::pow (t, 2.0f) * (float) (n - 1)));
                const float y = (float) area.getBottom() - juce::jlimit (0.0f, 1.0f, mag[(size_t) bin] / peak) * (float) (area.getHeight() - 8);
                if (x == 0)
                    path.startNewSubPath ((float) area.getX(), y);
                else
                    path.lineTo ((float) (area.getX() + x), y);
            }
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (1.2f));
        };
        plot (mainMag, juce::Colour (0xffd7efe8));
        plot (scMag, juce::Colour (0xffe29a4a));
        plot (refMag, juce::Colour (0xffe7a0c4));
    }

    if (apvts.getRawParameterValue ("eqOn")->load() > 0.5f && mode != 4)
    {
        juce::Path curve;
        bool started = false;
        const float nyq = displayNyquist();
        for (int x = 0; x < area.getWidth(); ++x)
        {
            const float t = (float) x / (float) juce::jmax (1, area.getWidth() - 1);
            const float hz = displayHz (t);
            if (hz > nyq)
                break;
            const float db = juce::jlimit (-24.0f, 24.0f, audioProcessor.responseDb (hz));
            const float y = (float) area.getCentreY() - db / 24.0f * (float) area.getHeight() * 0.45f;
            if (! started)
            {
                curve.startNewSubPath ((float) (area.getX() + x), y);
                started = true;
            }
            else
                curve.lineTo ((float) (area.getX() + x), y);
        }
        g.setColour (juce::Colour (0xfff2d56b));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
    }

    if (! scNodes.empty())
    {
        const double sr = audioProcessor.getCurrentSampleRate();
        const float nyq = displayNyquist();
        const int step = scNodes.size() > 80 ? 4 : 1;
        juce::Path action;
        bool started = false;
        for (int x = 0; x < area.getWidth(); x += step)
        {
            const float t = (float) x / (float) juce::jmax (1, area.getWidth() - 1);
            const float hz = displayHz (t);
            if (hz > nyq)
                break;
            float db = 0.0f;
            for (const auto& band : scNodes)
                db += chouchoueq::Biquad::peak (sr, band.hz, band.q, band.db).responseDb (hz, sr);
            db = juce::jlimit (-24.0f, 24.0f, db);
            const float y = (float) area.getCentreY() - db / 24.0f * (float) area.getHeight() * 0.45f;
            if (! started)
            {
                action.startNewSubPath ((float) (area.getX() + x), y);
                started = true;
            }
            else
                action.lineTo ((float) (area.getX() + x), y);
        }
        g.setColour (juce::Colour (0xff7ec8a3));
        g.strokePath (action, juce::PathStrokeType (1.6f));
        const bool numberThem = scNodes.size() <= 16;
        const float radius = numberThem ? 6.0f : 3.0f;
        for (int i = 0; i < (int) scNodes.size(); ++i)
        {
            const auto& band = scNodes[(size_t) i];
            if (! numberThem && std::abs (band.db) < 0.4f)
                continue;
            const float x = (float) area.getX() + displayT (band.hz) * (float) area.getWidth();
            const float y = (float) area.getCentreY() - juce::jlimit (-24.0f, 24.0f, band.db) / 24.0f * (float) area.getHeight() * 0.45f;
            g.setColour (juce::Colour (0xff7ec8a3));
            g.fillEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f);
            g.setColour (juce::Colour (0xff1c1f24));
            g.drawEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
            if (numberThem)
            {
                g.setFont (juce::FontOptions (9.0f));
                g.drawText (juce::String (i + 1), (int) x - 8, (int) y - 6, 16, 12, juce::Justification::centred);
            }
        }
    }
    else if (matchDb.size() > 2)
    {
        float extreme = 0.0f;
        for (float v : matchDb)
            extreme = juce::jmax (extreme, std::abs (v));
        if (extreme > 0.05f)
        {
            juce::Path action;
            bool started = false;
            for (int x = 0; x < area.getWidth(); ++x)
            {
                const float t = (float) x / (float) juce::jmax (1, area.getWidth() - 1);
                const float hz = displayHz (t);
                const float bin = hz * (float) audioProcessor.getFftSize() / (float) audioProcessor.getCurrentSampleRate();
                const int n = (int) matchDb.size();
                const float pos = juce::jlimit (0.0f, (float) (n - 1), bin);
                const int i0 = (int) pos;
                const int i1 = juce::jmin (n - 1, i0 + 1);
                const float frac = pos - (float) i0;
                const float db = juce::jlimit (-24.0f, 24.0f, matchDb[(size_t) i0] * (1.0f - frac) + matchDb[(size_t) i1] * frac);
                const float y = (float) area.getCentreY() - db / 24.0f * (float) area.getHeight() * 0.45f;
                if (! started)
                {
                    action.startNewSubPath ((float) (area.getX() + x), y);
                    started = true;
                }
                else
                    action.lineTo ((float) (area.getX() + x), y);
            }
            g.setColour (juce::Colour (0xff7ec8a3));
            g.strokePath (action, juce::PathStrokeType (1.6f));
        }
    }

    if (viewPage == 1 && apvts.getRawParameterValue ("anMeas")->load() > 0.5f && anFrame.fundamental > 0.0f)
    {
        g.setColour (kText.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f));
        const juce::String measures = "Fundamental " + juce::String (anFrame.fundamental, 0) + " Hz"
            + "   Centroid " + juce::String (anFrame.centroid, 0)
            + "   Flatness " + juce::String (anFrame.flatness, 2)
            + "   Rolloff " + juce::String (anFrame.rolloff, 0)
            + "   Slope " + juce::String (anFrame.tilt, 1) + " dB/oct";
        g.drawText (measures, area.getX() + 8, area.getBottom() - 34, area.getWidth() - 16, 14, juce::Justification::left);
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (juce::Colour (0xfff2d56b));
        for (int i = 0; i < anFrame.peakCount; ++i)
        {
            const float px = (float) area.getX() + displayT (anFrame.peaks[i]) * (float) area.getWidth();
            g.drawVerticalLine ((int) px, (float) area.getY() + 16.0f, (float) area.getBottom() - 36.0f);
            const float hz = anFrame.peaks[i];
            const juce::String name = (hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " k" : juce::String (juce::roundToInt (hz)));
            g.drawText (name, (int) px - 22, area.getY() + 16, 44, 12, juce::Justification::centred);
        }
        g.setColour (juce::Colour (0xffe7a0c4));
        for (int i = 0; i < anFrame.harmonicCount; ++i)
        {
            const float px = (float) area.getX() + displayT (anFrame.harmonics[i]) * (float) area.getWidth();
            g.drawVerticalLine ((int) px, (float) area.getCentreY(), (float) area.getBottom() - 36.0f);
            g.drawText ("H" + juce::String (i + 2), (int) px - 12, area.getBottom() - 48, 24, 12, juce::Justification::centred);
        }
    }

    if (cursorOn)
    {
        float hz = xToHz (cursorPos.x);
        juce::String note;
        if (apvts.getRawParameterValue ("anSnap")->load() > 0.5f && hz > 20.0f)
        {
            const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
            const int n = (int) std::lround (midi);
            static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            note = juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1) + "  ";
            hz = 440.0f * std::pow (2.0f, ((float) n - 69.0f) / 12.0f);
        }
        const float db = levelAt (hz, anFrame.db);
        g.setColour (kText.withAlpha (0.35f));
        g.drawVerticalLine ((int) cursorPos.x, (float) area.getY(), (float) area.getBottom());
        g.setColour (kText);
        juce::String cursor = note + juce::String (hz, hz >= 1000.0f ? 0 : 1) + " Hz   " + juce::String (db, 1) + " dB";
        if (apvts.getRawParameterValue ("anNotes")->load() < 0.5f)
            cursor = juce::String (hz, hz >= 1000.0f ? 0 : 1) + " Hz   " + juce::String (db, 1) + " dB";
        g.drawText (cursor, area.getRight() - 220, area.getY() + 18, 210, 14, juce::Justification::right);
    }

    g.setColour (kText.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Main", area.getX() + 8, area.getY() + 4, 70, 14, juce::Justification::left);
    g.setColour (juce::Colour (0xffe29a4a));
    g.drawText ("Sidechain", area.getX() + 78, area.getY() + 4, 70, 14, juce::Justification::left);
    g.setColour (juce::Colour (0xffe7a0c4));
    g.drawText ("Reference", area.getX() + 152, area.getY() + 4, 80, 14, juce::Justification::left);
    g.setColour (juce::Colour (0xfff2d56b));
    g.drawText ("EQ curve", area.getX() + 236, area.getY() + 4, 80, 14, juce::Justification::left);
    g.setColour (juce::Colour (0xff7ec8a3));
    juce::String copyName = "Copy";
    if (! scNodes.empty())
        copyName << "  " << (int) scNodes.size() << " bands";
    else if (matchDb.size() > 2)
    {
        float extreme = 0.0f;
        for (float v : matchDb)
            extreme = juce::jmax (extreme, std::abs (v));
        if (extreme > 0.05f)
            copyName << "  spectral";
    }
    g.drawText (copyName, area.getX() + 320, area.getY() + 4, 160, 14, juce::Justification::left);

    const int n = shownBands();
    if (n == 0)
    {
        g.setColour (kText.withAlpha (0.7f));
        g.drawText ("Double-click: add a band. Drag: frequency and gain. Wheel: Q.",
                    area.reduced (8), juce::Justification::centredBottom);
    }

    for (int i = 0; i < n; ++i)
    {
        const auto c = nodeCentre (i);
        const bool on = apvts.getRawParameterValue (bandId (i, "on"))->load() > 0.5f;
        const bool sel = i == selectedBand;
        g.setColour (on ? juce::Colour (0xfff2d56b) : juce::Colour (0xff6a6254));
        g.fillEllipse (c.x - (sel ? 7.0f : 5.0f), c.y - (sel ? 7.0f : 5.0f), sel ? 14.0f : 10.0f, sel ? 14.0f : 10.0f);
        g.setColour (juce::Colour (0xff1c1f24));
        g.drawEllipse (c.x - (sel ? 7.0f : 5.0f), c.y - (sel ? 7.0f : 5.0f), sel ? 14.0f : 10.0f, sel ? 14.0f : 10.0f, 1.2f);
        g.setColour (sel ? juce::Colour (0xff1c1f24) : kText);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (i + 1), (int) c.x - 8, (int) c.y - 6, 16, 12, juce::Justification::centred);
    }
}

void ChouchouFFTEQEditor::drawPhase (juce::Graphics& g)
{
    auto area = spectrumArea;
    g.setColour (juce::Colour (0xff14181c));
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    const bool grid = apvts.getRawParameterValue ("phGrid")->load() > 0.5f;
    if (grid)
    {
        g.setColour (juce::Colour (0xff2c333a));
        g.drawLine ((float) area.getX(), (float) area.getCentreY(), (float) area.getRight(), (float) area.getCentreY());
        g.drawLine ((float) area.getCentreX(), (float) area.getY(), (float) area.getCentreX(), (float) area.getBottom());
    }

    const float gain = apvts.getRawParameterValue ("phGain")->load() * apvts.getRawParameterValue ("phZoom")->load();
    const float rot = apvts.getRawParameterValue ("phRot")->load() * juce::MathConstants<float>::pi / 180.0f;
    const float c = std::cos (rot), s = std::sin (rot);
    const float sx = apvts.getRawParameterValue ("phScaleX")->load();
    const float sy = apvts.getRawParameterValue ("phScaleY")->load();
    const int mode = (int) std::lround (apvts.getRawParameterValue ("phMode")->load());
    const bool midIn = apvts.getRawParameterValue ("phInput")->load() > 0.5f;
    const float bright = apvts.getRawParameterValue ("phBright")->load();
    const float thick = apvts.getRawParameterValue ("phThick")->load();
    const float decay = apvts.getRawParameterValue ("phDecay")->load();
    const float persist = juce::jmax (40.0f, apvts.getRawParameterValue ("phPersist")->load());
    const auto now = juce::Time::getMillisecondCounter();

    auto place = [&] (float x, float y, float& ox, float& oy)
    {
        if (midIn || mode == 2)
        {
            const float mid = 0.5f * (x + y);
            const float side = 0.5f * (x - y);
            x = mid;
            y = side;
        }
        x *= sx * gain;
        y *= sy * gain;
        const float xr = x * c - y * s;
        const float yr = x * s + y * c;
        ox = (float) area.getCentreX() + xr * (float) area.getWidth() * 0.42f;
        oy = (float) area.getCentreY() - yr * (float) area.getHeight() * 0.42f;
    };

    if (mode == 1)
    {
        juce::Path path;
        for (int i = 0; i < (int) corrHistory.size(); ++i)
        {
            const float x = (float) area.getX() + (float) i / (float) juce::jmax (1, (int) corrHistory.size() - 1) * (float) area.getWidth();
            const float y = (float) area.getCentreY() - corrHistory[(size_t) i] * (float) area.getHeight() * 0.4f;
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xfff2d56b).withAlpha (bright));
        g.strokePath (path, juce::PathStrokeType (thick));
    }
    else
    {
        for (const auto& dot : trail)
        {
            const float age = juce::jlimit (0.0f, 1.0f, (float) (now - dot.born) / persist);
            float px, py;
            place (dot.x, dot.y, px, py);
            g.setColour (juce::Colour (0xffd7efe8).withAlpha (bright * (1.0f - age) * (1.0f - 0.85f * decay)));
            g.fillEllipse (px - thick, py - thick, thick * 2.0f, thick * 2.0f);
        }
    }

    g.setColour (kText);
    g.setFont (juce::FontOptions (12.0f));
    const juce::String info = "Correlation " + juce::String (phFrame.correlation, 2)
        + "    Phase " + juce::String (phFrame.phaseDeg, 1) + " deg"
        + "    Width " + juce::String (phFrame.width, 2)
        + "    Mono " + juce::String (phFrame.mono, 2);
    g.drawText (info, area.reduced (8), juce::Justification::bottomLeft);
}

void ChouchouFFTEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1f24));
    if (viewPage == 2)
        drawPhase (g);
    else
        drawSpectrum (g, spectrumArea);
    if (viewPage == 0)
        paintFftCode (g);

    if (viewPage == 0 && shownBands() > 0 && bandHeader.getHeight() > 0)
    {
        g.setColour (kText.withAlpha (0.75f));
        g.setFont (juce::FontOptions (11.0f));
        const juce::String titles[] { "Frequency", "Gain", "Slope", "Calc", "Q", "Delay", "Pan" };
        for (int i = 0; i < 7; ++i)
            g.drawText (titles[i], bandHeader.getX() + colX[(size_t) i], bandHeader.getY(),
                        colW[(size_t) i], bandHeader.getHeight(), juce::Justification::centredLeft);
    }
}

void ChouchouFFTEQEditor::syncCutRows()
{
    const int n = shownBands();
    for (int i = 0; i < n; ++i)
    {
        const int type = (int) std::lround (apvts.getRawParameterValue (bandId (i, "type"))->load());
        const bool cut = type >= 3;
        auto& row = bands[(size_t) i];
        row.gain.setAlpha (cut ? 0.35f : 1.0f);
        row.slope.setAlpha (cut ? 1.0f : 0.35f);
        row.calc.setAlpha (cut ? 1.0f : 0.35f);
        row.q.setTooltip (cut
                              ? "Butterworth: 0.71 is flat, higher adds a resonance. Q mode: this Q is used on every stage."
                              : "Width of the band.");
    }
}

void ChouchouFFTEQEditor::layoutBands()
{
    const int n = shownBands();
    selectedBand = juce::jlimit (0, juce::jmax (0, n - 1), selectedBand);
    const bool eqPage = viewPage == 0;
    bandViewport.setVisible (eqPage && n > 0);
    const int rowH = 26;
    const int viewW = juce::jmax (1, bandViewport.getWidth());
    const bool scroll = n * rowH > bandViewport.getHeight();
    const int innerW = juce::jmax (1, viewW - (scroll ? 14 : 0));
    bandHolder.setSize (innerW, juce::jmax (1, n * rowH));

    for (int i = 0; i < ChouchouFFTEQAudioProcessor::kMaxBands; ++i)
    {
        auto& row = bands[(size_t) i];
        const bool vis = eqPage && i < n;
        row.index.setVisible (vis);
        row.on.setVisible (vis);
        row.type.setVisible (vis);
        row.freq.setVisible (vis);
        row.gain.setVisible (vis);
        row.slope.setVisible (vis);
        row.calc.setVisible (vis);
        row.q.setVisible (vis);
        row.delay.setVisible (vis);
        row.pan.setVisible (vis);
        if (! vis)
            continue;

        auto r = juce::Rectangle<int> (0, i * rowH, innerW, rowH).reduced (0, 1);
        row.index.setBounds (r.removeFromLeft (26));
        row.index.setColour (juce::TextButton::buttonColourId,
                             i == selectedBand ? juce::Colour (0xfff2d56b) : kBox);
        row.index.setColour (juce::TextButton::textColourOffId,
                             i == selectedBand ? juce::Colour (0xff1c1f24) : kText);
        row.on.setBounds (r.removeFromLeft (68));
        row.type.setBounds (r.removeFromLeft (112).reduced (2, 0));
        const int w = r.getWidth() / 7;
        juce::Component* cells[] { &row.freq, &row.gain, &row.slope, &row.calc, &row.q, &row.delay, &row.pan };
        for (int c = 0; c < 7; ++c)
        {
            const auto cell = r.removeFromLeft (c == 6 ? r.getWidth() : w).reduced (2, 0);
            cells[c]->setBounds (cell);
            if (i == 0)
            {
                colX[(size_t) c] = 26 + 68 + 112 + c * w;
                colW[(size_t) c] = cell.getWidth();
            }
        }
    }
    syncCutRows();
}

void ChouchouFFTEQEditor::resized()
{
    auto area = getLocalBounds().reduced (10, 8);

    auto header = area.removeFromTop (28);
    title.setBounds (header.removeFromLeft (140));
    eqOn.setBounds (header.removeFromLeft (48));
    fftOn.setBounds (header.removeFromLeft (56).reduced (2, 2));
    autoGain.setBounds (header.removeFromLeft (84));
    usOn.setBounds (header.removeFromLeft (92));
    freeze.setBounds (header.removeFromLeft (58));
    windowLabel.setBounds (header.removeFromLeft (84));
    fftSize.setBounds (header.removeFromLeft (72).reduced (0, 2));
    mixCaption.setBounds (header.removeFromLeft (52));
    mix.setBounds (header.reduced (0, 4));

    area.removeFromTop (6);
    const bool eqPage = viewPage == 0;
    const bool ultraPage = viewPage == 3;
    const int bottomH = eqPage ? 346 : (viewPage == 1 ? 96 : (ultraPage ? 168 : 76));
    auto bottom = area.removeFromBottom (bottomH);
    area.removeFromTop (4);
    spectrumArea = area;
    auto tabs = spectrumArea.removeFromTop (22);
    pageUltra.setBounds (tabs.removeFromRight (64).reduced (2, 1));
    pagePhase.setBounds (tabs.removeFromRight (72).reduced (2, 1));
    pageSpec.setBounds (tabs.removeFromRight (88).reduced (2, 1));
    pageEq.setBounds (tabs.removeFromRight (48).reduced (2, 1));

    analyserUi.setVisible (viewPage == 1);
    phaseUi.setVisible (viewPage == 2);
    if (viewPage == 1)
        analyserUi.setBounds (bottom);
    if (viewPage == 2)
        phaseUi.setBounds (bottom);

    for (auto& k : knobs)
    {
        k.slider.setVisible (eqPage);
        k.name.setVisible (eqPage);
    }
    fftCaption.setVisible (eqPage);
    experimentBox.setVisible (eqPage || ultraPage);
    bandViewport.setVisible (eqPage && shownBands() > 0);
    scMode.setVisible (eqPage);
    scCapture.setVisible (eqPage);
    scMatch.setVisible (eqPage);
    scFollow.setVisible (eqPage);
    scUseBands.setVisible (eqPage);
    scBandCaption.setVisible (eqPage);
    scBandN.setVisible (eqPage);
    amountCaption.setVisible (eqPage);
    scAmount.setVisible (eqPage);
    engineLabel.setVisible (eqPage);
    eqEngine.setVisible (eqPage);
    addBand.setVisible (eqPage);
    removeBand.setVisible (eqPage);

    for (auto& k : ultraKnobs)
    {
        k.slider.setVisible (ultraPage);
        k.name.setVisible (ultraPage);
    }
    ultraMin.slider.setVisible (ultraPage);
    ultraMin.name.setVisible (ultraPage);
    ultraMax.slider.setVisible (ultraPage);
    ultraMax.name.setVisible (ultraPage);
    usSolo.setVisible (ultraPage);
    ultraLinear.setVisible (ultraPage);
    ultraHint.setVisible (ultraPage);
    ultraRangeCaption.setVisible (ultraPage);
    ultraDspCaption.setVisible (ultraPage);
    for (auto* b : { &rangeAudible, &range1to60, &rangeFull, &floor18, &floor20, &floor30, &ultraAddBand, &ultraRemoveBand })
        b->setVisible (ultraPage);

    if (ultraPage)
    {
        fftCodeArea = {};
        auto status = bottom.removeFromTop (22);
        experimentBox.setBounds (status.removeFromLeft (120).reduced (0, 1));
        ultraLinear.setBounds (status.removeFromLeft (70));
        usSolo.setBounds (status.removeFromLeft (58));
        ultraAddBand.setBounds (status.removeFromRight (78).reduced (2, 1));
        ultraRemoveBand.setBounds (status.removeFromRight (70).reduced (2, 1));
        ultraHint.setBounds (status.reduced (6, 0));

        auto rangeRow = bottom.removeFromTop (28);
        ultraRangeCaption.setBounds (rangeRow.removeFromLeft (48));
        rangeAudible.setBounds (rangeRow.removeFromLeft (70).reduced (2, 2));
        range1to60.setBounds (rangeRow.removeFromLeft (64).reduced (2, 2));
        rangeFull.setBounds (rangeRow.removeFromLeft (52).reduced (2, 2));
        {
            auto cell = rangeRow.removeFromLeft (rangeRow.getWidth() / 2).reduced (3, 1);
            ultraMin.name.setBounds (cell.removeFromLeft (52));
            ultraMin.slider.setBounds (cell);
        }
        {
            auto cell = rangeRow.reduced (3, 1);
            ultraMax.name.setBounds (cell.removeFromLeft (52));
            ultraMax.slider.setBounds (cell);
        }

        auto dspCap = bottom.removeFromTop (22);
        ultraDspCaption.setBounds (dspCap.removeFromLeft (52));
        floor18.setBounds (dspCap.removeFromLeft (78).reduced (2, 1));
        floor20.setBounds (dspCap.removeFromLeft (78).reduced (2, 1));
        floor30.setBounds (dspCap.removeFromLeft (78).reduced (2, 1));

        auto knobsRow = bottom.removeFromTop (34);
        const int cellW = knobsRow.getWidth() / (int) ultraKnobs.size();
        for (auto& k : ultraKnobs)
        {
            auto cell = knobsRow.removeFromLeft (cellW).reduced (4, 2);
            k.name.setBounds (cell.removeFromLeft (44));
            k.slider.setBounds (cell);
        }
        updateUltraReadout();
        layoutBands();
        return;
    }

    if (! eqPage)
    {
        fftCodeArea = {};
        layoutBands();
        return;
    }

    auto expRow = bottom.removeFromTop (24);
    bottom.removeFromTop (6);
    fftCodeArea = bottom.removeFromTop (48);
    bottom.removeFromTop (2);
    for (int row = 0; row < 4; ++row)
    {
        auto line = bottom.removeFromTop (22);
        const int cellW = line.getWidth() / 3;
        for (int col = 0; col < 3; ++col)
        {
            auto cell = line.removeFromLeft (cellW).reduced (3, 1);
            auto& k = knobs[(size_t) (row * 3 + col)];
            k.name.setBounds (cell.removeFromLeft (78));
            k.slider.setBounds (cell);
        }
    }
    bottom.removeFromTop (2);
    auto side = bottom.removeFromTop (24);
    bottom.removeFromTop (2);
    auto bandRow = bottom.removeFromTop (24);
    bottom.removeFromTop (2);
    auto inspector = bottom.removeFromTop (26);
    bottom.removeFromTop (2);
    bandHeader = bottom.removeFromTop (14);
    bandViewport.setBounds (bottom);

    fftCaption.setBounds (expRow.removeFromLeft (42));
    experimentBox.setBounds (expRow.removeFromLeft (130).reduced (0, 2));

    scMode.setBounds (side.removeFromLeft (118).reduced (0, 2));
    scCapture.setBounds (side.removeFromLeft (78));
    scMatch.setBounds (side.removeFromLeft (124));
    scFollow.setBounds (side.removeFromLeft (108));
    amountCaption.setBounds (side.removeFromLeft (58));
    scAmount.setBounds (side.reduced (0, 3));

    scUseBands.setBounds (bandRow.removeFromLeft (84));
    scBandCaption.setBounds (bandRow.removeFromLeft (58));
    scBandN.setBounds (bandRow.removeFromLeft (220).reduced (0, 3));

    engineLabel.setBounds (inspector.removeFromLeft (58));
    eqEngine.setBounds (inspector.removeFromLeft (92).reduced (0, 4));
    addBand.setBounds (inspector.removeFromRight (78).reduced (4, 4));
    removeBand.setBounds (inspector.removeFromRight (92).reduced (4, 4));

    layoutBands();
}
