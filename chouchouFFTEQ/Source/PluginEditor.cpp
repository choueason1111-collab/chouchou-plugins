#include "PluginEditor.h"
#include <algorithm>

namespace
{
juce::String fr (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

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
    setSize (920, 612);
    setResizable (true, true);
    setResizeLimits (780, 540, 1100, 740);

    styleLabel (title, "ChouchouFFTEQ", 16.0f);
    addAndMakeVisible (title);

    eqOn.setButtonText ("EQ");
    fftOn.setButtonText ("FFT");
    autoGain.setButtonText ("Auto Gain");
    scMatch.setButtonText ("Copier le spectre");
    scFollow.setButtonText ("Suivi de hauteur");
    scCapture.setButtonText ("Capturer");
    freeze.setButtonText ("Geler");
    for (auto* b : { &eqOn, &fftOn, &autoGain, &scMatch, &scFollow, &scCapture, &freeze })
    {
        styleToggle (*b);
        addAndMakeVisible (b);
    }

    styleLinear (mix);
    addAndMakeVisible (mix);
    styleLinear (scAmount);
    addAndMakeVisible (scAmount);

    styleCombo (scMode);
    styleCombo (fftSize);
    styleCombo (eqEngine);
    scMode.addItemList (juce::StringArray { fr ("Arr\u00eat"), fr ("Instantan\u00e9"), fr ("Temps r\u00e9el") }, 1);
    fftSize.addItemList (juce::StringArray { "512", "1024", "2048", "4096" }, 1);
    eqEngine.addItemList (juce::StringArray { "Original", "IIR", "FIR" }, 1);
    addAndMakeVisible (scMode);
    addAndMakeVisible (fftSize);
    addAndMakeVisible (eqEngine);

    addBand.setButtonText ("Ajouter");
    addBand.onClick = [this]
    {
        const int n = shownBands();
        if (n < ChouchouFFTEQAudioProcessor::kMaxBands)
            addBandAt (1000.0f, 0.0f);
    };
    addAndMakeVisible (addBand);
    removeBand.setButtonText ("Supprimer");
    removeBand.onClick = [this] { removeSelectedBand(); };
    removeBand.setTooltip ("Enleve la bande selectionnee. Les suivantes remontent.");
    addAndMakeVisible (removeBand);

    styleLabel (engineLabel, "Moteur", 12.0f);
    addAndMakeVisible (engineLabel);
    styleLabel (windowLabel, fr ("Taille de fen\u00eatre"), 12.0f);
    addAndMakeVisible (windowLabel);
    styleLabel (mixCaption, "Dry/Wet", 12.0f);
    addAndMakeVisible (mixCaption);
    styleLabel (amountCaption, fr ("Quantit\u00e9"), 12.0f);
    addAndMakeVisible (amountCaption);
    styleLabel (fftCaption, "code", 13.0f);
    addAndMakeVisible (fftCaption);
    fftCode.setFont (juce::FontOptions (12.0f).withName (juce::Font::getDefaultMonospacedFontName()));
    fftCode.setColour (juce::Label::textColourId, juce::Colour (0xfff2d56b));
    fftCode.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (fftCode);
    styleCombo (experimentBox);
    addAndMakeVisible (experimentBox);

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
        row.on.setButtonText ("Marche");
        styleToggle (row.on);
        bandHolder.addAndMakeVisible (row.on);
        styleCombo (row.type);
        row.type.addItemList (juce::StringArray { fr ("Cr\u00eate"), "Shelf bas", "Shelf haut", "Coupe bas", "Coupe haut" }, 1);
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
        row.slope.setTooltip (fr ("Pente de la coupe. 6 dB/oct = 1er ordre, 12 = 2e, 24 = 4e, 48 = 8e."));
        styleCombo (row.calc);
        row.calc.setTooltip (fr ("Butterworth : pente r\u00e9guli\u00e8re. Q : chaque palier de 12 dB/oct reprend le Q."));
        bandHolder.addAndMakeVisible (row.calc);
        row.delay.setTextValueSuffix (" ms");
        row.delay.setNumDecimalPlacesToDisplay (2);
        row.delay.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
        row.pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 18);
        row.delay.setTooltip (fr ("D\u00e9lai de cette bande. 0 ms garde la phase d'origine."));
        row.pan.setDoubleClickReturnValue (true, 0.0);
        row.pan.setTooltip ("Pan de cette bande. Double-clic : centre.");
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
        { "shift", "shift", "peekPolar(idx * shift, reel, image). 1 = le bin lit sa propre place." },
        { "shiftreel", "shiftreel", "reel = peek(idx * shiftreel). L'amplitude vient d'un autre bin." },
        { "shiftimage", "shiftimage", "image = peek(idx * shiftimage). La phase vient d'un autre bin." },
        { "mulReel", "mulreel", "re = reel * cos(image) * mulreel. 1 = aucun changement." },
        { "mulImage", "mulimage", "im = reel * sin(image) * mulimage. 1 = aucun changement." },
        { "offsetBins", "offsetBins", "peekPolar(idx + offsetBins, reel, image). 0 = aucun changement." },
        { "pitch", "pitch", "0_chaine : src = idx * pitch. Au-dessus de 1, la hauteur descend." },
        { "formant", "formant", "Deplace l'enveloppe du timbre. 1 = aucun changement." },
        { "tilt", "tilt", "Penche le spectre. 0 = plat." },
        { "contrast", "contrast", "Accentue les pics. 1 = aucun changement." },
        { "gate", "gate", "if (reel < gate * peak) reel = 0. 0 = tout passe." },
        { "blur", "blur", "Moyenne re/im sur les bins voisins. 0 = net." }
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
    matchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "scMatch", scMatch);
    followAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "scFollow", scFollow);
    capAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "scCapture", scCapture);
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "freeze", freeze);
    mixAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "mix", mix);
    amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "scAmount", scAmount);
    scModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "scMode", scMode);
    fftSizeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "fftSize", fftSize);
    engineAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "eqEngine", eqEngine);
    experimentAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "experiment", experimentBox);

    for (auto* b : { &pageEq, &pageSpec, &pagePhase })
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

    addAndMakeVisible (analyserUi);
    addAndMakeVisible (phaseUi);
    analyserUi.capRef.onClick = [this] { refCurve = anFrame.db; };
    analyserUi.capA.onClick = [this] { slotA = anFrame.db; };
    analyserUi.capB.onClick = [this] { slotB = anFrame.db; };

    apvts.addParameterListener ("bandCount", this);
    audioProcessor.setDisplayOpen (true);
    startTimerHz (timerHz);
}

