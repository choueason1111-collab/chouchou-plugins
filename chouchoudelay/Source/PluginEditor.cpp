#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

using namespace chouchou_delay_ui;

namespace
{
    const juce::Colour kBg       { 0xff14161b };
    const juce::Colour kPanel    { 0xff1e222a };
    const juce::Colour kPanelHi  { 0xff2a303b };
    const juce::Colour kText     { 0xffe6e9ef };
    const juce::Colour kTextDim  { 0xff8a93a3 };
    const juce::Colour kAccent   { 0xff4fd1c5 };
    const juce::Colour kWarm     { 0xffff9f43 };
    const juce::Colour kCool     { 0xff8c7bff };

    juce::Colour pitchColour (double semis)
    {
        const float t = (float) juce::jlimit (-1.0, 1.0, semis / 24.0);
        return t >= 0.0f ? kAccent.interpolatedWith (kWarm, t) : kAccent.interpolatedWith (kCool, -t);
    }

    juce::String formatSeconds (double sec)
    {
        const double a = std::abs (sec);
        if (a >= 1.0)  return juce::String (sec, 3) + " s";
        if (a >= 0.01) return juce::String (sec * 1000.0, 2) + " ms";
        return juce::String (sec * 1000.0, 3) + " ms";
    }

    juce::String signedString (double v, int decimals)
    {
        return (v > 0.0 ? "+" : "") + juce::String (v, decimals);
    }

    juce::String gainToDbString (float g)
    {
        if (g <= 0.0f)
            return "-inf dB";
        return juce::String (juce::Decibels::gainToDecibels (g), 1) + " dB";
    }

    juce::String formatMegabytes (juce::int64 bytes)
    {
        return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
    }
}

//==============================================================================
DelayLookAndFeel::DelayLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, kPanelHi);
    setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    setColour (juce::Label::textColourId, kText);
    setColour (juce::TextButton::buttonColourId, kPanelHi);
    setColour (juce::TextButton::buttonOnColourId, kAccent.darker (0.3f));
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, kText);
    setColour (juce::ToggleButton::textColourId, kText);
    setColour (juce::ToggleButton::tickColourId, kAccent);
    setColour (juce::ToggleButton::tickDisabledColourId, kTextDim);
    setColour (juce::ComboBox::backgroundColourId, kPanelHi);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, kTextDim);
    setColour (juce::ComboBox::arrowColourId, kAccent);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.darker (0.4f));
    setColour (juce::PopupMenu::highlightedTextColourId, kText);
}

void DelayLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (3.0f, radius * 0.14f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (kPanelHi);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    float fromAngle = startAngle;
    if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
        fromAngle = startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                         juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = arcR - lineW * 1.2f;
    g.setColour (kPanel.brighter (0.15f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-lineW * 0.35f, -knobR, lineW * 0.7f, knobR * 0.55f, lineW * 0.3f);
    g.setColour (kText);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

//==============================================================================
void CloseButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (down ? juce::Colour (0xffc0392b) : highlighted ? juce::Colour (0xffe74c3c) : kPanelHi);
    g.fillEllipse (r);

    const auto c = r.reduced (r.getWidth() * 0.3f);
    g.setColour (kText);
    g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 2.0f);
    g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 2.0f);
}

