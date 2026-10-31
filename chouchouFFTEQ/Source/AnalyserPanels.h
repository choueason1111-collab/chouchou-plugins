#pragma once

#include <JuceHeader.h>
#include <memory>

namespace
{
inline juce::String panelText (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

inline void stylePanelSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 14);
    s.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe6e1d6));
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff2a2e33));
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

inline void stylePanelLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (10.0f));
    l.setColour (juce::Label::textColourId, juce::Colour (0xffe6e1d6));
    l.setJustificationType (juce::Justification::centredLeft);
}
}

class AnalyserControls : public juce::Component
{
public:
    explicit AnalyserControls (juce::AudioProcessorValueTreeState& state) : apvts (state)
    {
        addSlider (overlap, overlapAtt, "anOverlap", "Recouvrement");
        addSlider (average, averageAtt, "anAvg", "Moyenne");
        addSlider (smooth, smoothAtt, "anSmooth", "Lissage");
        addSlider (rate, rateAtt, "anRate", "Rafraichissement");
        addSlider (minHz, minAtt, "anMin", panelText ("Fr\u00e9quence min"));
        addSlider (maxHz, maxAtt, "anMax", panelText ("Fr\u00e9quence max"));
        addSlider (dbRange, dbAtt, "anDb", "Plage dB");
        addSlider (refLevel, refAtt, "anRef", panelText ("Niveau r\u00e9f."));
        addSlider (ampRange, ampAtt, "anAmp", "Plage amplitude");

        addChoice (fftBox, fftAtt, "anFft", "Taille FFT");
        addChoice (winBox, winAtt, "anWin", panelText ("Fen\u00eatre"));
        addChoice (padBox, padAtt, "anPad", "Zero padding");
        addChoice (modeBox, modeAtt, "anMode", "Mode");
        addChoice (freqScale, freqAtt, "anFreqScale", panelText ("\u00c9chelle fr\u00e9q."));
        addChoice (ampScale, ampScaleAtt, "anAmpScale", panelText ("\u00c9chelle amp."));
        addChoice (channel, chanAtt, "anChan", "Canal");

        addToggle (analysis, anAtt, "anOn", "Analyse");
        addToggle (snap, snapAtt, "anSnap", "Calage notes");
        addToggle (notes, notesAtt, "anNotes", "Nom de note");
        addToggle (measures, measAtt, "anMeas", "Mesures");
        addToggle (diff, diffAtt, "anDiff", panelText ("Diff\u00e9rence"));
        addToggle (sidechain, scAtt, "anSc", "Sidechain");

        capRef.setButtonText (panelText ("R\u00e9f\u00e9rence"));
        capA.setButtonText ("A");
        capB.setButtonText ("B");
        addAndMakeVisible (capRef);
        addAndMakeVisible (capA);
        addAndMakeVisible (capB);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto row = area.removeFromTop (34);
        place (row, { &fftBox, &winBox, &padBox, &modeBox, &channel, &freqScale, &ampScale });
        row = area.removeFromTop (36);
        place (row, { &minHz, &maxHz, &dbRange, &refLevel, &ampRange, &overlap, &average, &smooth, &rate });
        row = area.removeFromTop (24);
        place (row, { &analysis, &snap, &notes, &measures, &diff, &sidechain, &capRef, &capA, &capB });
    }

    juce::TextButton capRef, capA, capB;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BoxAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct SliderCell : juce::Component
    {
        juce::Label name;
        juce::Slider slider;
        void resized() override
        {
            auto r = getLocalBounds();
            name.setBounds (r.removeFromTop (12));
            slider.setBounds (r);
        }
    };

    struct ChoiceCell : juce::Component
    {
        juce::Label name;
        juce::ComboBox box;
        void resized() override
        {
            auto r = getLocalBounds();
            name.setBounds (r.removeFromTop (12));
            box.setBounds (r.reduced (0, 1));
        }
    };

    void addSlider (SliderCell& cell, std::unique_ptr<SliderAtt>& att, const char* id, const juce::String& name)
    {
        stylePanelLabel (cell.name, name);
        stylePanelSlider (cell.slider);
        addAndMakeVisible (cell);
        att = std::make_unique<SliderAtt> (apvts, id, cell.slider);
    }

