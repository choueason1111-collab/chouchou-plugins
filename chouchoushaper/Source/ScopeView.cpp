/*
  ==============================================================================

    chouchouShaper — scope section

  ==============================================================================
*/

#include "ScopeView.h"

namespace
{
    using namespace scopecolours;

    constexpr float kGainRangeDb = 24.0f;
    constexpr float kSpecFloorDb = -96.0f;

    float gainToY (float db, juce::Rectangle<float> r)
    {
        const float t = juce::jlimit (-1.0f, 1.0f, db / kGainRangeDb);
        return r.getCentreY() - t * r.getHeight() * 0.5f;
    }

    juce::String dbText (float db, int decimals = 1)
    {
        return (db > 0.05f ? "+" : "") + juce::String (db, decimals);
    }

    juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    void drawLaneFrame (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
    {
        g.setColour (bg);
        g.fillRect (r);
        g.setColour (grid);
        g.drawRect (r, 1.0f);
        g.setColour (dim);
        g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
        g.drawText (title, r.reduced (5.0f, 2.0f).withHeight (12.0f), juce::Justification::left);
    }

    void drawGainGrid (juce::Graphics& g, juce::Rectangle<float> r, bool labels)
    {
        g.setFont (juce::FontOptions (9.0f));
        for (float db : { -12.0f, 0.0f, 12.0f })
        {
            const float y = gainToY (db, r);
            g.setColour (db == 0.0f ? dim.withAlpha (0.55f) : grid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            if (labels)
            {
                g.setColour (dim);
                g.drawText (dbText (db, 0), juce::Rectangle<float> (r.getRight() - 28.0f, y - 10.0f, 25.0f, 10.0f),
                            juce::Justification::right);
            }
        }
    }

    /** Stacked component bars (positive parts up, negative parts down) at column x. */
    void drawStack (juce::Graphics& g, juce::Rectangle<float> r, float x, float w,
                    float ratioDb, float attackDb, float sustainDb)
    {
        float pos = 0.0f, neg = 0.0f;
        const std::pair<float, juce::Colour> parts[] = { { ratioDb, ratio }, { attackDb, attack }, { sustainDb, sustain } };
        for (const auto& [v, colour] : parts)
        {
            if (std::abs (v) < 0.02f)
                continue;
            float& base = v > 0.0f ? pos : neg;
            const float y0 = gainToY (base, r), y1 = gainToY (base + v, r);
            base += v;
            g.setColour (colour.withAlpha (0.72f));
            g.fillRect (x, juce::jmin (y0, y1), w, juce::jmax (1.0f, std::abs (y1 - y0)));
        }
    }

    void drawHoverBox (juce::Graphics& g, juce::Rectangle<float> bounds, float x,
                       const juce::StringArray& lines, const juce::Array<juce::Colour>& colours)
    {
        const float w = 190.0f, lineH = 14.0f;
        const float h = lineH * (float) lines.size() + 8.0f;
        float bx = x + 10.0f;
        if (bx + w > bounds.getRight()) bx = x - 10.0f - w;
        juce::Rectangle<float> box (bx, bounds.getY() + 16.0f, w, h);

        g.setColour (juce::Colours::black.withAlpha (0.78f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (grid);
        g.drawRoundedRectangle (box, 4.0f, 1.0f);

        g.setFont (juce::FontOptions (11.0f));
        auto row = box.reduced (7.0f, 4.0f);
        for (int i = 0; i < lines.size(); ++i)
        {
            g.setColour (i < colours.size() ? colours[i] : text);
            g.drawText (lines[i], row.removeFromTop (lineH), juce::Justification::left);
        }
    }
}

//==============================================================================
ScopeView::ScopeView (ChouchouShaperAudioProcessor& p) : proc (p), ring ((size_t) kRing) {}

void ScopeView::pull (bool keep)
{
    proc.getScopeFifo().drain ([this, keep] (const ScopeColumn& c)
    {
        if (! keep)
            return;
        ring[(size_t) head] = c;
        head = (head + 1) % kRing;
        filled = juce::jmin (filled + 1, kRing);
    });
}

const ScopeColumn* ScopeView::columnAt (int back) const
{
    if (back < 0 || back >= filled)
        return nullptr;
    return &ring[(size_t) ((head - 1 - back + kRing) % kRing)];
}

int ScopeView::visibleColumns() const
{
    const double sr = juce::jmax (1.0, proc.getCurrentSampleRate());
    return juce::jlimit (16, kRing / 2, (int) (windowSeconds * sr / kScopeColumnSamples));
}

ScopeView::PixelData ScopeView::gatherPixel (int ageLo, int ageHi) const
{
    // Ages are in input time: age 0 = newest column whose output already exists.
    const int lat = proc.getScopeLatencyColumns();
    const int specShift = lat / 2;

    PixelData d;
    int n = 0;
    for (int a = ageLo; a <= ageHi; ++a)
    {
        const auto* in  = columnAt (a + lat);
        const auto* out = columnAt (a);
        const auto* sp  = columnAt (a + lat - specShift);
        if (in == nullptr || out == nullptr || sp == nullptr)
            continue;

        if (n == 0)
        {
            d.inMin = in->inMin;   d.inMax = in->inMax;
            d.outMin = out->outMin; d.outMax = out->outMax;
            d.scMin = in->scMin;   d.scMax = in->scMax;
        }
        else
        {
            d.inMin = juce::jmin (d.inMin, in->inMin);    d.inMax = juce::jmax (d.inMax, in->inMax);
            d.outMin = juce::jmin (d.outMin, out->outMin); d.outMax = juce::jmax (d.outMax, out->outMax);
            d.scMin = juce::jmin (d.scMin, in->scMin);    d.scMax = juce::jmax (d.scMax, in->scMax);
        }
        d.scActive = d.scActive || in->scActive;

        d.tRatio += in->tRatio;  d.tAttack += in->tAttack;  d.tSustain += in->tSustain;  d.tTotal += in->tTotal;
        d.sRatio += sp->sRatio;  d.sAttack += sp->sAttack;  d.sSustain += sp->sSustain;  d.sTotal += sp->sTotal;
        ++n;
    }

    if (n > 0)
    {
        const float inv = 1.0f / (float) n;
        d.tRatio *= inv; d.tAttack *= inv; d.tSustain *= inv; d.tTotal *= inv;
        d.sRatio *= inv; d.sAttack *= inv; d.sSustain *= inv; d.sTotal *= inv;
        d.valid = true;
    }
    return d;
}

void ScopeView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float waveH = r.getHeight() * 0.48f;
    auto waveLane = r.removeFromTop (waveH);
    r.removeFromTop (3.0f);
    auto timeLane = r.removeFromTop ((r.getHeight() - 3.0f) * 0.5f);
    r.removeFromTop (3.0f);
    auto specLane = r;

    // Gather one PixelData per horizontal pixel (newest on the right).
    const int w = juce::jmax (1, (int) waveLane.getWidth());
    const int v = visibleColumns();
    std::vector<PixelData> px ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        const int ageLo = (int) ((int64_t) (w - 1 - x) * v / w);
        const int ageHi = juce::jmax (ageLo, (int) ((int64_t) (w - x) * v / w) - 1);
        px[(size_t) x] = gatherPixel (ageLo, ageHi);
    }

    paintWaveLane (g, waveLane, px);
    paintGainLane (g, timeLane, px, false);
    paintGainLane (g, specLane, px, true);
    paintHover (g, getLocalBounds().toFloat(), px);
}

void ScopeView::paintWaveLane (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<PixelData>& px)
{
    drawLaneFrame (g, r, "WAVE   in (grey)  /  out (green)  /  sidechain (purple)"
                         + juce::String (zoom > 1.0f ? "   x" + juce::String ((int) zoom) : ""));

    auto y = [&] (float s) { return juce::jlimit (r.getY(), r.getBottom(), r.getCentreY() - s * zoom * r.getHeight() * 0.48f); };

    g.setColour (grid);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    const float x0 = r.getX();
    juce::Path outTop, outBot, scTop, scBot;
    bool started = false, scStarted = false;

    for (size_t i = 0; i < px.size(); ++i)
    {
        const auto& d = px[i];
        if (! d.valid)
            continue;
        const float x = x0 + (float) i;

        g.setColour (input.withAlpha (0.55f));
        g.fillRect (x, y (d.inMax), 1.0f, juce::jmax (1.0f, y (d.inMin) - y (d.inMax)));

        g.setColour (output.withAlpha (0.28f));
        g.fillRect (x, y (d.outMax), 1.0f, juce::jmax (1.0f, y (d.outMin) - y (d.outMax)));

        if (! started) { outTop.startNewSubPath (x, y (d.outMax)); outBot.startNewSubPath (x, y (d.outMin)); started = true; }
        else           { outTop.lineTo (x, y (d.outMax));          outBot.lineTo (x, y (d.outMin)); }

        if (d.scActive)
        {
            if (! scStarted) { scTop.startNewSubPath (x, y (d.scMax)); scBot.startNewSubPath (x, y (d.scMin)); scStarted = true; }
            else             { scTop.lineTo (x, y (d.scMax));          scBot.lineTo (x, y (d.scMin)); }
        }
    }

    g.setColour (output);
    g.strokePath (outTop, juce::PathStrokeType (1.2f));
    g.strokePath (outBot, juce::PathStrokeType (1.2f));
    if (scStarted)
    {
        g.setColour (side.withAlpha (0.85f));
        g.strokePath (scTop, juce::PathStrokeType (1.0f));
        g.strokePath (scBot, juce::PathStrokeType (1.0f));
    }

    // Current readout (newest ~50 ms) at the top right.
    PixelData now;
    int n = 0;
    for (size_t i = px.size() > 12 ? px.size() - 12 : 0; i < px.size(); ++i)
        if (px[i].valid)
        {
            const auto& d = px[i];
            now.tRatio += d.tRatio; now.tAttack += d.tAttack; now.tSustain += d.tSustain; now.tTotal += d.tTotal;
            now.sRatio += d.sRatio; now.sAttack += d.sAttack; now.sSustain += d.sSustain; now.sTotal += d.sTotal;
            ++n;
        }

    if (n > 0)
    {
        const float inv = 1.0f / (float) n;
        auto line = [&] (const juce::String& name, float ra, float at, float su, float tot, juce::Colour c, float yy)
        {
            juce::Rectangle<float> row (r.getRight() - 330.0f, yy, 325.0f, 13.0f);
            g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
            g.setColour (c);
            g.drawText (name + "  " + dbText (tot * inv) + " dB", row.removeFromLeft (120.0f), juce::Justification::left);
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (ratio);   g.drawText ("Ratio "   + dbText (ra * inv), row.removeFromLeft (70.0f), juce::Justification::left);
            g.setColour (attack);  g.drawText ("Attack "  + dbText (at * inv), row.removeFromLeft (70.0f), juce::Justification::left);
            g.setColour (sustain); g.drawText ("Sustain " + dbText (su * inv), row, juce::Justification::left);
        };
        line ("TIME",     now.tRatio, now.tAttack, now.tSustain, now.tTotal, timeStage,     r.getY() + 15.0f);
        line ("SPECTRAL", now.sRatio, now.sAttack, now.sSustain, now.sTotal, spectralStage, r.getY() + 29.0f);
    }
}

void ScopeView::paintGainLane (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<PixelData>& px,
                               bool spectralLane)
{
    drawLaneFrame (g, r, spectralLane ? "SPECTRAL gain (energy-weighted average)" : "TIME gain");
    drawGainGrid (g, r, true);

    juce::Path total;
    bool started = false;
    for (size_t i = 0; i < px.size(); ++i)
    {
        const auto& d = px[i];
        if (! d.valid)
            continue;
        const float x = r.getX() + (float) i;

        if (spectralLane) drawStack (g, r, x, 1.0f, d.sRatio, d.sAttack, d.sSustain);
        else              drawStack (g, r, x, 1.0f, d.tRatio, d.tAttack, d.tSustain);

        const float yy = gainToY (spectralLane ? d.sTotal : d.tTotal, r);
        if (! started) { total.startNewSubPath (x, yy); started = true; }
        else           total.lineTo (x, yy);
    }

    g.setColour (spectralLane ? spectralStage : timeStage);
    g.strokePath (total, juce::PathStrokeType (1.8f));
}

void ScopeView::paintHover (juce::Graphics& g, juce::Rectangle<float> lanes, const std::vector<PixelData>& px)
{
    if (hoverX < 0 || hoverX >= (int) px.size() || ! px[(size_t) hoverX].valid)
        return;

    const auto& d = px[(size_t) hoverX];
    const float x = lanes.getX() + (float) hoverX;
    g.setColour (text.withAlpha (0.5f));
    g.drawVerticalLine ((int) x, lanes.getY(), lanes.getBottom());

    const double sr = juce::jmax (1.0, proc.getCurrentSampleRate());
    const int w = (int) px.size();
    const double ageMs = (double) (w - 1 - hoverX) * visibleColumns() / w * kScopeColumnSamples / sr * 1000.0;
    auto peakDb = [] (float lo, float hi) { return juce::Decibels::gainToDecibels (juce::jmax (std::abs (lo), std::abs (hi)), -120.0f); };

    juce::StringArray lines;
    juce::Array<juce::Colour> colours;
    lines.add ("-" + juce::String (ageMs, 0) + " ms");                                   colours.add (dim);
    lines.add ("in  " + juce::String (peakDb (d.inMin, d.inMax), 1) + " dBFS");           colours.add (input.brighter (0.4f));
    lines.add ("out " + juce::String (peakDb (d.outMin, d.outMax), 1) + " dBFS");         colours.add (output);
    if (d.scActive) { lines.add ("sc  " + juce::String (peakDb (d.scMin, d.scMax), 1) + " dBFS"); colours.add (side); }
    lines.add ("TIME " + dbText (d.tTotal) + " dB");                                      colours.add (timeStage);
    lines.add ("  R " + dbText (d.tRatio) + "  A " + dbText (d.tAttack) + "  S " + dbText (d.tSustain)); colours.add (text);
    lines.add ("SPECTRAL " + dbText (d.sTotal) + " dB");                                  colours.add (spectralStage);
    lines.add ("  R " + dbText (d.sRatio) + "  A " + dbText (d.sAttack) + "  S " + dbText (d.sSustain)); colours.add (text);

    drawHoverBox (g, lanes, x, lines, colours);
}

void ScopeView::mouseMove (const juce::MouseEvent& e) { hoverX = e.x; repaint(); }
void ScopeView::mouseExit (const juce::MouseEvent&)   { hoverX = -1; repaint(); }

//==============================================================================
float SpectrumView::freqAtX (float x, juce::Rectangle<float> r) const
{
    const float fMax = (float) juce::jmin (20000.0, juce::jmax (1.0, proc.getCurrentSampleRate()) * 0.5);
    return 20.0f * std::pow (fMax / 20.0f, (x - r.getX()) / juce::jmax (1.0f, r.getWidth()));
}

float SpectrumView::xAtFreq (float f, juce::Rectangle<float> r) const
{
    const float fMax = (float) juce::jmin (20000.0, juce::jmax (1.0, proc.getCurrentSampleRate()) * 0.5);
    return r.getX() + r.getWidth() * std::log (juce::jmax (20.0f, f) / 20.0f) / std::log (fMax / 20.0f);
}

void SpectrumView::update (bool keep)
{
    if (! keep)
        return;

    const auto r = getLocalBounds().toFloat();
    const int w = juce::jmax (1, (int) r.getWidth());
    const auto& stage = proc.getSpectralStage();
    const int nb = stage.numBins();
    const float binHz = (float) (juce::jmax (1.0, proc.getCurrentSampleRate()) / stage.getFftSize());

    if ((int) inDb.size() != w)
    {
        for (auto* v : { &inDb, &outDb })
            v->assign ((size_t) w, kSpecFloorDb);
        for (auto* v : { &gainDb, &ratioDb, &attackDb, &sustainDb })
            v->assign ((size_t) w, 0.0f);
    }

    auto& apvts = proc.getAPVTS();
    const float loHz = apvts.getRawParameterValue ("s_lo_hz")->load();
    const float hiHz = apvts.getRawParameterValue ("s_hi_hz")->load();
    maxCutDb = maxBoostDb = 0.0f;

    for (int x = 0; x < w; ++x)
    {
        const float f0 = freqAtX ((float) x, r), f1 = freqAtX ((float) x + 1.0f, r);
        const int b0 = juce::jlimit (0, nb - 1, juce::roundToInt (f0 / binHz));
        const int b1 = juce::jlimit (b0, nb - 1, juce::roundToInt (f1 / binHz));

        float in = -200.0f, out = -200.0f, gn = 0, ra = 0, at = 0, su = 0;
        for (int b = b0; b <= b1; ++b)
        {
            in  = juce::jmax (in, stage.getMeterInDb (b));
            out = juce::jmax (out, stage.getMeterOutDb (b));
            gn += stage.getMeterGainDb (b);
            ra += stage.getMeterRatioDb (b);
            at += stage.getMeterAttackDb (b);
            su += stage.getMeterSustainDb (b);
        }
        const float inv = 1.0f / (float) (b1 - b0 + 1);
        const auto i = (size_t) x;

        // Levels: instant rise, slow fall. Gains: light smoothing.
        inDb[i]  = juce::jmax (in,  inDb[i] - 1.5f);
        outDb[i] = juce::jmax (out, outDb[i] - 1.5f);
        gainDb[i]    += 0.45f * (gn * inv - gainDb[i]);
        ratioDb[i]   += 0.45f * (ra * inv - ratioDb[i]);
        attackDb[i]  += 0.45f * (at * inv - attackDb[i]);
        sustainDb[i] += 0.45f * (su * inv - sustainDb[i]);

        const float fc = std::sqrt (f0 * f1);
        if (fc >= loHz && fc <= hiHz)
        {
            if (gainDb[i] < maxCutDb)   { maxCutDb = gainDb[i];   maxCutHz = fc; }
            if (gainDb[i] > maxBoostDb) { maxBoostDb = gainDb[i]; maxBoostHz = fc; }
        }
    }
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    auto r = full;
    auto specLane = r.removeFromTop (r.getHeight() * 0.56f);
    r.removeFromTop (3.0f);
    auto gainLane = r;

    drawLaneFrame (g, specLane, "SPECTRUM   in (grey)  /  out (blue)");
    drawLaneFrame (g, gainLane, "GAIN PER FREQUENCY   total + Ratio / Attack / Sustain");

    auto& apvts = proc.getAPVTS();
    const float loHz = apvts.getRawParameterValue ("s_lo_hz")->load();
    const float hiHz = apvts.getRawParameterValue ("s_hi_hz")->load();
    const int w = (int) inDb.size();

    // Frequency grid.
    g.setFont (juce::FontOptions (9.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xAtFreq (f, full);
        if (x >= full.getRight()) continue;
        g.setColour (grid);
        g.drawVerticalLine ((int) x, specLane.getY() + 14.0f, specLane.getBottom());
        g.drawVerticalLine ((int) x, gainLane.getY() + 14.0f, gainLane.getBottom());
        g.setColour (dim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x + 2.0f, gainLane.getBottom() - 11.0f, 26.0f, 10.0f),
                    juce::Justification::left);
    }

    for (float db : { -72.0f, -48.0f, -24.0f })
    {
        const float y = specLane.getBottom() - (db - kSpecFloorDb) / -kSpecFloorDb * specLane.getHeight();
        g.setColour (grid);
        g.drawHorizontalLine ((int) y, specLane.getX(), specLane.getRight());
        g.setColour (dim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (specLane.getRight() - 28.0f, y - 10.0f, 25.0f, 10.0f),
                    juce::Justification::right);
    }
    drawGainGrid (g, gainLane, true);

    if (w > 0)
    {
        auto levelY = [&] (float db)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (db - kSpecFloorDb) / -kSpecFloorDb);
            return specLane.getBottom() - t * (specLane.getHeight() - 14.0f);
        };

        juce::Path inFill, outLine, total, pr, pa, ps;
        inFill.startNewSubPath (full.getX(), specLane.getBottom());
        for (int x = 0; x < w; ++x)
        {
            const float xx = full.getX() + (float) x;
            const auto i = (size_t) x;
            inFill.lineTo (xx, levelY (inDb[i]));
            if (x == 0)
            {
                outLine.startNewSubPath (xx, levelY (outDb[i]));
                total.startNewSubPath (xx, gainToY (gainDb[i], gainLane));
                pr.startNewSubPath (xx, gainToY (ratioDb[i], gainLane));
                pa.startNewSubPath (xx, gainToY (attackDb[i], gainLane));
                ps.startNewSubPath (xx, gainToY (sustainDb[i], gainLane));
            }
            else
            {
                outLine.lineTo (xx, levelY (outDb[i]));
                total.lineTo (xx, gainToY (gainDb[i], gainLane));
                pr.lineTo (xx, gainToY (ratioDb[i], gainLane));
                pa.lineTo (xx, gainToY (attackDb[i], gainLane));
                ps.lineTo (xx, gainToY (sustainDb[i], gainLane));
            }
        }
        inFill.lineTo (full.getX() + (float) (w - 1), specLane.getBottom());
        inFill.closeSubPath();

        g.setColour (input.withAlpha (0.45f));
        g.fillPath (inFill);
        g.setColour (spectralStage);
        g.strokePath (outLine, juce::PathStrokeType (1.4f));

        g.setColour (ratio.withAlpha (0.85f));   g.strokePath (pr, juce::PathStrokeType (1.0f));
        g.setColour (attack.withAlpha (0.85f));  g.strokePath (pa, juce::PathStrokeType (1.0f));
        g.setColour (sustain.withAlpha (0.85f)); g.strokePath (ps, juce::PathStrokeType (1.0f));
        g.setColour (text);
        g.strokePath (total, juce::PathStrokeType (2.0f));
    }