//==============================================================================
void TimelineView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (area, 8.0f);

    auto plot = area.reduced (14.0f, 10.0f).withTrimmedBottom (16.0f).withTrimmedLeft (34.0f);
    const int count = proc.getEchoCount();

    double maxT = 0.0;
    for (int e = 1; e <= count; ++e)
        maxT = juce::jmax (maxT, proc.getEchoTimeSeconds (e));
    maxT = juce::jmax (1.0e-4, maxT * 1.08);

    auto dbToY = [&] (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 86.0f);
        return plot.getBottom() - t * plot.getHeight();
    };
    auto timeToX = [&] (double t) { return plot.getX() + (float) (t / maxT) * plot.getWidth(); };

    g.setFont (juce::FontOptions (11.0f));
    for (float db : { 0.0f, -40.0f, -80.0f })
    {
        const float y = dbToY (db);
        g.setColour (kPanelHi);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (kTextDim);
        g.drawText (juce::String ((int) db) + " dB", juce::Rectangle<float> (area.getX() + 4.0f, y - 7.0f, 44.0f, 14.0f),
                    juce::Justification::centredLeft);
    }

    g.drawText ("0", juce::Rectangle<float> (plot.getX() - 4.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f),
                juce::Justification::centredLeft);
    g.drawText (formatSeconds (maxT), juce::Rectangle<float> (plot.getRight() - 90.0f, plot.getBottom() + 2.0f, 90.0f, 14.0f),
                juce::Justification::centredRight);
    g.drawText ("decay curve / echo gain", juce::Rectangle<float> (plot.getCentreX() - 100.0f, plot.getBottom() + 2.0f, 200.0f, 14.0f),
                juce::Justification::centred);

    const double decay = juce::jmax (ChouchouDelayAudioProcessor::kMinDecaySeconds,
                                     (double) proc.getAPVTS().getRawParameterValue ("decay")->load());
    juce::Path curve;
    for (int i = 0; i <= 200; ++i)
    {
        const double t = maxT * i / 200.0;
        const float db = (float) (ChouchouDelayAudioProcessor::kDecayTargetDb * t / decay);
        const float x = timeToX (t), y = dbToY (db);
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    g.setColour (kTextDim.withAlpha (0.6f));
    g.strokePath (curve, juce::PathStrokeType (1.2f));

    struct Mark
    {
        int echo = 0;
        float trueX = 0.0f;
        float drawX = 0.0f;
        float top = 0.0f;
        bool audible = false;
        juce::Colour colour { 0xff4fd1c5 };
    };
    std::vector<Mark> marks;
    marks.reserve ((size_t) count);
    const float barW = juce::jlimit (3.0f, 12.0f, plot.getWidth() / (float) (count * 2 + 1));
    for (int e = 1; e <= count; ++e)
    {
        Mark m;
        m.echo = e;
        m.trueX = timeToX (proc.getEchoTimeSeconds (e));
        m.drawX = m.trueX;
        const float gain = proc.getEchoTotalGain (e);
        m.audible = gain > 0.0f;
        m.top = m.audible ? dbToY (juce::Decibels::gainToDecibels (gain)) : plot.getBottom() - 8.0f;
        m.colour = pitchColour (proc.getEchoPitchSemitones (e));
        marks.push_back (m);
    }
    std::sort (marks.begin(), marks.end(), [] (const Mark& a, const Mark& b) { return a.trueX < b.trueX; });

    const float gap = barW + 4.0f;
    for (size_t i = 0; i < marks.size();)
    {
        size_t j = i + 1;
        while (j < marks.size() && marks[j].trueX - marks[i].trueX < gap * (float) (j - i))
            ++j;
        const float centre = 0.5f * (marks[i].trueX + marks[j - 1].trueX);
        const float span = gap * (float) (j - i);
        float start = centre - span * 0.5f + gap * 0.5f;
        const float left = plot.getX() + barW * 0.5f;
        const float right = plot.getRight() - span + gap * 0.5f;
        start = right > left ? juce::jlimit (left, right, start) : left;
        for (size_t k = i; k < j; ++k)
            marks[k].drawX = start + gap * (float) (k - i);
        i = j;
    }

    g.setFont (juce::FontOptions (11.0f));
    for (const auto& m : marks)
    {
        if (std::abs (m.drawX - m.trueX) > 2.0f)
        {
            g.setColour (m.colour.withAlpha (0.45f));
            g.drawLine (m.trueX, plot.getBottom(), m.drawX, m.top, 1.0f);
        }
        if (m.audible)
        {
            g.setColour (m.colour);
            g.fillRoundedRectangle (m.drawX - barW * 0.5f, m.top, barW, plot.getBottom() - m.top, 2.0f);
        }
        else
        {
            g.setColour (kTextDim.withAlpha (0.5f));
            g.drawRoundedRectangle (m.drawX - barW * 0.5f, plot.getBottom() - 8.0f, barW, 8.0f, 2.0f, 1.0f);
        }
        g.setColour (kText);
        g.drawText (juce::String (m.echo), juce::Rectangle<float> (m.drawX - 12.0f, plot.getY() - 1.0f, 24.0f, 12.0f),
                    juce::Justification::centred);
    }
}

