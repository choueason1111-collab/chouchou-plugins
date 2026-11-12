/*
  Offline self-test for chouchouEQGate / EQGate2.
  Verifies gate-only behaviour (no expand) and Auto Gain.
*/

#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

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

    void setParam (NewProjectAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getAPVTS().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
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

    float peakOf (const juce::AudioBuffer<float>& buf)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buf.getMagnitude (ch, 0, buf.getNumSamples()));
        return peak;
    }

    float steadyPeak (NewProjectAudioProcessor& p,
                      juce::AudioBuffer<float>& buf,
                      double sr,
                      float hz,
                      float amp,
                      int warmBlocks)
    {
        juce::MidiBuffer midi;
        double phase = 0.0;
        for (int b = 0; b < warmBlocks; ++b)
        {
            fillSine (buf, sr, hz, amp, phase);
            p.processBlock (buf, midi);
        }

        fillSine (buf, sr, hz, amp, phase);
        p.processBlock (buf, midi);
        return peakOf (buf);
    }
}

int main()
{
    std::cout << "\n=== chouchouEQGate SelfTest ===\n";

    NewProjectAudioProcessor proc;
    const double sr = 48000.0;
    const int block = 512;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    juce::AudioBuffer<float> buf (2, block);

    expect (proc.getAPVTS().getParameter ("mode") == nullptr,
            "Expand mode parameter removed");
    expect (proc.getAPVTS().getParameter ("autoGain") != nullptr,
            "Auto Gain parameter exists");

    //--------------------------------------------------------------------------
    std::cout << "\n[A] Gate cuts quiet bins, never boosts loud ones\n";
    {
        setParam (proc, "amt", 4.0f);
        setParam (proc, "threshold", -30.0f);
        setParam (proc, "attack", 0.0f);
        setParam (proc, "release", 0.0f);
        setParam (proc, "mix", 1.0f);
        setParam (proc, "makeup", 0.0f);
        setParam (proc, "autoGain", 0.0f);
        setParam (proc, "fftsize", 2.0f);

        // Loud tone above threshold → near unity (no expand boost)
        const float loudAmp = juce::Decibels::decibelsToGain (-6.0f);
        const float loudDb = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96), -120.0f);
        expect (std::abs (loudDb - (-6.0f)) < 2.5f,
                "Loud tone stays near input level (no expand)",
                "out=" + juce::String (loudDb, 1).toStdString() + " dBFS");

        // Quiet tone below threshold → attenuated
        const float quietAmp = juce::Decibels::decibelsToGain (-50.0f);
        const float quietDb = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, quietAmp, 96), -120.0f);
        expect (quietDb < -50.0f - 3.0f,
                "Quiet tone is gated down",
                "out=" + juce::String (quietDb, 1).toStdString() + " dBFS");
    }

    //--------------------------------------------------------------------------
    std::cout << "\n[B] Auto Gain restores level after gating\n";
    {
        // Mild gate (~6–12 dB cut) so Auto Gain stays within its ±30 dB clamp.
        setParam (proc, "amt", 1.5f);
        setParam (proc, "threshold", -24.0f);
        setParam (proc, "attack", 0.0f);
        setParam (proc, "release", 0.0f);
        setParam (proc, "mix", 1.0f);
        setParam (proc, "makeup", 0.0f);
        setParam (proc, "fftsize", 2.0f);

        const float amp = juce::Decibels::decibelsToGain (-30.0f);

        setParam (proc, "autoGain", 0.0f);
        const float gated = steadyPeak (proc, buf, sr, 1000.0f, amp, 120);
        const float gatedDb = juce::Decibels::gainToDecibels (gated, -120.0f);

        setParam (proc, "autoGain", 1.0f);
        const float ag = steadyPeak (proc, buf, sr, 1000.0f, amp, 200);
        const float agDb = juce::Decibels::gainToDecibels (ag, -120.0f);

        expect (gatedDb < -30.0f - 2.0f,
                "Without Auto Gain, gated tone is quieter",
                "out=" + juce::String (gatedDb, 1).toStdString() + " dBFS");
        expect (std::abs (agDb - (-30.0f)) < 4.0f,
                "With Auto Gain, output ≈ input level",
                "out=" + juce::String (agDb, 1).toStdString() + " dBFS");
        expect (ag > gated * 1.25f,
                "Auto Gain raises level vs gated-only",
                "ag=" + juce::String (agDb, 1).toStdString()
                    + " gated=" + juce::String (gatedDb, 1).toStdString());
    }

    //--------------------------------------------------------------------------
    std::cout << "\n[C] Makeup still works with Auto Gain off\n";
    {
        setParam (proc, "amt", 0.0f); // unity spectral gain
        setParam (proc, "mix", 1.0f);
        setParam (proc, "autoGain", 0.0f);
        setParam (proc, "makeup", 12.0f);
        setParam (proc, "attack", 0.0f);
        setParam (proc, "release", 0.0f);

        const float amp = juce::Decibels::decibelsToGain (-18.0f);
        const float outDb = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, amp, 96), -120.0f);
        expect (outDb > -18.0f + 9.0f,
                "Makeup +12 dB raises output when amt=0",
                "out=" + juce::String (outDb, 1).toStdString() + " dBFS");
    }

    proc.releaseResources();

    std::cout << "\n=== Result: " << (gFails == 0 ? "ALL PASS" : "FAILURES")
              << " (" << gFails << " failed) ===\n\n";
    return gFails == 0 ? 0 : 1;
}
