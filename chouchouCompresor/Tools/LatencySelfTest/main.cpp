/*
  Latency / PDC self-test for chouchouCompressor.

  Verifies:
    - getLatencySamples() == FFT size for every FFT choice
    - reported ms == samples / SR * 1000
    - dry-align samples match reported latency
    - changing FFT updates host latency report
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

    void checkLatencyAt (NewProjectAudioProcessor& p, int fftChoice, int expectFft, double sr)
    {
        setParam (p, "fftsize", (float) fftChoice);
        p.applyFftSizeFromParameterLocked (true);

        const int reported = p.getReportedLatencySamples();
        const int fftN = p.getFftSize();
        const int dryN = p.getDryAlignSamples();
        const float ms = p.getReportedLatencyMilliseconds();
        const float expectMs = (float) (1000.0 * (double) expectFft / sr);

        expect (fftN == expectFft,
                "FFT choice " + std::to_string (fftChoice) + " → size " + std::to_string (expectFft),
                "got " + std::to_string (fftN));
        expect (reported == expectFft,
                "Host latency samples == FFT " + std::to_string (expectFft),
                "reported=" + std::to_string (reported));
        expect (reported == p.getLatencySamples(),
                "getReportedLatencySamples matches AudioProcessor::getLatencySamples");
        expect (dryN == reported,
                "Dry-align samples match reported latency",
                "dry=" + std::to_string (dryN) + " lat=" + std::to_string (reported));
        expect (std::abs (ms - expectMs) < 0.05f,
                "Latency ms = samples/SR*1000",
                "ms=" + juce::String (ms, 3).toStdString()
                    + " expect=" + juce::String (expectMs, 3).toStdString());
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    constexpr double sr = 48000.0;
    constexpr int bs = 512;

    NewProjectAudioProcessor proc;
    proc.prepareToPlay (sr, bs);

    std::cout << "\n=== Latency / PDC self-test ===\n";
    std::cout << "SR=" << sr << "\n\n";

    std::cout << "[A] FFT 512 / 1024 / 2048 / 4096\n";
    checkLatencyAt (proc, 0, 512, sr);
    checkLatencyAt (proc, 1, 1024, sr);
    checkLatencyAt (proc, 2, 2048, sr);
    checkLatencyAt (proc, 3, 4096, sr);

    std::cout << "\n[B] Process still runs after FFT changes\n";
    {
        setParam (proc, "fftsize", 2.0f); // 2048
        proc.applyFftSizeFromParameterLocked (true);
        setParam (proc, "downratio", 4.0f);
        setParam (proc, "highthresh", -18.0f);
        setParam (proc, "upratio", 1.0f);
        setParam (proc, "mix", 1.0f);

        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const double w = juce::MathConstants<double>::twoPi * 1000.0 / sr;
        float peak = 0.0f;

        for (int n = 0; n < 64; ++n)
        {
            for (int i = 0; i < bs; ++i)
            {
                const float s = 0.25f * (float) std::sin (phase);
                phase += w;
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            proc.processBlock (buf, midi);
            peak = juce::jmax (peak, buf.getMagnitude (0, 0, bs));
        }

        expect (std::isfinite (peak) && peak > 1.0e-5f,
                "Audio still processes after latency/FFT update",
                "peak=" + juce::String (peak, 4).toStdString());
        expect (proc.getProcessCounter() > 0, "processCounter advances");
        expect (proc.getReportedLatencySamples() == 2048, "Latency still 2048 after process");
    }

    std::cout << "\n[C] 44.1 kHz ms conversion\n";
    {
        NewProjectAudioProcessor p2;
        p2.prepareToPlay (44100.0, 512);
        setParam (p2, "fftsize", 2.0f);
        p2.applyFftSizeFromParameterLocked (true);
        const float ms = p2.getReportedLatencyMilliseconds();
        const float expectMs = (float) (1000.0 * 2048.0 / 44100.0);
        expect (std::abs (ms - expectMs) < 0.05f,
                "ms correct at 44.1 kHz for FFT 2048",
                "ms=" + juce::String (ms, 3).toStdString()
                    + " expect=" + juce::String (expectMs, 3).toStdString());
    }

    proc.releaseResources();
    std::cout << "\n=== Result: " << (gFails == 0 ? "ALL PASS" : "FAILURES")
              << " (" << gFails << " failed) ===\n\n";
    return gFails == 0 ? 0 : 1;
}