//==============================================================================
EchoCell::EchoCell (ChouchouDelayAudioProcessor& p, int echoIndex) : proc (p), echo (echoIndex)
{
    onButton.setTooltip ("Echo " + juce::String (echo) + " on/off");
    addAndMakeVisible (onButton);
    onAttach = std::make_unique<ButtonAttachment> (proc.getAPVTS(), ChouchouDelayAudioProcessor::idOn (echo), onButton);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void EchoCell::refresh (bool selected)
{
    isSelected = selected;
    setAlpha (echo <= proc.getEchoCount() ? 1.0f : 0.35f);
    repaint();
}

void EchoCell::resized()
{
    onButton.setBounds (getWidth() - 26, 2, 24, 22);
}

void EchoCell::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool on = proc.isEchoOn (echo);
    const double semis = proc.getEchoPitchSemitones (echo);

    g.setColour (isSelected ? kPanelHi.brighter (0.15f) : kPanel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? pitchColour (semis).withAlpha (0.9f) : kPanelHi);
    g.drawRoundedRectangle (r, 6.0f, isSelected ? 2.0f : 1.0f);

    auto text = r.reduced (6.0f, 4.0f);
    g.setColour (on ? kText : kTextDim);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (juce::String (echo), text.removeFromTop (18.0f), juce::Justification::centredLeft);

    const float lvlDb = proc.getAPVTS().getRawParameterValue (ChouchouDelayAudioProcessor::idLevel (echo))->load();
    const auto lvl = lvlDb <= ChouchouDelayAudioProcessor::kLevelFloorDb ? juce::String ("-inf") : signedString (lvlDb, 1);
    const float pan = proc.getEchoPan (echo);

    auto bar = text.removeFromBottom (7.0f).reduced (2.0f, 1.0f);
    g.setColour (kPanelHi);
    g.fillRoundedRectangle (bar, 2.0f);
    const float x = bar.getX() + (pan + 1.0f) * 0.5f * bar.getWidth();
    g.setColour (on ? kAccent : kTextDim);
    g.fillRoundedRectangle (juce::Rectangle<float> (x - 2.0f, bar.getY(), 4.0f, bar.getHeight()), 1.0f);

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (on ? kText.withAlpha (0.85f) : kTextDim);
    const float lineH = juce::jmin (15.0f, text.getHeight() / 3.0f);
    const bool wholeSemis = std::abs (semis - std::round (semis)) < 1.0e-6;
    static const char* shortNote[] = { "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
    static const char* tupletMark[] = { "", "t", "/5", "/6", "/7" };
    juce::String third = "T " + signedString (proc.getEchoOffsetPercent (echo), 1) + "%";
    if (proc.getTempoMode() != 0)
        third = juce::String (shortNote[proc.getEchoRhythm (echo)])
                + tupletMark[proc.getEchoTuplet (echo)]
                + " x" + juce::String (proc.getEchoRhythmCount (echo));
    const juce::String lines[] = { "L " + lvl,
                                   "P " + signedString (semis, wholeSemis ? 0 : 2),
                                   third };
    for (const auto& line : lines)
        g.drawFittedText (line, text.removeFromTop (lineH).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
}

void EchoCell::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && onOpen)
        onOpen (echo);
}

