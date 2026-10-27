/*
  Offline checks for chouchouFFT1.
  Dry/Wet 0 is the input delayed by the reported latency.
  Output is a decibel gain on that mix.
  Dry/Wet 0.5 is the linear blend of the dry and wet renders.
  Every experiment stays finite, and experiment 0 really follows mulreel/mulimage.
*/

#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    constexpr double kSr = 48000.0;
    constexpr double kPi = 3.14159265358979323846;
    int gFails = 0;

    void expect (bool ok, const std::string& name, const std::string& detail = {})
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << name;
        if (! detail.empty())
            std::cout << "  (" << detail << ")";
        std::cout << "\n";
        if (! ok)
            ++gFails;
    }

    void setParam (NewProjectAudioProcessor& p, const char* id, float plain)
    {
        auto* param = p.getAPVTS().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }

    void render (NewProjectAudioProcessor& p, const std::vector<float>& in, std::vector<float>& out)
    {
        juce::AudioBuffer<float> buffer (2, (int) in.size());
        for (int i = 0; i < (int) in.size(); ++i)
            buffer.setSample (0, i, in[(size_t) i]);
        buffer.copyFrom (1, 0, buffer, 0, 0, (int) in.size());
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        out.resize (in.size());
        for (int i = 0; i < (int) in.size(); ++i)
            out[(size_t) i] = buffer.getSample (0, i);
    }

    void setParam (const std::unique_ptr<NewProjectAudioProcessor>& p, const char* id, float plain)
    {
        setParam (*p, id, plain);
    }

    void render (const std::unique_ptr<NewProjectAudioProcessor>& p, const std::vector<float>& in, std::vector<float>& out)
    {
        render (*p, in, out);
    }

    std::vector<float> tone (int n)
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = 0.25f * std::sin (2.0 * kPi * 440.0 * i / kSr)
                          + (i % 800 == 0 ? 0.4f : 0.0f);
        return x;
    }

    std::unique_ptr<NewProjectAudioProcessor> prepared (int fftChoice)
    {
        auto p = std::make_unique<NewProjectAudioProcessor>();
        setParam (*p, "fftSize", (float) fftChoice);
        setParam (*p, "experiment", 0.0f);
        setParam (*p, "mulreel", 1.0f);
        setParam (*p, "mulimage", 1.0f);
        setParam (*p, "mix", 0.0f);
        setParam (*p, "output", 0.0f);
        p->prepareToPlay (kSr, 256);
        return p;
    }

    double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b, int from)
    {
        double m = 0.0;
        for (int i = from; i < (int) a.size(); ++i)
            m = std::max (m, (double) std::abs (a[(size_t) i] - b[(size_t) i]));
        return m;
    }

    bool allFinite (const std::vector<float>& x, float limit)
    {
        for (float s : x)
            if (! std::isfinite (s) || std::abs (s) > limit)
                return false;
        return true;
    }
}

int main()
{
    std::cout << "chouchouFFT1\n";

    for (int choice = 0; choice < 4; ++choice)
    {
        auto p = prepared (choice);
        const int latency = p->getLatencySamples();
        const int n = latency + 6000;
        const auto in = tone (n);
        std::vector<float> out;
        render (p, in, out);

        double err = 0.0;
        for (int i = latency; i < n; ++i)
            err = std::max (err, (double) std::abs (out[(size_t) i] - in[(size_t) (i - latency)]));
        expect (latency == p->getFftSize() && err < 1.0e-6,
                "dry matches input delayed by FFT " + std::to_string (p->getFftSize()),
                "err " + std::to_string (err));
    }

    {
        auto quiet = prepared (1);
        auto loud = prepared (1);
        setParam (loud, "output", 6.0f);
        const int n = quiet->getLatencySamples() + 4000;
        const auto in = tone (n);
        std::vector<float> a, b;
        render (quiet, in, a);
        render (loud, in, b);
        const float g = juce::Decibels::decibelsToGain (6.0f);
        double err = 0.0;
        for (int i = 0; i < n; ++i)
            err = std::max (err, (double) std::abs (b[(size_t) i] - a[(size_t) i] * g));
        expect (err < 1.0e-5, "output +6 dB doubles the dry path", "err " + std::to_string (err));
    }

    {
        auto dry = prepared (1);
        auto wet = prepared (1);
        auto mid = prepared (1);
        setParam (wet, "mix", 1.0f);
        setParam (mid, "mix", 0.5f);
        setParam (dry, "shift", 1.7f);
        setParam (wet, "shift", 1.7f);
        setParam (mid, "shift", 1.7f);
        setParam (dry, "experiment", 1.0f);
        setParam (wet, "experiment", 1.0f);
        setParam (mid, "experiment", 1.0f);
        const int n = 8000;
        const auto in = tone (n);
        std::vector<float> a, b, c;
        render (dry, in, a);
        render (wet, in, b);
        render (mid, in, c);
        double err = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float expectMid = a[(size_t) i] + 0.5f * (b[(size_t) i] - a[(size_t) i]);
            err = std::max (err, (double) std::abs (c[(size_t) i] - expectMid));
        }
        expect (err < 1.0e-5, "Dry/Wet 0.5 is the linear blend", "err " + std::to_string (err));
    }

    {
        auto unity = prepared (1);
        auto killed = prepared (1);
        setParam (unity, "mix", 1.0f);
        setParam (killed, "mix", 1.0f);
        setParam (killed, "mulreel", 0.0f);
        setParam (killed, "mulimage", 0.0f);
        const int n = 8000;
        const auto in = tone (n);
        std::vector<float> a, b;
        render (unity, in, a);
        render (killed, in, b);
        double sumA = 0.0, sumB = 0.0;
        for (int i = unity->getLatencySamples(); i < n; ++i)
        {
            sumA += (double) a[(size_t) i] * a[(size_t) i];
            sumB += (double) b[(size_t) i] * b[(size_t) i];
        }
        expect (sumA > 1.0 && sumB < sumA * 1.0e-4,
                "experiment 0 follows mulreel and mulimage",
                "energy " + std::to_string (sumA) + " -> " + std::to_string (sumB));
    }

    {
        NewProjectAudioProcessor p;
        p.prepareToPlay (kSr, 256);
        auto* exp = dynamic_cast<juce::AudioParameterChoice*> (p.getAPVTS().getParameter ("experiment"));
        const int nExp = exp->choices.size();
        bool ok = true;
        for (int e = 0; e < nExp; ++e)
        {
            setParam (p, "experiment", (float) e);
            setParam (p, "mix", 1.0f);
            setParam (p, "output", 3.0f);
            setParam (p, "shift", 1.25f);
            setParam (p, "shiftreel", 0.8f);
            setParam (p, "shiftimage", 1.2f);
            setParam (p, "mulreel", 0.7f);
            setParam (p, "mulimage", 0.7f);
            setParam (p, "offsetBins", 12.0f);
            std::vector<float> out;
            render (p, tone (4096), out);
            if (! allFinite (out, 32.0f))
                ok = false;
        }
        expect (ok && nExp == 5, "all 5 experiments stay finite");
    }

    std::cout << (gFails == 0 ? "OK\n" : "FAILED\n");
    return gFails == 0 ? 0 : 1;
}
