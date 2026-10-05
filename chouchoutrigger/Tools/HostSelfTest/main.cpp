/*
  Mini DAW host test for chouchouTrigger.

  Loads the installed ~/Library/Audio/Plug-Ins/VST3/chouchouTrigger.vst3 and
  ~/Library/Audio/Plug-Ins/Components/chouchouTrigger.component through AudioPluginFormatManager
  (like a DAW would), then checks bus layouts, host parameter control, sample loading through the
  saved state, trigger timing at several block sizes / sample rates, dry/wet, mono/stereo,
  project save + reload, and opening/closing the editor.
*/

#import <Foundation/Foundation.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <string>

namespace
{
    int gFails = 0;

    void expect (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << name;
        if (! ok && detail.isNotEmpty())
            std::cout << "  (" << detail << ")";
        std::cout << "\n";
        if (! ok) ++gFails;
    }

    enum class Fmt { vst3, au };
    juce::String fmtName (Fmt f) { return f == Fmt::vst3 ? "VST3" : "AU"; }

    //==============================================================================
    juce::File writeWav (const juce::String& name, int channels, int length, double sr,
                         std::function<float (int ch, int i)> fn)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        f.deleteFile();
        juce::AudioBuffer<float> b (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                b.setSample (ch, i, fn (ch, i));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f));
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate (sr)
                                                  .withNumChannels (channels)
                                                  .withBitsPerSample (32)
                                                  .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (b, 0, length);
        return f;
    }

    //==============================================================================
    bool findDescription (juce::AudioPluginFormatManager& fm, Fmt fmt, juce::PluginDescription& out)
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        const auto vst3 = home.getChildFile ("Library/Audio/Plug-Ins/VST3/chouchouTrigger.vst3");
        const auto comp = home.getChildFile ("Library/Audio/Plug-Ins/Components/chouchouTrigger.component");

        for (auto* format : fm.getFormats())
        {
            juce::OwnedArray<juce::PluginDescription> types;

            if (fmt == Fmt::vst3 && format->getName() == "VST3" && vst3.isDirectory())
                format->findAllTypesForFile (types, vst3.getFullPathName());

            if (fmt == Fmt::au && format->getName().containsIgnoreCase ("AudioUnit"))
            {
                if (comp.isDirectory())
                    format->findAllTypesForFile (types, comp.getFullPathName());
                if (types.isEmpty())
                    format->findAllTypesForFile (types, "AudioUnit:Effects/aufx,cTrg,ChoU");
            }

            for (auto* t : types)
                if (t->name.containsIgnoreCase ("chouchouTrigger"))
                {
                    out = *t;
                    return true;
                }
        }
        return false;
    }

    juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
    {
        for (auto* param : p.getParameters())
            if (param->getName (128) == name)
                return param;
        return nullptr;
    }

    bool setText (juce::AudioPluginInstance& p, const juce::String& name, const juce::String& text)
    {
        if (auto* param = findParam (p, name))
        {
            param->setValueNotifyingHost (param->getValueForText (text));
            return true;
        }
        return false;
    }

    bool setNorm (juce::AudioPluginInstance& p, const juce::String& name, float v)
    {
        if (auto* param = findParam (p, name))
        {
            param->setValueNotifyingHost (v);
            return true;
        }
        return false;
    }

    juce::String paramText (juce::AudioPluginInstance& p, const juce::String& name)
    {
        if (auto* param = findParam (p, name))
            return param->getText (param->getValue(), 64);
        return "<missing>";
    }

    //==============================================================================
    // The host wraps the plug-in's own state (VST3: XML with base64 "IComponent"; AU: plist key
    // "jucePluginState"). These helpers unwrap / rewrap it, like restoring a DAW project.
    bool unwrapState (Fmt fmt, const juce::MemoryBlock& outer, juce::MemoryBlock& inner)
    {
        if (fmt == Fmt::vst3)
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (outer.getData(), (int) outer.getSize());
            if (xml == nullptr)
                return false;
            auto* comp = xml->getChildByName ("IComponent");
            return comp != nullptr && inner.fromBase64Encoding (comp->getAllSubText());
        }

        NSData* data = [NSData dataWithBytes: outer.getData() length: outer.getSize()];
        NSError* err = nil;
        id plist = [NSPropertyListSerialization propertyListWithData: data options: NSPropertyListImmutable format: nil error: &err];
        if (! [plist isKindOfClass: [NSDictionary class]])
            return false;
        NSData* st = [(NSDictionary*) plist objectForKey: @"jucePluginState"];
        if (st == nil)
            return false;
        inner.replaceAll (st.bytes, st.length);
        return true;
    }

    bool rewrapState (Fmt fmt, const juce::MemoryBlock& outer, const juce::MemoryBlock& inner, juce::MemoryBlock& result)
    {
        if (fmt == Fmt::vst3)
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (outer.getData(), (int) outer.getSize());
            if (xml == nullptr)
                return false;
            auto* comp = xml->getChildByName ("IComponent");
            if (comp == nullptr)
                return false;
            comp->deleteAllTextElements();
            comp->addTextElement (inner.toBase64Encoding());
            juce::AudioProcessor::copyXmlToBinary (*xml, result);
            return true;
        }

        NSData* data = [NSData dataWithBytes: outer.getData() length: outer.getSize()];
        NSError* err = nil;
        id plist = [NSPropertyListSerialization propertyListWithData: data options: NSPropertyListMutableContainersAndLeaves format: nil error: &err];
        if (! [plist isKindOfClass: [NSMutableDictionary class]])
            return false;
        NSMutableDictionary* dict = (NSMutableDictionary*) plist;
        [dict setObject: [NSData dataWithBytes: inner.getData() length: inner.getSize()] forKey: @"jucePluginState"];
        NSData* out = [NSPropertyListSerialization dataWithPropertyList: dict format: NSPropertyListBinaryFormat_v1_0 options: 0 error: &err];
        if (out == nil)
            return false;
        result.replaceAll (out.bytes, out.length);
        return true;
    }

    std::unique_ptr<juce::XmlElement> readPluginXml (juce::AudioPluginInstance& p, Fmt fmt)
    {
        juce::MemoryBlock outer, inner;
        p.getStateInformation (outer);
        if (! unwrapState (fmt, outer, inner))
            return nullptr;
        return juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
    }

    bool setSlotPaths (juce::AudioPluginInstance& p, Fmt fmt, const std::map<int, juce::String>& paths)
    {
        juce::MemoryBlock outer, inner, newInner, newOuter;
        p.getStateInformation (outer);
        if (! unwrapState (fmt, outer, inner))
            return false;
        auto xml = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
        if (xml == nullptr)
            return false;

        auto* samples = xml->getChildByName ("SAMPLES");
        if (samples == nullptr)
            return false;
        for (auto* slot : samples->getChildWithTagNameIterator ("SLOT"))
        {
            const auto it = paths.find (slot->getIntAttribute ("index"));
            if (it != paths.end())
                slot->setAttribute ("path", it->second);
        }

        juce::AudioProcessor::copyXmlToBinary (*xml, newInner);
        if (! rewrapState (fmt, outer, newInner, newOuter))
            return false;
        p.setStateInformation (newOuter.getData(), (int) newOuter.getSize());
        return true;
    }

    juce::String slotPathInXml (const juce::XmlElement& xml, int slot)
    {
        if (auto* samples = xml.getChildByName ("SAMPLES"))
            for (auto* e : samples->getChildWithTagNameIterator ("SLOT"))
                if (e->getIntAttribute ("index") == slot)
                    return e->getStringAttribute ("path");
        return {};
    }

    double paramInXml (const juce::XmlElement& xml, const juce::String& id)
    {
        for (auto* e : xml.getChildWithTagNameIterator ("PARAM"))
            if (e->getStringAttribute ("id") == id)
                return e->getDoubleAttribute ("value");
        return std::nan ("");
    }

    //==============================================================================
    juce::AudioBuffer<float> hitPattern (int channels, double sr)
    {
        juce::AudioBuffer<float> in (channels, (int) std::lround (0.4 * sr));
        in.clear();
        auto burst = [&] (double at)
        {
            const int s0 = (int) std::lround (at * sr);
            const int len = (int) std::lround (0.01 * sr);
            for (int i = 0; i < len; ++i)
            {
                const float v = 0.8f * std::exp (-4.0f * (float) i / (float) len)
                              * (float) std::cos (juce::MathConstants<double>::twoPi * 200.0 * i / sr);
                for (int ch = 0; ch < channels; ++ch)
                    in.setSample (ch, s0 + i, v);
            }
        };
        burst (0.10);   // fires
        burst (0.13);   // inside the 50 ms retrigger window
        burst (0.25);   // fires again
        return in;
    }

    // blockSize <= 0 means random block sizes (1..700).
    void processAll (juce::AudioPluginInstance& p, juce::AudioBuffer<float>& io, int blockSize)
    {
        juce::MidiBuffer midi;
        std::mt19937 rng (99);
        std::uniform_int_distribution<int> dist (1, 700);
        int pos = 0;
        while (pos < io.getNumSamples())
        {
            const int n = juce::jmin (blockSize > 0 ? blockSize : dist (rng), io.getNumSamples() - pos);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), pos, n);
            p.processBlock (view, midi);
            midi.clear();
            pos += n;
        }
    }

    std::vector<int> onsets (const juce::AudioBuffer<float>& out)
    {
        std::vector<int> v;
        for (int i = 1; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > 1.0e-3f && std::abs (out.getSample (0, i - 1)) < 1.0e-5f)
                v.push_back (i);
        return v;
    }

    juce::String listString (const std::vector<int>& v)
    {
        juce::String s;
        for (auto x : v) s << x << " ";
        return s.trim();
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)) || std::abs (b.getSample (ch, i)) > 8.0f)
                    return false;
        return true;
    }

    struct CaseOptions
    {
        double sr = 48000.0;
        int blockSize = 512;
        bool monoBus = false;
        bool monoOut = false;
        juce::String dry = "-60", wet = "0";
    };

    struct CaseResult
    {
        bool created = false, layoutOk = false, stateOk = false, paramsOk = false;
        juce::String paramInfo;
        juce::AudioBuffer<float> in, out;
    };

    CaseResult runCase (juce::AudioPluginFormatManager& fm, const juce::PluginDescription& desc, Fmt fmt,
                        const juce::File& sample, const CaseOptions& o)
    {
        CaseResult r;
        juce::String err;
        const int maxBlock = 1024;
        auto p = fm.createPluginInstance (desc, o.sr, maxBlock, err);
        if (p == nullptr)
            return r;
        r.created = true;

        juce::AudioProcessor::BusesLayout layout;
        const auto set = o.monoBus ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        r.layoutOk = p->setBusesLayout (layout);

        r.stateOk = setSlotPaths (*p, fmt, { { 1, sample.getFullPathName() } });

        p->prepareToPlay (o.sr, maxBlock);

        r.paramsOk = setText (*p, "Threshold", "-20")
                  && setText (*p, "Retrigger", "50")
                  && setText (*p, "Release", "3")
                  && setText (*p, "Dry", o.dry)
                  && setText (*p, "Wet", o.wet)
                  && setNorm (*p, "Output Mode", o.monoOut ? 0.0f : 1.0f)
                  && setNorm (*p, "Slot 1 On", 1.0f)
                  && setText (*p, "Slot 1 Level", "0")
                  && setText (*p, "Slot 1 Pitch", "0");
        r.paramInfo = "Threshold=" + paramText (*p, "Threshold") + " Retrigger=" + paramText (*p, "Retrigger");

        // A little silence first so queued parameter changes reach the plug-in.
        const int numCh = o.monoBus ? 1 : 2;
        juce::AudioBuffer<float> warm (numCh, 2048);
        warm.clear();
        processAll (*p, warm, 256);

        r.in = hitPattern (numCh, o.sr);
        r.out = r.in;
        processAll (*p, r.out, o.blockSize);

        p->releaseResources();
        return r;
    }

    //==============================================================================
    void testFormat (juce::AudioPluginFormatManager& fm, Fmt fmt, const juce::File& sample, const juce::File& sample2)
    {
        const auto tag = "[" + fmtName (fmt) + "] ";
        std::cout << "\n--- " << fmtName (fmt) << " ---\n";

        juce::PluginDescription desc;
        const bool found = findDescription (fm, fmt, desc);
        expect (found, tag + "Installed plug-in found (" + desc.fileOrIdentifier + ")");
        if (! found)
            return;

        // Main case: stereo track, 48 kHz, 512.
        {
            auto r = runCase (fm, desc, fmt, sample, {});
            expect (r.created, tag + "Instance created");
            if (! r.created)
                return;
            expect (r.layoutOk, tag + "Stereo -> stereo bus layout accepted");
            expect (r.stateOk, tag + "Sample path written through the host state");
            expect (r.paramsOk && r.paramInfo.contains ("-20") && r.paramInfo.contains ("50"),
                    tag + "Parameters set through the host (" + r.paramInfo + ")", r.paramInfo);

            const auto on = onsets (r.out);
            const std::vector<int> want { 4800, 12000 };
            expect (on == want, tag + "Triggers at 100 ms and 250 ms only; hit inside retrigger window ignored", listString (on));
            expect (std::abs (r.out.getSample (0, 4810) - 0.5f) < 1.0e-3f && std::abs (r.out.getSample (1, 4810) + 0.25f) < 1.0e-3f,
                    tag + "Stereo mode: sample L/R preserved (0.5 / -0.25)");
            expect (allFinite (r.out), tag + "Output finite and not exploding");
        }

        // Block sizes.
        for (int bs : { 32, 64, 1024, 0 })
        {
            CaseOptions o;
            o.blockSize = bs;
            auto r = runCase (fm, desc, fmt, sample, o);
            const auto on = onsets (r.out);
            expect (on == std::vector<int> { 4800, 12000 },
                    tag + "Block size " + (bs > 0 ? juce::String (bs) : juce::String ("random")) + ": trigger positions exact",
                    listString (on));
        }

        // Sample rates.
        for (double sr : { 44100.0, 96000.0 })
        {
            CaseOptions o;
            o.sr = sr;
            auto r = runCase (fm, desc, fmt, sample, o);
            const auto on = onsets (r.out);
            const std::vector<int> want { (int) std::lround (0.10 * sr), (int) std::lround (0.25 * sr) };
            expect (on == want && std::abs (r.out.getSample (0, want[0] + 10) - 0.5f) < 1.0e-3f,
                    tag + juce::String (sr / 1000.0, 1) + " kHz: triggers exact, sample plays", listString (on));
        }

        // Output mode Mono on a stereo track.
        {
            CaseOptions o;
            o.monoOut = true;
            auto r = runCase (fm, desc, fmt, sample, o);
            expect (std::abs (r.out.getSample (0, 4810) - 0.125f) < 1.0e-3f
                        && std::abs (r.out.getSample (0, 4810) - r.out.getSample (1, 4810)) < 1.0e-6f,
                    tag + "Mono output mode: L and R identical ((0.5 - 0.25) / 2)");
        }

        // Mono track.
        {
            CaseOptions o;
            o.monoBus = true;
            auto r = runCase (fm, desc, fmt, sample, o);
            const auto on = onsets (r.out);
            expect (r.layoutOk && on == std::vector<int> { 4800, 12000 } && std::abs (r.out.getSample (0, 4810) - 0.125f) < 1.0e-3f,
                    tag + "Mono track: layout accepted, triggers, stereo sample summed to mono", listString (on));
        }

        // Dry / wet.
        {
            CaseOptions o;
            o.dry = "0";
            o.wet = "-60";
            auto r = runCase (fm, desc, fmt, sample, o);
            float diff = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < r.in.getNumSamples(); ++i)
                    diff = juce::jmax (diff, std::abs (r.in.getSample (ch, i) - r.out.getSample (ch, i)));
            expect (diff < 1.0e-5f, tag + "Dry 0 dB / Wet -inf: input passes unchanged", juce::String (diff));
        }
        {
            CaseOptions o;
            o.dry = "-6";
            o.wet = "-6";
            auto r = runCase (fm, desc, fmt, sample, o);
            const float g = juce::Decibels::decibelsToGain (-6.0f);
            const float want = r.in.getSample (0, 4900) * g + 0.5f * g;
            expect (std::abs (r.out.getSample (0, 4900) - want) < 1.0e-3f, tag + "Dry -6 dB + Wet -6 dB mix correctly");
        }

        // Save project -> reopen in a new instance.
        {
            juce::String err;
            auto a = fm.createPluginInstance (desc, 48000.0, 512, err);
            auto b = fm.createPluginInstance (desc, 48000.0, 512, err);
            if (a != nullptr && b != nullptr)
            {
                setSlotPaths (*a, fmt, { { 1, sample.getFullPathName() }, { 3, sample2.getFullPathName() } });
                a->prepareToPlay (48000.0, 512);
                setText (*a, "Threshold", "-33");
                setText (*a, "Slot 3 Pitch", "-7");
                setNorm (*a, "Output Mode", 0.0f);
                juce::AudioBuffer<float> warm (2, 2048);
                warm.clear();
                processAll (*a, warm, 256);

                juce::MemoryBlock saved;
                a->getStateInformation (saved);
                b->setStateInformation (saved.getData(), (int) saved.getSize());

                auto xml = readPluginXml (*b, fmt);
                expect (xml != nullptr && slotPathInXml (*xml, 1) == sample.getFullPathName()
                            && slotPathInXml (*xml, 3) == sample2.getFullPathName() && slotPathInXml (*xml, 2).isEmpty(),
                        tag + "Project reload: slot file paths restored");
                expect (xml != nullptr && std::abs (paramInXml (*xml, "threshold") + 33.0) < 0.05
                            && std::abs (paramInXml (*xml, "pitch3") + 7.0) < 0.01
                            && paramInXml (*xml, "outMode") < 0.5
                            && paramText (*b, "Threshold").contains ("-33"),
                        tag + "Project reload: parameters restored (Threshold " + paramText (*b, "Threshold") + ")");

                // The reloaded instance must actually play the restored sample.
                b->prepareToPlay (48000.0, 512);
                setText (*b, "Dry", "-60");
                processAll (*b, warm, 256);
                auto io = hitPattern (2, 48000.0);
                processAll (*b, io, 512);
                expect (io.getMagnitude (0, 4800, 1000) > 0.1f, tag + "Project reload: restored sample plays on the next hit");
                a->releaseResources();
                b->releaseResources();
            }
            else
            {
                expect (false, tag + "Project reload instances created", err);
            }
        }

        // Editor open / close.
        {
            juce::String err;
            auto p = fm.createPluginInstance (desc, 48000.0, 512, err);
            if (p != nullptr)
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorIfNeeded());
                bool ok = ed != nullptr;
                if (ok)
                {
                    juce::Component holder;
                    holder.setSize (juce::jmax (200, ed->getWidth()), juce::jmax (200, ed->getHeight()));
                    holder.addAndMakeVisible (*ed);
                    holder.addToDesktop (juce::ComponentPeer::windowIsTemporary);
                    holder.setVisible (true);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
                    ok = ed->getWidth() >= 1000 && ed->getHeight() >= 700;
                    holder.removeChildComponent (ed.get());
                    holder.removeFromDesktop();
                    ed.reset();
                }
                expect (ok, tag + "Editor opens at full size and closes without crashing");
            }
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::cout << "\n=== chouchouTrigger mini-DAW host test ===\n";

    const auto sample = writeWav ("chouchouTrigger_host_stereo.wav", 2, 4800, 48000.0,
                                  [] (int ch, int) { return ch == 0 ? 0.5f : -0.25f; });
    const auto sample2 = writeWav ("chouchouTrigger_host_mono.wav", 1, 9600, 48000.0,
                                   [] (int, int i) { return 0.3f * (float) std::sin (i * 0.02); });

    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);

    testFormat (fm, Fmt::vst3, sample, sample2);
    testFormat (fm, Fmt::au, sample, sample2);

    std::cout << "\n=== Result: " << (gFails == 0 ? "ALL PASS" : "FAILURES") << " (" << gFails << " failed) ===\n\n";
    return gFails == 0 ? 0 : 1;
}