//==============================================================================
EchoPage::EchoPage (ChouchouDelayAudioProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);

    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);

    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);

    prevButton.setTooltip ("Previous echo");
    nextButton.setTooltip ("Next echo");
    prevButton.onClick = [this] { showEcho (echo > 1 ? echo - 1 : ChouchouDelayAudioProcessor::kMaxEchoes); };
    nextButton.onClick = [this] { showEcho (echo < ChouchouDelayAudioProcessor::kMaxEchoes ? echo + 1 : 1); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (onButton);

    setupKnob (levelKnob, levelName, "Level");
    setupKnob (pitchKnob, pitchName, "Pitch");
    setupKnob (fineKnob, fineName, "Fine");
    setupKnob (offsetKnob, offsetName, "Time Offset");
    setupKnob (panKnob, panName, "Pan");
    setupKnob (countKnob, countName, "Count");
    panKnob.setTooltip ("Pan for this echo. Centre feeds both sides.");
    countKnob.setTooltip ("How many of the chosen note, from 1 to 64.");

    rhythmName.setText ("Rhythm", juce::dontSendNotification);
    rhythmName.setJustificationType (juce::Justification::centred);
    rhythmName.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (rhythmName);
    rhythmBox.addItemList ({ "Half", "Quarter", "8th", "16th", "32nd", "64th" }, 1);
    rhythmBox.setTooltip ("Note for this echo when tempo is Real-time or Tap. 64th is the shortest.");
    addAndMakeVisible (rhythmBox);

    tupletName.setText ("Tuplet", juce::dontSendNotification);
    tupletName.setJustificationType (juce::Justification::centred);
    tupletName.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (tupletName);
    tupletBox.addItemList ({ "Straight", "Triplet", "Quintuplet", "Sextuplet", "Septuplet" }, 1);
    tupletBox.setTooltip ("Straight, or a tuplet: triplet (3 in the time of 2), quintuplet, sextuplet or septuplet (in the time of 4).");
    addAndMakeVisible (tupletBox);

    for (auto* l : { &offsetReadout, &infoReadout })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::textColourId, kText);
        addAndMakeVisible (*l);
    }
    offsetReadout.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    offsetReadout.setColour (juce::Label::textColourId, kAccent);
    infoReadout.setFont (juce::FontOptions (13.0f));
    infoReadout.setColour (juce::Label::textColourId, kTextDim);

    setVisible (false);
}

void EchoPage::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 22);
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (l);
}

void EchoPage::showEcho (int echoIndex)
{
    echo = juce::jlimit (1, ChouchouDelayAudioProcessor::kMaxEchoes, echoIndex);
    auto& apvts = proc.getAPVTS();

    onAttach.reset(); levelAttach.reset(); pitchAttach.reset(); fineAttach.reset(); offsetAttach.reset();
    panAttach.reset(); countAttach.reset(); rhythmAttach.reset(); tupletAttach.reset();
    onAttach     = std::make_unique<ButtonAttachment> (apvts, ChouchouDelayAudioProcessor::idOn (echo), onButton);
    levelAttach  = std::make_unique<SliderAttachment> (apvts, ChouchouDelayAudioProcessor::idLevel (echo), levelKnob);
    pitchAttach  = std::make_unique<SliderAttachment> (apvts, ChouchouDelayAudioProcessor::idPitch (echo), pitchKnob);
    fineAttach   = std::make_unique<SliderAttachment> (apvts, ChouchouDelayAudioProcessor::idFine (echo), fineKnob);
    offsetAttach = std::make_unique<SliderAttachment> (apvts, ChouchouDelayAudioProcessor::idOffset (echo), offsetKnob);
    panAttach    = std::make_unique<SliderAttachment> (apvts, ChouchouDelayAudioProcessor::idPan (echo), panKnob);
    countAttach  = std::make_unique<SliderAttachment> (apvts, ChouchouDelayAudioProcessor::idRhythmCount (echo), countKnob);
    rhythmAttach = std::make_unique<ComboAttachment> (apvts, ChouchouDelayAudioProcessor::idRhythm (echo), rhythmBox);
    tupletAttach = std::make_unique<ComboAttachment> (apvts, ChouchouDelayAudioProcessor::idTuplet (echo), tupletBox);

    title.setText ("Echo " + juce::String (echo), juce::dontSendNotification);
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    refresh();
    repaint();
}

void EchoPage::close()
{
    setVisible (false);
    if (onClosed)
        onClosed();
}

