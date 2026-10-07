/*
  Offline self-test for chouchouShaper.

  Verifies:
    1. Unity: ratio 1, Attack / Sustain 0 dB -> output equals the input delayed by
       the reported latency (every FFT size)
    2. Mode A: contour compression narrows the envelope range of an AM sine
       (time and spectral stage); ratio < 1 widens it
    3. Mode B: positive Attack raises the onset of decaying bursts relative to the
       tail, positive Sustain lowers it (time and spectral stage)
    4. Dry/Wet 0 on each stage -> exactly the delayed input
    5. Sidechain: a steady main signal follows the rhythm of sidechain bursts; SC on
       without a connected sidechain equals SC off
    6. Scope data: one column per 64 samples, unity gives 0 dB everywhere, Ratio-only /
       Attack-only move only their own part, spectral bins satisfy out = in + gain

  Snapshot: ShaperSelfTest --snapshot file.png  /  --snapshot-scope file.png
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{
    using Proc = ChouchouShaperAudioProcessor;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kSr = 48000.0;
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

    std::string fmt (double v, int decimals = 2) { return juce::String (v, decimals).toStdString(); }

    using Signal = std::vector<float>;

    struct Rig
    {
        std::unique_ptr<Proc> proc = std::make_unique<Proc>();
        bool withSc;
        int bs;

        explicit Rig (bool sidechainBus = true, int blockSize = 512) : withSc (sidechainBus), bs (blockSize)
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.inputBuses.add (withSc ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
            const bool ok = proc->setBusesLayout (layout);
            jassert (ok);
            juce::ignoreUnused (ok);
        }

        void set (const char* id, float value)
        {
            auto* p = proc->getAPVTS().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        }

        void prepare()
        {
            proc->applyFftSizeFromParameterLocked (false);
            proc->prepareToPlay (kSr, bs);
        }

        int latency() const { return proc->getLatencySamples(); }

        /** Mono main (copied to both channels) and optional mono sidechain; returns left output. */
        Signal run (const Signal& main, const Signal* sc = nullptr)
        {
            const int n = (int) main.size();
            const int numCh = withSc ? 4 : 2;
            Signal out ((size_t) n);
            juce::AudioBuffer<float> buf (numCh, bs);
            juce::MidiBuffer midi;

            for (int pos = 0; pos < n; pos += bs)
            {
                const int len = juce::jmin (bs, n - pos);
                buf.setSize (numCh, len, false, false, true);
                for (int c = 0; c < 2; ++c)
                    buf.copyFrom (c, 0, main.data() + pos, len);
                for (int c = 2; c < numCh; ++c)
                {
                    if (sc != nullptr) buf.copyFrom (c, 0, sc->data() + pos, len);
                    else               buf.clear (c, 0, len);
                }

                proc->processBlock (buf, midi);
                std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + len, out.begin() + pos);
            }
            return out;
        }
    };

    Signal delayed (const Signal& x, int d)
    {
        Signal y (x.size(), 0.0f);
        for (size_t i = (size_t) d; i < x.size(); ++i)
            y[i] = x[i - (size_t) d];
        return y;
    }

    double maxAbsDiff (const Signal& a, const Signal& b, size_t from = 0)
    {
        double m = 0.0;
        for (size_t i = from; i < a.size(); ++i)
            m = std::max (m, (double) std::abs (a[i] - b[i]));
        return m;
    }

    double rms (const Signal& x, size_t from, size_t len)
    {
        double s = 0.0;
        len = std::min (len, x.size() - from);
        for (size_t i = from; i < from + len; ++i)
            s += (double) x[i] * x[i];
        return std::sqrt (s / (double) std::max<size_t> (1, len)) + 1.0e-12;
    }

    double peak (const Signal& x, size_t from, size_t len)
    {
        double m = 0.0;
        for (size_t i = from; i < std::min (x.size(), from + len); ++i)
            m = std::max (m, (double) std::abs (x[i]));
        return m + 1.0e-12;
    }

    /** Max / min of 10 ms RMS windows in dB, from sample 'from' on. */
    double envelopeRangeDb (const Signal& x, size_t from)
    {
        const size_t w = (size_t) (0.01 * kSr);
        double lo = 1.0e9, hi = 0.0;
        for (size_t i = from; i + w <= x.size(); i += w)
        {
            const double r = rms (x, i, w);
            lo = std::min (lo, r);
            hi = std::max (hi, r);
        }
        return 20.0 * std::log10 (hi / lo);
    }

    Signal noiseAndSines (int n, unsigned seed = 1)
    {
        std::mt19937 rng (seed);
        std::normal_distribution<float> nd (0.0f, 0.1f);
        Signal x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = nd (rng) + 0.3f * (float) std::sin (2.0 * kPi * 440.0 * i / kSr)
                          + 0.2f * (float) std::sin (2.0 * kPi * 3150.0 * i / kSr);
        return x;
    }

    Signal amSine (int n)
    {
        Signal x ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double env = 0.25 * (1.0 + 0.8 * std::sin (2.0 * kPi * 3.0 * i / kSr));
            x[(size_t) i] = (float) (env * std::sin (2.0 * kPi * 1000.0 * i / kSr));
        }
        return x;
    }

    constexpr int kBurstPeriod = (int) (0.4 * kSr);

    Signal decayingBursts (int n)
    {
        Signal x ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (i % kBurstPeriod) / kSr;
            x[(size_t) i] = (float) (0.5 * std::exp (-t / 0.08) * std::sin (2.0 * kPi * 1000.0 * i / kSr));
        }
        return x;
    }

    /** Average onset-peak / tail-RMS ratio (dB) over bursts after the first second. */
    double onsetToTailDb (const Signal& y, int latency)
    {
        double sum = 0.0;
        int count = 0;
        for (int start = 3 * kBurstPeriod; start + kBurstPeriod < (int) y.size(); start += kBurstPeriod)
        {
            const size_t s = (size_t) (start + latency);
            const double on = peak (y, s, (size_t) (0.025 * kSr));
            const double tail = rms (y, s + (size_t) (0.15 * kSr), (size_t) (0.1 * kSr));
            sum += 20.0 * std::log10 (on / tail);
            ++count;
        }
        return sum / juce::jmax (1, count);
    }

    //==========================================================================
    void testUnity()
    {
        std::cout << "1. Unity\n";
        const Signal x = noiseAndSines ((int) (1.5 * kSr));

        for (int choice = 0; choice < 4; ++choice)
        {
            Rig rig;
            rig.set ("s_fft_size", (float) choice);
            rig.prepare();
            const Signal y = rig.run (x);
            const int lat = rig.latency();
            const double err = maxAbsDiff (y, delayed (x, lat));
            expect (lat == (512 << choice) && err < 1.0e-4,
                    "unity, FFT " + std::to_string (512 << choice),
                    "latency " + std::to_string (lat) + ", max err " + fmt (err, 7));
        }
    }

    void testModeA()
    {
        std::cout << "2. Mode A (contour compression)\n";
        const Signal x = amSine ((int) (4.0 * kSr));
        const size_t from = (size_t) (1.5 * kSr);
        const double inRange = envelopeRangeDb (x, from);

        auto runTime = [&] (float ratio)
        {
            Rig rig;
            rig.set ("s_on", 0.0f);
            rig.set ("t_ratio", ratio);
            rig.set ("t_fast_ms", 5.0f);
            rig.set ("t_slow_ms", 300.0f);
            rig.prepare();
            return envelopeRangeDb (rig.run (x), from);
        };

        auto runSpectral = [&] (float ratio)
        {
            Rig rig;
            rig.set ("t_on", 0.0f);
            rig.set ("s_ratio", ratio);
            rig.set ("s_fast_ms", 10.0f);
            rig.set ("s_slow_ms", 300.0f);
            rig.set ("s_fft_size", 1.0f);
            rig.prepare();
            return envelopeRangeDb (rig.run (x), from);
        };

        const double t4 = runTime (4.0f), t05 = runTime (0.5f);
        const double s4 = runSpectral (4.0f), s05 = runSpectral (0.5f);
        const std::string inStr = "input " + fmt (inRange, 1) + " dB";

        expect (t4 < inRange - 6.0, "time ratio 4 narrows range", inStr + ", out " + fmt (t4, 1));
        expect (t05 > inRange + 3.0, "time ratio 0.5 widens range", inStr + ", out " + fmt (t05, 1));
        expect (s4 < inRange - 6.0, "spectral ratio 4 narrows range", inStr + ", out " + fmt (s4, 1));
        expect (s05 > inRange + 3.0, "spectral ratio 0.5 widens range", inStr + ", out " + fmt (s05, 1));
    }

    void testModeB()
    {
        std::cout << "3. Mode B (transient shaping)\n";
        const Signal x = decayingBursts ((int) (4.0 * kSr));
        const double inRatio = onsetToTailDb (x, 0);

        auto runTime = [&] (float att, float sus)
        {
            Rig rig;
            rig.set ("s_on", 0.0f);
            rig.set ("t_attack_db", att);
            rig.set ("t_sustain_db", sus);
            rig.set ("t_fast_ms", 3.0f);
            rig.set ("t_slow_ms", 80.0f);
            rig.prepare();
            return onsetToTailDb (rig.run (x), rig.latency());
        };

        auto runSpectral = [&] (float att, float sus)
        {
            Rig rig;
            rig.set ("t_on", 0.0f);
            rig.set ("s_attack_db", att);
            rig.set ("s_sustain_db", sus);
            rig.set ("s_fast_ms", 5.0f);
            rig.set ("s_slow_ms", 120.0f);
            rig.set ("s_fft_size", 0.0f);
            rig.prepare();
            return onsetToTailDb (rig.run (x), rig.latency());
        };

        const std::string inStr = "input " + fmt (inRatio, 1) + " dB";
        const double ta = runTime (12.0f, 0.0f), ts = runTime (0.0f, 12.0f);
        const double sa = runSpectral (12.0f, 0.0f), ss = runSpectral (0.0f, 12.0f);

        expect (ta > inRatio + 3.0, "time Attack +12 lifts onset", inStr + ", out " + fmt (ta, 1));
        expect (ts < inRatio - 3.0, "time Sustain +12 lifts tail", inStr + ", out " + fmt (ts, 1));
        expect (sa > inRatio + 3.0, "spectral Attack +12 lifts onset", inStr + ", out " + fmt (sa, 1));
        expect (ss < inRatio - 3.0, "spectral Sustain +12 lifts tail", inStr + ", out " + fmt (ss, 1));
    }

    void testDryWet()
    {
        std::cout << "4. Dry/Wet 0\n";
        const Signal x = decayingBursts ((int) (2.0 * kSr));

        {
            Rig rig;
            rig.set ("t_ratio", 4.0f);
            rig.set ("t_attack_db", 12.0f);
            rig.set ("t_mix", 0.0f);
            rig.prepare();
            const Signal y = rig.run (x);
            const double err = maxAbsDiff (y, delayed (x, rig.latency()), (size_t) rig.latency());
            expect (err < 1.0e-4, "time Dry/Wet 0 = input", "max err " + fmt (err, 7));
        }
        {
            Rig rig;
            rig.set ("t_on", 0.0f);
            rig.set ("s_ratio", 4.0f);
            rig.set ("s_attack_db", 12.0f);
            rig.set ("s_mix", 0.0f);
            rig.prepare();
            const Signal y = rig.run (x);
            const double err = maxAbsDiff (y, delayed (x, rig.latency()));
            expect (err == 0.0, "spectral Dry/Wet 0 = delayed input (bit exact)", "max err " + fmt (err, 9));
        }
    }

    void testSidechain()
    {
        std::cout << "5. Sidechain\n";
        const int n = (int) (4.0 * kSr);
        const size_t from = (size_t) (1.5 * kSr);

        Signal main ((size_t) n), sc ((size_t) n);
        std::mt19937 rng (7);
        std::normal_distribution<float> nd (0.0f, 0.3f);
        const int period = (int) (0.25 * kSr), burst = (int) (0.03 * kSr);
        for (int i = 0; i < n; ++i)
        {
            main[(size_t) i] = 0.3f * (float) std::sin (2.0 * kPi * 1000.0 * i / kSr);
            sc[(size_t) i] = (i % period) < burst ? nd (rng) : 0.0f;
        }
        const double inRange = envelopeRangeDb (main, from);

        auto run = [&] (bool timeSc, bool specSc, bool connect, bool timeOn, bool specOn)
        {
            Rig rig (connect);
            rig.set ("t_on", timeOn ? 1.0f : 0.0f);
            rig.set ("s_on", specOn ? 1.0f : 0.0f);
            rig.set ("t_sc", timeSc ? 1.0f : 0.0f);
            rig.set ("s_sc", specSc ? 1.0f : 0.0f);
            rig.set ("t_attack_db", -12.0f);
            rig.set ("t_fast_ms", 3.0f);
            rig.set ("t_slow_ms", 120.0f);
            rig.set ("s_attack_db", -12.0f);
            rig.set ("s_fast_ms", 5.0f);
            rig.set ("s_slow_ms", 150.0f);
            rig.prepare();
            return rig.run (main, connect ? &sc : nullptr);
        };

        const double tRange = envelopeRangeDb (run (true, false, true, true, false), from);
        const double sRange = envelopeRangeDb (run (false, true, true, false, true), from);
        expect (tRange > inRange + 4.0, "time SC: main ducks with sidechain bursts",
                "input " + fmt (inRange, 1) + " dB, out " + fmt (tRange, 1));
        expect (sRange > inRange + 4.0, "spectral SC: main ducks with sidechain bursts",
                "input " + fmt (inRange, 1) + " dB, out " + fmt (sRange, 1));

        const Signal onNoBus  = run (true, true, false, true, true);
        const Signal offNoBus = run (false, false, false, true, true);
        expect (maxAbsDiff (onNoBus, offNoBus) == 0.0, "SC on without sidechain = SC off");
    }

    void testScope()
    {
        std::cout << "6. Scope data\n";

        auto collect = [] (Rig& rig)
        {
            std::vector<ScopeColumn> cols;
            rig.proc->getScopeFifo().drain ([&] (const ScopeColumn& c) { cols.push_back (c); });
            return cols;
        };

        auto maxAbs = [] (const std::vector<ScopeColumn>& cols, float ScopeColumn::* field)
        {
            float m = 0.0f;
            for (const auto& c : cols)
                m = std::max (m, std::abs (c.*field));
            return m;
        };

        const int n = (int) kSr;
        {
            Rig rig;
            rig.prepare();
            rig.run (noiseAndSines (n));
            const auto cols = collect (rig);

            float worst = 0.0f;
            for (auto f : { &ScopeColumn::tRatio, &ScopeColumn::tAttack, &ScopeColumn::tSustain, &ScopeColumn::tTotal,
                            &ScopeColumn::sRatio, &ScopeColumn::sAttack, &ScopeColumn::sSustain, &ScopeColumn::sTotal })
                worst = std::max (worst, maxAbs (cols, f));

            expect ((int) cols.size() == n / kScopeColumnSamples, "column count",
                    std::to_string (cols.size()) + " columns for " + std::to_string (n) + " samples");
            expect (worst < 1.0e-6f, "unity: every gain column is 0 dB", "max " + fmt (worst, 7));
        }
        {
            Rig rig;
            rig.set ("s_on", 0.0f);
            rig.set ("t_ratio", 4.0f);
            rig.prepare();
            rig.run (amSine (2 * n));
            const auto cols = collect (rig);
            const float r = maxAbs (cols, &ScopeColumn::tRatio);
            const float other = std::max ({ maxAbs (cols, &ScopeColumn::tAttack), maxAbs (cols, &ScopeColumn::tSustain),
                                            maxAbs (cols, &ScopeColumn::sTotal) });
            expect (r > 1.0f && other == 0.0f, "Ratio only: only the ratio part moves",
                    "ratio " + fmt (r) + " dB, others " + fmt (other, 4));
        }
        {
            Rig rig;
            rig.set ("s_on", 0.0f);
            rig.set ("t_attack_db", 12.0f);
            rig.prepare();
            rig.run (decayingBursts (2 * n));
            const auto cols = collect (rig);
            const float a = maxAbs (cols, &ScopeColumn::tAttack);
            const float other = std::max (maxAbs (cols, &ScopeColumn::tRatio), maxAbs (cols, &ScopeColumn::tSustain));            expect (a > 1.0f && other == 0.0f, "Attack only: only the attack part moves",
                    "attack " + fmt (a) + " dB, others " + fmt (other, 4));
        }
        {
            Rig rig;
            rig.set ("t_on", 0.0f);
            rig.set ("s_ratio", 4.0f);
            rig.set ("s_fast_ms", 10.0f);
            rig.prepare();
            rig.run (amSine (2 * n));
            const auto cols = collect (rig);

            const auto& stage = rig.proc->getSpectralStage();
            double worst = 0.0;
            int checked = 0;
            for (int b = 0; b < stage.numBins(); ++b)
            {
                const float in = stage.getMeterInDb (b);
                if (in < -60.0f)
                    continue;
                worst = std::max (worst, (double) std::abs (stage.getMeterOutDb (b) - (in + stage.getMeterGainDb (b))));
                ++checked;
            }
            expect (checked > 0 && worst < 0.01, "spectral bins: out = in + gain",
                    std::to_string (checked) + " bins, max err " + fmt (worst, 4) + " dB");
            expect (maxAbs (cols, &ScopeColumn::sRatio) > 1.0f, "spectral ratio shows in the scope columns",
                    "max " + fmt (maxAbs (cols, &ScopeColumn::sRatio)) + " dB");
        }
    }

    //==========================================================================
    int snapshot (const juce::String& path, bool withScope)
    {
        Rig rig;
        rig.set ("t_ratio", 2.0f);
        rig.set ("t_attack_db", 6.0f);
        rig.set ("s_ratio", 3.0f);
        rig.set ("s_attack_db", 8.0f);
        rig.set ("s_sustain_db", -4.0f);
        rig.set ("s_lo_hz", 80.0f);
        rig.set ("s_hi_hz", 12000.0f);
        if (withScope)
        {
            rig.set ("t_sc", 1.0f);
            rig.set ("t_attack_db", -8.0f);
        }
        rig.prepare();
        rig.proc->setScopeOpen (withScope);

        std::unique_ptr<juce::AudioProcessorEditor> editor (rig.proc->createEditor());
        auto* ed = dynamic_cast<ChouchouShaperAudioProcessorEditor*> (editor.get());
        const Signal x = decayingBursts ((int) (3.0 * kSr));
        const Signal xn = noiseAndSines ((int) (3.0 * kSr));
        Signal mixSig (x.size()), scSig (x.size(), 0.0f);
        for (size_t i = 0; i < x.size(); ++i)
        {
            mixSig[i] = x[i] + 0.3f * xn[i];
            const int ph = (int) (i % (size_t) (0.5 * kSr));
            if (ph < (int) (0.06 * kSr))
                scSig[i] = 0.8f * (float) (std::exp (-ph / (0.02 * kSr)) * std::sin (2.0 * kPi * 55.0 * ph / kSr));
        }

        // Feed audio in slices and tick the editor timer so the trace fills.
        const int slice = (int) (kSr / 30.0);
        for (int pos = 0; pos + slice <= (int) mixSig.size(); pos += slice)
        {
            Signal part (mixSig.begin() + pos, mixSig.begin() + pos + slice);
            Signal scPart (scSig.begin() + pos, scSig.begin() + pos + slice);
            rig.run (part, &scPart);
            if (ed != nullptr)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
        }

        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
        file.deleteFile();
        juce::FileOutputStream out (file);
        const bool ok = out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
        std::cout << (ok ? "snapshot written: " : "snapshot FAILED: ") << file.getFullPathName() << "\n";
        return ok ? 0 : 1;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc == 3 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2], false);
    if (argc == 3 && juce::String (argv[1]) == "--snapshot-scope")
        return snapshot (argv[2], true);

    testUnity();
    testModeA();
    testModeB();
    testDryWet();
    testSidechain();
    testScope();

    std::cout << (gFails == 0 ? "\nALL PASS\n" : "\nFAILURES: " + std::to_string (gFails) + "\n");
    return gFails == 0 ? 0 : 1;
}
