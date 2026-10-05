#include "PluginEditor.h"

#include <cmath>

using namespace chouchou_trigger_ui;
using Proc = ChouchouTriggerAudioProcessor;

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
    const juce::Colour kHot      { 0xffff5e7a };

    constexpr float kScopeFloorDb = -60.0f;

    juce::Colour pitchColour (double semis)
    {
        const float t = (float) juce::jlimit (-1.0, 1.0, semis / 24.0);
        return t >= 0.0f ? kAccent.interpolatedWith (kWarm, t) : kAccent.interpolatedWith (kCool, -t);
    }

    bool isSupportedAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;mp3;aif;aiff;flac");
    }
}

//==============================================================================
TriggerLookAndFeel::TriggerLookAndFeel()
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
}

void TriggerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
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
    if (s.getMinimum() < 0.0 && std::abs (s.getMaximum() + s.getMinimum()) < 1.0e-6)
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
ScopeView::ScopeView (Proc& p) : proc (p)
{
    history.resize ((size_t) kHistory);
    scratch.resize (4096);

    lengthButton.setTooltip ("Visible time span");
    lengthButton.onClick = [this]
    {
        setDisplaySeconds (displaySeconds < 1.5 ? 2.0 : displaySeconds < 3.0 ? 5.0 : 1.0);
    };
    scaleButton.setTooltip ("Amplitude scale: dB (easier to line up with the threshold) or linear");
    scaleButton.onClick = [this] { setDbScale (! dbScale); };

    addAndMakeVisible (lengthButton);
    addAndMakeVisible (scaleButton);
    setDisplaySeconds (2.0);
    setDbScale (true);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void ScopeView::setDisplaySeconds (double s)
{
    displaySeconds = s;
    lengthButton.setButtonText (juce::String ((int) s) + " s");
    repaint();
}

void ScopeView::setDbScale (bool shouldUseDb)
{
    dbScale = shouldUseDb;
    scaleButton.setButtonText (dbScale ? "dB" : "Lin");
    repaint();
}

void ScopeView::pull()
{
    for (;;)
    {
        const int k = proc.popScopeColumns (scratch.data(), (int) scratch.size());
        for (int i = 0; i < k; ++i)
        {
            history[(size_t) writePos] = scratch[(size_t) i];
            writePos = (writePos + 1) % kHistory;
            ++totalColumns;
        }
        if (k < (int) scratch.size())
            break;
    }
}

int ScopeView::countTriggerMarkers() const
{
    const int n = (int) juce::jmin<juce::int64> (totalColumns, kHistory);
    int count = 0;
    for (int i = 0; i < n; ++i)
        if ((history[(size_t) i].flags & Proc::scopeTrigger) != 0)
            ++count;
    return count;
}

void ScopeView::resized()
{
    auto header = getLocalBounds().reduced (10, 6).removeFromTop (22);
    scaleButton.setBounds (header.removeFromRight (46));
    header.removeFromRight (6);
    lengthButton.setBounds (header.removeFromRight (46));
}

juce::Rectangle<float> ScopeView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 6.0f).withTrimmedTop (26.0f).withTrimmedBottom (14.0f);
}

float ScopeView::valueToY (float v, const juce::Rectangle<float>& plot) const
{
    const float half = plot.getHeight() * 0.5f;
    const float cy = plot.getCentreY();
    float n;
    if (dbScale)
    {
        const float m = std::abs (v);
        n = m <= 0.0f ? 0.0f : juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (m) - kScopeFloorDb) / -kScopeFloorDb);
        n = v < 0.0f ? -n : n;
    }
    else
    {
        n = juce::jlimit (-1.0f, 1.0f, v);
    }
    return cy - n * half;
}

float ScopeView::thresholdFromY (float y) const
{
    const auto plot = plotArea();
    const float d = juce::jlimit (0.0f, 1.0f, std::abs (y - plot.getCentreY()) / (plot.getHeight() * 0.5f));
    const float db = dbScale ? kScopeFloorDb + d * -kScopeFloorDb
                             : juce::Decibels::gainToDecibels (d, kScopeFloorDb);
    return juce::jlimit (kScopeFloorDb, 0.0f, db);
}

