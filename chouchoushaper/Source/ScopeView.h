/*
  ==============================================================================

    chouchouShaper — scope section (ASCII-only UI strings)

      ScopeView     time-domain scope: waveform lane (input / output / sidechain),
                    TIME gain lane and SPECTRAL gain lane, each split into
                    Ratio / Attack / Sustain with the stage total on top
      SpectrumView  input / output spectrum and per-frequency gain split by mode
      ScopeSection  toolbar (freeze, time window, zoom, legend) + both views

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>

namespace scopecolours
{
    const juce::Colour bg       { 0xff1e2126 };
    const juce::Colour panel    { 0xff292d33 };
    const juce::Colour grid     { 0xff3a3f46 };
    const juce::Colour text     { 0xffe6e6e6 };
    const juce::Colour dim      { 0xff8a9099 };
    const juce::Colour input    { 0xff7d838c };
    const juce::Colour output   { 0xff7ee0a8 };
    const juce::Colour side     { 0xffb58ce0 };
    const juce::Colour timeStage { 0xfff0a35e };
    const juce::Colour spectralStage { 0xff6fb7e8 };
    const juce::Colour ratio    { 0xff4fd1c5 };
    const juce::Colour attack   { 0xffff6b6b };
    const juce::Colour sustain  { 0xfff7d154 };
}

//==============================================================================
class ScopeView  : public juce::Component
{
public:
    explicit ScopeView (ChouchouShaperAudioProcessor& p);

    /** Pulls new columns from the processor. When hidden or frozen they are discarded. */
    void pull (bool keep);

    void setWindowSeconds (double s) { windowSeconds = s; repaint(); }
    void setZoom (float z)           { zoom = z; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct PixelData
    {
        float inMin = 0, inMax = 0, outMin = 0, outMax = 0, scMin = 0, scMax = 0;
        bool scActive = false;
        float tRatio = 0, tAttack = 0, tSustain = 0, tTotal = 0;
        float sRatio = 0, sAttack = 0, sSustain = 0, sTotal = 0;
        bool valid = false;
    };

    /** Column with input time 'age' columns before the newest aligned column. */
    const ScopeColumn* columnAt (int ringIndexFromNewest) const;
    PixelData gatherPixel (int firstAge, int lastAge) const;
    int visibleColumns() const;

    void paintWaveLane (juce::Graphics&, juce::Rectangle<float>, const std::vector<PixelData>&);
    void paintGainLane (juce::Graphics&, juce::Rectangle<float>, const std::vector<PixelData>&,
                        bool spectralLane);
    void paintHover (juce::Graphics&, juce::Rectangle<float> lanes, const std::vector<PixelData>&);

    ChouchouShaperAudioProcessor& proc;

    static constexpr int kRing = 16384;
    std::vector<ScopeColumn> ring;
    int head = 0;     // next write index
    int filled = 0;   // valid columns in the ring

    double windowSeconds = 2.0;
    float zoom = 1.0f;
    int hoverX = -1;
};

//==============================================================================
class SpectrumView  : public juce::Component
{
public:
    explicit SpectrumView (ChouchouShaperAudioProcessor& p) : proc (p) {}

    /** Refreshes the display buffers (with decay so the picture is readable). */
    void update (bool keep);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    float freqAtX (float x, juce::Rectangle<float> r) const;
    float xAtFreq (float f, juce::Rectangle<float> r) const;

    ChouchouShaperAudioProcessor& proc;

    // One entry per horizontal pixel of the plot area.
    std::vector<float> inDb, outDb, gainDb, ratioDb, attackDb, sustainDb;
    float maxCutDb = 0, maxCutHz = 0, maxBoostDb = 0, maxBoostHz = 0;
    int hoverX = -1;
};

//==============================================================================
class ScopeSection  : public juce::Component
{
public:
    explicit ScopeSection (ChouchouShaperAudioProcessor& p);

    /** Called from the editor timer. */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ScopeView scope;
    SpectrumView spectrum;

    juce::ToggleButton freezeButton { "Freeze" };
    juce::ComboBox windowBox, zoomBox;
    juce::Rectangle<int> legendArea, readoutArea;
};