void EchoPage::refresh()
{
    if (! isVisible())
        return;

    const double sr = proc.getSampleRateSafe();
    const double interval = proc.getIntervalSeconds();
    const double pct = proc.getEchoOffsetPercent (echo);
    const bool tempo = proc.getTempoMode() != 0;
    const double baseSeconds = tempo
        ? (double) proc.getEchoRhythmCount (echo) * (60.0 / (double) proc.getCurrentBpm())
              * ChouchouDelayAudioProcessor::rhythmBeats (proc.getEchoRhythm (echo))
              * ChouchouDelayAudioProcessor::tupletFactor (proc.getEchoTuplet (echo))
        : (double) echo * interval;
    const double baseSamples = juce::jmax (1.0, (double) std::llround (baseSeconds * sr));
    const double shiftSamples = proc.getEchoDelaySamples (echo) - baseSamples;
    const juce::String baseText = tempo
        ? juce::String (proc.getEchoRhythmCount (echo)) + " x " + ChouchouDelayAudioProcessor::rhythmName (proc.getEchoRhythm (echo))
              + " " + ChouchouDelayAudioProcessor::tupletName (proc.getEchoTuplet (echo))
        : formatSeconds (interval);

    offsetReadout.setText (signedString (pct, 1) + "% of " + baseText + " = "
                               + (shiftSamples > 0 ? "+" : shiftSamples < 0 ? "-" : "")
                               + formatSeconds (std::abs (shiftSamples) / sr) + " / "
                               + signedString (shiftSamples, 0) + " smp",
                           juce::dontSendNotification);

    const float rhythmAlpha = tempo ? 1.0f : 0.35f;
    rhythmBox.setAlpha (rhythmAlpha);
    rhythmName.setAlpha (rhythmAlpha);
    tupletBox.setAlpha (rhythmAlpha);
    tupletName.setAlpha (rhythmAlpha);
    countKnob.setAlpha (rhythmAlpha);
    countName.setAlpha (rhythmAlpha);

    const auto pitchCol = pitchColour (proc.getEchoPitchSemitones (echo));
    pitchKnob.setColour (juce::Slider::rotarySliderFillColourId, pitchCol);
    fineKnob.setColour (juce::Slider::rotarySliderFillColourId, pitchCol);

    juce::String info = "Time " + formatSeconds (proc.getEchoTimeSeconds (echo))
                        + " (" + juce::String ((juce::int64) proc.getEchoDelaySamples (echo)) + " smp)   |   Gain "
                        + gainToDbString (proc.getEchoTotalGain (echo))
                        + "  (decay " + gainToDbString (proc.getEchoDecayGain (echo))
                        + ", level " + gainToDbString (proc.getEchoLevelGain (echo)) + ")";
    if (echo > proc.getEchoCount())
        info = "Inactive: beyond Echo Count (" + juce::String (proc.getEchoCount()) + ")   |   " + info;
    else if (echo > proc.getBufferCapacityEchoes())
        info = "Buffer growing, echo will fade in shortly   |   " + info;
    infoReadout.setText (info, juce::dontSendNotification);
}

juce::Rectangle<int> EchoPage::panelBounds() const
{
    const int w = juce::jmin (860, getWidth() - 40);
    const int h = juce::jmin (520, getHeight() - 40);
    return getLocalBounds().withSizeKeepingCentre (w, h);
}

void EchoPage::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto panel = panelBounds().toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (panel, 12.0f);
    g.setColour (pitchColour (proc.getEchoPitchSemitones (echo)).withAlpha (0.8f));
    g.drawRoundedRectangle (panel.reduced (0.5f), 12.0f, 1.5f);
}