    // Dim the frequencies outside Low / High.
    {
        const float xl = xAtFreq (loHz, full), xh = xAtFreq (hiHz, full);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        if (xl > full.getX()) g.fillRect (full.withRight (xl));
        if (xh < full.getRight()) g.fillRect (full.withLeft (xh));
    }

    // Max cut / boost readout.
    g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    auto readout = specLane.reduced (6.0f, 0.0f).withTrimmedTop (15.0f).withHeight (13.0f);
    g.setColour (attack);
    g.drawText ("max cut " + (maxCutDb < -0.05f ? dbText (maxCutDb) + " dB @ " + hzText (maxCutHz) : juce::String ("-")),
                readout, juce::Justification::left);
    g.setColour (output);
    g.drawText ("max boost " + (maxBoostDb > 0.05f ? dbText (maxBoostDb) + " dB @ " + hzText (maxBoostHz) : juce::String ("-")),
                readout.translated (0.0f, 14.0f), juce::Justification::left);

    // Hover.
    if (hoverX >= 0 && hoverX < w)
    {
        const auto i = (size_t) hoverX;
        const float x = full.getX() + (float) hoverX;
        g.setColour (text.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, full.getY(), full.getBottom());

        juce::StringArray lines;
        juce::Array<juce::Colour> colours;
        lines.add (hzText (freqAtX (x, full)));                           colours.add (dim);
        lines.add ("in  " + juce::String (inDb[i], 1) + " dB");           colours.add (input.brighter (0.4f));
        lines.add ("out " + juce::String (outDb[i], 1) + " dB");          colours.add (spectralStage);
        lines.add ("gain " + dbText (gainDb[i]) + " dB");                 colours.add (text);
        lines.add ("  Ratio "   + dbText (ratioDb[i]));                   colours.add (ratio);
        lines.add ("  Attack "  + dbText (attackDb[i]));                  colours.add (attack);
        lines.add ("  Sustain " + dbText (sustainDb[i]));                 colours.add (sustain);
        drawHoverBox (g, full, x, lines, colours);
    }
}

