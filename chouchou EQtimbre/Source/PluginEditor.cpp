#include "PluginEditor.h"
#include "TimbreModel.h"

namespace chouchou_timbre_ui
{
    // Non-ASCII literals must go through fromUTF8, otherwise JUCE reads them as Latin-1.
    static juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    static void strokeMaybeDashed (juce::Graphics& g, const juce::Path& path, float thickness, bool dashed)
    {
        if (! dashed)
        {
            g.strokePath (path, juce::PathStrokeType (thickness));
            return;
        }
        juce::Path dashedPath;
        const float pattern[] = { 4.0f, 3.0f };
        juce::PathStrokeType (thickness).createDashedStroke (dashedPath, path, pattern, 2);
        g.fillPath (dashedPath);
    }

    //==============================================================================
    TimbreLookAndFeel::TimbreLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, Palette::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, Palette::text);
        setColour (juce::ComboBox::backgroundColourId, Palette::background);
        setColour (juce::ComboBox::outlineColourId, Palette::outline);
        setColour (juce::ComboBox::textColourId, Palette::text);
        setColour (juce::PopupMenu::backgroundColourId, Palette::panel);
        setColour (juce::ToggleButton::textColourId, Palette::text);
        setColour (juce::ToggleButton::tickColourId, Palette::accent);
        setColour (juce::TooltipWindow::backgroundColourId, Palette::panel);
        setColour (juce::TextButton::buttonColourId, Palette::panel);
        setColour (juce::TextButton::buttonOnColourId, Palette::random.darker (0.6f));
        setColour (juce::TextButton::textColourOffId, Palette::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::Slider::trackColourId, Palette::random.withAlpha (0.7f));
        setColour (juce::Slider::backgroundColourId, Palette::background);
        setColour (juce::Slider::thumbColourId, Palette::text);
    }

    void TimbreLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                              float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::outline.withMultipliedAlpha (alpha));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float from = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle)
                                   : startAngle;
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f,
                             juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (colour.withMultipliedAlpha (alpha));
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Random stage: reachable range as an outer arc, current random value as a dot.
        RandomRange range;
        const auto& indexVar = slider.getProperties()["rndIndex"];
        if (marks != nullptr && ! indexVar.isVoid() && marks->get ((int) indexVar, range) && range.amount > 0.0f)
        {
            const float ringR = radius + 1.5f;
            const float a0 = startAngle + range.lo * (endAngle - startAngle);
            const float a1 = startAngle + range.hi * (endAngle - startAngle);
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, a0, juce::jmax (a1, a0 + 0.02f), true);
            g.setColour (Palette::random.withAlpha (0.5f * alpha));
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const float av = startAngle + range.value * (endAngle - startAngle);
            const juce::Point<float> dot (centre.x + std::sin (av) * ringR, centre.y - std::cos (av) * ringR);
            g.setColour (Palette::random.withMultipliedAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));
        }

        g.setColour (Palette::panel.brighter (0.15f));
        g.fillEllipse (juce::Rectangle<float> (radius * 1.3f, radius * 1.3f).withCentre (centre));

        const juce::Point<float> tip (centre.x + std::sin (angle) * radius * 0.6f,
                                      centre.y - std::cos (angle) * radius * 0.6f);
        g.setColour (Palette::text.withMultipliedAlpha (alpha));
        g.drawLine ({ centre, tip }, 2.0f);
    }

    // Small "?" in the corner of a switch that the random stage may flip.
    static void paintSwitchMark (juce::Graphics& g, juce::Rectangle<int> bounds, const RandomMarks* marks, int index)
    {
        RandomRange range;
        if (marks == nullptr || ! marks->get (index, range) || range.amount <= 0.0f)
            return;
        auto r = bounds.removeFromRight (14).removeFromTop (14).toFloat();
        g.setColour (Palette::random.withAlpha (0.25f + 0.75f * range.amount));
        g.fillEllipse (r.reduced (1.0f));
        g.setColour (Palette::background);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("?", r, juce::Justification::centred);
    }

    //==============================================================================
    Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& name,
                const juce::String& tooltip, juce::Colour colour)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (13.0f));
        label.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (label);

        attachment = std::make_unique<SliderAttachment> (state, paramId, slider);
        addMouseListener (this, true);
    }

    void Knob::resized()
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        slider.setBounds (r);
    }

    Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& name,
                    const juce::String& tooltip)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            box.addItemList (choice->choices, 1);
        box.setTooltip (tooltip);
        addAndMakeVisible (box);

        label.setText (name, juce::dontSendNotification);
        label.setFont (juce::FontOptions (12.0f));
        label.setColour (juce::Label::textColourId, Palette::dimText);
        addAndMakeVisible (label);

        attachment = std::make_unique<ComboAttachment> (state, paramId, box);
        addMouseListener (this, true);
    }

    void Choice::resized()
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        box.setBounds (r.removeFromTop (24));
    }

    void Choice::paintOverChildren (juce::Graphics& g) { paintSwitchMark (g, getLocalBounds(), marks, randomIndex); }

    Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& name,
                    const juce::String& tooltip)
    {
        button.setButtonText (name);
        button.setTooltip (tooltip);
        addAndMakeVisible (button);
        attachment = std::make_unique<ButtonAttachment> (state, paramId, button);
        addMouseListener (this, true);
    }

    void Toggle::resized() { button.setBounds (getLocalBounds()); }

    void Toggle::paintOverChildren (juce::Graphics& g) { paintSwitchMark (g, getLocalBounds(), marks, randomIndex); }

    //==============================================================================
    namespace
    {
        constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
        constexpr float kTopDb = 0.0f, kBottomDb = -100.0f;
        constexpr float kCombRangeDb = 24.0f;

        float freqToX (float hz, juce::Rectangle<float> r, float maxHz)
        {
            return r.getX() + r.getWidth() * std::log (hz / kMinHz) / std::log (maxHz / kMinHz);
        }

        float dbToY (float db, juce::Rectangle<float> r)
        {
            const float t = (kTopDb - juce::jlimit (kBottomDb, kTopDb, db)) / (kTopDb - kBottomDb);
            return r.getY() + t * r.getHeight();
        }

        float combDbToY (float db, juce::Rectangle<float> r)
        {
            return r.getCentreY() - juce::jlimit (-1.0f, 1.0f, db / kCombRangeDb) * r.getHeight() * 0.45f;
        }

        juce::Colour harmonicColour (int k) { return (k % 2 == 0) ? Palette::even : Palette::odd; }
    }

    HarmonicDisplay::HarmonicDisplay (ChouchouEQtimbreAudioProcessor& p) : proc (p)
    {
        pullBuffer.assign ((size_t) chouchou::AnalyzerFifo::kSize, 0.0f);
        setTooltip (fr ("Blanc : spectre mesuré en sortie. Courbe verte : réponse du peigne Rééquilibrer (échelle ±24 dB à droite). "
                        "Encadré en haut à gauche : effet du dernier réglage survolé ou touché ; forme d’onde sans ce réglage "
                        "(pointillés) et avec (plein), et changement de chaque harmonique. "
                        "Barres : plein = mesuré, cadre = harmoniques que Générer devrait ajouter, flèche = gain de Rééquilibrer. "
                        "Pointillés violets : l’étage aléatoire."));
        startTimerHz (30);
    }

    HarmonicDisplay::~HarmonicDisplay() { stopTimer(); }

    void HarmonicDisplay::timerCallback()
    {
        const int n = proc.getAnalyzerFifo().pull (pullBuffer.data(), (int) pullBuffer.size());
        analyzer.pushSamples (pullBuffer.data(), n);

        f0 = proc.getCurrentF0();
        f0Valid = proc.isPitchActive();
        analyzer.compute (proc.getSampleRateSafe(), f0, f0Valid);

        auto& state = proc.getAPVTS();
        outputGainLin = juce::Decibels::decibelsToGain (state.getRawParameterValue (ParamIDs::outputGain)->load());
        inputGainLin = juce::Decibels::decibelsToGain (state.getRawParameterValue (ParamIDs::inputGain)->load());

        mainValues = proc.getMainValues();
        mainMix = state.getRawParameterValue (ParamIDs::mix)->load() * 0.01f;
        mainView.settings = chouchou::EngineVoice::Settings::fromValues (mainValues, mainMix);
        mainView.inputRms = proc.getMainInputRms();
        mainView.autoGain = proc.getMainAutoGain();
        mainView.f0 = f0;
        mainView.pitchActive = f0Valid;

        randomOn = proc.isRandomAudible();
        randomView.settings = chouchou::EngineVoice::Settings::fromValues (proc.getRandomValues(),
                                                                          state.getRawParameterValue (ParamIDs::rndMix)->load() * 0.01f);
        randomView.inputRms = proc.getRandomInputRms();
        randomView.autoGain = proc.getRandomAutoGain();
        randomView.f0 = proc.getRandomF0();
        randomView.pitchActive = proc.isRandomPitchActive();
        repaint();
    }

    juce::String HarmonicDisplay::statusText() const
    {
        using Status = chouchou::PitchSource::Status;
        const auto hz = juce::String (f0, 1) + " Hz";

        switch (proc.getPitchStatus())
        {
            case Status::manual:        return "f0 " + hz + "  (manuel)";
            case Status::midiNote:      return "f0 " + hz + "  (note MIDI)";
            case Status::midiHeld:      return "f0 " + hz + "  (MIDI, note maintenue)";
            case Status::midiIdle:      return fr ("f0 --  (MIDI : aucune note, rééquilibrage contourné)");
            case Status::autoLocked:    return "f0 " + hz + "  (auto, confiance "
                                               + juce::String (juce::roundToInt (proc.getAutoConfidence() * 100.0f)) + " %)";
            case Status::autoSearching: return fr ("f0 --  (auto : pas de hauteur stable, rééquilibrage contourné)");
        }
        return {};
    }

    void HarmonicDisplay::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Palette::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto inner = r.reduced (12.0f);
        auto header = inner.removeFromTop (20.0f);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (Palette::text);
        g.drawText (statusText(), header, juce::Justification::centredLeft);

        auto legend = header.removeFromRight (150.0f);
        g.setColour (Palette::odd);
        g.drawText ("impairs", legend.removeFromLeft (80.0f), juce::Justification::centredRight);
        g.setColour (Palette::even);
        g.drawText ("pairs", legend, juce::Justification::centredRight);

        if (randomOn)
        {
            g.setColour (Palette::random);
            g.drawText (fr ("étage aléatoire actif (pointillés)"), header.removeFromRight (230.0f), juce::Justification::centredRight);
        }

        inner.removeFromTop (6.0f);
        auto bars = inner.removeFromRight (inner.getWidth() * 0.34f);
        inner.removeFromRight (12.0f);

        paintSpectrum (g, inner);
        paintBars (g, bars);
    }

    float HarmonicDisplay::maxDisplayHz() const
    {
        return juce::jmin (kMaxHz, (float) (0.5 * proc.getSampleRateSafe()));
    }

    void HarmonicDisplay::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const float maxHz = maxDisplayHz();
        g.setColour (Palette::background);
        g.fillRect (r);

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (Palette::dimText);
        g.drawText (fr ("sortie, somme mono  |  jusqu’à ") + juce::String (maxHz / 1000.0f, 1).replaceCharacter ('.', ',') + " kHz",
                    r.reduced (30.0f, 4.0f).removeFromTop (12.0f), juce::Justification::centredRight);

        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            if (hz >= maxHz)
                continue;
            const float x = freqToX (hz, r, maxHz);
            g.setColour (Palette::outline);
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            g.setColour (Palette::dimText);
            g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz),
                        juce::Rectangle<float> (x + 2.0f, r.getBottom() - 12.0f, 30.0f, 12.0f),
                        juce::Justification::centredLeft);
        }

        const double sr = proc.getSampleRateSafe();
        const auto& spec = analyzer.getSpectrumDb();
        const double binHz = sr / chouchou::HarmonicAnalyzer::kSize;

        using Kind = chouchou::TimbreModel::FocusKind;
        const auto kind = chouchou::TimbreModel::focusKind (focusIndex());

        if (f0Valid && f0 > 0.0f)
        {
            const bool pitchFocus = kind == Kind::pitch;
            for (int k = 1; k <= chouchou::HarmonicAnalyzer::kHarmonics; ++k)
            {
                const float hz = f0 * (float) k;
                if (hz < kMinHz || hz > maxHz)
                    continue;
                const float x = freqToX (hz, r, maxHz);
                g.setColour (harmonicColour (k).withAlpha (pitchFocus ? 0.8f : 0.35f));
                if (pitchFocus)
                    g.fillRect (x - 0.75f, r.getY(), 1.5f, r.getHeight());
                else
                    g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
                if (pitchFocus && k <= 6)
                {
                    g.setFont (juce::FontOptions (9.0f));
                    g.drawText ("H" + juce::String (k), juce::Rectangle<float> (x + 2.0f, r.getBottom() - 26.0f, 22.0f, 11.0f),
                                juce::Justification::centredLeft);
                }
            }
        }

        juce::Path path;
        bool started = false;
        const int width = (int) r.getWidth();
        for (int px = 0; px < width; ++px)
        {
            const float f1 = kMinHz * std::pow (maxHz / kMinHz, (float) px / (float) width);
            const float f2 = kMinHz * std::pow (maxHz / kMinHz, (float) (px + 1) / (float) width);
            const int b1 = juce::jlimit (1, (int) spec.size() - 1, (int) (f1 / binHz));
            const int b2 = juce::jlimit (b1, (int) spec.size() - 1, (int) (f2 / binHz));
            float db = chouchou::HarmonicAnalyzer::kFloorDb;
            for (int b = b1; b <= b2; ++b)
                db = juce::jmax (db, spec[(size_t) b]);

            const float y = dbToY (db, r);
            if (! started) { path.startNewSubPath (r.getX() + (float) px, y); started = true; }
            else           path.lineTo (r.getX() + (float) px, y);
        }

        g.setColour (Palette::text.withAlpha (0.8f));
        g.strokePath (path, juce::PathStrokeType (1.2f));

        const bool combFocus = kind == Kind::rebalance || kind == Kind::pitch || kind == Kind::glide
                               || kind == Kind::confidence || kind == Kind::mode;
        paintCombCurve (g, r, mainView, false, combFocus);
        if (randomOn)
            paintCombCurve (g, r, randomView, true, combFocus);
        paintFilterCurves (g, r);

        const float panelW = juce::jmin (320.0f, r.getWidth() * 0.56f);
        const float panelH = r.getHeight() >= 250.0f ? 136.0f : 114.0f;
        paintFocusPanel (g, juce::Rectangle<float> (r.getX() + 6.0f, r.getY() + 6.0f, panelW, panelH));
    }

    int HarmonicDisplay::focusIndex() const
    {
        return focus != nullptr ? focus->index : chouchou::epEven;
    }

    juce::RangedAudioParameter* HarmonicDisplay::focusParameter() const
    {
        const int index = focusIndex();
        auto& state = proc.getAPVTS();
        switch (index)
        {
            case chouchou::TimbreModel::focusInput:  return state.getParameter (ParamIDs::inputGain);
            case chouchou::TimbreModel::focusOutput: return state.getParameter (ParamIDs::outputGain);
            case chouchou::TimbreModel::focusMix:    return state.getParameter (ParamIDs::mix);
            default: break;
        }
        return juce::isPositiveAndBelow (index, (int) chouchou::kNumEngineParams)
                   ? state.getParameter (chouchou::engineParamInfo()[(size_t) index].id) : nullptr;
    }

    // Tonalité low-pass and Coupe DC high-pass, on the comb's ±24 dB scale, when their knob has focus.
    void HarmonicDisplay::paintFilterCurves (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const int index = focusIndex();
        if (index != chouchou::epTone && index != chouchou::epDcCut)
            return;

        const float maxHz = maxDisplayHz();
        const auto& p = mainView.settings.gen;
        const bool isTone = index == chouchou::epTone;
        const float cutoff = isTone ? p.toneHz : p.dcHz;

        juce::Path path;
        const int width = (int) r.getWidth() - 34;
        for (int px = 0; px <= width; px += 2)
        {
            const double hz = kMinHz * std::pow ((double) maxHz / kMinHz, (double) px / (double) r.getWidth());
            const double mag = isTone ? chouchou::TimbreModel::toneMagnitude (hz, cutoff)
                                      : hz / std::sqrt (hz * hz + (double) cutoff * cutoff);
            const float x = r.getX() + (float) px, y = combDbToY ((float) (20.0 * std::log10 (mag + 1.0e-9)), r);
            if (px == 0) path.startNewSubPath (x, y);
            else         path.lineTo (x, y);
        }
        g.setColour (Palette::accent);
        g.strokePath (path, juce::PathStrokeType (2.2f));

        g.setFont (juce::FontOptions (10.0f));
        const float zeroY = combDbToY (0.0f, r);
        if (isTone && cutoff < maxHz)
        {
            const float x = freqToX (juce::jmax (kMinHz, cutoff), r, maxHz);
            juce::Path marker;
            marker.startNewSubPath (x, r.getY());
            marker.lineTo (x, r.getBottom());
            g.setColour (Palette::accent.withAlpha (0.6f));
            strokeMaybeDashed (g, marker, 1.0f, true);
            g.setColour (Palette::accent);
            g.drawText (fr ("Tonalité ") + juce::String (juce::roundToInt (cutoff)) + " Hz",
                        juce::Rectangle<float> (x + 4.0f, zeroY - 16.0f, 110.0f, 12.0f), juce::Justification::centredLeft);
        }
        else if (! isTone)
        {
            // The cutoff (2..30 Hz) is usually below the 20 Hz edge: mark it with an arrow.
            juce::Path arrow;
            arrow.addArrow ({ r.getX() + 40.0f, zeroY + 14.0f, r.getX() + 4.0f, zeroY + 14.0f }, 1.5f, 7.0f, 6.0f);
            g.setColour (Palette::accent);
            g.fillPath (arrow);
            g.drawText (fr ("Coupe DC ") + juce::String (cutoff, 1).replaceCharacter ('.', ',') + fr (" Hz (branche paire)"),
                        juce::Rectangle<float> (r.getX() + 44.0f, zeroY + 8.0f, 200.0f, 12.0f), juce::Justification::centredLeft);
        }
    }

    void HarmonicDisplay::paintCombCurve (juce::Graphics& g, juce::Rectangle<float> r, const VoiceView& v, bool dashed, bool emphasis)
    {
        const auto& s = v.settings;
        if (s.mode == 0 || ! v.pitchActive || v.f0 <= 0.0f || s.rebLevel * s.mix <= 0.0f)
            return;

        const float maxHz = maxDisplayHz();
        const juce::Colour colour = dashed ? Palette::random : Palette::comb;

        if (! dashed)
        {
            g.setFont (juce::FontOptions (9.0f));
            for (float db : { kCombRangeDb, 12.0f, 0.0f, -12.0f, -kCombRangeDb })
            {
                const float y = combDbToY (db, r);
                g.setColour (colour.withAlpha (db == 0.0f ? 0.35f : 0.15f));
                g.drawHorizontalLine ((int) y, r.getRight() - 30.0f, r.getRight());
                g.setColour (colour.withAlpha (0.8f));
                g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db),
                            juce::Rectangle<float> (r.getRight() - 32.0f, y - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
            }
        }

        juce::Path path;
        const int width = (int) r.getWidth() - 34;
        for (int px = 0; px <= width; px += 2)
        {
            const double hz = kMinHz * std::pow ((double) maxHz / kMinHz, (double) px / (double) r.getWidth());
            const auto h = chouchou::TimbreModel::rebalanceResponse (s, hz, v.f0);
            const float db = (float) (20.0 * std::log10 (std::abs (h) + 1.0e-9));
            const float x = r.getX() + (float) px, y = combDbToY (db, r);
            if (px == 0) path.startNewSubPath (x, y);
            else         path.lineTo (x, y);
        }
        g.setColour (colour.withAlpha (emphasis ? 1.0f : 0.45f));
        strokeMaybeDashed (g, path, emphasis ? 2.4f : 1.2f, dashed);
    }

    namespace
    {
        const char* focusExplanation (int index)
        {
            using namespace chouchou;
            switch (index)
            {
                case epMode:         return "G\xc3\xa9n\xc3\xa9rer ajoute des harmoniques, R\xc3\xa9\xc3\xa9quilibrer modifie celles qui existent.";
                case epDrive:        return "Pousse le signal dans le saturateur : plus de drive, plus d\xe2\x80\x99harmoniques.";
                case epDriveComp:    return "Compense la baisse de niveau caus\xc3\xa9\x65 par le drive.";
                case epAutoDrive:    return "Adapte le drive au niveau d\xe2\x80\x99\x65ntr\xc3\xa9\x65 (\xc2\xb1\x32\x34 dB).";
                case epBias:         return "Rend la courbe asym\xc3\xa9trique : c\xe2\x80\x99\x65st ce qui cr\xc3\xa9\x65 les paires.";
                case epEven:         return "Partie asym\xc3\xa9trique : ajoute H2, H4, H6\xe2\x80\xa6 (onde pench\xc3\xa9\x65).";
                case epOdd:          return "Partie sym\xc3\xa9trique : ajoute H3, H5\xe2\x80\xa6 et arrondit les cr\xc3\xaates.";
                case epTone:         return "Passe-bas sur les harmoniques ajout\xc3\xa9\x65s seulement.";
                case epDcCut:        return "Retire le d\xc3\xa9\x63\x61lage cr\xc3\xa9\xc3\xa9 par la partie paire.";
                case epGenLevel:     return "Quantit\xc3\xa9 de G\xc3\xa9n\xc3\xa9rer ajout\xc3\xa9\x65 au signal.";
                case epBalance:      return "Peigne accord\xc3\xa9 sur f0 : + favorise les paires, \xe2\x88\x92 les impaires.";
                case epRebLevel:     return "Quantit\xc3\xa9 de R\xc3\xa9\xc3\xa9quilibrer appliqu\xc3\xa9\x65.";
                case epPitchSource:  return "D\xe2\x80\x99o\xc3\xb9 vient f0 : Manuel, MIDI ou Auto.";
                case epManualHz:     return "f0 en mode Manuel : doit correspondre \xc3\xa0 la note jou\xc3\xa9\x65.";
                case epMidiPriority: return "Quelle note tenue fixe f0 quand il y en a plusieurs.";
                case epMidiHold:     return "Garde la derni\xc3\xa8re note apr\xc3\xa8s rel\xc3\xa2\x63hement.";
                case epProtect:      return "Laisse H1 intact pendant le r\xc3\xa9\xc3\xa9quilibrage.";
                case epConfidence:   return "Mode Auto : confiance minimale avant de r\xc3\xa9\xc3\xa9quilibrer.";
                case epGlide:        return "Suivi des petits changements de hauteur (moins d\xe2\x80\x99un demi-ton).";
                case epJumpFade:     return "Fondu vers un nouveau peigne quand la note saute.";
                case TimbreModel::focusInput:  return "Gain avant les moteurs : plus fort = plus de saturation.";
                case TimbreModel::focusOutput: return "Volume de sortie seulement : ne change pas le timbre.";
                case TimbreModel::focusMix:    return "Sec/trait\xc3\xa9 : 0 % = entr\xc3\xa9\x65 intacte.";
                default: break;
            }
            return "";
        }

        juce::String focusName (int index)
        {
            switch (index)
            {
                case chouchou::TimbreModel::focusInput:  return fr ("Entrée");
                case chouchou::TimbreModel::focusOutput: return "Sortie";
                case chouchou::TimbreModel::focusMix:    return "Mix";
                default: break;
            }
            return juce::isPositiveAndBelow (index, (int) chouchou::kNumEngineParams)
                       ? fr (chouchou::engineParamInfo()[(size_t) index].shortName) : juce::String();
        }

        juce::Colour focusColour (int index)
        {
            using namespace chouchou;
            if (index == epEven) return Palette::even;
            if (index == epOdd)  return Palette::odd;
            if (TimbreModel::focusKind (index) == TimbreModel::FocusKind::rebalance) return Palette::comb;
            return Palette::accent;
        }

        juce::String decimalFr (double value, int places)
        {
            if (places <= 0)
                return juce::String (juce::roundToInt (value));
            return juce::String (value, places).replaceCharacter ('.', ',');
        }

        juce::String paramText (juce::RangedAudioParameter& p, float normalised)
        {
            juce::String text;
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (&p))
            {
                const double v = f->convertFrom0to1 (normalised), mag = std::abs (v);
                const int places = (p.getLabel() == "%" || mag >= 100.0) ? 0 : (mag >= 10.0 ? 1 : 2);
                text = decimalFr (v, places);
            }
            else
            {
                text = p.getText (normalised, 0);
            }
            if (p.getLabel().isNotEmpty())
                text << " " << p.getLabel();
            return text;
        }

        juce::String signedDb (double db)
        {
            return (db >= 0.0 ? "+" : fr ("−")) + decimalFr (std::abs (db), std::abs (db) < 10.0 ? 1 : 0) + " dB";
        }

        // French note names: La3 = 440 Hz.
        juce::String noteName (float hz)
        {
            if (hz <= 0.0f)
                return {};
            static const char* names[] = { "Do", "Do#", "R\xc3\xa9", "R\xc3\xa9#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
            const int midi = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
            return fr (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 2);
        }
    }

    void HarmonicDisplay::paintFocusPanel (juce::Graphics& g, juce::Rectangle<float> box)
    {
        const int index = focusIndex();
        auto* param = focusParameter();
        if (param == nullptr)
            return;

        g.setColour (Palette::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (focusColour (index).withAlpha (0.5f));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);

        auto inner = box.reduced (7.0f, 5.0f);

        auto title = inner.removeFromTop (15.0f);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (focusColour (index));
        const auto name = focusName (index);
        const float nameW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 8.0f;
        g.drawText (name, title.removeFromLeft (nameW), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (Palette::text);
        g.drawText (paramText (*param, param->getValue()), title, juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::dimText);
        g.drawFittedText (fr (focusExplanation (index)), inner.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        RandomRange range;
        if (index < chouchou::kNumEngineParams && marks != nullptr && marks->get (index, range) && range.amount > 0.0f)
        {
            juce::String text = fr ("aléatoire : ");
            if (chouchou::Randomizer::isSwitch (index))
                text << juce::roundToInt (range.amount * 100.0f) << " % de chance";
            else
                text << paramText (*param, range.lo) << fr (" – ") << paramText (*param, range.hi);
            text << ", actuel " << paramText (*param, range.value);
            g.setColour (Palette::random);
            g.drawFittedText (text, inner.removeFromBottom (12.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }
        inner.removeFromTop (3.0f);

        using Kind = chouchou::TimbreModel::FocusKind;
        const auto& s = mainView.settings;
        const auto kind = chouchou::TimbreModel::focusKind (index);
        const int bars = box.getHeight() >= 130.0f ? 8 : 6;

        switch (kind)
        {
            case Kind::generate:
            case Kind::tone:
                if (s.mode == 1)
                    paintTextBody (g, inner, { fr ("Générer est inactif en mode Rééquilibrer.") });
                else
                    paintEffectBody (g, inner, bars);
                break;
            case Kind::rebalance:
                if (s.mode == 0)
                    paintTextBody (g, inner, { fr ("Rééquilibrer est inactif en mode Générer.") });
                else
                    paintEffectBody (g, inner, bars);
                break;
            case Kind::dcCut:
                paintTextBody (g, inner, {
                    fr ("Passe-haut à ") + decimalFr (s.gen.dcHz, 1) + fr (" Hz sur la branche paire seulement."),
                    fr ("La partie paire crée un décalage (DC) ; ce filtre l’enlève."),
                    fr ("Audible seulement sous ~") + juce::String (juce::roundToInt (3.0f * s.gen.dcHz)) + " Hz." });
                break;
            case Kind::pitch:
            {
                juce::StringArray lines;
                if (f0Valid && f0 > 0.0f)
                    lines.add ("f0 " + decimalFr (f0, 1) + " Hz  =  " + noteName (f0));
                else
                    lines.add (fr ("aucune f0 : Rééquilibrer est contourné"));
                lines.add (statusText());
                lines.add (fr ("Lignes verticales du spectre : H1…H10 attendues ; elles doivent tomber sur les pics."));
                paintTextBody (g, inner, lines);
                break;
            }
            case Kind::confidence: paintConfidence (g, inner); break;
            case Kind::glide:      paintGlideDiagram (g, inner); break;
            case Kind::mode:
                paintTextBody (g, inner, {
                    fr ("Générer : nouvelles harmoniques (cadres dans les barres)."),
                    fr ("Rééquilibrer : change celles qui existent (courbe verte, flèches)."),
                    fr ("Les deux : les deux en parallèle.") });
                break;
            case Kind::level:
                paintTextBody (g, inner, {
                    fr ("Gain de sortie : ") + signedDb (juce::Decibels::gainToDecibels (outputGainLin)),
                    fr ("Toutes les harmoniques montent ou baissent ensemble :"),
                    fr ("les rapports entre elles (le timbre) ne changent pas.") });
                break;
        }
    }

    void HarmonicDisplay::paintTextBody (juce::Graphics& g, juce::Rectangle<float> r, const juce::StringArray& lines)
    {
        g.setFont (juce::FontOptions (10.5f));
        for (int i = 0; i < lines.size(); ++i)
        {
            g.setColour (i == 0 ? Palette::text : Palette::dimText);
            g.drawFittedText (lines[i], r.removeFromTop (15.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
        }
    }

    // Waveform without the knob (dashed) and with it (solid), then the change of each harmonic.
    void HarmonicDisplay::paintEffectBody (juce::Graphics& g, juce::Rectangle<float> r, int bars)
    {
        namespace TM = chouchou::TimbreModel;
        const int index = focusIndex();
        const auto effect = TM::knobEffect (mainValues, mainMix, index, mainView.inputRms, mainView.autoGain,
                                            f0Valid && f0 > 0.0f ? f0 : 220.0f, inputGainLin);

        const auto inWave = TM::waveformOf (effect.input);
        const auto offWave = TM::waveformOf (effect.neutral);
        const auto nowWave = TM::waveformOf (effect.current);

        auto wave = r.removeFromLeft (r.getWidth() * 0.47f);
        r.removeFromLeft (8.0f);

        // Waveform: one cycle.
        g.setFont (juce::FontOptions (8.5f));
        g.setColour (Palette::dimText);
        g.drawText (effect.sawInput ? fr ("pointillé : sans ce réglage (dents de scie)")
                                    : fr ("pointillé : sans ce réglage (sinus)"),
                    wave.removeFromBottom (10.0f), juce::Justification::centredLeft);
        g.setColour (Palette::background);
        g.fillRect (wave);
        auto diffStrip = wave.removeFromBottom (wave.getHeight() * 0.32f);

        float peak = 1.0e-6f, diffPeak = 0.0f;
        TM::Waveform diffWave {};
        for (const auto* w : { &inWave, &offWave, &nowWave })
            for (float y : *w)
                peak = juce::jmax (peak, std::abs (y));
        for (int j = 0; j < TM::kWavePoints; ++j)
        {
            diffWave[(size_t) j] = nowWave[(size_t) j] - offWave[(size_t) j];
            diffPeak = juce::jmax (diffPeak, std::abs (diffWave[(size_t) j]));
        }

        auto pathOf = [] (const TM::Waveform& w, juce::Rectangle<float> area, float scale)
        {
            juce::Path p;
            for (int j = 0; j < TM::kWavePoints; ++j)
            {
                const juce::Point<float> pt (area.getX() + area.getWidth() * (float) j / (float) (TM::kWavePoints - 1),
                                             area.getCentreY() - 0.45f * area.getHeight() * w[(size_t) j] / scale);
                if (j == 0) p.startNewSubPath (pt);
                else        p.lineTo (pt);
            }
            return p;
        };

        g.setColour (Palette::outline);
        g.drawHorizontalLine ((int) wave.getCentreY(), wave.getX(), wave.getRight());

        // Entrée / Drive: where the saturator starts to bend (|drive * x| = 1 at the shaper input).
        const auto& gen = mainView.settings.gen;
        if ((index == TM::focusInput || index == chouchou::epDrive) && ! effect.sawInput
            && gen.drive >= chouchou::HarmonicGenerator::kLinearDriveThreshold)
        {
            const float a = gen.autoDrive ? mainView.autoGain : 1.0f;
            const float knee = 1.0f / (gen.drive * juce::jmax (1.0e-6f, a));
            if (knee < peak)
            {
                g.setFont (juce::FontOptions (8.5f));
                for (float sign : { 1.0f, -1.0f })
                {
                    const float y = wave.getCentreY() - 0.45f * wave.getHeight() * sign * knee / peak;
                    juce::Path line;
                    line.startNewSubPath (wave.getX(), y);
                    line.lineTo (wave.getRight(), y);
                    g.setColour (Palette::accent.withAlpha (0.55f));
                    strokeMaybeDashed (g, line, 0.8f, true);
                }
                g.setColour (Palette::accent.withAlpha (0.8f));
                g.drawText (fr ("saturation au-delà"), juce::Rectangle<float> (wave.getX() + 2.0f, wave.getY(), wave.getWidth(), 10.0f),
                            juce::Justification::centredLeft);
            }
        }

        g.setColour (Palette::dimText.withAlpha (0.6f));
        g.strokePath (pathOf (inWave, wave, peak), juce::PathStrokeType (0.8f));
        g.setColour (Palette::text.withAlpha (0.75f));
        strokeMaybeDashed (g, pathOf (offWave, wave, peak), 1.2f, true);
        g.setColour (focusColour (index));
        g.strokePath (pathOf (nowWave, wave, peak), juce::PathStrokeType (1.7f));

        // What the knob adds or removes (plein − pointillé), enlarged so its shape stays visible.
        g.setColour (Palette::outline.withAlpha (0.6f));
        g.drawHorizontalLine ((int) diffStrip.getY(), diffStrip.getX(), diffStrip.getRight());
        g.drawHorizontalLine ((int) diffStrip.getCentreY(), diffStrip.getX(), diffStrip.getRight());
        if (diffPeak > 1.0e-4f * peak)
        {
            const float zoom = peak / diffPeak;
            g.setColour (focusColour (index).withAlpha (0.8f));
            g.strokePath (pathOf (diffWave, diffStrip, diffPeak), juce::PathStrokeType (1.1f));
            g.setFont (juce::FontOptions (8.5f));
            g.setColour (Palette::dimText);
            g.drawText (fr ("différence") + (zoom >= 1.5f ? fr (" ×") + decimalFr (zoom, zoom < 10.0f ? 1 : 0) : juce::String()),
                        diffStrip.reduced (2.0f, 0.0f), juce::Justification::topRight);
        }

        // Change of each harmonic, in dB, from "without" to "with".
        const double floorDb = TM::harmonicDb (effect.input, 1) - 100.0;
        std::array<double, TM::kHarmonics> delta {}, offRel {}, nowRel {};
        const double nowH1 = juce::jmax (floorDb, TM::harmonicDb (effect.current, 1));
        const double offH1 = juce::jmax (floorDb, TM::harmonicDb (effect.neutral, 1));
        for (int k = 1; k <= bars; ++k)
        {
            const double now = juce::jmax (floorDb, TM::harmonicDb (effect.current, k));
            const double off = juce::jmax (floorDb, TM::harmonicDb (effect.neutral, k));
            delta[(size_t) k - 1] = now - off;
            nowRel[(size_t) k - 1] = now - nowH1;
            offRel[(size_t) k - 1] = off - offH1;
        }

        auto textArea = r.removeFromTop (24.0f);
        auto labels = r.removeFromBottom (10.0f);
        g.setColour (Palette::background);
        g.fillRect (r);
        const float slot = r.getWidth() / (float) bars;
        constexpr double kRange = 30.0;
        g.setColour (Palette::outline);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

        for (int k = 1; k <= bars; ++k)
        {
            const double d = juce::jlimit (-kRange, kRange, delta[(size_t) k - 1]);
            const float x = r.getX() + slot * (float) (k - 1);
            const float h = (float) (d / kRange) * r.getHeight() * 0.48f;
            g.setColour (harmonicColour (k));
            if (std::abs (d) < 0.05)
                g.fillRect (x + slot * 0.3f, r.getCentreY() - 0.5f, slot * 0.4f, 1.0f);
            else
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x + slot * 0.2f, juce::jmin (r.getCentreY(), r.getCentreY() - h),
                                                                         x + slot * 0.8f, juce::jmax (r.getCentreY(), r.getCentreY() - h)));
            g.setFont (juce::FontOptions (8.5f));
            g.setColour (Palette::dimText);
            g.drawText ("H" + juce::String (k), juce::Rectangle<float> (x, labels.getY(), slot, 10.0f), juce::Justification::centred);
        }

        // The two biggest changes, in words.
        std::array<int, TM::kHarmonics> order {};
        for (int k = 0; k < bars; ++k)
            order[(size_t) k] = k + 1;
        std::sort (order.begin(), order.begin() + bars,
                   [&] (int a, int b) { return std::abs (delta[(size_t) a - 1]) > std::abs (delta[(size_t) b - 1]); });

        juce::StringArray lines;
        for (int i = 0; i < 2; ++i)
        {
            const int k = order[(size_t) i];
            const double d = delta[(size_t) k - 1];
            if (std::abs (d) < 0.5)
                break;
            const auto h = "H" + juce::String (k);
            if (k == 1)
                lines.add (h + " " + signedDb (d));
            else if (offRel[(size_t) k - 1] < -90.0)
                lines.add (h + fr (" apparaît, ") + signedDb (nowRel[(size_t) k - 1]) + " / H1");
            else if (nowRel[(size_t) k - 1] < -90.0)
                lines.add (h + fr (" disparaît"));
            else
                lines.add (h + " " + signedDb (d) + fr ("  (") + signedDb (nowRel[(size_t) k - 1]) + " / H1)");
        }
        if (lines.isEmpty())
            lines.add (fr ("aucun effet à ce réglage"));

        g.setFont (juce::FontOptions (10.0f));
        for (int i = 0; i < lines.size(); ++i)
        {
            g.setColour (i == 0 ? Palette::text : Palette::dimText);
            g.drawFittedText (lines[i], textArea.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
        }
    }

    // Small changes glide at the Glissement rate; jumps cross-fade to a second comb over Fondu.
    void HarmonicDisplay::paintGlideDiagram (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& s = mainView.settings;
        const bool glideFocus = focusIndex() == chouchou::epGlide;
        auto left = r.removeFromLeft (r.getWidth() * 0.5f).reduced (2.0f, 0.0f);
        auto right = r.reduced (2.0f, 0.0f);

        auto panel = [&] (juce::Rectangle<float> area, const juce::String& caption, const juce::String& value, bool on)
        {
            g.setFont (juce::FontOptions (9.5f));
            g.setColour (on ? Palette::text : Palette::dimText);
            g.drawText (caption, area.removeFromTop (12.0f), juce::Justification::centredLeft);
            g.setColour (on ? focusColour (chouchou::epGlide) : Palette::dimText);
            g.drawText (value, area.removeFromBottom (12.0f), juce::Justification::centredLeft);
            g.setColour (Palette::background);
            g.fillRect (area);
            return area.reduced (4.0f, 4.0f);
        };

        // Glide: pitch step of one octave reached in Glissement ms (axis 0..200 ms).
        {
            auto plot = panel (left, fr ("< ½ ton : glissement"), decimalFr (s.glide, 0) + " ms / octave", glideFocus);
            const float t = juce::jlimit (0.0f, 1.0f, s.glide / 200.0f);
            juce::Path p;
            p.startNewSubPath (plot.getX(), plot.getBottom());
            p.lineTo (plot.getX() + plot.getWidth() * 0.15f, plot.getBottom());
            p.lineTo (plot.getX() + plot.getWidth() * (0.15f + 0.85f * t), plot.getY());
            p.lineTo (plot.getRight(), plot.getY());
            g.setColour (glideFocus ? Palette::accent : Palette::dimText);
            g.strokePath (p, juce::PathStrokeType (glideFocus ? 2.0f : 1.2f));
            g.setFont (juce::FontOptions (8.5f));
            g.setColour (Palette::dimText);
            g.drawText ("f0", plot.withWidth (20.0f).withHeight (10.0f), juce::Justification::centredLeft);
        }

        // Jump: old comb fades out while the comb on the new note fades in (axis 0..100 ms).
        {
            auto plot = panel (right, fr ("> ½ ton : fondu"), decimalFr (s.jumpFade, 0) + " ms", ! glideFocus);
            const float t = juce::jlimit (0.02f, 1.0f, s.jumpFade / 100.0f);
            const float x0 = plot.getX() + plot.getWidth() * 0.15f, x1 = x0 + plot.getWidth() * 0.85f * t;
            juce::Path fadeOut, fadeIn;
            fadeOut.startNewSubPath (plot.getX(), plot.getY());
            fadeOut.lineTo (x0, plot.getY());
            fadeOut.lineTo (x1, plot.getBottom());
            fadeOut.lineTo (plot.getRight(), plot.getBottom());
            fadeIn.startNewSubPath (plot.getX(), plot.getBottom());
            fadeIn.lineTo (x0, plot.getBottom());
            fadeIn.lineTo (x1, plot.getY());
            fadeIn.lineTo (plot.getRight(), plot.getY());
            const float width = glideFocus ? 1.2f : 2.0f;
            g.setColour ((glideFocus ? Palette::dimText : Palette::comb).withAlpha (0.6f));
            strokeMaybeDashed (g, fadeOut, width, true);
            g.setColour (glideFocus ? Palette::dimText : Palette::comb);
            g.strokePath (fadeIn, juce::PathStrokeType (width));
            g.setFont (juce::FontOptions (8.5f));
            g.setColour (Palette::dimText);
            g.drawText (fr ("ancien / nouveau peigne"), plot.withHeight (10.0f).withTrimmedLeft (plot.getWidth() * 0.3f),
                        juce::Justification::centredRight);
        }
    }

    void HarmonicDisplay::paintConfidence (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& s = mainView.settings;
        if (s.pitchSource != 2)
        {
            paintTextBody (g, r, { fr ("Utilisé seulement avec Source de hauteur = Auto."),
                                   fr ("En Manuel ou MIDI, f0 est connue : pas de seuil.") });
            return;
        }

        const float conf = juce::jlimit (0.0f, 1.0f, proc.getAutoConfidence());
        const float threshold = juce::jlimit (0.0f, 1.0f, s.confidence);
        const bool locked = conf >= threshold;

        paintTextBody (g, r.removeFromTop (30.0f), {
            "confiance " + juce::String (juce::roundToInt (conf * 100.0f)) + " %  /  seuil "
                + juce::String (juce::roundToInt (threshold * 100.0f)) + " %",
            locked ? fr ("au-dessus du seuil : rééquilibrage actif") : fr ("sous le seuil : rééquilibrage contourné") });

        auto bar = r.removeFromTop (14.0f).reduced (0.0f, 1.0f);
        g.setColour (Palette::background);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (locked ? Palette::comb : Palette::dimText);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * conf), 3.0f);
        const float tx = bar.getX() + bar.getWidth() * threshold;
        g.setColour (Palette::accent);
        g.fillRect (tx - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f);
    }

    void HarmonicDisplay::paintBars (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Palette::background);
        g.fillRect (r);

        auto legend = r.removeFromTop (24.0f).reduced (4.0f, 1.0f);
        g.setFont (juce::FontOptions (9.5f));
        g.setColour (Palette::dimText);
        g.drawFittedText (fr ("plein = mesuré   cadre = ajout estimé de Générer\nflèche = gain estimé de Rééquilibrer   violet = aléatoire"),
                          legend.toNearestInt(), juce::Justification::centredLeft, 2);
        const int n = chouchou::HarmonicAnalyzer::kHarmonics;
        auto labels = r.removeFromBottom (28.0f);
        const float slot = r.getWidth() / (float) n;

        // Only the estimates of the engine the focused knob belongs to.
        using Kind = chouchou::TimbreModel::FocusKind;
        const auto kind = chouchou::TimbreModel::focusKind (focusIndex());
        const bool showGen = kind == Kind::generate || kind == Kind::tone || kind == Kind::dcCut
                             || kind == Kind::mode || kind == Kind::level;
        const bool showReb = kind == Kind::rebalance || kind == Kind::pitch || kind == Kind::confidence
                             || kind == Kind::glide || kind == Kind::mode || kind == Kind::level;

        const bool genMain = showGen && f0Valid && mainView.settings.mode != 1 && mainView.settings.mix * mainView.settings.genLevel > 0.0f;
        const bool genRand = showGen && f0Valid && randomOn && randomView.settings.mode != 1;
        const bool rebMain = showReb && f0Valid && mainView.settings.mode != 0;
        const bool rebRand = showReb && randomOn && randomView.pitchActive && randomView.settings.mode != 0;

        chouchou::TimbreModel::Levels genMainLv {}, genRandLv {}, rebMainDb {}, rebRandDb {};
        if (genMain) genMainLv = chouchou::TimbreModel::generateOutput (mainView.settings, mainView.inputRms, mainView.autoGain, f0, outputGainLin);
        if (genRand) genRandLv = chouchou::TimbreModel::generateOutput (randomView.settings, randomView.inputRms, randomView.autoGain, f0, outputGainLin);
        if (rebMain) rebMainDb = chouchou::TimbreModel::rebalanceHarmonicDb (mainView.settings);
        if (rebRand) rebRandDb = chouchou::TimbreModel::rebalanceHarmonicDb (randomView.settings);

        auto arrow = [&] (float x, float fromDb, float toDb, juce::Colour c)
        {
            const float y0 = dbToY (fromDb, r), y1 = dbToY (toDb, r);
            if (std::abs (y1 - y0) < 2.0f)
                return;
            juce::Path p;
            p.addArrow ({ x, y0, x, y1 }, 1.5f, 7.0f, 6.0f);
            g.setColour (c);
            g.fillPath (p);
        };

        for (int k = 1; k <= n; ++k)
        {
            const float db = analyzer.getHarmonicDb (k);
            const float x = r.getX() + slot * (float) (k - 1);
            const float top = dbToY (db, r);
            auto bar = juce::Rectangle<float> (x + slot * 0.18f, top, slot * 0.64f, r.getBottom() - top);

            g.setColour (harmonicColour (k).withAlpha (f0Valid ? 0.9f : 0.25f));
            g.fillRect (bar);

            auto estimateBox = [&] (float amp, bool dashed)
            {
                const float edb = juce::Decibels::gainToDecibels (amp, -200.0f);
                if (edb <= kBottomDb)
                    return;
                const float ey = dbToY (edb, r);
                juce::Path p;
                p.addRectangle (juce::Rectangle<float> (x + slot * 0.12f, ey, slot * 0.76f, r.getBottom() - ey));
                g.setColour (dashed ? Palette::random : Palette::text.withAlpha (0.85f));
                strokeMaybeDashed (g, p, 1.2f, dashed);
            };
            if (genMain) estimateBox (genMainLv[(size_t) k - 1], false);
            if (genRand) estimateBox (genRandLv[(size_t) k - 1], true);

            if (rebMain && std::abs (rebMainDb[(size_t) k - 1]) > 0.3f && db > kBottomDb)
                arrow (x + slot * 0.5f, db - juce::jlimit (-40.0f, 40.0f, rebMainDb[(size_t) k - 1]), db, Palette::comb);
            if (rebRand && std::abs (rebRandDb[(size_t) k - 1]) > 0.3f && db > kBottomDb)
                arrow (x + slot * 0.8f, db - juce::jlimit (-40.0f, 40.0f, rebRandDb[(size_t) k - 1]), db, Palette::random);

            g.setFont (juce::FontOptions (11.0f));
            g.setColour (Palette::text);
            g.drawText ("H" + juce::String (k), juce::Rectangle<float> (x, labels.getY(), slot, 14.0f),
                        juce::Justification::centred);
            g.setColour (Palette::dimText);
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (db > kBottomDb ? juce::String (juce::roundToInt (db)) : "-",
                        juce::Rectangle<float> (x, labels.getY() + 14.0f, slot, 12.0f), juce::Justification::centred);
        }
    }

    //==============================================================================
    void ClipLight::refresh()
    {
        const bool now = proc.hasClipped();
        if (now != lit)
        {
            lit = now;
            repaint();
        }
    }

    void ClipLight::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (lit ? juce::Colour (0xffe0483e) : Palette::panel);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (lit ? juce::Colour (0xffe0483e).brighter (0.3f) : Palette::outline);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (lit ? juce::Colours::white : Palette::dimText);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (fr ("ÉCRÊTAGE"), r, juce::Justification::centred);
    }

    //==============================================================================
    AmountRow::AmountRow (ChouchouEQtimbreAudioProcessor& p, const RandomMarks& m, int i) : marks (m), index (i)
    {
        label.setText (fr (chouchou::engineParamInfo()[(size_t) i].shortName), juce::dontSendNotification);
        label.setFont (juce::FontOptions (12.0f));
        label.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::LinearBar);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
        slider.setColour (juce::Slider::textBoxTextColourId, Palette::text);
        slider.setTooltip (chouchou::Randomizer::isSwitch (i)
                               ? fr ("Probabilité que l’étage aléatoire choisisse une option au hasard.")
                               : fr ("Degré de hasard : 0 % = valeur de base, 100 % = complètement aléatoire."));
        addAndMakeVisible (slider);
        attachment = std::make_unique<SliderAttachment> (p.getAPVTS(), ParamIDs::rndAmount (i), slider);
    }

    void AmountRow::resized()
    {
        auto r = getLocalBounds().reduced (4, 2);
        barArea = r.removeFromBottom (8);
        r.removeFromBottom (2);
        label.setBounds (r.removeFromLeft (juce::jmin (112, r.getWidth() / 2)));
        slider.setBounds (r);
    }

    void AmountRow::paint (juce::Graphics& g)
    {
        RandomRange range;
        const bool on = marks.get (index, range);
        const auto bar = barArea.toFloat();
        const float alpha = on ? 1.0f : 0.45f;

        g.setColour (Palette::background);
        g.fillRoundedRectangle (bar, 2.0f);

        const int choices = chouchou::engineParamInfo()[(size_t) index].numChoices;
        if (choices == 0)
        {
            const float lo = bar.getX() + range.lo * bar.getWidth(), hi = bar.getX() + range.hi * bar.getWidth();
            g.setColour (Palette::random.withAlpha (0.35f * alpha));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (lo, bar.getY(), juce::jmax (hi, lo + 1.0f), bar.getBottom()));
            g.setColour (Palette::text.withAlpha (alpha));
            g.fillRect (bar.getX() + range.base * bar.getWidth() - 1.0f, bar.getY() - 1.0f, 2.0f, bar.getHeight() + 2.0f);
            g.setColour (Palette::random.withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ bar.getX() + range.value * bar.getWidth(), bar.getCentreY() }));
        }
        else
        {
            const float w = bar.getWidth() / (float) choices;
            const int baseIdx = juce::roundToInt (range.base * (float) (choices - 1));
            const int valueIdx = juce::roundToInt (range.value * (float) (choices - 1));
            for (int c = 0; c < choices; ++c)
            {
                auto cell = juce::Rectangle<float> (bar.getX() + w * (float) c, bar.getY(), w, bar.getHeight()).reduced (1.0f, 0.0f);
                g.setColour (Palette::random.withAlpha ((c == valueIdx ? 0.9f : 0.15f + 0.3f * range.amount) * alpha));
                g.fillRect (cell);
                if (c == baseIdx)
                {
                    g.setColour (Palette::text.withAlpha (alpha));
                    g.drawRect (cell, 1.0f);
                }
            }
        }
    }

    RandomPage::LabelledSlider::LabelledSlider (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                                                const juce::String& name, const juce::String& tooltip)
    {
        label.setText (name, juce::dontSendNotification);
        label.setFont (juce::FontOptions (12.0f));
        label.setColour (juce::Label::textColourId, Palette::dimText);
        addAndMakeVisible (label);
        slider.setSliderStyle (juce::Slider::LinearBar);
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);
        attachment = std::make_unique<SliderAttachment> (state, paramId, slider);
    }

    void RandomPage::LabelledSlider::resized()
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        slider.setBounds (r.removeFromTop (22));
    }

    RandomPage::RandomPage (ChouchouEQtimbreAudioProcessor& p, const RandomMarks& marks)
        : proc (p),
          bypass   (p.getAPVTS(), ParamIDs::rndBypass, fr ("Bypass aléatoire"),
                    fr ("Contourne l’étage aléatoire : on entend seulement l’effet principal (latence inchangée).")),
          autoRoll (p.getAPVTS(), ParamIDs::rndAuto, "Auto",
                    fr ("Relance automatiquement le tirage à intervalle régulier (seulement si l’étage est actif).")),
          sync     (p.getAPVTS(), ParamIDs::rndSync, "Sync",
                    fr ("Cale l’intervalle automatique sur le tempo de l’hôte.")),
          mix      (p.getAPVTS(), ParamIDs::rndMix, fr ("Mix aléatoire"),
                    fr ("Sec/traité de l’étage aléatoire. 0 % = le son de l’effet principal, tel quel.")),
          interval (p.getAPVTS(), ParamIDs::rndInterval, "Intervalle",
                    fr ("Temps entre deux tirages automatiques, sans Sync.")),
          morph    (p.getAPVTS(), ParamIDs::rndMorph, "Morph",
                    fr ("Durée du glissement vers un nouveau tirage. Les commutateurs changent à mi-chemin.")),
          division (p.getAPVTS(), ParamIDs::rndDivision, "Division", fr ("Intervalle automatique en valeurs de note, avec Sync."))
    {
        rollButton.setButtonText ("Lancer");
        rollButton.setTooltip (fr ("Nouveau tirage pour tous les paramètres."));
        rollButton.onClick = [this] { proc.requestRoll(); };

        adoptButton.setButtonText ("Adopter");
        adoptButton.setTooltip (fr ("Copie les valeurs de l’étage aléatoire dans les réglages principaux et contourne l’étage. "
                                    "Le son n’est pas identique : deux passages deviennent un seul."));
        adoptButton.onClick = [this] { proc.adoptRandom(); };

        rollInfo.setFont (juce::FontOptions (11.0f));
        rollInfo.setColour (juce::Label::textColourId, Palette::dimText);

        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &bypass, &rollButton, &adoptButton, &mix, &autoRoll, &interval, &sync, &division, &morph, &rollInfo })
            addAndMakeVisible (c);

        for (int i = 0; i < ChouchouEQtimbreAudioProcessor::kNumRandom; ++i)
        {
            rows.push_back (std::make_unique<AmountRow> (p, marks, i));
            addAndMakeVisible (*rows.back());
        }

        startTimerHz (10);
    }

    RandomPage::~RandomPage() { stopTimer(); }

    void RandomPage::timerCallback()
    {
        const int count = proc.getRollCount();
        if (count != lastRollCount)
        {
            lastRollCount = count;
            rollInfo.setText (fr ("Valeur = base + quantité × (tirage − base) ; 100 % = complètement aléatoire.   Tirages : ")
                                  + juce::String (count), juce::dontSendNotification);
        }
        for (auto& r : rows)
            r->repaint();
    }

    void RandomPage::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Palette::random.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (fr ("ALÉATOIRE  —  second effet en série après l’effet principal ; le spectre montre la sortie après cet étage"),
                    getLocalBounds().reduced (12, 6).removeFromTop (18), juce::Justification::centredLeft);
    }

    void RandomPage::resized()
    {
        auto r = getLocalBounds().reduced (12, 6);
        r.removeFromTop (22);

        auto top = r.removeFromTop (40);
        auto place = [&top] (juce::Component& c, int w, bool buttonLike)
        {
            auto cell = top.removeFromLeft (w);
            top.removeFromLeft (6);
            c.setBounds (buttonLike ? cell.withTrimmedTop (14).withHeight (24) : cell);
        };
        place (bypass, 140, true);
        place (rollButton, 74, true);
        place (adoptButton, 84, true);
        place (mix, 150, false);
        place (autoRoll, 64, true);
        place (interval, 140, false);
        place (sync, 64, true);
        place (division, 110, false);
        place (morph, juce::jmax (80, top.getWidth()), false);

        r.removeFromTop (2);
        rollInfo.setBounds (r.removeFromTop (16));
        r.removeFromTop (2);

        constexpr int kCols = 4, kRows = 5;
        const int colW = r.getWidth() / kCols, rowH = r.getHeight() / kRows;
        for (int i = 0; i < (int) rows.size(); ++i)
            rows[(size_t) i]->setBounds (r.getX() + colW * (i / kRows), r.getY() + rowH * (i % kRows), colW, rowH);
    }
}