    void addChoice (ChoiceCell& cell, std::unique_ptr<BoxAtt>& att, const char* id, const juce::String& name)
    {
        stylePanelLabel (cell.name, name);
        cell.box.setColour (juce::ComboBox::textColourId, juce::Colour (0xffe6e1d6));
        cell.box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2e33));
        addAndMakeVisible (cell);
        att = std::make_unique<BoxAtt> (apvts, id, cell.box);
    }

    void addToggle (juce::ToggleButton& button, std::unique_ptr<ButtonAtt>& att, const char* id, const juce::String& name)
    {
        button.setButtonText (name);
        button.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffe6e1d6));
        button.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xfff2d56b));
        addAndMakeVisible (button);
        att = std::make_unique<ButtonAtt> (apvts, id, button);
    }

    void place (juce::Rectangle<int> row, std::initializer_list<juce::Component*> items)
    {
        const int n = (int) items.size();
        if (n <= 0)
            return;
        const int w = row.getWidth() / n;
        for (auto* item : items)
            item->setBounds (row.removeFromLeft (w).reduced (2, 0));
    }

    juce::AudioProcessorValueTreeState& apvts;
    SliderCell overlap, average, smooth, rate, minHz, maxHz, dbRange, refLevel, ampRange;
    ChoiceCell fftBox, winBox, padBox, modeBox, freqScale, ampScale, channel;
    juce::ToggleButton analysis, snap, notes, measures, diff, sidechain;
    std::unique_ptr<SliderAtt> overlapAtt, averageAtt, smoothAtt, rateAtt, minAtt, maxAtt, dbAtt, refAtt, ampAtt;
    std::unique_ptr<BoxAtt> fftAtt, winAtt, padAtt, modeAtt, freqAtt, ampScaleAtt, chanAtt;
    std::unique_ptr<ButtonAtt> anAtt, snapAtt, notesAtt, measAtt, diffAtt, scAtt;
};

class PhaseControls : public juce::Component
{
public:
    explicit PhaseControls (juce::AudioProcessorValueTreeState& state) : apvts (state)
    {
        addSlider (gain, gainAtt, "phGain", "Gain affichage");
        addSlider (decay, decayAtt, "phDecay", "Extinction");
        addSlider (persist, persistAtt, "phPersist", "Persistance");
        addSlider (rotation, rotAtt, "phRot", "Rotation");
        addSlider (zoom, zoomAtt, "phZoom", "Zoom");
        addSlider (scaleX, xAtt, "phScaleX", "Echelle X");
        addSlider (scaleY, yAtt, "phScaleY", "Echelle Y");
        addSlider (thick, thickAtt, "phThick", panelText ("\u00c9paisseur"));
        addSlider (bright, brightAtt, "phBright", panelText ("Luminosit\u00e9"));
        addChoice (mode, modeAtt, "phMode", "Mode");
        addChoice (input, inputAtt, "phInput", panelText ("Entr\u00e9e"));
        addToggle (enabled, onAtt, "phOn", "Phase");
        addToggle (grid, gridAtt, "phGrid", "Grille");
        addToggle (freeze, freezeAtt, "phFreeze", "Figer");
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto row = area.removeFromTop (36);
        place (row, { &gain, &decay, &persist, &rotation, &zoom, &scaleX, &scaleY });
        row = area.removeFromTop (36);
        place (row, { &thick, &bright, &mode, &input, &enabled, &grid, &freeze });
    }

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BoxAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct SliderCell : juce::Component
    {
        juce::Label name;
        juce::Slider slider;
        void resized() override
        {
            auto r = getLocalBounds();
            name.setBounds (r.removeFromTop (12));
            slider.setBounds (r);
        }
    };
    struct ChoiceCell : juce::Component
    {
        juce::Label name;
        juce::ComboBox box;
        void resized() override
        {
            auto r = getLocalBounds();
            name.setBounds (r.removeFromTop (12));
            box.setBounds (r.reduced (0, 1));
        }
    };

    void addSlider (SliderCell& cell, std::unique_ptr<SliderAtt>& att, const char* id, const juce::String& name)
    {
        stylePanelLabel (cell.name, name);
        stylePanelSlider (cell.slider);
        addAndMakeVisible (cell);
        att = std::make_unique<SliderAtt> (apvts, id, cell.slider);
    }
    void addChoice (ChoiceCell& cell, std::unique_ptr<BoxAtt>& att, const char* id, const juce::String& name)
    {
        stylePanelLabel (cell.name, name);
        cell.box.setColour (juce::ComboBox::textColourId, juce::Colour (0xffe6e1d6));
        cell.box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2e33));
        addAndMakeVisible (cell);
        att = std::make_unique<BoxAtt> (apvts, id, cell.box);
    }
    void addToggle (juce::ToggleButton& button, std::unique_ptr<ButtonAtt>& att, const char* id, const juce::String& name)
    {
        button.setButtonText (name);
        button.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffe6e1d6));
        button.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xfff2d56b));
        addAndMakeVisible (button);
        att = std::make_unique<ButtonAtt> (apvts, id, button);
    }
    void place (juce::Rectangle<int> row, std::initializer_list<juce::Component*> items)
    {
        const int n = (int) items.size();
        const int w = n > 0 ? row.getWidth() / n : 0;
        for (auto* item : items)
            item->setBounds (row.removeFromLeft (w).reduced (2, 0));
    }

    juce::AudioProcessorValueTreeState& apvts;
    SliderCell gain, decay, persist, rotation, zoom, scaleX, scaleY, thick, bright;
    ChoiceCell mode, input;
    juce::ToggleButton enabled, grid, freeze;
    std::unique_ptr<SliderAtt> gainAtt, decayAtt, persistAtt, rotAtt, zoomAtt, xAtt, yAtt, thickAtt, brightAtt;
    std::unique_ptr<BoxAtt> modeAtt, inputAtt;
    std::unique_ptr<ButtonAtt> onAtt, gridAtt, freezeAtt;
};