void ScopeView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (area, 8.0f);

    const auto plot = plotArea();
    g.setColour (kBg.brighter (0.03f));
    g.fillRoundedRectangle (plot, 4.0f);

    const float thrDb = proc.getThresholdDb();
    g.setColour (kTextDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("INPUT   threshold " + juce::String (thrDb, 1) + " dB   (drag to set)",
                getLocalBounds().reduced (12, 6).removeFromTop (22), juce::Justification::centredLeft);

    const double sr = proc.getSampleRateSafe();
    const double colsPerSecond = sr / (double) juce::jmax (1, proc.getScopeSamplesPerColumn());
    const juce::int64 shown = juce::jlimit<juce::int64> (1, kHistory, (juce::int64) std::llround (displaySeconds * colsPerSecond));
    const int W = juce::jmax (1, (int) plot.getWidth());
    const juce::int64 oldestKept = juce::jmax<juce::int64> (0, totalColumns - kHistory);

    // Time grid.
    g.setColour (kPanelHi);
    const double step = displaySeconds > 3.0 ? 1.0 : 0.25;
    for (double t = step; t < displaySeconds; t += step)
    {
        const float x = plot.getRight() - (float) (t / displaySeconds) * plot.getWidth();
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

    juce::RectangleList<float> holdoff;
    juce::RectangleList<float> wave;
    std::vector<float> markers;

    for (int px = 0; px < W; ++px)
    {
        const juce::int64 c0 = (juce::int64) px * shown / W;
        const juce::int64 c1 = juce::jmax (c0 + 1, (juce::int64) (px + 1) * shown / W);

        float lo = 1.0e9f, hi = -1.0e9f;
        juce::uint8 flags = 0;
        bool any = false;
        for (juce::int64 a = c0; a < c1; ++a)
        {
            const juce::int64 absCol = totalColumns - shown + a;
            if (absCol < oldestKept || absCol < 0)
                continue;
            const auto& c = history[(size_t) (absCol % kHistory)];
            lo = juce::jmin (lo, c.lo);
            hi = juce::jmax (hi, c.hi);
            flags |= c.flags;
            any = true;
        }
        if (! any)
            continue;

        const float x = plot.getX() + (float) px;
        if ((flags & Proc::scopeHoldoff) != 0)
            holdoff.addWithoutMerging ({ x, plot.getY(), 1.0f, plot.getHeight() });

        const float yTop = valueToY (hi, plot);
        const float yBot = valueToY (lo, plot);
        wave.addWithoutMerging ({ x, juce::jmin (yTop, yBot), 1.0f, juce::jmax (1.0f, std::abs (yBot - yTop)) });

        if ((flags & Proc::scopeTrigger) != 0)
            markers.push_back (x);
    }

    g.setColour (kWarm.withAlpha (0.10f));
    g.fillRectList (holdoff);

    const float thrLin = juce::Decibels::decibelsToGain (thrDb);
    const float yUp = valueToY (thrLin, plot);
    const float yDn = valueToY (-thrLin, plot);
    g.setColour (kHot.withAlpha (dragging ? 0.95f : 0.7f));
    g.drawLine (plot.getX(), yUp, plot.getRight(), yUp, dragging ? 2.0f : 1.2f);
    g.drawLine (plot.getX(), yDn, plot.getRight(), yDn, dragging ? 2.0f : 1.2f);

    g.setColour (kAccent);
    g.fillRectList (wave);

    for (float x : markers)
    {
        g.setColour (kWarm);
        g.drawLine (x, plot.getY(), x, plot.getBottom(), 2.0f);
        juce::Path tri;
        tri.addTriangle (x - 5.0f, plot.getY(), x + 5.0f, plot.getY(), x, plot.getY() + 8.0f);
        g.fillPath (tri);
    }

    g.setColour (kTextDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("-" + juce::String (displaySeconds, 0) + " s", juce::Rectangle<float> (plot.getX(), plot.getBottom(), 60.0f, 14.0f),
                juce::Justification::centredLeft);
    g.drawText ("now", juce::Rectangle<float> (plot.getRight() - 60.0f, plot.getBottom(), 60.0f, 14.0f),
                juce::Justification::centredRight);
}

void ScopeView::mouseDown (const juce::MouseEvent& e)
{
    auto* param = proc.getAPVTS().getParameter ("threshold");
    if (param == nullptr || ! plotArea().expanded (0.0f, 10.0f).contains (e.position))
        return;

    dragging = true;
    param->beginChangeGesture();
    mouseDrag (e);
}

void ScopeView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    if (auto* param = proc.getAPVTS().getParameter ("threshold"))
        param->setValueNotifyingHost (param->convertTo0to1 (thresholdFromY (e.position.y)));
    repaint();
}

void ScopeView::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    if (auto* param = proc.getAPVTS().getParameter ("threshold"))
        param->endChangeGesture();
    repaint();
}

//==============================================================================
void InputMeter::update (float peak, bool triggered)
{
    level = juce::jmax (peak, level * 0.82f);
    flash = triggered ? 1.0f : flash * 0.85f;
    repaint();
}

void InputMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (area, 6.0f);

    auto lamp = area.removeFromTop (18.0f).reduced (6.0f, 3.0f);
    g.setColour (kPanelHi.interpolatedWith (kWarm, flash));
    g.fillEllipse (lamp.withSizeKeepingCentre (12.0f, 12.0f));

    auto bar = area.reduced (10.0f, 6.0f);
    g.setColour (kBg);
    g.fillRoundedRectangle (bar, 3.0f);

    auto dbToY = [&] (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - kScopeFloorDb) / -kScopeFloorDb);
        return bar.getBottom() - t * bar.getHeight();
    };

    const float db = level > 0.0f ? juce::Decibels::gainToDecibels (level) : kScopeFloorDb;
    const float y = dbToY (db);
    const float thrDb = proc.getThresholdDb();
    g.setColour (db >= thrDb ? kWarm : kAccent);
    g.fillRect (bar.withTop (y));

    g.setColour (kHot);
    const float ty = dbToY (thrDb);
    g.drawLine (bar.getX() - 4.0f, ty, bar.getRight() + 4.0f, ty, 2.0f);
}

//==============================================================================
SlotStrip::SlotStrip (Proc& p, int slotIndex) : proc (p), slot (slotIndex)
{
    numberLabel.setText (juce::String (slot), juce::dontSendNotification);
    numberLabel.setJustificationType (juce::Justification::centred);
    numberLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    numberLabel.setColour (juce::Label::backgroundColourId, kAccent.darker (0.5f));
    addAndMakeVisible (numberLabel);

    nameLabel.setFont (juce::FontOptions (12.0f));
    nameLabel.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (nameLabel);

    loadButton.setTooltip ("Choose a WAV / MP3 / AIFF / FLAC file (or drop one onto this slot)");
    loadButton.onClick = [this] { openChooser(); };
    clearButton.onClick = [this] { proc.clearSlot (slot); refresh(); };
    previewButton.setTooltip ("Play this slot once");
    previewButton.onClick = [this] { proc.triggerPreview (slot); };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (previewButton);

    addAndMakeVisible (onButton);
    onAttach = std::make_unique<ButtonAttachment> (proc.getAPVTS(), Proc::idOn (slot), onButton);

    setupKnob (levelKnob, levelName, "Level");
    setupKnob (pitchKnob, pitchName, "Pitch");
    setupKnob (fineKnob, fineName, "Fine");
    levelAttach = std::make_unique<SliderAttachment> (proc.getAPVTS(), Proc::idLevel (slot), levelKnob);
    pitchAttach = std::make_unique<SliderAttachment> (proc.getAPVTS(), Proc::idPitch (slot), pitchKnob);
    fineAttach  = std::make_unique<SliderAttachment> (proc.getAPVTS(), Proc::idFine (slot), fineKnob);
    pitchKnob.setTextValueSuffix (" st");
    fineKnob.setTextValueSuffix (" ct");
    pitchKnob.onValueChange = [this]
    {
        pitchKnob.setColour (juce::Slider::rotarySliderFillColourId, pitchColour (pitchKnob.getValue()));
    };
    pitchKnob.onValueChange();

    refresh();
}

void SlotStrip::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::FontOptions (11.0f));
    l.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (l);
}

void SlotStrip::refresh()
{
    const auto data = proc.getSlotData (slot);
    const auto path = proc.getSlotPath (slot);
    const auto fileName = juce::File::isAbsolutePath (path) ? juce::File (path).getFileName() : path;

    if (data != nullptr)
        nameLabel.setText (fileName, juce::dontSendNotification);
    else if (proc.isSlotMissing (slot))
        nameLabel.setText ("missing: " + fileName, juce::dontSendNotification);
    else
        nameLabel.setText ("Empty (drop a file)", juce::dontSendNotification);

    nameLabel.setColour (juce::Label::textColourId, proc.isSlotMissing (slot) ? kHot : data != nullptr ? kText : kTextDim);
    nameLabel.setTooltip (path);

    peaks.clear();
    if (data != nullptr && data->buffer.getNumSamples() > 0)
    {
        const int buckets = 160;
        const int len = data->buffer.getNumSamples();
        peaks.resize ((size_t) buckets, 0.0f);
        for (int b = 0; b < buckets; ++b)
        {
            const int s0 = (int) ((juce::int64) b * len / buckets);
            const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (b + 1) * len / buckets));
            float m = 0.0f;
            for (int ch = 0; ch < data->buffer.getNumChannels(); ++ch)
                m = juce::jmax (m, data->buffer.getMagnitude (ch, s0, juce::jmin (s1, len) - s0));
            peaks[(size_t) b] = m;
        }
    }

    clearButton.setEnabled (data != nullptr || proc.isSlotMissing (slot));
    previewButton.setEnabled (data != nullptr);
    repaint();
}