void EchoPage::resized()
{
    auto r = panelBounds().reduced (18, 14);

    auto header = r.removeFromTop (36);
    closeButton.setBounds (header.removeFromRight (36));
    header.removeFromRight (12);
    nextButton.setBounds (header.removeFromRight (40).reduced (0, 4));
    header.removeFromRight (6);
    prevButton.setBounds (header.removeFromRight (40).reduced (0, 4));
    header.removeFromRight (16);
    onButton.setBounds (header.removeFromRight (80));
    title.setBounds (header);

    r.removeFromTop (10);
    infoReadout.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (4);
    offsetReadout.setBounds (r.removeFromBottom (26));
    r.removeFromBottom (8);

    auto row1 = r.removeFromTop (r.getHeight() / 2);
    auto row2 = r;
    const int knobW = row1.getWidth() / 4;
    juce::Slider* knobs[] = { &levelKnob, &pitchKnob, &fineKnob, &panKnob };
    juce::Label* names[] = { &levelName, &pitchName, &fineName, &panName };
    for (int i = 0; i < 4; ++i)
    {
        auto col = row1.removeFromLeft (knobW).reduced (8, 0);
        names[i]->setBounds (col.removeFromTop (20));
        knobs[i]->setBounds (col);
    }

    auto offsetCol = row2.removeFromLeft (knobW).reduced (8, 0);
    offsetName.setBounds (offsetCol.removeFromTop (20));
    offsetKnob.setBounds (offsetCol);

    auto countCol = row2.removeFromRight (knobW).reduced (8, 0);
    countName.setBounds (countCol.removeFromTop (20));
    countKnob.setBounds (countCol);

    auto tupletCol = row2.removeFromRight (knobW).reduced (8, 0);
    tupletName.setBounds (tupletCol.removeFromTop (20));
    tupletBox.setBounds (tupletCol.removeFromTop (28).reduced (6, 0));

    auto rhythmCol = row2.reduced (8, 0);
    rhythmName.setBounds (rhythmCol.removeFromTop (20));
    rhythmBox.setBounds (rhythmCol.removeFromTop (28).reduced (6, 0));
}

void EchoPage::mouseDown (const juce::MouseEvent& e)
{
    if (! panelBounds().contains (e.getPosition()))
        close();
}

bool EchoPage::keyPressed (const juce::KeyPress& key)
{
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;

    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        proc.tapTempo();
        if (auto* mode = proc.getAPVTS().getParameter ("tempoMode"))
            mode->setValueNotifyingHost (mode->convertTo0to1 (2.0f));
        return true;
    }
    return false;
}

//==============================================================================
ChouchouDelayAudioProcessorEditor::ChouchouDelayAudioProcessorEditor (ChouchouDelayAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), timeline (p), page (p)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    title.setText ("chouchouDelay", juce::dontSendNotification);
    title.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    addAndMakeVisible (title);
    subtitle.setText ("multi-tap  |  pan per echo  |  manual, real-time or tap tempo", juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (subtitle);

    auto& apvts = proc.getAPVTS();
    setupKnob (countKnob, countName, "Echo Count");
    setupKnob (intervalKnob, intervalName, "Interval");
    setupKnob (decayKnob, decayName, "Decay (to -80 dB)");
    setupKnob (mixKnob, mixName, "Mix (dry / wet)");

    countAttach    = std::make_unique<SliderAttachment> (apvts, "echoCount", countKnob);
    intervalAttach = std::make_unique<SliderAttachment> (apvts, "interval", intervalKnob);
    decayAttach    = std::make_unique<SliderAttachment> (apvts, "decay", decayKnob);
    mixAttach      = std::make_unique<SliderAttachment> (apvts, "mix", mixKnob);

    tempoModeBox.addItemList ({ "Manual", "Real-time", "Tap" }, 1);
    tempoModeBox.setTooltip ("Manual: time in seconds. Real-time: tempo is measured from the signal. Tap: set the tempo with the space bar.");
    addAndMakeVisible (tempoModeBox);
    tempoModeAttach = std::make_unique<chouchou_delay_ui::ComboAttachment> (apvts, "tempoMode", tempoModeBox);

    tapButton.setTooltip ("Tap the tempo. The space bar does the same and switches to Tap mode.");
    tapButton.onClick = [this]
    {
        proc.tapTempo();
        if (auto* mode = proc.getAPVTS().getParameter ("tempoMode"))
            mode->setValueNotifyingHost (mode->convertTo0to1 (2.0f));
    };
    addAndMakeVisible (tapButton);

    bpmReadout.setJustificationType (juce::Justification::centredLeft);
    bpmReadout.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (bpmReadout);
    tempoHint.setJustificationType (juce::Justification::centredLeft);
    tempoHint.setColour (juce::Label::textColourId, kTextDim);
    tempoHint.setText ("Each echo: its own pan, note (half to 64th), count, and tuplet (triplet, quintuplet, ...).",
                       juce::dontSendNotification);
    addAndMakeVisible (tempoHint);

    unitButton.setTooltip ("Show interval in milliseconds or samples");
    unitButton.onClick = [this]
    {
        proc.getAPVTS().state.setProperty ("intervalUnit", intervalInSamples() ? "ms" : "smp", nullptr);
        applyIntervalUnit();
    };
    addAndMakeVisible (unitButton);

    intervalReadout.setJustificationType (juce::Justification::centred);
    intervalReadout.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (intervalReadout);

    addAndMakeVisible (timeline);

    gridTitle.setText ("Echoes  (click a cell to open its page)", juce::dontSendNotification);
    gridTitle.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (gridTitle);

    for (int i = 0; i < ChouchouDelayAudioProcessor::kMaxEchoes; ++i)
    {
        cells[(size_t) i] = std::make_unique<EchoCell> (proc, i + 1);
        cells[(size_t) i]->onOpen = [this] (int e) { openEcho (e); };
        addAndMakeVisible (*cells[(size_t) i]);
    }

    footer.setColour (juce::Label::textColourId, kTextDim);
    footer.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (footer);

    page.onClosed = [this] { grabKeyboardFocus(); timerCallback(); };
    addChildComponent (page);

    applyIntervalUnit();

    setResizable (true, true);
    setResizeLimits (1000, 660, 1800, 1200);
    setSize (1200, 760);

    timerCallback();
    startTimerHz (20);
}