//==============================================================================
using namespace chouchou_timbre_ui;

ChouchouEQtimbreAudioProcessorEditor::ChouchouEQtimbreAudioProcessorEditor (ChouchouEQtimbreAudioProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      mode         (p.getAPVTS(), ParamIDs::mode, "Mode",
                    fr ("Générer ajoute de nouvelles harmoniques, Rééquilibrer modifie celles qui existent, Les deux additionne les deux.")),
      oversampling (p.getAPVTS(), ParamIDs::oversampling, fr ("Suréchantillonnage"),
                    fr ("Suréchantillonnage du saturateur de Générer. Plus élevé = plus propre, mais plus de CPU et de latence.")),
      autoGain     (p.getAPVTS(), ParamIDs::autoGain, "Gain auto",
                    fr ("Aligne le volume de sortie sur l’entrée pour comparer à niveau égal.")),
      autoDrive    (p.getAPVTS(), ParamIDs::autoDrive, "Drive auto",
                    fr ("Générer : adapte le drive au niveau d’entrée (±24 dB), pour obtenir autant d’harmoniques "
                        "sur une piste faible que sur une piste forte. Le signal sec n’est pas modifié.")),
      clipLight    (p),
      drive        (p.getAPVTS(), ParamIDs::drive, "Drive",
                    fr ("Attaque du saturateur. 0 = propre. Plus de drive baisse aussi le niveau, sauf si Comp. Drive est monté.")),
      driveComp    (p.getAPVTS(), ParamIDs::driveComp, "Comp. Drive",
                    fr ("0 % : le niveau baisse quand le drive monte. 100 % garde le niveau des petits signaux, mais seulement jusqu’à +24 dB "
                        "de compensation ; avec beaucoup de Drive x Biais, ça ne suffit plus. Ce n’est pas une égalisation de volume.")),
      bias         (p.getAPVTS(), ParamIDs::bias, "Biais",
                    fr ("Asymétrie du saturateur. Nécessaire pour les harmoniques paires ; change aussi le caractère des impaires.")),
      even         (p.getAPVTS(), ParamIDs::even, "Pairs",
                    fr ("Quantité de la partie paire (asymétrique), jusqu’à 200 %."), Palette::even),
      odd          (p.getAPVTS(), ParamIDs::odd, "Impairs",
                    fr ("Quantité de la partie impaire (symétrique), jusqu’à 200 %. Inclut la compression de H1."), Palette::odd),
      tone         (p.getAPVTS(), ParamIDs::tone, fr ("Tonalité"), fr ("Passe-bas appliqué uniquement aux harmoniques ajoutées.")),
      dcCut        (p.getAPVTS(), ParamIDs::dcCut, "Coupe DC", fr ("Fréquence du filtre anti-DC sur la branche paire.")),
      genLevel     (p.getAPVTS(), ParamIDs::genLevel, "Niveau", fr ("Niveau du moteur Générer.")),
      balance      (p.getAPVTS(), ParamIDs::balance, "Balance",
                    fr ("-100 % favorise les harmoniques impaires, +100 % les paires. À 0 %, aucun effet.")),
      rebLevel     (p.getAPVTS(), ParamIDs::rebLevel, "Niveau", fr ("Niveau du moteur Rééquilibrer.")),
      manualHz     (p.getAPVTS(), ParamIDs::manualHz, "Hauteur",
                    fr ("Fondamentale utilisée en mode Manuel. Doit correspondre à la note jouée.")),
      confidence   (p.getAPVTS(), ParamIDs::confidence, "Confiance",
                    fr ("Mode Auto : confiance de détection minimale avant de rééquilibrer.")),
      glide        (p.getAPVTS(), ParamIDs::glide, "Glissement",
                    fr ("Vitesse de suivi maximale pour les changements de moins d’un demi-ton, en ms par octave.")),
      jumpFade     (p.getAPVTS(), ParamIDs::jumpFade, "Fondu saut",
                    fr ("Un saut de plus d’un demi-ton passe en fondu enchaîné vers un second filtre en peigne accordé sur la nouvelle note, en ce temps-là.")),
      pitchSource  (p.getAPVTS(), ParamIDs::pitchSource, "Source de hauteur",
                    fr ("D’où vient f0. Auto ne fonctionne que sur des sources monophoniques.")),
      midiPriority (p.getAPVTS(), ParamIDs::midiPriority, "Accord MIDI",
                    fr ("Quelle note tenue fixe f0 quand plusieurs sont tenues.")),
      midiHold     (p.getAPVTS(), ParamIDs::midiHold, fr ("Maintenir la dernière note"),
                    fr ("Continue à rééquilibrer sur la dernière note après relâchement ; sinon retour progressif au signal sec.")),
      protect      (p.getAPVTS(), ParamIDs::protect, "Protection fondamentale",
                    fr ("Le rééquilibrage ne modifie pas H1.")),
      inputGain    (p.getAPVTS(), ParamIDs::inputGain, fr ("Entrée"), fr ("Gain d’entrée avant les deux moteurs.")),
      outputGain   (p.getAPVTS(), ParamIDs::outputGain, "Sortie", "Gain de sortie."),
      mix          (p.getAPVTS(), ParamIDs::mix, "Mix",
                    fr ("Sec/traité de l’effet principal. 0 % = l’entrée exacte (latence compensée).")),
      display (p),
      randomPage (p, marks)
{
    auto& state = p.getAPVTS();
    marks.query = [&state, &p] (int index, RandomRange& r)
    {
        auto* param = state.getParameter (chouchou::engineParamInfo()[(size_t) index].id);
        r.base = param->getValue();
        r.amount = juce::jlimit (0.0f, 1.0f, state.getRawParameterValue (ParamIDs::rndAmount (index))->load() * 0.01f);
        const int choices = chouchou::engineParamInfo()[(size_t) index].numChoices;
        r.lo = choices == 0 ? r.base - r.amount * r.base : 0.0f;
        r.hi = choices == 0 ? r.base + r.amount * (1.0f - r.base) : 1.0f;
        r.value = p.getRandomNormalised (index);
        return state.getRawParameterValue (ParamIDs::rndBypass)->load() <= 0.5f;
    };
    lookAndFeel.marks = &marks;
    setLookAndFeel (&lookAndFeel);

    using namespace chouchou;
    drive.setRandomIndex (epDrive);
    driveComp.setRandomIndex (epDriveComp);
    bias.setRandomIndex (epBias);
    even.setRandomIndex (epEven);
    odd.setRandomIndex (epOdd);
    tone.setRandomIndex (epTone);
    dcCut.setRandomIndex (epDcCut);
    genLevel.setRandomIndex (epGenLevel);
    balance.setRandomIndex (epBalance);
    rebLevel.setRandomIndex (epRebLevel);
    manualHz.setRandomIndex (epManualHz);
    confidence.setRandomIndex (epConfidence);
    glide.setRandomIndex (epGlide);
    jumpFade.setRandomIndex (epJumpFade);
    mode.setRandomMarks (&marks, epMode);
    pitchSource.setRandomMarks (&marks, epPitchSource);
    midiPriority.setRandomMarks (&marks, epMidiPriority);
    autoDrive.setRandomMarks (&marks, epAutoDrive);
    midiHold.setRandomMarks (&marks, epMidiHold);
    protect.setRandomMarks (&marks, epProtect);

    using TimbreModel::focusInput, TimbreModel::focusOutput, TimbreModel::focusMix;
    for (auto [knob, index] : std::initializer_list<std::pair<Knob*, int>> {
             { &drive, epDrive }, { &driveComp, epDriveComp }, { &bias, epBias }, { &even, epEven }, { &odd, epOdd },
             { &tone, epTone }, { &dcCut, epDcCut }, { &genLevel, epGenLevel }, { &balance, epBalance },
             { &rebLevel, epRebLevel }, { &manualHz, epManualHz }, { &confidence, epConfidence }, { &glide, epGlide },
             { &jumpFade, epJumpFade }, { &inputGain, focusInput }, { &outputGain, focusOutput }, { &mix, focusMix } })
        knob->setFocus (&knobFocus, index);
    mode.setFocus (&knobFocus, epMode);
    pitchSource.setFocus (&knobFocus, epPitchSource);
    midiPriority.setFocus (&knobFocus, epMidiPriority);
    autoDrive.setFocus (&knobFocus, epAutoDrive);
    midiHold.setFocus (&knobFocus, epMidiHold);
    protect.setFocus (&knobFocus, epProtect);
    display.setSources (&knobFocus, &marks);

    randomButton.setButtonText (fr ("ALÉATOIRE"));
    randomButton.setClickingTogglesState (true);
    randomButton.setTooltip (fr ("Affiche les réglages de l’étage aléatoire (un second effet en série) à la place du spectre."));
    randomButton.onClick = [this] { showRandomPage (randomButton.getToggleState()); };

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &mode, &oversampling, &autoGain, &autoDrive, &randomButton, &clipLight,
             &drive, &driveComp, &bias, &even, &odd, &tone, &dcCut, &genLevel,
             &balance, &rebLevel, &manualHz, &confidence, &glide, &jumpFade, &pitchSource, &midiPriority, &midiHold, &protect,
             &inputGain, &outputGain, &mix, &display })
        addAndMakeVisible (c);
    addChildComponent (randomPage);

    autoNote.setText ("Auto : sources monophoniques uniquement", juce::dontSendNotification);
    autoNote.setFont (juce::FontOptions (11.0f));
    autoNote.setColour (juce::Label::textColourId, Palette::dimText);
    addAndMakeVisible (autoNote);

    setSize (kWidth, kBaseHeight);
    startTimerHz (10);
    timerCallback();
}