void SlotStrip::setFlash (float amount)
{
    if (std::abs (amount - flash) > 0.01f)
    {
        flash = amount;
        repaint();
    }
}

void SlotStrip::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const bool live = ! peaks.empty() && onButton.getToggleState();
    g.setColour (kPanel.interpolatedWith (kWarm.withAlpha (1.0f), live ? flash * 0.25f : 0.0f));
    g.fillRoundedRectangle (area, 8.0f);

    if (dragOver)
    {
        g.setColour (kAccent);
        g.drawRoundedRectangle (area.reduced (1.0f), 8.0f, 2.0f);
    }

    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (28);
    const auto thumb = r.removeFromTop (56).toFloat();
    g.setColour (kBg);
    g.fillRoundedRectangle (thumb, 4.0f);

    if (! peaks.empty())
    {
        g.setColour (onButton.getToggleState() ? kAccent : kTextDim);
        const float w = thumb.getWidth() / (float) peaks.size();
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float h = juce::jmax (1.0f, juce::jmin (1.0f, peaks[i]) * thumb.getHeight() * 0.9f);
            g.fillRect (thumb.getX() + (float) i * w, thumb.getCentreY() - h * 0.5f, juce::jmax (1.0f, w), h);
        }
    }
    else
    {
        g.setColour (kTextDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("drop WAV / MP3 here", thumb, juce::Justification::centred);
    }
}

void SlotStrip::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (24);
    numberLabel.setBounds (top.removeFromLeft (24));
    top.removeFromLeft (6);
    nameLabel.setBounds (top);
    r.removeFromTop (4);
    r.removeFromTop (56);
    r.removeFromTop (6);

    auto buttons = r.removeFromTop (24);
    const int bw = (buttons.getWidth() - 8) / 3;
    loadButton.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (4);
    clearButton.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (4);
    previewButton.setBounds (buttons);

    r.removeFromTop (4);
    onButton.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);

    const int kw = r.getWidth() / 3;
    auto names = r.removeFromBottom (16);
    levelName.setBounds (names.removeFromLeft (kw));
    pitchName.setBounds (names.removeFromLeft (kw));
    fineName.setBounds (names);
    levelKnob.setBounds (r.removeFromLeft (kw));
    pitchKnob.setBounds (r.removeFromLeft (kw));
    fineKnob.setBounds (r);
}

bool SlotStrip::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isSupportedAudioFile (f))
            return true;
    return false;
}

void SlotStrip::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
    {
        if (isSupportedAudioFile (f))
        {
            if (! proc.loadSlot (slot, juce::File (f)))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "chouchouTrigger",
                                                        "Could not read " + juce::File (f).getFileName());
            break;
        }
    }
    refresh();
}

void SlotStrip::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Load sample for slot " + juce::String (slot),
                                                   juce::File(), Proc::getSupportedWildcard());
    juce::Component::SafePointer<SlotStrip> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr)
            return;
        const auto f = fc.getResult();
        if (! f.existsAsFile())
            return;
        if (! safe->proc.loadSlot (safe->slot, f))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "chouchouTrigger",
                                                    "Could not read " + f.getFileName());
        safe->refresh();
    });
}

