/*
  Offline self-test for chouchouPlugplugin (rack host).

  Verifies: construct → prepare → process empty → load compressor → process audio
  → bypass / clear — without Insert Basic / Audition.
*/

#include "PluginProcessor.h"
#include "PluginScanSupport.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <string>

namespace
{
    int gFails = 0;

    void expect (bool ok, const std::string& name, const std::string& detail = {})
    {
        if (ok)
        {
            std::cout << "  PASS  " << name << "\n";
            return;
        }

        ++gFails;
        std::cout << "  FAIL  " << name;
        if (! detail.empty())
            std::cout << "  (" << detail << ")";
        std::cout << "\n";
    }

    float peakOf (const juce::AudioBuffer<float>& buf)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buf.getMagnitude (ch, 0, buf.getNumSamples()));
        return peak;
    }

    void fillSine (juce::AudioBuffer<float>& buf, double sr, float hz, float amp, double& phase)
    {
        const double w = juce::MathConstants<double>::twoPi * (double) hz / sr;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float s = amp * (float) std::sin (phase);
            phase += w;
            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                buf.setSample (ch, i, s);
        }
    }

    bool findCompressorDescription (juce::AudioPluginFormatManager& fm,
                                    juce::PluginDescription& out)
    {
        const juce::File vst3 = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                    .getChildFile ("Library/Audio/Plug-Ins/VST3/chouchouCompressor.vst3");
        const juce::File component = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("Library/Audio/Plug-Ins/Components/chouchouCompressor.component");

        juce::OwnedArray<juce::PluginDescription> types;

        for (auto* format : fm.getFormats())
        {
            types.clearQuick (true);

            if (format->getName() == "VST3" && vst3.isDirectory())
                format->findAllTypesForFile (types, vst3.getFullPathName());
            else if (format->getName().containsIgnoreCase ("AudioUnit") && component.isDirectory())
                format->findAllTypesForFile (types, component.getFullPathName());

            for (auto* t : types)
                if (t->name.containsIgnoreCase ("chouchouCompressor")
                    || t->name.containsIgnoreCase ("Compressor"))
                {
                    out = *t;
                    return true;
                }
        }

        return false;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "\n=== chouchouPlugplugin offline self-test ===\n\n";

    std::cout << "[A] Host lifecycle (empty rack)\n";
    {
        NewProjectAudioProcessor rack;
        expect (true, "Constructed without crash");

        rack.prepareToPlay (48000.0, 512);
        expect (true, "prepareToPlay");

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        buf.clear();
        rack.processBlock (buf, midi);
        expect (peakOf (buf) < 1.0e-6f, "Empty rack passes silence");

        double phase = 0.0;
        fillSine (buf, 48000.0, 440.0f, 0.25f, phase);
        const float inPeak = peakOf (buf);
        rack.processBlock (buf, midi);
        expect (std::abs (peakOf (buf) - inPeak) < 1.0e-4f,
                "Empty rack is unity on sine",
                "in=" + juce::String (inPeak, 4).toStdString()
                    + " out=" + juce::String (peakOf (buf), 4).toStdString());

        rack.releaseResources();
        expect (true, "releaseResources");
    }

    std::cout << "\n[B] Scanner host-safety policy\n";
    {
        const auto exe = findChouChouScannerExecutable();
        expect (exe.existsAsFile(),
                "Scanner helper is installed",
                exe.getFullPathName().toStdString());

        // When hosted (this test binary is NOT .app), in-process fallback must be off.
        // Our test exe is a naked binary → isChouChouRunningAsStandaloneApp() == false.
        expect (! isChouChouRunningAsStandaloneApp(),
                "Test binary is treated as hosted (not Standalone.app)");
    }

    std::cout << "\n[C] Load chouchouCompressor into slot + process\n";
    {
        NewProjectAudioProcessor rack;
        rack.prepareToPlay (48000.0, 512);

        juce::PluginDescription desc;
        const bool found = findCompressorDescription (rack.getFormatManager(), desc);
        expect (found, "Found installed chouchouCompressor",
                found ? desc.name.toStdString() : "not installed");

        if (found)
        {
            juce::String err;
            const bool loaded = rack.loadPluginBlocking (0, desc, err);
            expect (loaded, "loadPluginBlocking slot 0", err.toStdString());
            expect (rack.isSlotLoaded (0), "isSlotLoaded(0)");
            expect (rack.getSlotName (0).containsIgnoreCase ("chouchou")
                        || rack.getSlotName (0).containsIgnoreCase ("Compress"),
                    "Slot name looks like compressor",
                    rack.getSlotName (0).toStdString());

            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;

            // Warm STFT latency of hosted compressor
            for (int i = 0; i < 16; ++i)
            {
                fillSine (buf, 48000.0, 1000.0f, 0.2f, phase);
                rack.processBlock (buf, midi);
            }

            fillSine (buf, 48000.0, 1000.0f, 0.2f, phase);
            rack.processBlock (buf, midi);
            expect (std::isfinite (peakOf (buf)) && peakOf (buf) < 8.0f,
                    "Hosted compressor processBlock is finite / non-exploding",
                    "peak=" + juce::String (peakOf (buf), 4).toStdString());

            rack.setSlotBypassed (0, true);
            fillSine (buf, 48000.0, 1000.0f, 0.2f, phase);
            const float bypassIn = peakOf (buf);
            rack.processBlock (buf, midi);
            expect (std::abs (peakOf (buf) - bypassIn) < 1.0e-4f,
                    "Bypassed slot is unity",
                    "in=" + juce::String (bypassIn, 4).toStdString()
                        + " out=" + juce::String (peakOf (buf), 4).toStdString());

            rack.clearSlot (0);
            expect (! rack.isSlotLoaded (0), "clearSlot empties slot");
        }

        rack.releaseResources();
    }

    std::cout << "\n[D] Self-load rejection\n";
    {
        NewProjectAudioProcessor rack;
        rack.prepareToPlay (48000.0, 512);

        juce::PluginDescription self;
        self.name = "chouchouPlugplugin";
        self.fileOrIdentifier = "/tmp/chouchouPlugplugin.vst3";
        self.manufacturerName = "chouchou";
        self.pluginFormatName = "VST3";

        juce::String err;
        const bool loaded = rack.loadPluginBlocking (0, self, err);
        expect (! loaded, "Rejects loading Plugplugin into itself", err.toStdString());
        rack.releaseResources();
    }

    std::cout << "\n[E] Compressor in slot 3 (third effect) still processes\n";
    {
        NewProjectAudioProcessor rack;
        rack.prepareToPlay (48000.0, 512);

        juce::PluginDescription desc;
        const bool found = findCompressorDescription (rack.getFormatManager(), desc);
        expect (found, "Found compressor for slot-3 test");

        if (found)
        {
            juce::String err;
            // Fill first two slots with compressors too (serial chain), then slot 3.
            expect (rack.loadPluginBlocking (0, desc, err), "load slot 1", err.toStdString());
            expect (rack.loadPluginBlocking (1, desc, err), "load slot 2", err.toStdString());
            expect (rack.loadPluginBlocking (2, desc, err), "load slot 3", err.toStdString());
            expect (rack.isSlotLoaded (2), "slot 3 loaded");

            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;

            for (int i = 0; i < 48; ++i)
            {
                fillSine (buf, 48000.0, 1000.0f, 0.35f, phase);
                rack.processBlock (buf, midi);
            }

            fillSine (buf, 48000.0, 1000.0f, 0.35f, phase);
            rack.processBlock (buf, midi);
            const float outPeak = peakOf (buf);
            expect (std::isfinite (outPeak) && outPeak > 1.0e-4f && outPeak < 8.0f,
                    "Slot-3 chain still outputs audio",
                    "peak=" + juce::String (outPeak, 4).toStdString());

            // Slot 3 alone (clear 1+2): should still pass signal
            rack.clearSlot (0);
            rack.clearSlot (1);
            for (int i = 0; i < 48; ++i)
            {
                fillSine (buf, 48000.0, 1000.0f, 0.35f, phase);
                rack.processBlock (buf, midi);
            }
            fillSine (buf, 48000.0, 1000.0f, 0.35f, phase);
            rack.processBlock (buf, midi);
            expect (peakOf (buf) > 1.0e-4f,
                    "Compressor alone in slot 3 still outputs",
                    "peak=" + juce::String (peakOf (buf), 4).toStdString());
        }

        rack.releaseResources();
    }

    std::cout << "\n=== Result: " << (gFails == 0 ? "ALL PASS" : "FAILURES")
              << " (" << gFails << " failed) ===\n\n";
    return gFails == 0 ? 0 : 1;
}