ChouchouEQtimbreAudioProcessorEditor::~ChouchouEQtimbreAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ChouchouEQtimbreAudioProcessorEditor::showRandomPage (bool show)
{
    randomButton.setToggleState (show, juce::dontSendNotification);
    randomPage.setVisible (show);
    const int height = show ? kHeightWithRandom : kBaseHeight;
    if (getHeight() != height)
        setSize (kWidth, height);
    else
        resized();
}

void ChouchouEQtimbreAudioProcessorEditor::timerCallback()
{
    auto& state = proc.getAPVTS();
    const int modeIndex = (int) state.getRawParameterValue (ParamIDs::mode)->load();
    const int source = (int) state.getRawParameterValue (ParamIDs::pitchSource)->load();

    const bool genOn = modeIndex != (int) ChouchouEQtimbreAudioProcessor::Mode::rebalance;
    const bool rebOn = modeIndex != (int) ChouchouEQtimbreAudioProcessor::Mode::generate;

    for (juce::Component* c : std::initializer_list<juce::Component*> { &drive, &driveComp, &bias, &even, &odd, &tone, &dcCut, &genLevel, &autoDrive })
        c->setAlpha (genOn ? 1.0f : 0.4f);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &balance, &rebLevel, &glide, &jumpFade, &pitchSource, &protect })
        c->setAlpha (rebOn ? 1.0f : 0.4f);

    if (genOn != genActive || rebOn != rebActive)
    {
        genActive = genOn;
        rebActive = rebOn;
        repaint();
    }

    clipLight.refresh();

    manualHz.setEnabled (source == 0);
    midiPriority.setEnabled (source == 1);
    midiHold.setEnabled (source == 1);
    confidence.setEnabled (source == 2);
    autoNote.setVisible (source == 2);

    manualHz.setAlpha (rebOn && source == 0 ? 1.0f : 0.4f);
    midiPriority.setAlpha (rebOn && source == 1 ? 1.0f : 0.4f);
    midiHold.setAlpha (rebOn && source == 1 ? 1.0f : 0.4f);
    confidence.setAlpha (rebOn && source == 2 ? 1.0f : 0.4f);

    const bool randomOn = state.getRawParameterValue (ParamIDs::rndBypass)->load() <= 0.5f;
    randomButton.setColour (juce::TextButton::textColourOffId, randomOn ? Palette::random : Palette::text);
    if (randomOn || randomOn != marksVisible)
    {
        marksVisible = randomOn;
        for (auto* k : { &drive, &driveComp, &bias, &even, &odd, &tone, &dcCut, &genLevel,
                         &balance, &rebLevel, &manualHz, &confidence, &glide, &jumpFade })
            k->refreshMarks();
        for (juce::Component* c : std::initializer_list<juce::Component*> { &mode, &pitchSource, &midiPriority, &autoDrive, &midiHold, &protect })
            c->repaint();
    }
}

void ChouchouEQtimbreAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("chouchou EQtimbre", 16, 10, 300, 28, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (Palette::dimText);
    g.drawText ("rendu des harmoniques paires / impaires", 16, 34, 300, 14, juce::Justification::centredLeft);

    auto drawPanel = [&g] (juce::Rectangle<int> area, const juce::String& title, bool active)
    {
        const auto r = area.toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Palette::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto titleArea = area.reduced (12, 6).removeFromTop (18);
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title, titleArea, juce::Justification::centredLeft);

        if (! active)
        {
            g.setColour (Palette::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (fr ("inactif dans ce mode, réglages conservés"), titleArea, juce::Justification::centredRight);
        }
    };

    drawPanel (genArea, fr ("GÉNÉRER"), genActive);
    drawPanel (rebArea, fr ("RÉÉQUILIBRER"), rebActive);
    drawPanel (globalArea, "GLOBAL", true);
}

void ChouchouEQtimbreAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto header = r.removeFromTop (48);
    header.removeFromLeft (320);
    mode.setBounds (header.removeFromLeft (150).withTrimmedTop (4));
    header.removeFromLeft (12);
    oversampling.setBounds (header.removeFromLeft (130).withTrimmedTop (4));
    header.removeFromLeft (12);
    autoGain.setBounds (header.removeFromLeft (105).withTrimmedTop (20).withHeight (24));
    autoDrive.setBounds (header.removeFromLeft (110).withTrimmedTop (20).withHeight (24));
    clipLight.setBounds (header.removeFromRight (84).withTrimmedTop (20).withHeight (22));
    header.removeFromRight (8);
    randomButton.setBounds (header.removeFromRight (100).withTrimmedTop (19).withHeight (24));

    r.removeFromTop (8);
    auto panels = r.removeFromTop (250);
    r.removeFromTop (10);
    if (randomPage.isVisible())
    {
        randomPage.setBounds (r.removeFromBottom (kRandomPageHeight));
        r.removeFromBottom (10);
    }
    display.setBounds (r);

    genArea = panels.removeFromLeft (420);
    panels.removeFromLeft (10);
    globalArea = panels.removeFromRight (170);
    panels.removeFromRight (10);
    rebArea = panels;

    {
        auto a = genArea.reduced (10).withTrimmedTop (24);
        auto row1 = a.removeFromTop (a.getHeight() / 2);
        const int w = row1.getWidth() / 4;
        for (auto* k : { &drive, &driveComp, &bias, &dcCut })
            k->setBounds (row1.removeFromLeft (w).reduced (4));
        for (auto* k : { &even, &odd, &tone, &genLevel })
            k->setBounds (a.removeFromLeft (w).reduced (4));
    }

    {
        auto a = rebArea.reduced (10).withTrimmedTop (24);
        auto row1 = a.removeFromTop (a.getHeight() / 2);
        const int w = row1.getWidth() / 6;
        for (auto* k : { &balance, &rebLevel, &manualHz, &confidence, &glide, &jumpFade })
            k->setBounds (row1.removeFromLeft (w).reduced (4));

        a.removeFromTop (4);
        auto choices = a.removeFromTop (44);
        pitchSource.setBounds (choices.removeFromLeft (choices.getWidth() / 2).reduced (4, 0));
        midiPriority.setBounds (choices.reduced (4, 0));

        a.removeFromTop (6);
        auto toggles = a.removeFromTop (24);
        protect.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2));
        midiHold.setBounds (toggles);
        autoNote.setBounds (a.removeFromTop (18));
    }

    {
        auto a = globalArea.reduced (10).withTrimmedTop (24);
        auto row1 = a.removeFromTop (a.getHeight() / 2);
        inputGain.setBounds (row1.removeFromLeft (row1.getWidth() / 2).reduced (4));
        outputGain.setBounds (row1.reduced (4));
        mix.setBounds (a.withSizeKeepingCentre (row1.getWidth() + 8, a.getHeight()).reduced (4));
    }
}