ChouchouDelayAudioProcessorEditor::~ChouchouDelayAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ChouchouDelayAudioProcessorEditor::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 120, 24);
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (l);
}

bool ChouchouDelayAudioProcessorEditor::intervalInSamples() const
{
    return proc.getAPVTS().state.getProperty ("intervalUnit", "ms").toString() == "smp";
}

void ChouchouDelayAudioProcessorEditor::applyIntervalUnit()
{
    const bool smp = intervalInSamples();
    unitButton.setButtonText (smp ? "unit: samples" : "unit: ms");

    intervalKnob.textFromValueFunction = [this] (double v)
    {
        if (intervalInSamples())
            return juce::String (juce::jmax ((juce::int64) 1, (juce::int64) std::llround (v * proc.getSampleRateSafe()))) + " smp";
        return formatSeconds (v);
    };
    intervalKnob.valueFromTextFunction = [this] (const juce::String& text)
    {
        auto t = text.trim().toLowerCase();
        const double v = t.getDoubleValue();
        const double sr = proc.getSampleRateSafe();
        double sec;
        if (t.contains ("smp") || t.contains ("sample")) sec = v / sr;
        else if (t.contains ("ms"))                      sec = v / 1000.0;
        else if (t.endsWith ("s"))                       sec = v;
        else                                             sec = intervalInSamples() ? v / sr : v / 1000.0;
        return juce::jlimit (ChouchouDelayAudioProcessor::kMinIntervalSeconds,
                             ChouchouDelayAudioProcessor::kMaxIntervalSeconds, sec);
    };
    intervalKnob.updateText();
}

void ChouchouDelayAudioProcessorEditor::openEcho (int echo)
{
    page.setBounds (getLocalBounds());
    page.showEcho (echo);
    timerCallback();
}

bool ChouchouDelayAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;

    if (key == juce::KeyPress::escapeKey && page.isVisible())
    {
        page.close();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        proc.tapTempo();
        if (auto* mode = proc.getAPVTS().getParameter ("tempoMode"))
            mode->setValueNotifyingHost (mode->convertTo0to1 (2.0f));
        return true;
    }
    return false;
}