ChouchouFFTEQEditor::~ChouchouFFTEQEditor()
{
    audioProcessor.setDisplayOpen (false);
    apvts.removeParameterListener ("bandCount", this);
}

void ChouchouFFTEQEditor::parameterChanged (const juce::String&, float)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ChouchouFFTEQEditor> (this)]
    {
        if (safe != nullptr)
        {
            safe->selectedBand = juce::jlimit (0, juce::jmax (0, safe->shownBands() - 1), safe->selectedBand);
            safe->resized();
        }
    });
}

void ChouchouFFTEQEditor::showPage (int page)
{
    viewPage = juce::jlimit (0, 2, page);
    if (viewPage == 1)
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
    repaint();
}

void ChouchouFFTEQEditor::updateFftCode()
{
    const int experiment = (int) apvts.getRawParameterValue ("experiment")->load();
    const auto away = [] (const char* id, float neutral, juce::AudioProcessorValueTreeState& tree)
    {
        return std::abs (tree.getRawParameterValue (id)->load() - neutral) > 0.001f;
    };
    const bool live = apvts.getRawParameterValue ("fftOn")->load() > 0.5f
                      || experiment != 0
                      || away ("pitch", 1.0f, apvts)
                      || away ("formant", 1.0f, apvts)
                      || away ("offsetBins", 0.0f, apvts)
                      || away ("mulReel", 1.0f, apvts)
                      || away ("mulImage", 1.0f, apvts)
                      || away ("tilt", 0.0f, apvts)
                      || away ("contrast", 1.0f, apvts)
                      || apvts.getRawParameterValue ("gate")->load() > 0.001f
                      || apvts.getRawParameterValue ("blur")->load() > 0.01f
                      || away ("shift", 1.0f, apvts)
                      || away ("shiftreel", 1.0f, apvts)
                      || away ("shiftimage", 1.0f, apvts);
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
        default:
            line = "src = idx * pitch;   reel, image = peek(src);\nre = reel * cos(image) * mulreel;   im = reel * sin(image) * mulimage;";
            break;
    }
    if (! live)
        line = "bypass   // coche FFT, ou eloigne shift, mulreel, mulimage de 1\n" + line;
    else if (apvts.getRawParameterValue ("autoGain")->load() > 0.5f)
        line += "\nautogain ramene le niveau : il cache mulreel";
    fftCode.setText (line, juce::dontSendNotification);

    bool used[12] {};
    const auto mark = [&used] (std::initializer_list<int> ids)
    {
        for (int id : ids)
            used[id] = true;
    };
    mark ({ 3, 4, 7, 8, 9, 10, 11 });
    if (experiment == 0)
        mark ({ 5, 6 });
    if (experiment == 1 || experiment == 4)
        mark ({ 0 });
    if (experiment == 2)
        mark ({ 1, 2 });
    if (experiment == 3)
        mark ({ 5 });
    for (int i = 0; i < 12; ++i)
    {
        const float alpha = used[i] ? 1.0f : 0.35f;
        knobs[(size_t) i].slider.setAlpha (alpha);
        knobs[(size_t) i].name.setAlpha (alpha);
    }
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
    setPlain (id ("freq"), hz);
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
    setPlain (id ("freq"), xToHz (e.position.x));
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

