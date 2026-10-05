/*
  Renders the chouchouTrigger editor offscreen after feeding drum hits through the processor.
  Writes PNGs to the given folder (default: next to the binary):
    editor_main.png, editor_min_size.png
*/

#include "PluginEditor.h"
#include <iostream>

namespace
{
    using Proc = ChouchouTriggerAudioProcessor;
    constexpr double kSr = 48000.0;
    int gFails = 0;

    void expect (bool ok, const std::string& name)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << name << "\n";
        if (! ok) ++gFails;
    }

    template <typename T>
    std::vector<T*> findAll (juce::Component& root)
    {
        std::vector<T*> out;
        for (auto* c : root.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c))
                out.push_back (t);
            auto sub = findAll<T> (*c);
            out.insert (out.end(), sub.begin(), sub.end());
        }
        return out;
    }

    void savePng (juce::Component& c, const juce::File& f)
    {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << "        wrote " << f.getFullPathName() << "\n";
    }

    void setParam (Proc& p, const juce::String& id, float value)
    {
        if (auto* param = p.getAPVTS().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    juce::File writeWav (const juce::String& name, int length, std::function<float (int)> fn)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        f.deleteFile();
        juce::AudioBuffer<float> b (1, length);
        for (int i = 0; i < length; ++i)
            b.setSample (0, i, fn (i));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f));
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (kSr).withNumChannels (1).withBitsPerSample (24));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (b, 0, length);
        return f;
    }

    bool layoutOk (juce::Component& ed, const std::vector<chouchou_trigger_ui::SlotStrip*>& strips)
    {
        for (auto* c : ed.getChildren())
            if (c->isVisible() && ! ed.getLocalBounds().contains (c->getBounds()))
                return false;
        for (size_t i = 0; i < strips.size(); ++i)
            for (size_t j = i + 1; j < strips.size(); ++j)
                if (strips[i]->getBounds().intersects (strips[j]->getBounds()))
                    return false;
        return true;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto outDir = argc > 1 ? juce::File (juce::String (argv[1]))
                                 : juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

    const auto kick = writeWav ("chouchouTrigger_kick.wav", 24000, [] (int i)
    {
        const double t = i / kSr;
        return (float) (0.9 * std::exp (-t * 9.0) * std::sin (juce::MathConstants<double>::twoPi * (50.0 + 120.0 * std::exp (-t * 30.0)) * t));
    });
    const auto snare = writeWav ("chouchouTrigger_snare.wav", 12000, [] (int i)
    {
        juce::Random r (i);
        return (float) (0.6 * std::exp (-i / kSr * 25.0) * (r.nextFloat() * 2.0 - 1.0));
    });

    Proc proc;
    proc.loadSlot (1, kick);
    proc.loadSlot (2, snare);
    proc.loadSlot (4, kick);
    setParam (proc, Proc::idPitch (2), 7.0f);
    setParam (proc, Proc::idPitch (4), -12.0f);
    setParam (proc, Proc::idLevel (4), -9.0f);
    setParam (proc, Proc::idOn (4), 0.0f);
    setParam (proc, "threshold", -18.0f);
    setParam (proc, "retrigger", 120.0f);
    proc.prepareToPlay (kSr, 512);

    // 1.5 s of hits: 4 strong hits, one ghost note below threshold, one hit inside the retrigger window.
    juce::AudioBuffer<float> in (2, (int) (1.5 * kSr));
    in.clear();
    auto hit = [&] (double at, float amp)
    {
        const int s0 = (int) (at * kSr);
        for (int i = 0; i < 6000 && s0 + i < in.getNumSamples(); ++i)
        {
            const float v = amp * std::exp (-(float) i / 900.0f) * (float) std::sin (juce::MathConstants<double>::twoPi * 90.0 * i / kSr);
            in.setSample (0, s0 + i, v);
            in.setSample (1, s0 + i, v);
        }
    };
    hit (0.10, 0.8f);
    hit (0.40, 0.7f);
    hit (0.47, 0.7f);     // inside 120 ms window
    hit (0.70, 0.05f);    // ghost note, below threshold
    hit (1.00, 0.9f);
    hit (1.30, 0.6f);

    juce::MidiBuffer midi;
    for (int pos = 0; pos < in.getNumSamples(); pos += 512)
    {
        const int n = juce::jmin (512, in.getNumSamples() - pos);
        juce::AudioBuffer<float> view (in.getArrayOfWritePointers(), 2, pos, n);
        proc.processBlock (view, midi);
    }

    std::cout << "\n=== chouchouTrigger editor snapshot ===\n";
    expect (proc.getTriggerCount() == 4, "4 triggers from the test pattern (window + ghost note skipped)");

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<ChouchouTriggerAudioProcessorEditor*> (ed.get());
    ed->setSize (1100, 760);
    if (editor != nullptr)
        editor->refreshNow();

    auto strips = findAll<chouchou_trigger_ui::SlotStrip> (*ed);
    auto scopes = findAll<chouchou_trigger_ui::ScopeView> (*ed);
    expect (strips.size() == 6, "6 slot strips present");
    expect (scopes.size() == 1, "Scope present");
    expect (layoutOk (*ed, strips), "Layout fits at 1100x760 with no overlapping strips");

    if (strips.size() == 6)
    {
        expect (strips[0]->getDisplayName() == "chouchouTrigger_kick.wav", "Slot 1 shows the loaded file name");
        expect (strips[2]->getDisplayName().startsWith ("Empty"), "Slot 3 shows Empty");
    }

    if (scopes.size() == 1)
    {
        expect (scopes[0]->getTotalColumns() >= 1400, "Scope received ~1.5 s of columns");
        expect (scopes[0]->countTriggerMarkers() == 4, "Scope holds 4 trigger markers");
    }

    savePng (*ed, outDir.getChildFile ("editor_main.png"));

    ed->setSize (1000, 700);
    expect (layoutOk (*ed, strips), "Layout fits at minimum size 1000x700");
    savePng (*ed, outDir.getChildFile ("editor_min_size.png"));

    ed.reset();
    proc.releaseResources();
    std::cout << "\n=== Result: " << (gFails == 0 ? "ALL PASS" : "FAILURES") << " (" << gFails << " failed) ===\n\n";
    return gFails == 0 ? 0 : 1;
}