void ChouchouDelayAudioProcessorEditor::timerCallback()
{
    const double sr = proc.getSampleRateSafe();
    const double interval = proc.getIntervalSeconds();
    const int mode = proc.getTempoMode();
    intervalReadout.setText ("= " + juce::String (juce::jmax ((juce::int64) 1, (juce::int64) std::llround (interval * sr)))
                                 + " smp  |  " + formatSeconds (interval)
                                 + "  @ " + juce::String (sr / 1000.0, 1) + " kHz",
                             juce::dontSendNotification);
    const float bpmAlpha = mode == 0 ? 0.35f : 1.0f;
    intervalKnob.setAlpha (mode == 0 ? 1.0f : 0.35f);
    intervalName.setAlpha (mode == 0 ? 1.0f : 0.35f);
    unitButton.setAlpha (mode == 0 ? 1.0f : 0.35f);
    juce::String bpmText ("interval, no tempo");
    if (mode != 0)
        bpmText = (mode == 1 ? juce::String ("real-time ") : juce::String ("tap "))
                  + juce::String (proc.getCurrentBpm(), 1) + " BPM";
    bpmReadout.setText (bpmText, juce::dontSendNotification);
    bpmReadout.setAlpha (bpmAlpha);

    const int selected = page.isVisible() ? page.getEcho() : -1;
    for (int i = 0; i < ChouchouDelayAudioProcessor::kMaxEchoes; ++i)
        cells[(size_t) i]->refresh (i + 1 == selected);

    page.refresh();
    timeline.repaint();

    juce::String status = "Buffer: " + juce::String (proc.getBufferCapacityEchoes()) + " echoes, "
                          + formatMegabytes (proc.getBufferBytes())
                          + " (" + juce::String (proc.getBufferChannels()) + " ch)";
    if (proc.isBufferResizePending())
        status << "  |  resizing to " << proc.getEchoCount() << " echoes...";
    status << "  |  latency 0 smp";
    footer.setText (status, juce::dontSendNotification);
}

void ChouchouDelayAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
}

void ChouchouDelayAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 14);

    auto header = r.removeFromTop (36);
    title.setBounds (header.removeFromLeft (220));
    subtitle.setBounds (header);

    auto tempo = r.removeFromTop (28);
    tempoModeBox.setBounds (tempo.removeFromLeft (168).reduced (0, 2));
    tempo.removeFromLeft (6);
    tapButton.setBounds (tempo.removeFromLeft (88).reduced (0, 2));
    tempo.removeFromLeft (8);
    bpmReadout.setBounds (tempo.removeFromLeft (150));
    tempoHint.setBounds (tempo);

    footer.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (6);

    auto top = r.removeFromTop (juce::jlimit (190, 260, r.getHeight() / 3));
    const int colW = top.getWidth() / 4;
    juce::Slider* knobs[] = { &countKnob, &intervalKnob, &decayKnob, &mixKnob };
    juce::Label* names[] = { &countName, &intervalName, &decayName, &mixName };
    for (int i = 0; i < 4; ++i)
    {
        auto col = top.removeFromLeft (colW).reduced (16, 0);
        names[i]->setBounds (col.removeFromTop (22));
        if (i == 1)
        {
            auto bottom = col.removeFromBottom (52);
            intervalReadout.setBounds (bottom.removeFromTop (24));
            unitButton.setBounds (bottom.withSizeKeepingCentre (juce::jmin (140, bottom.getWidth()), 24));
        }
        knobs[i]->setBounds (col);
    }

    r.removeFromTop (10);
    timeline.setBounds (r.removeFromTop (juce::jlimit (130, 220, r.getHeight() / 3)));
    r.removeFromTop (10);
    gridTitle.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);

    constexpr int perRow = 16;
    const int rowH = r.getHeight() / 2;
    for (int row = 0; row < 2; ++row)
    {
        auto line = r.removeFromTop (rowH);
        const int cellW = line.getWidth() / perRow;
        for (int c = 0; c < perRow; ++c)
            cells[(size_t) (row * perRow + c)]->setBounds (line.removeFromLeft (cellW).reduced (2));
    }

    page.setBounds (getLocalBounds());
}
