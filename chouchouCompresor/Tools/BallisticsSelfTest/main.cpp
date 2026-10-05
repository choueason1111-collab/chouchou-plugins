/*
  Ballistics self-test: Attack / Speed / Release are independent.

    Attack  = how long to WAIT before gain starts moving
    Speed   = how fast to reach target AFTER the wait
    Release = how fast to return when signal leaves the zone
*/

#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <string>

namespace
{
    int gFails = 0;

    void expect (bool ok, const std::string& name, const std::string& detail = {})
    {
        if (ok) { std::cout << "  PASS  " << name << "\n"; return; }
        ++gFails;
        std::cout << "  FAIL  " << name;
        if (! detail.empty()) std::cout << "  (" << detail << ")";
        std::cout << "\n";
    }

    void setParam (NewProjectAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getAPVTS().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
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

    void fillSilence (juce::AudioBuffer<float>& buf) { buf.clear(); }

    void processN (NewProjectAudioProcessor& p, juce::AudioBuffer<float>& buf,
                   int blocks, double sr, float hz, float amp, double& phase)
    {
        juce::MidiBuffer midi;
        for (int i = 0; i < blocks; ++i)
        {
            fillSine (buf, sr, hz, amp, phase);
            p.processBlock (buf, midi);
        }
    }

    void silenceN (NewProjectAudioProcessor& p, juce::AudioBuffer<float>& buf, int blocks)
    {
        juce::MidiBuffer midi;
        for (int i = 0; i < blocks; ++i)
        {
            fillSilence (buf);
            p.processBlock (buf, midi);
        }
    }

    void flush (NewProjectAudioProcessor& p, juce::AudioBuffer<float>& buf)
    {
        silenceN (p, buf, p.getFftSize() / buf.getNumSamples() + 8);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    constexpr double sr = 48000.0;
    constexpr int bs = 512;

    NewProjectAudioProcessor proc;
    proc.prepareToPlay (sr, bs);
    proc.applyFftSizeFromParameterLocked (true);

    juce::AudioBuffer<float> buf (2, bs);
    const float quiet = juce::Decibels::decibelsToGain (-30.0f);
    const float loud  = juce::Decibels::decibelsToGain (-6.0f);
    const float hopMs = proc.getHopMilliseconds();

    std::cout << "\n=== Ballistics: Attack / Speed / Release ===\n";
    std::cout << "hop=" << hopMs << " ms\n\n";

    //==========================================================================
    std::cout << "[A] BOOST Attack = delay before engage\n";
    {
        // Long Attack + max Speed: early window must stay near unity; later must boost.
        setParam (proc, "upratio", 10.0f);
        setParam (proc, "downratio", 1.0f);
        setParam (proc, "lowthresh", -12.0f);
        setParam (proc, "mix", 1.0f);
        setParam (proc, "makeup", 0.0f);
        setParam (proc, "boostspeed", 100.0f);   // instant ramp once delay ends
        setParam (proc, "boostrel", 50.0f);
        setParam (proc, "boostatk", 80.0f);      // ~80ms wait

        proc.reset();
        flush (proc, buf);

        double phase = 0.0;
        // First ~2 hops after latency fill — still inside attack delay → little boost
        processN (proc, buf, 4, sr, 1000.0f, quiet, phase);
        const float earlyMeter = proc.getMeterBoostDb();
        const float earlyOut = juce::Decibels::gainToDecibels (peakOf (buf), -120.0f);

        // Wait past attack delay + ramp
        processN (proc, buf, 40, sr, 1000.0f, quiet, phase);
        const float lateMeter = proc.getMeterBoostDb();
        const float lateOut = juce::Decibels::gainToDecibels (peakOf (buf), -120.0f);

        expect (earlyMeter < 2.0f,
                "During Attack delay, BOOST meter still low",
                "early=" + juce::String (earlyMeter, 1).toStdString() + " dB");
        expect (lateMeter > earlyMeter + 3.0f,
                "After Attack delay, BOOST engages",
                "early=" + juce::String (earlyMeter, 1).toStdString()
                    + " late=" + juce::String (lateMeter, 1).toStdString());
        expect (lateOut > earlyOut + 2.0f,
                "Output rises after Attack delay",
                "earlyOut=" + juce::String (earlyOut, 1).toStdString()
                    + " lateOut=" + juce::String (lateOut, 1).toStdString());
    }

    //==========================================================================
    std::cout << "\n[B] BOOST Speed = ramp rate after delay (Attack fixed at 0)\n";
    {
        auto meterAfter = [&] (float speed, int blocks) -> float
        {
            setParam (proc, "upratio", 10.0f);
            setParam (proc, "downratio", 1.0f);
            setParam (proc, "lowthresh", -12.0f);
            setParam (proc, "mix", 1.0f);
            setParam (proc, "makeup", 0.0f);
            setParam (proc, "boostatk", 0.0f);      // no delay — isolate Speed
            setParam (proc, "boostrel", 50.0f);
            setParam (proc, "boostspeed", speed);
            proc.reset();
            flush (proc, buf);
            double phase = 0.0;
            processN (proc, buf, blocks, sr, 1000.0f, quiet, phase);
            return proc.getMeterBoostDb();
        };

        const float slow = meterAfter (0.0f, 8);
        const float fast = meterAfter (100.0f, 8);
        expect (fast > slow + 2.0f,
                "Speed 100 ramps Boost faster than Speed 0 (same Attack=0)",
                "s0=" + juce::String (slow, 1).toStdString()
                    + " s100=" + juce::String (fast, 1).toStdString());
    }

    //==========================================================================
    std::cout << "\n[C] BOOST Release independent of Speed\n";
    {
        // Release only runs when signal LEAVES the boost zone (above low thresh).
        // Going quieter still wants boost — that is not a release case.
        auto meterAfterLeave = [&] (float relMs) -> float
        {
            setParam (proc, "upratio", 10.0f);
            setParam (proc, "downratio", 1.0f);
            setParam (proc, "lowthresh", -12.0f);
            setParam (proc, "mix", 1.0f);
            setParam (proc, "makeup", 0.0f);
            setParam (proc, "boostatk", 0.0f);
            setParam (proc, "boostspeed", 100.0f);
            setParam (proc, "boostrel", relMs);
            proc.reset();
            flush (proc, buf);
            double phase = 0.0;
            processN (proc, buf, 48, sr, 1000.0f, quiet, phase); // engage boost
            processN (proc, buf, 10, sr, 1000.0f, loud, phase);  // leave zone → release
            return proc.getMeterBoostDb();
        };

        const float fastRel = meterAfterLeave (10.0f);
        const float slowRel = meterAfterLeave (800.0f);
        expect (slowRel > fastRel + 1.0f,
                "Slow Boost Release holds boost longer after leaving zone",
                "rel10=" + juce::String (fastRel, 1).toStdString()
                    + " rel800=" + juce::String (slowRel, 1).toStdString());
    }

    //==========================================================================
    std::cout << "\n[D] COMP Attack = delay before engage\n";
    {
        setParam (proc, "upratio", 1.0f);
        setParam (proc, "downratio", 10.0f);
        setParam (proc, "highthresh", -18.0f);
        setParam (proc, "mix", 1.0f);
        setParam (proc, "makeup", 0.0f);
        setParam (proc, "compspeed", 100.0f);
        setParam (proc, "grrel", 50.0f);
        setParam (proc, "gratk", 80.0f);

        proc.reset();
        flush (proc, buf);
        double phase = 0.0;
        processN (proc, buf, 4, sr, 1000.0f, loud, phase);
        const float early = proc.getMeterGrDb();
        processN (proc, buf, 40, sr, 1000.0f, loud, phase);
        const float late = proc.getMeterGrDb();

        expect (early < 1.5f,
                "During Comp Attack delay, GR meter low",
                "early=" + juce::String (early, 1).toStdString());
        expect (late > early + 2.0f,
                "After Comp Attack delay, GR engages",
                "early=" + juce::String (early, 1).toStdString()
                    + " late=" + juce::String (late, 1).toStdString());
    }

    //==========================================================================
    std::cout << "\n[E] COMP Speed = ramp rate (Attack=0)\n";
    {
        auto meterAfter = [&] (float speed, int blocks) -> float
        {
            setParam (proc, "upratio", 1.0f);
            setParam (proc, "downratio", 10.0f);
            setParam (proc, "highthresh", -18.0f);
            setParam (proc, "mix", 1.0f);
            setParam (proc, "makeup", 0.0f);
            setParam (proc, "gratk", 0.0f);
            setParam (proc, "grrel", 50.0f);
            setParam (proc, "compspeed", speed);
            proc.reset();
            flush (proc, buf);
            double phase = 0.0;
            processN (proc, buf, blocks, sr, 1000.0f, loud, phase);
            return proc.getMeterGrDb();
        };

        const float slow = meterAfter (0.0f, 8);
        const float fast = meterAfter (100.0f, 8);
        expect (fast > slow + 1.5f,
                "Comp Speed 100 ramps GR faster than Speed 0",
                "s0=" + juce::String (slow, 1).toStdString()
                    + " s100=" + juce::String (fast, 1).toStdString());
    }

    //==========================================================================
    std::cout << "\n[F] COMP Release independent\n";
    {
        auto outAfterDrop = [&] (float relMs) -> float
        {
            setParam (proc, "upratio", 1.0f);
            setParam (proc, "downratio", 10.0f);
            setParam (proc, "highthresh", -18.0f);
            setParam (proc, "mix", 1.0f);
            setParam (proc, "makeup", 0.0f);
            setParam (proc, "gratk", 0.0f);
            setParam (proc, "compspeed", 100.0f);
            setParam (proc, "grrel", relMs);
            proc.reset();
            flush (proc, buf);
            double phase = 0.0;
            processN (proc, buf, 48, sr, 1000.0f, loud, phase);
            const float soft = juce::Decibels::decibelsToGain (-30.0f);
            processN (proc, buf, 12, sr, 1000.0f, soft, phase);
            return juce::Decibels::gainToDecibels (peakOf (buf), -120.0f);
        };

        const float fastRel = outAfterDrop (10.0f);
        const float slowRel = outAfterDrop (800.0f);
        expect (fastRel > slowRel + 0.8f,
                "Slow Comp Release keeps GR longer (quieter after drop)",
                "rel10=" + juce::String (fastRel, 1).toStdString()
                    + " rel800=" + juce::String (slowRel, 1).toStdString());
    }

    //==========================================================================
    std::cout << "\n[G] Attack vs Speed independence (same Speed, different Attack)\n";
    {
        auto earlyMeter = [&] (float atkMs) -> float
        {
            setParam (proc, "upratio", 10.0f);
            setParam (proc, "downratio", 1.0f);
            setParam (proc, "lowthresh", -12.0f);
            setParam (proc, "mix", 1.0f);
            setParam (proc, "makeup", 0.0f);
            setParam (proc, "boostspeed", 100.0f);
            setParam (proc, "boostrel", 50.0f);
            setParam (proc, "boostatk", atkMs);
            proc.reset();
            flush (proc, buf);
            double phase = 0.0;
            processN (proc, buf, 6, sr, 1000.0f, quiet, phase); // ~60ms window
            return proc.getMeterBoostDb();
        };

        const float atk0   = earlyMeter (0.0f);
        const float atk200 = earlyMeter (200.0f);
        expect (atk0 > atk200 + 2.0f,
                "Attack 0 engages within early window; Attack 200 still waiting",
                "atk0=" + juce::String (atk0, 1).toStdString()
                    + " atk200=" + juce::String (atk200, 1).toStdString());
    }

    proc.releaseResources();
    std::cout << "\n=== Result: " << (gFails == 0 ? "ALL PASS" : "FAILURES")
              << " (" << gFails << " failed) ===\n\n";
    return gFails == 0 ? 0 : 1;
}
