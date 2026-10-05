/*
  Offline self-test for chouchouTrigger (processor only, no host).

  Verifies trigger timing / retrigger window / re-arm hysteresis, resampling pitch,
  dry/wet gains, mono/stereo output, scope trigger flags, voice stealing and state restore.
*/

#include "PluginProcessor.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <random>
#include <string>

namespace
{
    using Proc = ChouchouTriggerAudioProcessor;
    constexpr double kSr = 48000.0;
    int gFails = 0;

    void expect (bool ok, const std::string& name, const std::string& detail = {})
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << name;
        if (! ok && ! detail.empty())
            std::cout << "  (" << detail << ")";
        std::cout << "\n";
        if (! ok) ++gFails;
    }

    void setParam (Proc& p, const juce::String& id, float value)
    {
        if (auto* param = p.getAPVTS().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

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

    void addBurst (juce::AudioBuffer<float>& in, int at, float amp, int length = 480, double hz = 200.0)
    {
        for (int i = 0; i < length && at + i < in.getNumSamples(); ++i)
        {
            const float env = std::exp (-4.0f * (float) i / (float) length);
            const float s = amp * env * (float) std::cos (juce::MathConstants<double>::twoPi * hz * i / kSr);
            for (int ch = 0; ch < in.getNumChannels(); ++ch)
                in.setSample (ch, at + i, s);
        }
    }

    // Processes `in` through the plugin in blocks; blockSize <= 0 means random block sizes.
    juce::AudioBuffer<float> run (Proc& p, const juce::AudioBuffer<float>& in, int blockSize)
    {
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        std::mt19937 rng (1234);
        std::uniform_int_distribution<int> dist (1, 700);
        int pos = 0;
        while (pos < out.getNumSamples())
        {
            const int n = juce::jmin (blockSize > 0 ? blockSize : dist (rng), out.getNumSamples() - pos);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
            p.processBlock (view, midi);
            pos += n;
        }
        return out;
    }

    std::string logString (const std::vector<juce::int64>& v)
    {
        std::string s;
        for (auto x : v) s += std::to_string (x) + " ";
        return s;
    }

    void baseSetup (Proc& p, const juce::File& sample)
    {
        setParam (p, "threshold", -20.0f);
        setParam (p, "retrigger", 50.0f);
        setParam (p, "release", 3.0f);
        setParam (p, "dry", -60.0f);
        setParam (p, "wet", 0.0f);
        setParam (p, "outMode", 1.0f);
        p.loadSlot (1, sample);
    }

    juce::AudioBuffer<float> hitPattern (int channels)
    {
        juce::AudioBuffer<float> in (channels, (int) (0.4 * kSr));
        in.clear();
        addBurst (in, 4800, 0.8f);    // fires
        addBurst (in, 6240, 0.8f);    // 30 ms later: inside the 50 ms window
        addBurst (in, 12000, 0.8f);   // 150 ms later: fires again
        return in;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::cout << "\n=== chouchouTrigger self-test ===\n";

    // Stereo sample: constant L = 0.5, R = -0.25, 100 ms long.
    const auto sample = writeWav ("chouchouTrigger_test_stereo.wav", 2, 4800, kSr,
                                  [] (int ch, int) { return ch == 0 ? 0.5f : -0.25f; });
    const auto longSample = writeWav ("chouchouTrigger_test_long.wav", 1, 48000, kSr,
                                      [] (int, int i) { return 0.3f * (float) std::sin (i * 0.05); });

    // 1. Trigger timing and retrigger window.
    {
        Proc p;
        baseSetup (p, sample);
        expect (p.getSlotData (1) != nullptr && p.getSlotData (1)->buffer.getNumChannels() == 2,
                "WAV loads into slot 1 as stereo");
        p.prepareToPlay (kSr, 512);
        const auto in = hitPattern (2);
        const auto out = run (p, in, 512);
        const auto log = p.getTriggerLog();

        expect (log.size() == 2 && log[0] == 4800 && log[1] == 12000,
                "Hits fire at 4800 and 12000 only (hit inside 50 ms window ignored)", logString (log));
        expect (std::abs (out.getSample (0, 4799)) < 1.0e-6f && std::abs (out.getSample (0, 4800) - 0.5f) < 1.0e-4f,
                "Wet starts exactly on the trigger sample");
        expect (std::abs (out.getSample (1, 4800) + 0.25f) < 1.0e-4f, "Stereo mode keeps the right channel (-0.25)");
        expect (std::abs (out.getSample (0, 9599) - 0.5f) < 1.0e-3f && std::abs (out.getSample (0, 9601)) < 1.0e-6f,
                "0 st plays the 4800-sample file for 4800 samples");

        std::vector<Proc::ScopeColumn> cols (4096);
        const int n = p.popScopeColumns (cols.data(), (int) cols.size());
        std::vector<int> flagged;
        for (int i = 0; i < n; ++i)
            if ((cols[(size_t) i].flags & Proc::scopeTrigger) != 0)
                flagged.push_back (i);
        const int spc = p.getScopeSamplesPerColumn();
        expect (flagged.size() == 2 && flagged[0] == 4800 / spc && flagged[1] == 12000 / spc,
                "Scope trigger flags land in the columns of the trigger samples");
        expect (n > 4800 / spc + 10 && (cols[(size_t) (4800 / spc + 10)].flags & Proc::scopeHoldoff) != 0
                    && (cols[(size_t) (4800 / spc + 60)].flags & Proc::scopeHoldoff) == 0,
                "Scope hold-off shading covers the 50 ms retrigger window");
    }

    // 2. Same input at other block sizes and sample positions.
    for (int bs : { 32, 64, 1024, 0 })
    {
        Proc p;
        baseSetup (p, sample);
        p.prepareToPlay (kSr, bs > 0 ? bs : 700);
        run (p, hitPattern (2), bs);
        const auto log = p.getTriggerLog();
        expect (log.size() == 2 && log[0] == 4800 && log[1] == 12000,
                "Trigger positions identical at block size " + (bs > 0 ? std::to_string (bs) : std::string ("random")),
                logString (log));
    }

    // 3. Re-arm hysteresis: a sustained tone above threshold fires once.
    {
        Proc p;
        baseSetup (p, sample);
        p.prepareToPlay (kSr, 512);
        juce::AudioBuffer<float> in (2, (int) (0.5 * kSr));
        for (int i = 0; i < in.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                in.setSample (ch, i, i >= 2400 ? 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 80.0 * i / kSr) : 0.0f);
        run (p, in, 512);
        expect (p.getTriggerCount() == 1, "Sustained 80 Hz tone above threshold fires once",
                std::to_string (p.getTriggerCount()));
    }

    // 3b. A long decaying kick does not retrigger on its own tail, even with a 5 ms retrigger time.
    {
        Proc p;
        baseSetup (p, sample);
        setParam (p, "retrigger", 5.0f);
        p.prepareToPlay (kSr, 512);
        juce::AudioBuffer<float> in (2, (int) kSr);
        in.clear();
        addBurst (in, 2400, 0.9f, (int) (0.4 * kSr), 55.0);
        run (p, in, 512);
        expect (p.getTriggerCount() == 1, "Decaying 55 Hz kick (400 ms) fires once with 5 ms retrigger",
                std::to_string (p.getTriggerCount()));
    }

    // 4. Pitch +12 st halves the length.
    {
        Proc p;
        baseSetup (p, sample);
        setParam (p, Proc::idPitch (1), 12.0f);
        p.prepareToPlay (kSr, 512);
        const auto out = run (p, hitPattern (2), 512);
        int len = 0;
        for (int i = 4800; i < 12000 && std::abs (out.getSample (0, i)) > 1.0e-6f; ++i)
            ++len;
        expect (std::abs (len - 2400) <= 1, "+12 st plays in half the length", std::to_string (len));
    }

    // 5. Dry / wet gains.
    {
        Proc p;
        baseSetup (p, sample);
        setParam (p, "dry", 0.0f);
        setParam (p, "wet", -60.0f);
        p.prepareToPlay (kSr, 512);
        const auto in = hitPattern (2);
        const auto out = run (p, in, 512);
        float maxDiff = 0.0f;
        for (int i = 0; i < in.getNumSamples(); ++i)
            maxDiff = juce::jmax (maxDiff, std::abs (out.getSample (0, i) - in.getSample (0, i)));
        expect (maxDiff < 1.0e-6f, "Dry 0 dB / Wet -inf passes the input unchanged");
    }
    {
        Proc p;
        baseSetup (p, sample);
        setParam (p, "dry", -6.0f);
        setParam (p, "wet", -6.0f);
        p.prepareToPlay (kSr, 512);
        const auto in = hitPattern (2);
        const auto out = run (p, in, 512);
        const float g = juce::Decibels::decibelsToGain (-6.0f);
        const float expected = in.getSample (0, 4800 + 100) * g + 0.5f * g;
        expect (std::abs (out.getSample (0, 4900) - expected) < 1.0e-4f, "Dry -6 dB + Wet -6 dB sum correctly");
    }

    // 6. Mono output mode and slot level.
    {
        Proc p;
        baseSetup (p, sample);
        setParam (p, "outMode", 0.0f);
        p.prepareToPlay (kSr, 512);
        const auto out = run (p, hitPattern (2), 512);
        expect (std::abs (out.getSample (0, 5000) - 0.125f) < 1.0e-4f && std::abs (out.getSample (1, 5000) - 0.125f) < 1.0e-4f,
                "Mono mode: both channels get (L+R)/2");
    }
    {
        Proc p;
        baseSetup (p, sample);
        setParam (p, Proc::idLevel (1), -6.0f);
        p.prepareToPlay (kSr, 512);
        const auto out = run (p, hitPattern (2), 512);
        expect (std::abs (out.getSample (0, 5000) - 0.5f * juce::Decibels::decibelsToGain (-6.0f)) < 1.0e-4f,
                "Slot level -6 dB scales that slot");
    }
    {
        Proc p;
        baseSetup (p, sample);
        setParam (p, Proc::idOn (1), 0.0f);
        p.prepareToPlay (kSr, 512);
        const auto out = run (p, hitPattern (2), 512);
        expect (out.getMagnitude (0, 0, out.getNumSamples()) < 1.0e-6f, "Slot off stays silent");
    }

    // 7. Mono bus.
    {
        Proc p;
        baseSetup (p, sample);
        Proc::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        const bool ok = p.setBusesLayout (mono);
        p.prepareToPlay (kSr, 512);
        const auto out = run (p, hitPattern (1), 512);
        expect (ok && p.getTriggerCount() == 2 && std::abs (out.getSample (0, 5000) - 0.125f) < 1.0e-4f,
                "Mono track: fires and outputs the stereo sample summed to mono");
    }

    // 8. Six slots layered + voice stealing stays finite and bounded.
    {
        Proc p;
        baseSetup (p, sample);
        for (int s = 1; s <= Proc::kNumSlots; ++s)
            p.loadSlot (s, longSample);
        setParam (p, "retrigger", 5.0f);
        setParam (p, "release", 0.0f);
        p.prepareToPlay (kSr, 256);
        juce::AudioBuffer<float> in (2, (int) (2.0 * kSr));
        in.clear();
        for (int k = 0; k < 40; ++k)
            addBurst (in, 1000 + k * 1920, 0.9f, 120, 1000.0);   // 25 hits per second
        const auto out = run (p, in, 256);
        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (! std::isfinite (out.getSample (ch, i))) finite = false;
        const float peak = out.getMagnitude (0, 0, out.getNumSamples());
        expect (p.getTriggerCount() == 40, "40 rapid hits all fire with 5 ms retrigger", std::to_string (p.getTriggerCount()));
        expect (finite && peak < 6.0f * 0.3f * Proc::kVoicePool + 0.01f,
                "6 slots x voice stealing: output finite and bounded", std::to_string (peak));
    }

    // 9. State round trip.
    {
        Proc a;
        baseSetup (a, sample);
        a.loadSlot (3, longSample);
        setParam (a, "threshold", -33.0f);
        setParam (a, Proc::idPitch (3), -7.0f);
        setParam (a, "outMode", 0.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);

        Proc b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        expect (b.getSlotPath (1) == sample.getFullPathName() && b.getSlotPath (3) == longSample.getFullPathName()
                    && b.getSlotData (3) != nullptr && b.getSlotPath (2).isEmpty(),
                "State restores slot files");
        expect (std::abs (b.getThresholdDb() + 33.0f) < 0.05f
                    && std::abs (b.getAPVTS().getRawParameterValue (Proc::idPitch (3))->load() + 7.0f) < 0.01f
                    && b.getAPVTS().getRawParameterValue ("outMode")->load() < 0.5f,
                "State restores parameters");

        const auto gone = writeWav ("chouchouTrigger_test_gone.wav", 1, 100, kSr, [] (int, int) { return 0.1f; });
        Proc c;
        c.loadSlot (2, gone);
        juce::MemoryBlock mc;
        c.getStateInformation (mc);
        gone.deleteFile();
        Proc d;
        d.setStateInformation (mc.getData(), (int) mc.getSize());
        expect (d.isSlotMissing (2) && d.getSlotData (2) == nullptr && d.getSlotPath (2) == gone.getFullPathName(),
                "Missing file is flagged and its path kept");
    }

    // 10. Swapping a sample while playing.
    {
        Proc p;
        baseSetup (p, longSample);
        p.prepareToPlay (kSr, 512);
        auto in = hitPattern (2);
        run (p, in, 512);
        p.loadSlot (1, sample);
        const auto out = run (p, in, 512);
        expect (std::isfinite (out.getMagnitude (0, 0, out.getNumSamples())) && p.getSlotData (1)->buffer.getNumSamples() == 4800,
                "Replacing a slot while running is safe");
    }

    std::cout << "\n=== Result: " << (gFails == 0 ? "ALL PASS" : "FAILURES") << " (" << gFails << " failed) ===\n\n";
    return gFails == 0 ? 0 : 1;
}