void SpectrumView::mouseMove (const juce::MouseEvent& e) { hoverX = e.x; repaint(); }
void SpectrumView::mouseExit (const juce::MouseEvent&)   { hoverX = -1; repaint(); }

//==============================================================================
ScopeSection::ScopeSection (ChouchouShaperAudioProcessor& p) : scope (p), spectrum (p)
{
    addAndMakeVisible (scope);
    addAndMakeVisible (spectrum);

    freezeButton.setColour (juce::ToggleButton::textColourId, text);
    freezeButton.setColour (juce::ToggleButton::tickColourId, text);
    addAndMakeVisible (freezeButton);

    windowBox.addItemList ({ "0.5 s", "1 s", "2 s", "5 s" }, 1);
    windowBox.setSelectedId (3, juce::dontSendNotification);
    windowBox.onChange = [this]
    {
        const double secs[] = { 0.5, 1.0, 2.0, 5.0 };
        scope.setWindowSeconds (secs[juce::jlimit (0, 3, windowBox.getSelectedItemIndex())]);
    };
    addAndMakeVisible (windowBox);

    zoomBox.addItemList ({ "Zoom 1x", "Zoom 2x", "Zoom 4x", "Zoom 8x" }, 1);
    zoomBox.setSelectedId (1, juce::dontSendNotification);
    zoomBox.onChange = [this] { scope.setZoom ((float) (1 << juce::jlimit (0, 3, zoomBox.getSelectedItemIndex()))); };
    addAndMakeVisible (zoomBox);
}

