/*
  Renders the chouchouDelay editor offscreen and checks the echo page open/close flow.
  Writes PNGs to the given folder (default: next to the binary):
    editor_main.png, editor_echo_page.png, editor_after_close.png
*/

#include "PluginEditor.h"
#include <iostream>

namespace
{
    using Proc = ChouchouDelayAudioProcessor;
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

    void pump (int ms)
    {
        juce::Thread::sleep (ms);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto outDir = argc > 1 ? juce::File (juce::String (argv[1]))
                                 : juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

    Proc proc;
    setParam (proc, "echoCount", 12.0f);
    setParam (proc, "interval", 0.3f);
    setParam (proc, "decay", 4.0f);
    setParam (proc, "mix", 60.0f);
    setParam (proc, Proc::idPitch (2), 12.0f);
    setParam (proc, Proc::idPitch (4), -7.0f);
    setParam (proc, Proc::idFine (4), -25.0f);
    setParam (proc, Proc::idLevel (3), -12.0f);
    setParam (proc, Proc::idOffset (3), 1.0f);
    setParam (proc, Proc::idOn (6), 0.0f);
    setParam (proc, Proc::idPitch (9), 5.0f);
    proc.prepareToPlay (48000.0, 512);

    std::cout << "\n=== chouchouDelay editor snapshot ===\n";

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (1200, 760);
    pump (100);

    auto cells = findAll<chouchou_delay_ui::EchoCell> (*ed);
    auto pages = findAll<chouchou_delay_ui::EchoPage> (*ed);
    expect (cells.size() == 32, "32 echo cells present");
    expect (pages.size() == 1 && ! pages[0]->isVisible(), "Echo page exists and starts hidden");

    bool overlap = false;
    for (size_t i = 0; i < cells.size(); ++i)
        for (size_t j = i + 1; j < cells.size(); ++j)
            if (cells[i]->getBounds().intersects (cells[j]->getBounds())) overlap = true;
    expect (! overlap, "Echo cells do not overlap");

    bool allInside = true;
    for (auto* c : ed->getChildren())
        if (c->isVisible() && ! ed->getLocalBounds().contains (c->getBounds())) allInside = false;
    expect (allInside, "All visible components inside the editor at 1200x760");

    savePng (*ed, outDir.getChildFile ("editor_main.png"));

    if (cells.size() == 32 && pages.size() == 1)
    {
        auto* page = pages[0];
        cells[2]->onOpen (3);
        pump (100);
        expect (page->isVisible() && page->getEcho() == 3, "Clicking cell 3 opens the Echo 3 page");

        auto closeButtons = findAll<chouchou_delay_ui::CloseButton> (*page);
        expect (closeButtons.size() == 1 && closeButtons[0]->isVisible() && closeButtons[0]->getWidth() > 20,
                "Echo page has a visible X close button");

        auto labels = findAll<juce::Label> (*page);
        bool offsetText = false;
        for (auto* l : labels)
            if (l->getText().contains ("+1.0% of 300.00 ms = +3.000 ms / +144 smp")) offsetText = true;
        expect (offsetText, "Offset readout shows +1.0% of 300 ms = +3 ms / +144 smp");

        savePng (*ed, outDir.getChildFile ("editor_echo_page.png"));

        auto buttons = findAll<juce::TextButton> (*page);
        for (auto* b : buttons)
            if (b->getButtonText() == ">") b->onClick();
        pump (50);
        expect (page->isVisible() && page->getEcho() == 4, "Next arrow steps to Echo 4 without closing");

        if (! closeButtons.empty())
            closeButtons[0]->onClick();
        pump (50);
        expect (! page->isVisible(), "X button closes the page");

        cells[0]->onOpen (1);
        pump (50);
        page->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        pump (50);
        expect (! page->isVisible(), "Esc closes the page");

        savePng (*ed, outDir.getChildFile ("editor_after_close.png"));
    }

    ed->setSize (1000, 660);
    pump (50);
    bool minInside = true;
    for (auto* c : ed->getChildren())
        if (c->isVisible() && ! ed->getLocalBounds().contains (c->getBounds())) minInside = false;
    expect (minInside, "Layout fits at minimum size 1000x660");
    savePng (*ed, outDir.getChildFile ("editor_min_size.png"));

    ed.reset();
    proc.releaseResources();
    std::cout << "\n=== Result: " << (gFails == 0 ? "ALL PASS" : "FAILURES") << " (" << gFails << " failed) ===\n\n";
    return gFails == 0 ? 0 : 1;
}
