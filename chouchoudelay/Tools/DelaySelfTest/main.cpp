/*
  Offline self-test for chouchouDelay.

  Verifies:
    - echo positions (n * interval), including 1-sample and 10 s intervals
    - echo count, decay (-80 dB at decay time, down to 10 us), per-echo on/off and level
    - per-echo time offset (% of interval)
    - buffer sizing from the echo count, background resize with history kept,
      no allocation inside processBlock
    - per-echo pitch shift (+12 / -12 semitones)
    - dry/wet mix, finite output, no discontinuities while parameters move
*/

#include "PluginProcessor.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <random>
#include <string>
#include <vector>

//==============================================================================
namespace
{
    std::atomic<long> gAllocs { 0 };
    thread_local bool gCountAllocs = false;

    void* countedAlloc (std::size_t n)
    {
        if (gCountAllocs)
            ++gAllocs;
        if (void* p = std::malloc (n == 0 ? 1 : n))
            return p;
        throw std::bad_alloc();
    }
}

void* operator new (std::size_t n)                               { return countedAlloc (n); }
void* operator new[] (std::size_t n)                             { return countedAlloc (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept   { try { return countedAlloc (n); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { try { return countedAlloc (n); } catch (...) { return nullptr; } }
void operator delete (void* p) noexcept                          { std::free (p); }
void operator delete[] (void* p) noexcept                        { std::free (p); }
void operator delete (void* p, std::size_t) noexcept             { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept           { std::free (p); }

//==============================================================================
namespace
{
    using Proc = ChouchouDelayAudioProcessor;
    constexpr double kSr = 48000.0;
    constexpr int kBs = 512;
    int gFails = 0;

    void expect (bool ok, const std::string& name, const std::string& detail = {})
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << name;
        if (! detail.empty()) std::cout << "  (" << detail << ")";
        std::cout << "\n";
        if (! ok) ++gFails;
    }

    std::string num (double v, int d = 6) { return juce::String (v, d).toStdString(); }

    void setParam (Proc& p, const juce::String& id, float value)
    {
        if (auto* param = p.getAPVTS().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
        else
            expect (false, "parameter exists: " + id.toStdString());
    }

    void resetEchoParams (Proc& p)
    {
        for (int e = 1; e <= Proc::kMaxEchoes; ++e)
        {
            setParam (p, Proc::idOn (e), 1.0f);
            setParam (p, Proc::idLevel (e), 0.0f);
            setParam (p, Proc::idPitch (e), 0.0f);
            setParam (p, Proc::idFine (e), 0.0f);
            setParam (p, Proc::idOffset (e), 0.0f);
        }
    }

    void configure (Proc& p, int count, float intervalSec, float decaySec, float mixPct)
    {
        resetEchoParams (p);
        setParam (p, "echoCount", (float) count);
        setParam (p, "interval", intervalSec);
        setParam (p, "decay", decaySec);
        setParam (p, "mix", mixPct);
    }

    // Renders `input` (fed to both channels) and returns channel 0 and channel 1.
    std::vector<float> render (Proc& p, const std::vector<float>& input, std::vector<float>* right = nullptr)
    {
        std::vector<float> out (input.size());
        if (right) right->assign (input.size(), 0.0f);
        juce::AudioBuffer<float> buf (2, kBs);
        juce::MidiBuffer midi;

        for (size_t start = 0; start < input.size(); start += kBs)
        {
            const int n = (int) std::min ((size_t) kBs, input.size() - start);
            buf.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, input[start + (size_t) i]);
                buf.setSample (1, i, input[start + (size_t) i]);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < n; ++i)
            {
                out[start + (size_t) i] = buf.getSample (0, i);
                if (right) (*right)[start + (size_t) i] = buf.getSample (1, i);
            }
        }
        return out;
    }

    std::vector<float> impulse (size_t length)
    {
        std::vector<float> v (length, 0.0f);
        v[0] = 1.0f;
        return v;
    }

    float expectedDecay (double delaySamples, double decaySec)
    {
        return (float) std::pow (10.0, -4.0 * (delaySamples / kSr) / decaySec);
    }

    double maxAbsExcept (const std::vector<float>& v, const std::vector<size_t>& skip)
    {
        double m = 0.0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            bool s = false;
            for (auto k : skip) if (k == i) { s = true; break; }
            if (! s) m = std::max (m, (double) std::abs (v[i]));
        }
        return m;
    }

    double measureFrequency (const std::vector<float>& v, size_t from, size_t to)
    {
        int crossings = 0;
        size_t first = 0, last = 0;
        for (size_t i = from + 1; i < to; ++i)
        {
            if (v[i - 1] < 0.0f && v[i] >= 0.0f)
            {
                if (crossings == 0) first = i;
                last = i;
                ++crossings;
            }
        }
        if (crossings < 2) return 0.0;
        return (crossings - 1) * kSr / (double) (last - first);
    }
}

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::cout << "\n=== chouchouDelay self-test ===  SR=" << kSr << "\n";

    // -------------------------------------------------------------------------
    std::cout << "\n[1] Echo positions = n * interval\n";
    {
        Proc p;
        configure (p, 4, 0.25f, 60.0f, 100.0f);
        p.prepareToPlay (kSr, kBs);
        auto out = render (p, impulse (52000));
        std::vector<size_t> pos;
        bool allOk = true;
        std::string detail;
        for (int n = 1; n <= 4; ++n)
        {
            const size_t k = (size_t) (n * 12000);
            pos.push_back (k);
            const float g = expectedDecay ((double) k, 60.0);
            if (std::abs (out[k] - g) > 1.0e-5f) { allOk = false; detail += "echo" + std::to_string (n) + "=" + num (out[k]) + " "; }
        }
        expect (allOk, "4 echoes at 12000/24000/36000/48000 with decay gain", detail);
        expect (maxAbsExcept (out, pos) < 1.0e-6, "No energy outside echo positions", "max=" + num (maxAbsExcept (out, pos), 9));
    }
    {
        Proc p;
        configure (p, 3, (float) (1.0 / kSr), 60.0f, 100.0f);
        p.prepareToPlay (kSr, kBs);
        expect (p.getEchoDelaySamples (1) == 1.0 && p.getEchoDelaySamples (3) == 3.0, "1-sample interval maps to delays 1,2,3",
                "d1=" + num (p.getEchoDelaySamples (1), 1));
        auto out = render (p, impulse (64));
        expect (out[0] == 0.0f && out[1] > 0.99f && out[2] > 0.99f && out[3] > 0.99f && std::abs (out[4]) < 1.0e-7f,
                "1-sample interval: output at samples 1,2,3 only",
                "o0=" + num (out[0]) + " o1=" + num (out[1]) + " o3=" + num (out[3]) + " o4=" + num (out[4]));
    }
    {
        Proc p;
        configure (p, 1, 10.0f, 60.0f, 100.0f);
        p.prepareToPlay (kSr, kBs);
        auto out = render (p, impulse (480600));
        const float g = expectedDecay (480000.0, 60.0);
        expect (std::abs (out[480000] - g) < 1.0e-5f && maxAbsExcept (out, { 480000 }) < 1.0e-6,
                "10 s interval: single echo at sample 480000", "o=" + num (out[480000]) + " expect=" + num (g));
    }

    // -------------------------------------------------------------------------
    std::cout << "\n[2] Echo count honoured\n";
    {
        Proc p;
        configure (p, 3, 0.1f, 60.0f, 100.0f);
        p.prepareToPlay (kSr, kBs);
        auto out = render (p, impulse (48000));
        double after = 0.0;
        for (size_t i = 3 * 4800 + 1; i < out.size(); ++i) after = std::max (after, (double) std::abs (out[i]));
        expect (out[14400] > 0.1f && after < 1.0e-7, "3 echoes, nothing after echo 3", "after=" + num (after, 9));
    }

    // -------------------------------------------------------------------------
    std::cout << "\n[3] Decay: -80 dB at the decay time\n";
    {
        Proc p;
        configure (p, 6, 0.1f, 0.5f, 100.0f);
        p.prepareToPlay (kSr, kBs);
        auto out = render (p, impulse (32000));
        expect (std::abs (out[24000] - 1.0e-4f) < 1.0e-4f * 0.01f, "Echo at t = decay (0.5 s) is -80 dB",
                "gain=" + num (out[24000], 9) + " (" + num (juce::Decibels::gainToDecibels (out[24000]), 2) + " dB)");
        bool ok = true; std::string d;
        for (int n = 1; n <= 6; ++n)
        {
            const float g = expectedDecay (n * 4800.0, 0.5);
            if (std::abs (out[(size_t) n * 4800] - g) > g * 0.001f + 1.0e-8f) { ok = false; d += std::to_string (n) + " "; }
        }
        expect (ok, "Intermediate echoes follow 10^(-4 t / decay)", d);
    }
    {
        Proc p;
        auto* decayParam = dynamic_cast<juce::RangedAudioParameter*> (p.getAPVTS().getParameter ("decay"));
        const float lo = decayParam->getNormalisableRange().start;
        expect (std::abs (lo - 1.0e-5f) < 1.0e-9f, "Decay minimum is 10 us", "min=" + num (lo * 1.0e6, 3) + " us");
        expect (decayParam->getText (0.0f, 0) == "10.0 us", "Minimum decay displays as 10.0 us",
                decayParam->getText (0.0f, 0).toStdString());
        expect (std::abs (decayParam->getValueForText ("48 smp") - decayParam->convertTo0to1 (0.001f)) < 1.0e-4f,
                "Decay text accepts samples (48 smp = 1 ms)");
    }
    {
        Proc p;
        configure (p, 4, 0.001f, 0.002f, 100.0f);
        p.prepareToPlay (kSr, kBs);
        auto out = render (p, impulse (512));
        const float g1 = expectedDecay (48.0, 0.002);
        expect (std::abs (out[96] - 1.0e-4f) < 1.0e-6f && std::abs (out[48] - g1) < 1.0e-6f,
                "Short decay 2 ms: echo at 2 ms is -80 dB, echo at 1 ms is -40 dB",
                "o48=" + num (out[48], 7) + " o96=" + num (out[96], 9));
    }
    {
        Proc p;
        configure (p, 32, (float) (1.0 / kSr), (float) Proc::kMinDecaySeconds, 100.0f);
        p.prepareToPlay (kSr, kBs);
        auto out = render (p, impulse (256));
        double mx = 0.0;
        bool finite = true;
        for (float v : out) { mx = std::max (mx, (double) std::abs (v)); if (! std::isfinite (v)) finite = false; }
        expect (finite && mx < 1.0e-7, "Minimum decay (10 us) with 1-sample interval: echoes already below -80 dB",
                "max=" + num (mx, 9));
    }

    // -------------------------------------------------------------------------
    std::cout << "\n[4] Per-echo on/off, level, offset\n";
    {
        Proc p;
        configure (p, 4, 0.1f, 60.0f, 100.0f);
        setParam (p, Proc::idOn (3), 0.0f);
        setParam (p, Proc::idLevel (2), -6.0f);
        p.prepareToPlay (kSr, kBs);
        auto out = render (p, impulse (24000));
        expect (out[14400] == 0.0f, "Disabled echo 3 is silent", "o=" + num (out[14400], 9));
        const float g1 = expectedDecay (4800.0, 60.0), g2 = expectedDecay (9600.0, 60.0), g4 = expectedDecay (19200.0, 60.0);
        expect (std::abs (out[4800] - g1) < 1.0e-5f && std::abs (out[19200] - g4) < 1.0e-5f, "Other echoes unchanged");
        const float ratio = out[9600] / g2;
        expect (std::abs (ratio - 0.5f) < 0.005f, "Echo 2 at -6 dB has about half amplitude", "ratio=" + num (ratio, 5));
    }
    for (float pct : { 1.0f, -1.0f })
    {
        Proc p;
        configure (p, 3, 1.0f, 60.0f, 100.0f);
        setParam (p, Proc::idOffset (3), pct);
        p.prepareToPlay (kSr, kBs);
        auto out = render (p, impulse (150000));
        const size_t expectPos = pct > 0 ? 144480 : 143520;
        size_t peak = 100000;
        for (size_t i = 100000; i < out.size(); ++i) if (std::abs (out[i]) > std::abs (out[peak])) peak = i;
        expect (peak == expectPos && std::abs (out[144000]) < 1.0e-7f,
                "Echo 3 offset " + num (pct, 1) + "% of 1 s lands at " + std::to_string (expectPos),
                "peak=" + std::to_string (peak));
        expect (std::abs (out[48000] - expectedDecay (48000.0, 60.0)) < 1.0e-5f
                    && std::abs (out[96000] - expectedDecay (96000.0, 60.0)) < 1.0e-5f,
                "Echoes 1 and 2 unaffected by echo 3 offset",
                "o1=" + num (out[48000]) + " o2=" + num (out[96000]));
    }

    // -------------------------------------------------------------------------
    std::cout << "\n[4d] Buffer sized from echo count, background resize\n";
    {
        Proc p;
        configure (p, 32, 0.25f, 60.0f, 100.0f);
        p.prepareToPlay (kSr, kBs);
        expect (p.getBufferLengthSamples() == Proc::computeBufferLength (32, kSr) && p.getBufferCapacityEchoes() == 32,
                "32 echoes: buffer length matches formula",
                std::to_string (p.getBufferLengthSamples()) + " smp, " + num ((double) p.getBufferBytes() / 1048576.0, 1) + " MB");

        juce::AudioBuffer<float> buf (2, kBs);
        juce::MidiBuffer midi;
        double phase = 0.0;
        bool finite = true;
        float peakAbs = 0.0f;

        auto runUntil = [&] (int target, bool feedImpulse) -> bool
        {
            bool first = feedImpulse;
            for (int iter = 0; iter < 4000; ++iter)
            {
                for (int i = 0; i < kBs; ++i)
                {
                    float s = 0.1f * (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / kSr;
                    if (first && i == 0) s = 1.0f;
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                first = false;
                gCountAllocs = true;
                p.processBlock (buf, midi);
                gCountAllocs = false;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < kBs; ++i)
                    {
                        const float v = buf.getSample (c, i);
                        if (! std::isfinite (v)) finite = false;
                        peakAbs = std::max (peakAbs, std::abs (v));
                    }
                if (p.getBufferCapacityEchoes() == target && ! p.isBufferResizePending())
                    return true;
                juce::Thread::sleep (2);
            }
            return false;
        };

        gAllocs = 0;
        setParam (p, "echoCount", 4.0f);
        const bool shrunk = runUntil (4, false);
        expect (shrunk && p.getBufferLengthSamples() == Proc::computeBufferLength (4, kSr),
                "32 -> 4 echoes: buffer shrinks to formula",
                std::to_string (p.getBufferLengthSamples()) + " smp, " + num ((double) p.getBufferBytes() / 1048576.0, 1) + " MB");

        setParam (p, "echoCount", 16.0f);
        const bool grown = runUntil (16, false);
        expect (grown && p.getBufferLengthSamples() == Proc::computeBufferLength (16, kSr),
                "4 -> 16 echoes: buffer grows to formula",
                std::to_string (p.getBufferLengthSamples()) + " smp, " + num ((double) p.getBufferBytes() / 1048576.0, 1) + " MB");

        expect (gAllocs.load() == 0, "No allocation inside processBlock during resizes", "allocs=" + std::to_string (gAllocs.load()));
        expect (finite && peakAbs < 4.0f, "Output finite and bounded during swaps", "peak=" + num (peakAbs, 4));

        for (int n : { 1, 4, 8, 16, 32 })
            std::cout << "        info: " << n << " echoes -> "
                      << num ((double) Proc::computeBufferLength (n, kSr) * 2.0 * 4.0 / 1048576.0, 1) << " MB stereo @48k\n";
    }
    {
        // History survives a resize: impulse, then grow 2 -> 6 while echoes are pending.
        Proc p;
        configure (p, 2, 0.5f, 60.0f, 100.0f);
        p.prepareToPlay (kSr, kBs);
        std::vector<float> in (8 * 48000, 0.0f);
        in[0] = 1.0f;
        std::vector<float> out (in.size());
        juce::AudioBuffer<float> buf (2, kBs);
        juce::MidiBuffer midi;
        bool requested = false;
        for (size_t start = 0; start < in.size(); start += kBs)
        {
            if (! requested && start >= 4800) { setParam (p, "echoCount", 6.0f); requested = true; }
            for (int i = 0; i < kBs; ++i) { buf.setSample (0, i, in[start + (size_t) i]); buf.setSample (1, i, in[start + (size_t) i]); }
            p.processBlock (buf, midi);
            for (int i = 0; i < kBs; ++i) out[start + (size_t) i] = buf.getSample (0, i);
            juce::Thread::sleep (1);
        }
        bool ok = true; std::string d;
        for (int n = 1; n <= 6; ++n)
        {
            const size_t k = (size_t) n * 24000;
            if (std::abs (out[k] - expectedDecay ((double) k, 60.0)) > 1.0e-4f) { ok = false; d += std::to_string (n) + "=" + num (out[k], 4) + " "; }
        }
        expect (p.getBufferCapacityEchoes() == 6, "Resize 2 -> 6 completed while playing");
        expect (ok, "Impulse history kept across resize: all 6 echoes present", d);
    }

    // -------------------------------------------------------------------------
    std::cout << "\n[5] Per-echo pitch shift\n";
    for (float semis : { 12.0f, -12.0f, 7.0f })
    {
        Proc p;
        configure (p, 1, 0.2f, 60.0f, 100.0f);
        setParam (p, Proc::idPitch (1), semis);
        p.prepareToPlay (kSr, kBs);
        std::vector<float> in (2 * 48000);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) i / kSr);
        auto out = render (p, in);
        const double f = measureFrequency (out, 24000, 96000);
        const double expectF = 1000.0 * std::pow (2.0, semis / 12.0);
        expect (std::abs (f - expectF) / expectF < 0.03, "Pitch " + num (semis, 0) + " st: 1 kHz -> " + num (expectF, 1) + " Hz",
                "measured " + num (f, 1) + " Hz");
    }

    // -------------------------------------------------------------------------
    std::cout << "\n[6] Dry / wet mix\n";
    {
        Proc p;
        configure (p, 4, 0.01f, 60.0f, 0.0f);
        p.prepareToPlay (kSr, kBs);
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> dist (-0.8f, 0.8f);
        std::vector<float> in (20000);
        for (auto& v : in) v = dist (rng);
        auto out = render (p, in);
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) if (out[i] != in[i]) { same = false; break; }
        expect (same, "Mix 0% output equals dry input exactly");
    }
    {
        Proc p;
        configure (p, 2, 0.01f, 60.0f, 100.0f);
        p.prepareToPlay (kSr, kBs);
        auto out = render (p, impulse (2000));
        expect (out[0] == 0.0f && out[480] > 0.9f, "Mix 100%: no dry, wet present", "o0=" + num (out[0], 9));
    }
    {
        Proc p;
        configure (p, 1, 0.01f, 60.0f, 50.0f);
        p.prepareToPlay (kSr, kBs);
        auto out = render (p, impulse (1000));
        const float c = std::cos (0.5f * juce::MathConstants<float>::halfPi);
        expect (std::abs (out[0] - c) < 1.0e-5f, "Mix 50%: equal-power dry gain 0.707", "o0=" + num (out[0], 5));
    }

    // -------------------------------------------------------------------------
    std::cout << "\n[7] Stability while parameters move\n";
    {
        Proc p;
        configure (p, 8, 0.05f, 4.0f, 70.0f);
        setParam (p, Proc::idPitch (2), 5.0f);
        setParam (p, Proc::idPitch (5), -7.0f);
        p.prepareToPlay (kSr, kBs);

        juce::AudioBuffer<float> buf (2, kBs);
        juce::MidiBuffer midi;
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        double phase = 0.0;
        float prev = 0.0f, maxJump = 0.0f, peak = 0.0f;
        bool finite = true;

        for (int blk = 0; blk < 500; ++blk)
        {
            if (blk % 10 == 0)
            {
                const int e = 1 + (int) (u (rng) * 7.99f);
                setParam (p, Proc::idLevel (e), -30.0f + 36.0f * u (rng));
                setParam (p, Proc::idOffset (e), -50.0f + 100.0f * u (rng));
                setParam (p, Proc::idOn (1 + (int) (u (rng) * 7.99f)), u (rng) > 0.3f ? 1.0f : 0.0f);
                setParam (p, "interval", 0.045f + 0.01f * u (rng));
                setParam (p, "mix", 100.0f * u (rng));
            }
            for (int i = 0; i < kBs; ++i)
            {
                const float s = 0.3f * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * 200.0 / kSr;
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < kBs; ++i)
            {
                const float v = buf.getSample (0, i);
                if (! std::isfinite (v) || std::fpclassify (v) == FP_SUBNORMAL) finite = false;
                maxJump = std::max (maxJump, std::abs (v - prev));
                peak = std::max (peak, std::abs (v));
                prev = v;
            }
        }
        expect (finite, "No NaN / inf / denormals");
        expect (maxJump < 0.25f, "No discontinuities while parameters change", "max step=" + num (maxJump, 4) + " peak=" + num (peak, 3));
    }

    std::cout << "\n=== Result: " << (gFails == 0 ? "ALL PASS" : "FAILURES") << " (" << gFails << " failed) ===\n\n";
    return gFails == 0 ? 0 : 1;
}