void ScopeSection::tick()
{
    const bool keep = isVisible() && ! freezeButton.getToggleState();
    scope.pull (keep);
    spectrum.update (keep);
    if (isVisible())
    {
        scope.repaint();
        spectrum.repaint();
    }
}

void ScopeSection::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (output);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withHeight (3.0f), 1.5f);

    g.setColour (output);
    g.setFont (juce::FontOptions (14.0f).withStyle ("Bold"));
    g.drawText ("SCOPE", 12, 6, 70, 22, juce::Justification::centredLeft);

    // Legend chips.
    auto lg = legendArea;
    g.setFont (juce::FontOptions (11.0f));
    const std::pair<const char*, juce::Colour> items[] = {
        { "in", input }, { "out", output }, { "sc", side },
        { "Ratio", ratio }, { "Attack", attack }, { "Sustain", sustain },
        { "TIME total", timeStage }, { "SPECTRAL total", spectralStage } };
    for (const auto& [name, colour] : items)
    {
        const int tw = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (11.0f)), name) + 22;
        auto chip = lg.removeFromLeft (tw);
        g.setColour (colour);
        g.fillRoundedRectangle (chip.removeFromLeft (12).withSizeKeepingCentre (10, 10).toFloat(), 2.0f);
        g.setColour (text);
        g.drawText (name, chip.withTrimmedLeft (4), juce::Justification::centredLeft);
    }
}

void ScopeSection::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto bar = r.removeFromTop (24);
    bar.removeFromLeft (70);
    zoomBox.setBounds (bar.removeFromRight (96).reduced (2, 1));
    windowBox.setBounds (bar.removeFromRight (72).reduced (2, 1));
    freezeButton.setBounds (bar.removeFromRight (78).reduced (2, 0));
    legendArea = bar;

    r.removeFromTop (6);
    auto left = r.removeFromLeft (r.getWidth() * 3 / 5);
    r.removeFromLeft (8);
    scope.setBounds (left);
    spectrum.setBounds (r);
}