void ChouchouFFTEQEditor::drawSpectrum (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (juce::Colour (0xff14181c));
    g.fillRoundedRectangle (area.toFloat(), 6.0f);

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
        return (float) area.getBottom() - 8.0f - t * (float) (area.getHeight() - 22);
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
        for (int x = 0; x < area.getWidth(); ++x)
        {
            const float t = (float) x / (float) juce::jmax (1, area.getWidth() - 1);
            const float db = juce::jlimit (-24.0f, 24.0f, audioProcessor.responseDb (displayHz (t)));
            const float y = (float) area.getCentreY() - db / 24.0f * (float) area.getHeight() * 0.45f;
            if (x == 0)
                curve.startNewSubPath ((float) area.getX(), y);
            else
                curve.lineTo ((float) (area.getX() + x), y);
        }
        g.setColour (juce::Colour (0xfff2d56b));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
    }

    if (viewPage == 1 && apvts.getRawParameterValue ("anMeas")->load() > 0.5f && anFrame.fundamental > 0.0f)
    {
        g.setColour (kText.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f));
        const juce::String measures = "Fondamental " + juce::String (anFrame.fundamental, 0) + " Hz"
            + "   Centroide " + juce::String (anFrame.centroid, 0)
            + "   Plan\u00e9it\u00e9 " + juce::String (anFrame.flatness, 2)
            + "   Rolloff " + juce::String (anFrame.rolloff, 0)
            + "   Pente " + juce::String (anFrame.tilt, 1) + " dB/oct";
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
    g.drawText ("Principal", area.getX() + 8, area.getY() + 4, 70, 14, juce::Justification::left);
    g.setColour (juce::Colour (0xffe29a4a));
    g.drawText ("Sidechain", area.getX() + 78, area.getY() + 4, 70, 14, juce::Justification::left);
    g.setColour (juce::Colour (0xffe7a0c4));
    g.drawText (fr ("R\u00e9f\u00e9rence"), area.getX() + 152, area.getY() + 4, 80, 14, juce::Justification::left);
    g.setColour (juce::Colour (0xfff2d56b));
    g.drawText ("Courbe EQ", area.getX() + 236, area.getY() + 4, 80, 14, juce::Justification::left);

    const int n = shownBands();
    if (n == 0)
    {
        g.setColour (kText.withAlpha (0.7f));
        g.drawText (fr ("Double-clic : ajouter une bande. Glisser : fr\u00e9quence et gain. Molette : Q."),
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
        + "    Largeur " + juce::String (phFrame.width, 2)
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

    if (viewPage == 0 && shownBands() > 0 && bandHeader.getHeight() > 0)
    {
        g.setColour (kText.withAlpha (0.75f));
        g.setFont (juce::FontOptions (11.0f));
        const juce::String titles[] { fr ("Fr\u00e9quence"), "Gain", "Pente", "Calcul", "Q", fr ("D\u00e9lai"), "Pan" };
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
                              ? fr ("Butterworth : 0,71 est plat, plus haut ajoute une r\u00e9sonance. Mode Q : ce Q est repris sur chaque section.")
                              : fr ("Largeur de la bande."));
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
        row.type.setBounds (r.removeFromLeft (96).reduced (2, 0));
        const int w = r.getWidth() / 7;
        juce::Component* cells[] { &row.freq, &row.gain, &row.slope, &row.calc, &row.q, &row.delay, &row.pan };
        for (int c = 0; c < 7; ++c)
        {
            const auto cell = r.removeFromLeft (c == 6 ? r.getWidth() : w).reduced (2, 0);
            cells[c]->setBounds (cell);
            if (i == 0)
            {
                colX[(size_t) c] = 26 + 68 + 96 + c * w;
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
    title.setBounds (header.removeFromLeft (150));
    eqOn.setBounds (header.removeFromLeft (52));
    fftOn.setBounds (header.removeFromLeft (52));
    autoGain.setBounds (header.removeFromLeft (92));
    freeze.setBounds (header.removeFromLeft (64));
    windowLabel.setBounds (header.removeFromLeft (118));
    fftSize.setBounds (header.removeFromLeft (78).reduced (0, 2));
    mixCaption.setBounds (header.removeFromLeft (58));
    mix.setBounds (header.reduced (0, 4));

    area.removeFromTop (6);
    const bool eqPage = viewPage == 0;
    const int bottomH = eqPage ? 322 : (viewPage == 1 ? 96 : 76);
    auto bottom = area.removeFromBottom (bottomH);
    area.removeFromTop (4);
    spectrumArea = area;
    auto tabs = spectrumArea.removeFromTop (22);
    pagePhase.setBounds (tabs.removeFromRight (78).reduced (2, 1));
    pageSpec.setBounds (tabs.removeFromRight (84).reduced (2, 1));
    pageEq.setBounds (tabs.removeFromRight (52).reduced (2, 1));

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
    fftCode.setVisible (eqPage);
    experimentBox.setVisible (eqPage);
    bandViewport.setVisible (eqPage && shownBands() > 0);
    scMode.setVisible (eqPage);
    scCapture.setVisible (eqPage);
    scMatch.setVisible (eqPage);
    scFollow.setVisible (eqPage);
    amountCaption.setVisible (eqPage);
    scAmount.setVisible (eqPage);
    engineLabel.setVisible (eqPage);
    eqEngine.setVisible (eqPage);
    addBand.setVisible (eqPage);
    removeBand.setVisible (eqPage);
    if (! eqPage)
    {
        layoutBands();
        return;
    }

    auto expRow = bottom.removeFromTop (24);
    bottom.removeFromTop (2);
    fftCode.setBounds (bottom.removeFromTop (48));
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
    auto inspector = bottom.removeFromTop (26);
    bottom.removeFromTop (2);
    bandHeader = bottom.removeFromTop (14);
    bandViewport.setBounds (bottom);

    fftCaption.setBounds (expRow.removeFromLeft (42));
    experimentBox.setBounds (expRow.removeFromLeft (130).reduced (0, 2));

    scMode.setBounds (side.removeFromLeft (110).reduced (0, 2));
    scCapture.setBounds (side.removeFromLeft (84));
    scMatch.setBounds (side.removeFromLeft (130));
    scFollow.setBounds (side.removeFromLeft (130));
    amountCaption.setBounds (side.removeFromLeft (64));
    scAmount.setBounds (side.reduced (0, 3));

    engineLabel.setBounds (inspector.removeFromLeft (52));
    eqEngine.setBounds (inspector.removeFromLeft (92).reduced (0, 4));
    addBand.setBounds (inspector.removeFromRight (78).reduced (4, 4));
    removeBand.setBounds (inspector.removeFromRight (92).reduced (4, 4));

    layoutBands();
}