//==============================================================================
ChouchouTriggerAudioProcessorEditor::ChouchouTriggerAudioProcessorEditor (Proc& p)
    : AudioProcessorEditor (&p), proc (p), scope (p), meter (p)
{
    setLookAndFeel (&lnf);

    title.setText ("chouchouTrigger", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    addAndMakeVisible (title);
    subtitle.setText ("threshold drum trigger  -  6 layered samples", juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, kTextDim);
    subtitle.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (subtitle);

    addAndMakeVisible (scope);
    addAndMakeVisible (meter);

    auto& s = proc.getAPVTS();
    setupKnob (thresholdKnob, thresholdName, "Threshold");
    setupKnob (retriggerKnob, retriggerName, "Retrigger");
    setupKnob (releaseKnob, releaseName, "Re-arm");
    setupKnob (dryKnob, dryName, "Dry");
    setupKnob (wetKnob, wetName, "Wet");
    thresholdAttach = std::make_unique<chouchou_trigger_ui::SliderAttachment> (s, "threshold", thresholdKnob);
    retriggerAttach = std::make_unique<chouchou_trigger_ui::SliderAttachment> (s, "retrigger", retriggerKnob);
    releaseAttach   = std::make_unique<chouchou_trigger_ui::SliderAttachment> (s, "release", releaseKnob);
    dryAttach       = std::make_unique<chouchou_trigger_ui::SliderAttachment> (s, "dry", dryKnob);
    wetAttach       = std::make_unique<chouchou_trigger_ui::SliderAttachment> (s, "wet", wetKnob);
    thresholdKnob.setTextValueSuffix (" dB");
    retriggerKnob.setTextValueSuffix (" ms");
    releaseKnob.setTextValueSuffix (" dB");
    thresholdKnob.setColour (juce::Slider::rotarySliderFillColourId, kHot);
    thresholdKnob.setTooltip ("Input level (dB) that fires the samples");
    retriggerKnob.setTooltip ("Minimum time between two triggers");
    releaseKnob.setTooltip ("After a hit the input must fall this many dB below the threshold before it can fire again");

    modeButton.setClickingTogglesState (true);
    modeButton.setTooltip ("Mono: samples are summed to mono on every output channel. Stereo: samples keep their channels.");
    modeAttach = std::make_unique<chouchou_trigger_ui::ButtonAttachment> (s, "outMode", modeButton);
    modeButton.onStateChange = [this] { updateModeButton(); };
    updateModeButton();
    addAndMakeVisible (modeButton);
    modeName.setText ("Output", juce::dontSendNotification);
    modeName.setJustificationType (juce::Justification::centred);
    modeName.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (modeName);

    for (int i = 0; i < Proc::kNumSlots; ++i)
    {
        strips[(size_t) i] = std::make_unique<SlotStrip> (proc, i + 1);
        addAndMakeVisible (*strips[(size_t) i]);
    }

    lastTriggerCount = proc.getTriggerCount();
    lastSlotsVersion = proc.getSlotsVersion();

    setResizable (true, true);
    setResizeLimits (1000, 700, 1800, 1200);
    setSize (1100, 760);
    startTimerHz (60);
}

ChouchouTriggerAudioProcessorEditor::~ChouchouTriggerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ChouchouTriggerAudioProcessorEditor::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (l);
}

void ChouchouTriggerAudioProcessorEditor::updateModeButton()
{
    modeButton.setButtonText (modeButton.getToggleState() ? "Stereo" : "Mono");
}

void ChouchouTriggerAudioProcessorEditor::refreshNow()
{
    scope.pull();
    scope.repaint();

    const int tc = proc.getTriggerCount();
    const bool triggered = tc != lastTriggerCount;
    lastTriggerCount = tc;
    meter.update (proc.takeInputPeak(), triggered);

    stripFlash = triggered ? 1.0f : stripFlash * 0.85f;
    const int version = proc.getSlotsVersion();
    for (auto& s : strips)
    {
        if (version != lastSlotsVersion)
            s->refresh();
        s->setFlash (stripFlash < 0.02f ? 0.0f : stripFlash);
    }
    lastSlotsVersion = version;
}

void ChouchouTriggerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
}

void ChouchouTriggerAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (14);

    auto top = r.removeFromTop (34);
    title.setBounds (top.removeFromLeft (220));
    subtitle.setBounds (top);
    r.removeFromTop (6);

    scope.setBounds (r.removeFromTop (juce::jmax (180, (int) (getHeight() * 0.28f))));
    r.removeFromTop (10);

    auto row = r.removeFromTop (120);
    meter.setBounds (row.removeFromRight (40));
    row.removeFromRight (10);

    const int cw = row.getWidth() / 6;
    auto place = [&] (juce::Slider& k, juce::Label& l)
    {
        auto c = row.removeFromLeft (cw);
        l.setBounds (c.removeFromTop (18));
        k.setBounds (c);
    };
    place (thresholdKnob, thresholdName);
    place (retriggerKnob, retriggerName);
    place (releaseKnob, releaseName);
    place (dryKnob, dryName);
    place (wetKnob, wetName);
    modeName.setBounds (row.removeFromTop (18));
    modeButton.setBounds (row.withSizeKeepingCentre (juce::jmin (100, row.getWidth() - 10), 32));

    r.removeFromTop (10);
    const int gap = 8;
    const int sw = (r.getWidth() - gap * (Proc::kNumSlots - 1)) / Proc::kNumSlots;
    for (int i = 0; i < Proc::kNumSlots; ++i)
    {
        strips[(size_t) i]->setBounds (r.removeFromLeft (sw));
        r.removeFromLeft (gap);
    }
}
