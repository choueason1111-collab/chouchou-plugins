/*
  Offline self-test for chouchouEQtimbre.

  Verifies:
    - Generate purity on a single sine: Even-only adds no odd harmonics, Odd-only adds
      no even harmonics (H1 is expected in the odd branch), across sample rates and
      oversampling factors
    - drive = 0 / below the linear threshold: finite and identical to dry
    - Mix 0%: output is exactly the input delayed by the reported latency
    - Rebalance on a band-limited saw: +100% removes odd, -100% removes even,
      fundamental protect keeps H1
    - MIDI pitch source: chord priority, hold, idle
    - Auto pitch source: locks on a saw, rejects noise and silence
    - Parameter automation and mode switching: finite, no large jumps
    - Random stage: bypass / Mix 0% bit-exact, runs in series, triggers, re-roll clicks,
      state round trip, Adopter
    - Display predictions (comb arrows, Generate boxes, shaper inset) match measurements

  Snapshots: --snapshot file.png (plain), --snapshot-random file.png (random stage on),
             --snapshot-page file.png (random page); an optional last argument is the focused
             knob (engine parameter index, 20 Entrée, 21 Sortie, 22 Mix)
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TimbreModel.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{
    using Proc = ChouchouEQtimbreAudioProcessor;
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

    std::string fmt (double v, int decimals = 1)
    {
        return (decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals)).toStdString();
    }

    // Blackman-Harris windowed single-bin DFT, amplitude in dBFS.
    double levelDb (const std::vector<float>& s, double sr, double hz)
    {
        const size_t n = s.size();
        double re = 0.0, im = 0.0, wsum = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double t = 2.0 * kPi * (double) i / (double) (n - 1);
            const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
            const double ph = 2.0 * kPi * hz * (double) i / sr;
            re += w * s[i] * std::cos (ph);
            im -= w * s[i] * std::sin (ph);
            wsum += w;
        }
        return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / wsum + 1.0e-20);
    }

    struct Rig
    {
        std::unique_ptr<Proc> proc = std::make_unique<Proc>();
        double sr;
        int bs;

        explicit Rig (double sampleRate, int blockSize = 512) : sr (sampleRate), bs (blockSize) {}

        void set (const char* id, float value)
        {
            auto* p = proc->getAPVTS().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        }

        void prepare() { proc->setRateAndBufferSizeDetails (sr, bs); proc->prepareToPlay (sr, bs); }

        // Runs numSamples of a mono generator through both channels, returns left output.
        std::vector<float> run (const std::function<float (long)>& gen, long numSamples,
                                const std::function<void (Rig&, long, juce::MidiBuffer&)>& perBlock = {},
                                std::vector<float>* inputCopy = nullptr)
        {
            std::vector<float> out;
            out.reserve ((size_t) numSamples);
            juce::AudioBuffer<float> buffer (2, bs);
            juce::MidiBuffer midi;

            for (long pos = 0; pos < numSamples; pos += bs)
            {
                const int n = (int) std::min<long> (bs, numSamples - pos);
                buffer.setSize (2, n, false, false, true);
                midi.clear();
                if (perBlock)
                    perBlock (*this, pos, midi);

                for (int i = 0; i < n; ++i)
                {
                    const float v = gen (sample + i);
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, rightGen ? rightGen (sample + i) : v);
                    if (inputCopy != nullptr)
                        inputCopy->push_back (v);
                }
                proc->processBlock (buffer, midi);
                for (int i = 0; i < n; ++i)
                    out.push_back (buffer.getSample (0, i));
                sample += n;
            }
            return out;
        }

        long sample = 0;
        std::function<float (long)> rightGen;   // empty: right channel copies left
    };

    std::function<float (long)> sine (double sr, double hz, double amp)
    {
        return [=] (long n) { return (float) (amp * std::sin (2.0 * kPi * hz * (double) n / sr)); };
    }

    std::function<float (long)> saw (double sr, double hz, double amp)
    {
        return [=] (long n)
        {
            double v = 0.0;
            for (int k = 1; k * hz < sr * 0.45; ++k)
                v += std::sin (2.0 * kPi * k * hz * (double) n / sr) / k;
            return (float) (amp * (2.0 / kPi) * v);
        };
    }

    std::vector<float> tail (const std::vector<float>& v, size_t n)
    {
        return std::vector<float> (v.end() - (long) n, v.end());
    }

    void configureGenerate (Rig& rig, float even, float odd, int osIndex)
    {
        rig.set (ParamIDs::mode, 0.0f);
        rig.set (ParamIDs::oversampling, (float) osIndex);
        rig.set (ParamIDs::drive, 3.0f);
        rig.set (ParamIDs::bias, 0.3f);
        rig.set (ParamIDs::even, even);
        rig.set (ParamIDs::odd, odd);
        rig.set (ParamIDs::tone, 20000.0f);
        rig.set (ParamIDs::mix, 100.0f);
        rig.set (ParamIDs::genLevel, 100.0f);
    }

    //==============================================================================
    void testGeneratePurity()
    {
        std::cout << "\nGenerate purity (220 Hz sine, drive 3, bias 0.3)\n";
        constexpr double kLimitDb = -90.0;

        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            for (int os : { 0, 1, 2 })
            {
                const std::string tag = fmt (sr / 1000.0, 1) + "k " + std::to_string (2 << os) + "x";

                {
                    Rig rig (sr);
                    configureGenerate (rig, 100.0f, 0.0f, os);
                    rig.prepare();
                    const auto out = tail (rig.run (sine (sr, 220.0, 0.5), (long) (sr * 1.5)), (size_t) sr);
                    const double h1 = levelDb (out, sr, 220.0);
                    double worstOdd = -300.0;
                    for (int k : { 3, 5, 7, 9 })
                        worstOdd = std::max (worstOdd, levelDb (out, sr, 220.0 * k) - h1);
                    const double h2 = levelDb (out, sr, 440.0) - h1;
                    expect (h2 > -60.0 && worstOdd < kLimitDb, "Even only " + tag,
                            "H2 " + fmt (h2) + " dB, worst odd " + fmt (worstOdd) + " dB");
                }
                {
                    Rig rig (sr);
                    configureGenerate (rig, 0.0f, 100.0f, os);
                    rig.prepare();
                    const auto out = tail (rig.run (sine (sr, 220.0, 0.5), (long) (sr * 1.5)), (size_t) sr);
                    const double h1 = levelDb (out, sr, 220.0);
                    double worstEven = -300.0;
                    for (int k : { 2, 4, 6, 8 })
                        worstEven = std::max (worstEven, levelDb (out, sr, 220.0 * k) - h1);
                    const double h3 = levelDb (out, sr, 660.0) - h1;
                    expect (h1 > -12.0 && h3 > -60.0 && worstEven < kLimitDb, "Odd only " + tag,
                            "H1 " + fmt (h1) + " dBFS, H3 " + fmt (h3) + " dB, worst even " + fmt (worstEven) + " dB");
                }
            }
        }
    }

    void testDriveZeroAndMixZero()
    {
        std::cout << "\nLinear fallback and Mix 0%\n";
        constexpr double sr = 48000.0;

        for (float drive : { 0.0f, 1.0e-5f, 2.0e-4f })
        {
            Rig rig (sr);
            configureGenerate (rig, 100.0f, 100.0f, 1);
            rig.set (ParamIDs::drive, drive);
            rig.prepare();
            std::vector<float> in;
            const auto out = rig.run (sine (sr, 220.0, 0.8), 48000, {}, &in);
            const int lat = rig.proc->getLatencySamples();
            double maxErr = 0.0;
            bool finite = true;
            for (size_t i = (size_t) lat + 2000; i < out.size(); ++i)
            {
                finite = finite && std::isfinite (out[i]);
                maxErr = std::max (maxErr, (double) std::abs (out[i] - in[i - (size_t) lat]));
            }
            const double limit = drive < 1.0e-4f ? 1.0e-5 : 1.0e-3;
            expect (finite && maxErr < limit, "drive " + juce::String (drive).toStdString() + " is clean",
                    "max |out - dry| " + juce::String (maxErr, 8).toStdString());
        }

        for (int os : { 0, 1, 2 })
        {
            Rig rig (sr);
            rig.set (ParamIDs::mode, 2.0f);
            rig.set (ParamIDs::oversampling, (float) os);
            rig.set (ParamIDs::drive, 8.0f);
            rig.set (ParamIDs::even, 100.0f);
            rig.set (ParamIDs::odd, 100.0f);
            rig.set (ParamIDs::balance, 100.0f);
            rig.set (ParamIDs::mix, 0.0f);
            rig.prepare();
            std::vector<float> in;
            const auto out = rig.run (saw (sr, 220.0, 0.5), 24000, {}, &in);
            const int lat = rig.proc->getLatencySamples();
            double maxErr = 0.0;
            for (size_t i = (size_t) lat; i < out.size(); ++i)
                maxErr = std::max (maxErr, (double) std::abs (out[i] - in[i - (size_t) lat]));
            expect (lat > 0 && maxErr == 0.0, "Mix 0% equals delayed input, " + std::to_string (2 << os) + "x",
                    "latency " + std::to_string (lat) + ", max err " + juce::String (maxErr, 9).toStdString());
        }
    }

    void testRebalance()
    {
        std::cout << "\nRebalance (band-limited 220 Hz saw, Manual f0)\n";
        constexpr double sr = 48000.0;

        auto measure = [&] (float balance, bool protect, std::vector<double>& inDb, std::vector<double>& outDb)
        {
            Rig rig (sr);
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 0.0f);
            rig.set (ParamIDs::manualHz, 220.0f);
            rig.set (ParamIDs::balance, balance);
            rig.set (ParamIDs::protect, protect ? 1.0f : 0.0f);
            rig.set (ParamIDs::mix, 100.0f);
            rig.set (ParamIDs::rebLevel, 100.0f);
            rig.prepare();
            std::vector<float> in;
            const auto out = tail (rig.run (saw (sr, 220.0, 0.5), (long) sr * 2, {}, &in), (size_t) sr);
            const auto inTail = tail (in, (size_t) sr);
            inDb.clear(); outDb.clear();
            for (int k = 1; k <= 9; ++k)
            {
                inDb.push_back (levelDb (inTail, sr, 220.0 * k));
                outDb.push_back (levelDb (out, sr, 220.0 * k));
            }
        };

        std::vector<double> in, out;

        measure (100.0f, false, in, out);
        {
            double minOddCut = 1e9, maxEvenDev = 0.0;
            for (int k = 1; k <= 9; ++k)
            {
                const double change = out[(size_t) k - 1] - in[(size_t) k - 1];
                if (k % 2 == 1) minOddCut = std::min (minOddCut, -change);
                else            maxEvenDev = std::max (maxEvenDev, std::abs (change));
            }
            expect (minOddCut > 30.0 && maxEvenDev < 0.5, "Balance +100%: odd removed, even kept",
                    "min odd cut " + fmt (minOddCut) + " dB, max even change " + fmt (maxEvenDev, 2) + " dB");
        }

        measure (-100.0f, false, in, out);
        {
            double minEvenCut = 1e9, maxOddDev = 0.0;
            for (int k = 1; k <= 9; ++k)
            {
                const double change = out[(size_t) k - 1] - in[(size_t) k - 1];
                if (k % 2 == 0) minEvenCut = std::min (minEvenCut, -change);
                else            maxOddDev = std::max (maxOddDev, std::abs (change));
            }
            expect (minEvenCut > 30.0 && maxOddDev < 0.5, "Balance -100%: even removed, odd kept",
                    "min even cut " + fmt (minEvenCut) + " dB, max odd change " + fmt (maxOddDev, 2) + " dB");
        }

        measure (100.0f, true, in, out);
        {
            const double h1Change = out[0] - in[0];
            double minOddCut = 1e9;
            for (int k : { 3, 5, 7, 9 })
                minOddCut = std::min (minOddCut, in[(size_t) k - 1] - out[(size_t) k - 1]);
            expect (std::abs (h1Change) < 0.5 && minOddCut > 20.0, "Fundamental protect keeps H1",
                    "H1 change " + fmt (h1Change, 2) + " dB, min H3-H9 cut " + fmt (minOddCut) + " dB");
        }
    }

    void testMidi()
    {
        std::cout << "\nMIDI pitch source\n";
        constexpr double sr = 48000.0;
        Rig rig (sr);
        rig.set (ParamIDs::mode, 1.0f);
        rig.set (ParamIDs::pitchSource, 1.0f);
        rig.set (ParamIDs::midiPriority, 0.0f);
        rig.set (ParamIDs::midiHold, 1.0f);
        rig.prepare();

        auto silence = [] (long) { return 0.0f; };
        auto step = [&] (std::function<void (juce::MidiBuffer&)> events)
        {
            rig.run (silence, rig.bs, [&] (Rig&, long, juce::MidiBuffer& m) { if (events) events (m); });
        };
        auto near = [] (float a, float b) { return std::abs (a - b) < 0.05f; };

        step ({});
        expect (! rig.proc->isPitchActive(), "no note yet: inactive");

        step ([] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (1, 57, 0.8f), 0); });
        expect (rig.proc->isPitchActive() && near (rig.proc->getCurrentF0(), 220.0f), "A3 -> 220 Hz",
                fmt (rig.proc->getCurrentF0(), 2));

        step ([] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0); });
        expect (near (rig.proc->getCurrentF0(), 329.63f), "chord, Last -> E4", fmt (rig.proc->getCurrentF0(), 2));

        rig.set (ParamIDs::midiPriority, 2.0f);
        step ({});
        expect (near (rig.proc->getCurrentF0(), 220.0f), "chord, Lowest -> A3", fmt (rig.proc->getCurrentF0(), 2));

        rig.set (ParamIDs::midiPriority, 1.0f);
        step ({});
        expect (near (rig.proc->getCurrentF0(), 329.63f), "chord, Highest -> E4", fmt (rig.proc->getCurrentF0(), 2));

        step ([] (juce::MidiBuffer& m)
        {
            m.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
            m.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
        });
        expect (rig.proc->isPitchActive() && rig.proc->getPitchStatus() == chouchou::PitchSource::Status::midiHeld,
                "released with Hold: keeps last pitch");

        rig.set (ParamIDs::midiHold, 0.0f);
        step ({});
        expect (! rig.proc->isPitchActive(), "released without Hold: inactive");
    }

    void testAuto()
    {
        std::cout << "\nAuto pitch source (YIN)\n";
        constexpr double sr = 48000.0;

        for (double hz : { 110.0, 220.0, 440.0 })
        {
            Rig rig (sr);
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 2.0f);
            rig.prepare();
            rig.run (saw (sr, hz, 0.5), (long) (sr * 0.5));
            const float f0 = rig.proc->getCurrentF0();
            expect (rig.proc->isPitchActive() && std::abs (f0 - hz) / hz < 0.01, "saw " + fmt (hz, 0) + " Hz",
                    "detected " + fmt (f0, 2) + " Hz, confidence " + fmt (rig.proc->getAutoConfidence() * 100.0, 0) + "%");
        }

        {
            Rig rig (sr);
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 2.0f);
            rig.prepare();
            std::mt19937 rng (7);
            std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
            rig.run ([&] (long) { return dist (rng); }, (long) (sr * 0.5));
            expect (! rig.proc->isPitchActive(), "white noise: not locked",
                    "confidence " + fmt (rig.proc->getAutoConfidence() * 100.0, 0) + "%");
        }

        {
            Rig rig (sr);
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 2.0f);
            rig.prepare();
            rig.run ([] (long) { return 0.0f; }, (long) (sr * 0.5));
            expect (! rig.proc->isPitchActive(), "silence: not locked");
        }
    }

    void testAutomation()
    {
        std::cout << "\nAutomation and mode switching\n";
        constexpr double sr = 48000.0;

        auto maxStep = [] (const std::vector<float>& v, size_t from)
        {
            double m = 0.0;
            for (size_t i = from + 1; i < v.size(); ++i)
                m = std::max (m, (double) std::abs (v[i] - v[i - 1]));
            return m;
        };

        Rig still (sr);
        still.set (ParamIDs::mode, 2.0f);
        still.set (ParamIDs::drive, 4.0f);
        still.set (ParamIDs::even, 100.0f);
        still.set (ParamIDs::odd, 100.0f);
        still.set (ParamIDs::balance, 100.0f);
        still.prepare();
        const double baseline = maxStep (still.run (sine (sr, 220.0, 0.5), (long) sr), 4800);

        Rig rig (sr);
        rig.set (ParamIDs::mode, 2.0f);
        rig.prepare();
        const auto out = rig.run (sine (sr, 220.0, 0.5), (long) sr * 3, [] (Rig& r, long pos, juce::MidiBuffer&)
        {
            const double t = (double) pos / r.sr;
            r.set (ParamIDs::drive, (float) (4.0 + 4.0 * std::sin (2.0 * kPi * 0.7 * t)));
            r.set (ParamIDs::bias, (float) (0.6 * std::sin (2.0 * kPi * 0.3 * t)));
            r.set (ParamIDs::even, (float) (50.0 + 50.0 * std::sin (2.0 * kPi * 1.1 * t)));
            r.set (ParamIDs::odd, (float) (50.0 + 50.0 * std::cos (2.0 * kPi * 0.9 * t)));
            r.set (ParamIDs::balance, (float) (100.0 * std::sin (2.0 * kPi * 0.5 * t)));
            r.set (ParamIDs::mix, (float) (50.0 + 50.0 * std::sin (2.0 * kPi * 0.4 * t)));
            r.set (ParamIDs::mode, (float) (((long) (t * 4.0)) % 3));
            r.set (ParamIDs::protect, ((long) (t * 3.0)) % 2 == 0 ? 1.0f : 0.0f);
            r.set (ParamIDs::autoGain, ((long) (t * 2.0)) % 2 == 0 ? 1.0f : 0.0f);
        });

        bool finite = true;
        double peak = 0.0;
        for (auto v : out)
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, (double) std::abs (v));
        }
        const double worst = maxStep (out, 4800);
        expect (finite && peak < 4.0, "finite and bounded", "peak " + fmt (peak, 3));
        expect (worst < baseline * 1.5 + 0.02, "no jumps beyond static worst case",
                "max step " + fmt (worst, 4) + ", static baseline " + fmt (baseline, 4));
    }
}

namespace
{
    double maxStepFrom (const std::vector<float>& v, size_t from)
    {
        double m = 0.0;
        for (size_t i = from + 1; i < v.size(); ++i)
            m = std::max (m, (double) std::abs (v[i] - v[i - 1]));
        return m;
    }

    void testMidiTiming()
    {
        std::cout << "\nMIDI timing and note state\n";
        constexpr double sr = 48000.0;
        constexpr int kEventPos = 300;

        {
            Rig rig (sr);
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 1.0f);
            rig.set (ParamIDs::midiHold, 0.0f);
            rig.set (ParamIDs::balance, 100.0f);
            rig.set (ParamIDs::protect, 0.0f);
            rig.prepare();
            std::vector<float> in;
            const long noteBlock = 4L * rig.bs;
            const auto out = rig.run (sine (sr, 220.0, 0.5), noteBlock + rig.bs,
                                      [&] (Rig&, long pos, juce::MidiBuffer& m)
                                      {
                                          if (pos == noteBlock)
                                              m.addEvent (juce::MidiMessage::noteOn (1, 57, 0.8f), kEventPos);
                                      }, &in);
            const int lat = rig.proc->getLatencySamples();
            double before = 0.0, after = 0.0;
            for (long i = noteBlock; i < noteBlock + rig.bs; ++i)
            {
                const double err = std::abs (out[(size_t) i] - in[(size_t) (i - lat)]);
                double& worst = i < noteBlock + kEventPos ? before : after;
                worst = std::max (worst, err);
            }
            expect (before == 0.0 && after > 1.0e-4, "note-on at sample 300 starts at sample 300",
                    "max change before " + juce::String (before, 9).toStdString() + ", after " + fmt (after, 4));
        }

        auto makeMidiRig = [&] (Rig& rig, bool hold)
        {
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 1.0f);
            rig.set (ParamIDs::midiHold, hold ? 1.0f : 0.0f);
            rig.prepare();
        };
        auto step = [] (Rig& rig, std::function<void (juce::MidiBuffer&)> events)
        {
            rig.run ([] (long) { return 0.0f; }, rig.bs,
                     [&] (Rig&, long, juce::MidiBuffer& m) { if (events) events (m); });
        };

        {
            Rig rig (sr);
            makeMidiRig (rig, false);
            step (rig, [] (juce::MidiBuffer& m)
            {
                m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
                m.addEvent (juce::MidiMessage::noteOn (2, 60, 0.8f), 1);
                m.addEvent (juce::MidiMessage::noteOff (1, 60), 2);
            });
            const bool stillOn = rig.proc->isPitchActive();
            step (rig, [] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOff (2, 60), 0); });
            expect (stillOn && ! rig.proc->isPitchActive(), "same note on two channels: note-off is per channel");

            step (rig, [] (juce::MidiBuffer& m)
            {
                m.addEvent (juce::MidiMessage::noteOn (3, 64, 0.8f), 0);
                m.addEvent (juce::MidiMessage::allNotesOff (4), 1);
            });
            const bool survives = rig.proc->isPitchActive();
            step (rig, [] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::allNotesOff (3), 0); });
            expect (survives && ! rig.proc->isPitchActive(), "All Notes Off only affects its channel");
        }

        {
            Rig rig (sr);
            makeMidiRig (rig, false);
            step (rig, [] (juce::MidiBuffer& m)
            {
                m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
                m.addEvent (juce::MidiMessage::noteOn (1, 62, 0.8f), 1);
                m.addEvent (juce::MidiMessage::noteOff (1, 62), 2);
            });
            const bool sustained = rig.proc->isPitchActive();
            step (rig, [] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0); });
            expect (sustained && ! rig.proc->isPitchActive(), "CC64 sustains released notes until pedal up");
        }

        {
            Rig rig (sr);
            makeMidiRig (rig, false);
            rig.set (ParamIDs::midiPriority, 1.0f);
            step (rig, [] (juce::MidiBuffer& m)
            {
                for (int note = 40; note <= 56; ++note)
                    m.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), note - 40);
            });
            const float expected = chouchou::PitchSource::noteToHz (56);
            expect (std::abs (rig.proc->getCurrentF0() - expected) < 0.05f, "17th held note is tracked",
                    fmt (rig.proc->getCurrentF0(), 2) + " Hz");
        }

        {
            Rig rig (sr);
            makeMidiRig (rig, true);
            step (rig, [] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (1, 57, 0.8f), 0); });
            step (rig, [] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOff (1, 57), 0); });
            const bool held = rig.proc->isPitchActive();
            rig.prepare();
            step (rig, {});
            expect (held && ! rig.proc->isPitchActive(), "prepareToPlay clears the held MIDI pitch");
        }
    }

    void testAutoStates()
    {
        std::cout << "\nAuto state handling\n";
        constexpr double sr = 48000.0;

        auto autoRig = [&] (Rig& rig, float confidence)
        {
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 2.0f);
            rig.set (ParamIDs::confidence, confidence);
            rig.prepare();
        };

        {
            Rig rig (sr);
            autoRig (rig, 70.0f);
            rig.run (saw (sr, 220.0, 0.5), 2048);
            const bool earlyLock = rig.proc->isPitchActive();
            rig.run (saw (sr, 220.0, 0.5), (long) (sr * 0.3));
            expect (! earlyLock && rig.proc->isPitchActive(), "no lock before the analysis window is full");
        }

        {
            Rig rig (sr);
            autoRig (rig, 70.0f);
            rig.run (saw (sr, 220.0, 0.5), (long) (sr * 0.5));
            const bool locked220 = rig.proc->isPitchActive();
            rig.set (ParamIDs::pitchSource, 0.0f);
            rig.run (saw (sr, 440.0, 0.5), (long) (sr * 0.3));
            rig.set (ParamIDs::pitchSource, 2.0f);
            bool stale = false;
            for (int b = 0; b < 40; ++b)
            {
                rig.run (saw (sr, 440.0, 0.5), rig.bs);
                stale = stale || (rig.proc->isPitchActive() && std::abs (rig.proc->getCurrentF0() - 220.0f) < 10.0f);
            }
            const float f0 = rig.proc->getCurrentF0();
            expect (locked220 && ! stale && rig.proc->isPitchActive() && std::abs (f0 - 440.0f) < 5.0f,
                    "re-entering Auto never reuses the old 220 Hz", "now " + fmt (f0, 2) + " Hz");
        }

        {
            Rig rig (sr);
            autoRig (rig, 0.0f);
            rig.run (saw (sr, 220.0, 0.5), (long) (sr * 0.5));
            const bool locked = rig.proc->isPitchActive();
            rig.run ([] (long) { return 0.0f; }, (long) (sr * 0.3));
            expect (locked && ! rig.proc->isPitchActive(), "confidence 0%: silence releases the lock");
        }

        {
            Rig rig (sr);
            autoRig (rig, 70.0f);
            const auto left = saw (sr, 220.0, 0.5);
            rig.rightGen = [left] (long n) { return -left (n); };
            rig.run (left, (long) (sr * 0.5));
            const float f0 = rig.proc->getCurrentF0();
            expect (rig.proc->isPitchActive() && std::abs (f0 - 220.0f) < 2.2f, "anti-phase stereo still locks",
                    fmt (f0, 2) + " Hz");
        }
    }

    void testRebalancerGlide()
    {
        std::cout << "\nRebalance pitch jumps and glide\n";
        constexpr double sr = 48000.0;
        constexpr int kStep = 16;
        constexpr double kFadeMs = 15.0;

        {
            chouchou::HarmonicRebalancer reb;
            reb.prepare (sr, 1, 1.0f, false);
            reb.setJumpFade (kFadeMs);
            juce::AudioBuffer<float> x (1, kStep), d (1, kStep);
            x.clear();
            for (int i = 0; i < (int) (sr * 0.3) / kStep; ++i)
                reb.process (x, d, kStep, 220.0f, true, 1.0f, false);

            const double oldDelay = sr / 440.0, newDelay = sr / 880.0;
            auto onEither = [&] (double v)
            {
                return std::abs (v / oldDelay - 1.0) < 0.005 || std::abs (v / newDelay - 1.0) < 0.005;
            };

            const int startVoice = reb.getActiveVoice();
            bool clean = true;
            double doneMs = 1.0e9;
            for (int n = 0; n < (int) (sr * 0.1); n += kStep)
            {
                reb.process (x, d, kStep, 440.0f, true, 1.0f, false);
                if (reb.getCrossfade() < 1.0)
                    clean = clean && onEither (reb.getVoiceDelay (0)) && onEither (reb.getVoiceDelay (1));
                else if (doneMs > 1.0e8)
                    doneMs = 1000.0 * (n + kStep) / sr;
            }
            expect (clean && reb.getActiveVoice() != startVoice,
                    "220 -> 440 Hz: both combs stay on the old or new pitch while crossfading");
            expect (doneMs <= kFadeMs + 1.0, "crossfade finishes within Jump Fade + 1 ms", fmt (doneMs, 2) + " ms");
        }

        {
            Rig rig (sr);
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 1.0f);
            rig.set (ParamIDs::balance, 100.0f);
            rig.set (ParamIDs::protect, 0.0f);
            rig.set (ParamIDs::jumpFade, (float) kFadeMs);
            rig.prepare();

            const long switchAt = (long) (sr * 0.5);
            const auto s220 = saw (sr, 220.0, 0.5), s440 = saw (sr, 440.0, 0.5);
            std::vector<float> in;
            const auto out = rig.run ([&] (long n) { return n < switchAt ? s220 (n) : s440 (n); },
                                      switchAt + (long) (sr * 0.2),
                                      [&] (Rig& r, long pos, juce::MidiBuffer& m)
                                      {
                                          if (pos == 0)
                                              m.addEvent (juce::MidiMessage::noteOn (1, 57, 0.8f), 0);
                                          if (pos <= switchAt && switchAt < pos + r.bs)
                                          {
                                              m.addEvent (juce::MidiMessage::noteOff (1, 57), (int) (switchAt - pos));
                                              m.addEvent (juce::MidiMessage::noteOn (1, 69, 0.8f), (int) (switchAt - pos));
                                          }
                                      }, &in);

            const size_t from = (size_t) (switchAt + (long) (sr * (kFadeMs + 5.0) * 0.001));
            const size_t len = 2048;
            const std::vector<float> win (out.begin() + (long) from, out.begin() + (long) (from + len));
            const std::vector<float> ref (in.begin() + (long) from, in.begin() + (long) (from + len));
            double minCut = 1.0e9;
            for (int k : { 1, 3, 5, 7 })
                minCut = std::min (minCut, levelDb (ref, sr, 440.0 * k) - levelDb (win, sr, 440.0 * k));
            expect (minCut > 30.0, "saw 220 -> 440 Hz: odd harmonics cut > 30 dB at Jump Fade + 5 ms",
                    "min cut " + fmt (minCut) + " dB");
        }

        auto glideRun = [&] (double glideMs, double& maxSlopePerMs, bool& crossfaded)
        {
            chouchou::HarmonicRebalancer reb;
            reb.prepare (sr, 1, 1.0f, false);
            reb.setGlide (glideMs);
            juce::AudioBuffer<float> x (1, kStep), d (1, kStep);
            x.clear();
            for (int i = 0; i < (int) (sr * 0.3) / kStep; ++i)
                reb.process (x, d, kStep, 220.0f, true, 1.0f, false);

            const int voice = reb.getActiveVoice();
            const double target = sr / (2.0 * 232.0);
            double prev = std::log (reb.getDelaySamples());
            maxSlopePerMs = 0.0;
            crossfaded = false;
            for (int n = 0; n < (int) (sr * 0.2); n += kStep)
            {
                reb.process (x, d, kStep, 232.0f, true, 1.0f, false);
                const double now = std::log (reb.getDelaySamples());
                maxSlopePerMs = std::max (maxSlopePerMs, std::abs (now - prev) / (1000.0 * kStep / sr));
                prev = now;
                crossfaded = crossfaded || reb.getCrossfade() < 1.0 || reb.getActiveVoice() != voice;
            }
            return std::abs (std::log (reb.getDelaySamples() / target)) < 1.0e-3;
        };

        double fastSlope = 0.0, slowSlope = 0.0;
        bool fastFaded = false, slowFaded = false;
        const bool fastSettled = glideRun (2.0, fastSlope, fastFaded);
        const bool slowSettled = glideRun (100.0, slowSlope, slowFaded);
        const double slowLimit = std::log (2.0) / 100.0;
        expect (fastSettled && slowSettled && ! fastFaded && ! slowFaded,
                "220 -> 232 Hz (under a semitone) glides on one comb, no crossfade");
        expect (slowSlope <= slowLimit * 1.01 && fastSlope > slowSlope * 1.3,
                "Glide 100 ms/oct caps the glide speed, Glide 2 does not",
                "max slope " + fmt (fastSlope, 4) + " vs " + fmt (slowSlope, 4) + " per ms (cap " + fmt (slowLimit, 4) + ")");
    }

    void testAntiAlias()
    {
        std::cout << "\nAuto detection with high-frequency content\n";
        constexpr double sr = 48000.0;
        Rig rig (sr);
        rig.set (ParamIDs::mode, 1.0f);
        rig.set (ParamIDs::pitchSource, 2.0f);
        rig.prepare();
        const auto s = saw (sr, 220.0, 0.4);
        const auto hf = sine (sr, 9000.0, 0.4);
        rig.run ([&] (long n) { return s (n) + hf (n); }, (long) (sr * 0.5));
        const float f0 = rig.proc->getCurrentF0();
        expect (rig.proc->isPitchActive() && std::abs (f0 - 220.0f) / 220.0f < 0.01f,
                "220 Hz saw + 9 kHz sine locks on 220 Hz", fmt (f0, 2) + " Hz");
    }

    void testMidiControllers()
    {
        std::cout << "\nMIDI channel-mode messages\n";
        constexpr double sr = 48000.0;

        auto makeRig = [&] (Rig& rig)
        {
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 1.0f);
            rig.set (ParamIDs::midiHold, 0.0f);
            rig.prepare();
        };
        auto step = [] (Rig& rig, std::function<void (juce::MidiBuffer&)> events)
        {
            rig.run ([] (long) { return 0.0f; }, rig.bs,
                     [&] (Rig&, long, juce::MidiBuffer& m) { if (events) events (m); });
        };
        auto pedalAndNote = [] (juce::MidiBuffer& m)
        {
            m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 1);
        };
        auto noteOnOff = [] (juce::MidiBuffer& m)
        {
            m.addEvent (juce::MidiMessage::noteOn (1, 62, 0.8f), 0);
            m.addEvent (juce::MidiMessage::noteOff (1, 62), 1);
        };

        {
            Rig rig (sr);
            makeRig (rig);
            step (rig, pedalAndNote);
            step (rig, [] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
            const bool sustained = rig.proc->isPitchActive();
            step (rig, [] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0); });
            expect (sustained && ! rig.proc->isPitchActive(), "CC123 with pedal down: notes sustain until pedal up");
        }

        {
            Rig rig (sr);
            makeRig (rig);
            step (rig, [] (juce::MidiBuffer& m)
            {
                m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
                m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 1);
                m.addEvent (juce::MidiMessage::noteOff (1, 60), 2);
            });
            const bool sustained = rig.proc->isPitchActive();
            step (rig, [] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::controllerEvent (1, 121, 0), 0); });
            const bool released = ! rig.proc->isPitchActive();
            step (rig, noteOnOff);
            expect (sustained && released && ! rig.proc->isPitchActive(),
                    "CC121 releases sustained notes and lifts the pedal");
        }

        {
            Rig rig (sr);
            makeRig (rig);
            step (rig, pedalAndNote);
            step (rig, [] (juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::allSoundOff (1), 0); });
            const bool cleared = ! rig.proc->isPitchActive();
            step (rig, noteOnOff);
            expect (cleared && ! rig.proc->isPitchActive(), "CC120 clears notes and the pedal");
        }
    }

    void testAutoDriveAndRange()
    {
        std::cout << "\nAuto Drive and 200% range\n";
        constexpr double sr = 48000.0;

        auto h2 = [&] (double inDb, bool autoDrive, float even, float odd, double* worstOther = nullptr)
        {
            Rig rig (sr);
            configureGenerate (rig, even, odd, 1);
            rig.set (ParamIDs::autoDrive, autoDrive ? 1.0f : 0.0f);
            rig.prepare();
            const auto out = tail (rig.run (sine (sr, 220.0, juce::Decibels::decibelsToGain (inDb)), (long) (sr * 2.0)),
                                   (size_t) sr);
            const double h1 = levelDb (out, sr, 220.0);
            if (worstOther != nullptr)
            {
                *worstOther = -300.0;
                for (int k = even > 0.0f ? 3 : 2; k <= 9; k += 2)
                    *worstOther = std::max (*worstOther, levelDb (out, sr, 220.0 * k) - h1);
            }
            return levelDb (out, sr, 440.0) - h1;
        };

        const double offQuiet = h2 (-30.0, false, 100.0f, 0.0f), offLoud = h2 (-6.0, false, 100.0f, 0.0f);
        const double onQuiet = h2 (-30.0, true, 100.0f, 0.0f), onLoud = h2 (-6.0, true, 100.0f, 0.0f);
        expect (std::abs (onQuiet - onLoud) < 1.0 && std::abs (offQuiet - offLoud) > 15.0,
                "Auto Drive: H2 amount independent of input level",
                "-30 vs -6 dBFS: off " + fmt (offQuiet) + " / " + fmt (offLoud) + " dB, on " + fmt (onQuiet) + " / " + fmt (onLoud) + " dB");

        double worst = 0.0;
        h2 (-20.0, true, 100.0f, 0.0f, &worst);
        expect (worst < -90.0, "Auto Drive: Even only still adds no odd harmonics", "worst " + fmt (worst) + " dB");

        const double h100 = h2 (-18.0, false, 100.0f, 0.0f), h200 = h2 (-18.0, false, 200.0f, 0.0f);
        expect (std::abs (h200 - h100 - 6.02) < 0.3, "Even 200% is +6 dB over 100%", fmt (h200 - h100, 2) + " dB");
    }

    void testTailImpulse()
    {
        std::cout << "\nTail measured with an impulse\n";
        constexpr double sr = 48000.0;
        const double reported = Proc().getTailLengthSeconds();

        // Impulse: comb delay plus protect ringing. Tone burst: the protect band-passes carry the
        // full steady-state H1 level when the input stops, which is the stricter case.
        double worst = 0.0, worstBurst = 0.0;
        for (float balance : { 100.0f, -100.0f })
        for (bool burst : { false, true })
        {
            Rig rig (sr);
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 1.0f);
            rig.set (ParamIDs::midiHold, 1.0f);
            rig.set (ParamIDs::balance, balance);
            rig.set (ParamIDs::protect, 1.0f);
            rig.prepare();

            const long impulseAt = (long) (sr * 0.6);
            const auto tone = sine (sr, chouchou::PitchSource::noteToHz (16), 1.0);
            const auto input = [&] (long n)
            {
                if (burst)
                    return n < impulseAt ? tone (n) : 0.0f;
                return n == impulseAt ? 1.0f : 0.0f;
            };
            const auto out = rig.run (input, impulseAt + (long) sr,
                                      [] (Rig&, long pos, juce::MidiBuffer& m)
                                      {
                                          if (pos == 0)
                                              m.addEvent (juce::MidiMessage::noteOn (1, 16, 0.8f), 0);   // 20.6 Hz
                                      });
            const long start = impulseAt + rig.proc->getLatencySamples();
            long last = start;
            for (long i = start + 1; i < (long) out.size(); ++i)
                if (std::abs (out[(size_t) i]) > 1.0e-3f)
                    last = i;
            double& w = burst ? worstBurst : worst;
            w = std::max (w, (double) (last - start) / sr);
        }
        expect (worst < reported && worstBurst < reported,
                "rebalance at 20.6 Hz decays below -60 dB within the reported tail",
                "impulse " + fmt (worst, 3) + " s, 0 dBFS tone stop " + fmt (worstBurst, 3)
                    + " s, reported " + fmt (reported, 3) + " s");
    }

    void testOversamplingSwitch()
    {
        std::cout << "\nOversampling switch during playback\n";
        constexpr double sr = 48000.0;

        Rig still (sr);
        still.set (ParamIDs::mode, 2.0f);
        still.prepare();
        const double baseline = maxStepFrom (still.run (sine (sr, 220.0, 0.5), (long) sr), 4800);

        Rig rig (sr);
        rig.set (ParamIDs::mode, 2.0f);
        rig.prepare();
        const auto out = rig.run (sine (sr, 220.0, 0.5), (long) sr * 2, [] (Rig& r, long pos, juce::MidiBuffer&)
        {
            r.set (ParamIDs::oversampling, (float) ((pos / (long) (r.sr * 0.25)) % 3));
        });
        rig.set (ParamIDs::oversampling, 2.0f);
        rig.run (sine (sr, 220.0, 0.5), 4096);
        const int lat = rig.proc->getLatencySamples();

        const double worst = maxStepFrom (out, 4800);
        expect (worst < baseline * 1.5 + 0.02, "switching 2x/4x/8x does not jump",
                "max step " + fmt (worst, 4) + ", baseline " + fmt (baseline, 4));
        expect (lat == 130, "latency follows the final 8x setting (two stages of 65)", std::to_string (lat));

        int longestSilence = 0, run = 0;
        for (size_t i = 4800; i < out.size(); ++i)
        {
            run = out[i] == 0.0f ? run + 1 : 0;
            longestSilence = std::max (longestSilence, run);
        }
        expect (longestSilence > 0 && longestSilence <= Proc::kSwitchChunk,
                "fully silent stretch per switch is at most 64 samples (block size 512)",
                std::to_string (longestSilence) + " samples");
    }

    void testDriveComp()
    {
        std::cout << "\nDrive Comp\n";
        constexpr double sr = 48000.0;

        auto h1Change = [&] (float comp)
        {
            Rig rig (sr);
            configureGenerate (rig, 0.0f, 100.0f, 1);
            rig.set (ParamIDs::driveComp, comp);
            rig.prepare();
            std::vector<float> in;
            const auto out = tail (rig.run (sine (sr, 220.0, 0.01), (long) (sr * 1.5), {}, &in), (size_t) sr);
            return levelDb (out, sr, 220.0) - levelDb (tail (in, (size_t) sr), sr, 220.0);
        };

        const double full = h1Change (100.0f);
        const double none = h1Change (0.0f);
        expect (std::abs (full) < 0.1, "100%: -40 dBFS H1 unchanged", fmt (full, 3) + " dB (0%: " + fmt (none, 2) + " dB)");

        {
            Rig rig (sr);
            configureGenerate (rig, 0.0f, 100.0f, 1);
            rig.set (ParamIDs::drive, 10.0f);
            rig.set (ParamIDs::driveComp, 100.0f);
            rig.prepare();
            std::vector<float> in;
            const auto out = tail (rig.run (sine (sr, 220.0, 0.01), (long) (sr * 1.5), {}, &in), (size_t) sr);
            const double capped = levelDb (out, sr, 220.0) - levelDb (tail (in, (size_t) sr), sr, 220.0);
            expect (std::abs (capped + 16.0) < 1.0, "Drive 10, Bias 0.3, 100%: make-up capped, H1 about -16 dB",
                    fmt (capped, 2) + " dB");
        }

        for (bool evenOnly : { true, false })
        {
            Rig rig (sr);
            configureGenerate (rig, evenOnly ? 100.0f : 0.0f, evenOnly ? 0.0f : 100.0f, 1);
            rig.set (ParamIDs::driveComp, 100.0f);
            rig.prepare();
            const auto out = tail (rig.run (sine (sr, 220.0, 0.5), (long) (sr * 1.5)), (size_t) sr);
            const double h1 = levelDb (out, sr, 220.0);
            double worst = -300.0;
            for (int k = evenOnly ? 3 : 2; k <= 9; k += 2)
                worst = std::max (worst, levelDb (out, sr, 220.0 * k) - h1);
            expect (worst < -90.0, std::string ("100%: ") + (evenOnly ? "Even only keeps odd out" : "Odd only keeps even out"),
                    "worst " + fmt (worst) + " dB");
        }
    }

    void testTailAndSweeps()
    {
        std::cout << "\nTail length and fast modulation\n";
        constexpr double sr = 48000.0;

        Rig rig (sr);
        expect (rig.proc->getTailLengthSeconds() > 0.3, "tail covers rebalance delay and protect filters",
                fmt (rig.proc->getTailLengthSeconds(), 3) + " s");

        auto configure = [] (Rig& r)
        {
            r.set (ParamIDs::mode, 2.0f);
            r.set (ParamIDs::drive, 4.0f);
            r.set (ParamIDs::odd, 60.0f);
            r.set (ParamIDs::even, 80.0f);
            r.set (ParamIDs::balance, 100.0f);
            r.set (ParamIDs::protect, 1.0f);
        };

        Rig still (sr);
        configure (still);
        still.set (ParamIDs::tone, 1000.0f);
        still.set (ParamIDs::manualHz, 110.0f);
        still.prepare();
        const double baseline = maxStepFrom (still.run (sine (sr, 220.0, 0.5), (long) sr), 4800);

        configure (rig);
        rig.prepare();
        const auto out = rig.run (sine (sr, 220.0, 0.5), (long) sr * 2, [] (Rig& r, long pos, juce::MidiBuffer&)
        {
            const double t = (double) pos / r.sr;
            r.set (ParamIDs::tone, (float) (10500.0 + 9500.0 * std::sin (2.0 * kPi * 5.0 * t)));
            r.set (ParamIDs::manualHz, ((long) (t * 10.0)) % 2 == 0 ? 110.0f : 440.0f);
        });

        bool finite = true;
        for (auto v : out)
            finite = finite && std::isfinite (v);
        const double worst = maxStepFrom (out, 4800);
        expect (finite && worst < baseline * 1.5 + 0.02, "Tone sweep at 5 Hz + pitch jumps every 100 ms",
                "max step " + fmt (worst, 4) + ", baseline " + fmt (baseline, 4));
    }
}

//==============================================================================
// Random stage
namespace
{
    void setAllAmounts (Rig& rig, float amount)
    {
        for (int i = 0; i < Proc::kNumRandom; ++i)
            rig.set (ParamIDs::rndAmount (i).toRawUTF8(), amount);
    }

    void configureBoth (Rig& rig)
    {
        rig.set (ParamIDs::mode, 2.0f);
        rig.set (ParamIDs::drive, 4.0f);
        rig.set (ParamIDs::even, 100.0f);
        rig.set (ParamIDs::odd, 60.0f);
        rig.set (ParamIDs::balance, 60.0f);
        rig.set (ParamIDs::manualHz, 220.0f);
    }

    // Draws seeded values in one silent block, then re-prepares so the run starts from them.
    void seededRoll (Rig& rig, juce::int64 seed)
    {
        rig.proc->setRandomSeed (seed);
        rig.proc->requestRoll();
        rig.run ([] (long) { return 0.0f; }, rig.bs);
        rig.prepare();
        rig.sample = 0;
    }

    // Main voice alone, run on the same input; the processor output should be this delayed by L.
    std::vector<float> mainVoiceReference (Rig& rig, const std::function<float (long)>& gen, long numSamples)
    {
        auto& p = *rig.proc;
        const auto settings = chouchou::EngineVoice::Settings::fromValues (
            p.getMainValues(), p.getAPVTS().getRawParameterValue (ParamIDs::mix)->load() * 0.01f);
        chouchou::EngineVoice voice;
        voice.prepare (rig.sr, 2, rig.bs, (int) p.getAPVTS().getRawParameterValue (ParamIDs::oversampling)->load(), settings);

        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, rig.bs);
        for (long pos = 0; pos < numSamples; pos += rig.bs)
        {
            const int n = (int) std::min<long> (rig.bs, numSamples - pos);
            buffer.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                buffer.setSample (0, i, gen (pos + i));
                buffer.setSample (1, i, gen (pos + i));
            }
            voice.process (buffer, 0, n, settings, nullptr);
            for (int i = 0; i < n; ++i)
                out.push_back (buffer.getSample (0, i));
        }
        return out;
    }

    void testRandomBypassExact()
    {
        std::cout << "\nRandom stage: bypass and Mix 0% are exact\n";
        constexpr double sr = 48000.0;
        const auto input = saw (sr, 220.0, 0.4);
        constexpr long kLen = 48000;

        for (bool bypassOn : { true, false })
        {
            Rig rig (sr);
            configureBoth (rig);
            setAllAmounts (rig, 100.0f);
            rig.set (ParamIDs::rndBypass, bypassOn ? 1.0f : 0.0f);
            rig.set (ParamIDs::rndMix, bypassOn ? 100.0f : 0.0f);
            rig.prepare();
            seededRoll (rig, 5);
            const auto out = rig.run (input, kLen);
            const auto ref = mainVoiceReference (rig, input, kLen);
            const int lat = rig.proc->getLatencySamples(), half = lat / 2;

            double maxErr = 0.0;
            for (size_t i = (size_t) lat; i < out.size(); ++i)
                maxErr = std::max (maxErr, (double) std::abs (out[i] - ref[i - (size_t) half]));
            expect (lat > 0 && maxErr == 0.0,
                    bypassOn ? "Bypass on: output = main stage delayed by L, bit-exact"
                             : "Bypass off, Mix aleatoire 0%: output = main stage delayed by L, bit-exact",
                    "latency " + std::to_string (lat) + ", max err " + juce::String (maxErr, 9).toStdString());
        }
    }

    void testRandomSerial()
    {
        std::cout << "\nRandom stage runs in series\n";
        constexpr double sr = 48000.0;

        auto h2 = [&] (bool randomOn)
        {
            Rig rig (sr);
            configureGenerate (rig, 100.0f, 0.0f, 1);
            setAllAmounts (rig, 0.0f);
            rig.set (ParamIDs::rndBypass, randomOn ? 0.0f : 1.0f);
            rig.set (ParamIDs::rndMix, 100.0f);
            rig.prepare();
            const auto out = tail (rig.run (sine (sr, 220.0, 0.05), (long) (sr * 1.5)), (size_t) sr);
            return levelDb (out, sr, 440.0) - levelDb (out, sr, 220.0);
        };

        const double off = h2 (false), on = h2 (true);
        expect (std::abs (on - off - 6.02) < 0.5, "amounts 0%, Mix 100%, Even only: H2 about +6 dB (applied twice)",
                "bypass " + fmt (off, 2) + " dB, active " + fmt (on, 2) + " dB, difference " + fmt (on - off, 2) + " dB");
    }

    void testRandomBypassToggle()
    {
        std::cout << "\nRandom stage: toggling Bypass\n";
        constexpr double sr = 48000.0;

        auto make = [&] (Rig& rig, float bypass)
        {
            configureBoth (rig);
            setAllAmounts (rig, 30.0f);
            rig.set (ParamIDs::rndBypass, bypass);
            rig.prepare();
            seededRoll (rig, 11);
        };

        double baseline = 0.0;
        for (float b : { 0.0f, 1.0f })
        {
            Rig still (sr);
            make (still, b);
            baseline = std::max (baseline, maxStepFrom (still.run (sine (sr, 220.0, 0.5), (long) sr), 4800));
        }

        Rig rig (sr);
        make (rig, 1.0f);
        const int expectedLatency = rig.proc->getLatencySamples();
        bool latencyStable = true;
        const auto out = rig.run (sine (sr, 220.0, 0.5), (long) sr * 3, [&] (Rig& r, long pos, juce::MidiBuffer&)
        {
            r.set (ParamIDs::rndBypass, ((pos / (long) (r.sr * 0.25)) % 2) == 0 ? 1.0f : 0.0f);
            latencyStable = latencyStable && r.proc->getLatencySamples() == expectedLatency;
        });
        const double worst = maxStepFrom (out, 4800);
        expect (latencyStable && expectedLatency > 0 && expectedLatency % 2 == 0, "reported latency stays 2L while toggling",
                std::to_string (expectedLatency) + " samples at 4x");
        expect (worst < baseline * 1.5 + 0.02, "toggling Bypass every 250 ms does not jump",
                "max step " + fmt (worst, 4) + ", static baseline " + fmt (baseline, 4));
    }

    void testRandomReproducible()
    {
        std::cout << "\nRandom stage: fixed seed\n";
        constexpr double sr = 48000.0;

        auto render = [&] (juce::int64 seed, bool bypass, std::vector<float>* values)
        {
            Rig rig (sr);
            configureBoth (rig);
            setAllAmounts (rig, 100.0f);
            rig.set (ParamIDs::rndBypass, bypass ? 1.0f : 0.0f);
            rig.set (ParamIDs::rndMorph, 0.0f);
            rig.prepare();
            seededRoll (rig, seed);
            auto out = rig.run (saw (sr, 220.0, 0.3), (long) sr);
            if (values != nullptr)
                for (int i = 0; i < Proc::kNumRandom; ++i)
                    values->push_back (rig.proc->getRandomPlain (i) - rig.proc->getMainValues()[(size_t) i]);
            return out;
        };

        std::vector<float> diffs;
        const auto a = render (1234, false, &diffs), b = render (1234, false, nullptr);
        const auto plain = render (1234, true, nullptr);
        double ab = 0.0, ap = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            ab = std::max (ab, (double) std::abs (a[i] - b[i]));
            ap = std::max (ap, (double) std::abs (a[i] - plain[i]));
        }
        int changed = 0;
        for (float d : diffs)
            changed += std::abs (d) > 1.0e-4f ? 1 : 0;
        expect (ab == 0.0 && ap > 1.0e-3 && changed >= 12, "amount 100%, same seed: identical, and differs from the main stage",
                "max diff between runs " + juce::String (ab, 9).toStdString() + ", vs bypass " + fmt (ap, 4)
                    + ", " + std::to_string (changed) + "/20 parameters moved");
    }

    void testRandomTriggers()
    {
        std::cout << "\nRandom stage: triggers\n";
        constexpr double sr = 48000.0;
        auto silence = [] (long) { return 0.0f; };

        {
            Rig rig (sr);
            rig.prepare();
            const int start = rig.proc->getRollCount();
            rig.set (ParamIDs::rndTrigger, 1.0f);
            rig.run (silence, rig.bs * 6);
            const int afterFirst = rig.proc->getRollCount() - start;
            rig.set (ParamIDs::rndTrigger, 0.0f);
            rig.run (silence, rig.bs * 2);
            rig.set (ParamIDs::rndTrigger, 1.0f);
            rig.run (silence, rig.bs * 2);
            const int afterSecond = rig.proc->getRollCount() - start;
            rig.proc->requestRoll();
            rig.run (silence, rig.bs * 2);
            const int afterButton = rig.proc->getRollCount() - start;
            expect (afterFirst == 1 && afterSecond == 2 && afterButton == 3,
                    "trigger draws once per rising edge; Lancer draws once",
                    std::to_string (afterFirst) + ", " + std::to_string (afterSecond) + ", " + std::to_string (afterButton));
        }

        auto rollTimes = [&] (bool syncOn, float value, double seconds)
        {
            Rig rig (sr);
            rig.set (ParamIDs::rndBypass, 0.0f);
            rig.set (ParamIDs::rndAuto, 1.0f);
            rig.set (ParamIDs::rndSync, syncOn ? 1.0f : 0.0f);
            rig.set (syncOn ? ParamIDs::rndDivision : ParamIDs::rndInterval, value);
            rig.prepare();
            std::vector<long> times;
            int last = rig.proc->getRollCount();
            rig.run (silence, (long) (sr * seconds), [&] (Rig& r, long pos, juce::MidiBuffer&)
            {
                const int now = r.proc->getRollCount();
                if (now != last)
                    times.push_back (pos - r.bs);
                last = now;
            });
            return times;
        };

        auto check = [&] (const std::vector<long>& times, double expectedSamples, size_t minCount, const std::string& name)
        {
            double worst = 0.0;
            for (size_t i = 1; i < times.size(); ++i)
                worst = std::max (worst, std::abs ((double) (times[i] - times[i - 1]) - expectedSamples));
            expect (times.size() >= minCount && worst <= 512.0, name,
                    std::to_string (times.size()) + " draws, worst interval error " + fmt (worst, 0) + " samples");
        };

        check (rollTimes (false, 0.5f, 3.2), sr * 0.5, 5, "Auto every 0.5 s: error under one block");
        check (rollTimes (true, 4.0f, 7.0), sr * 2.0, 3, "Sync, 1 bar at 120 BPM (no host tempo) = 2 s");

        {
            Rig rig (sr);
            rig.set (ParamIDs::rndAuto, 1.0f);
            rig.set (ParamIDs::rndInterval, 0.25f);
            rig.prepare();
            const int start = rig.proc->getRollCount();
            rig.run (silence, (long) sr);
            expect (rig.proc->getRollCount() == start, "Auto does not draw while bypassed");
        }
    }

    // Each draw (including Mode and switch changes) must not click: the step size around a draw
    // stays within the static step sizes of the settings before and after it.
    void testRandomRerollClicks()
    {
        std::cout << "\nRandom stage: continuous re-rolls\n";
        constexpr double sr = 48000.0;

        for (float morphSec : { 0.0f, 0.2f })
        {
            Rig rig (sr);
            configureBoth (rig);
            setAllAmounts (rig, 100.0f);
            rig.set (ParamIDs::rndBypass, 0.0f);
            rig.set (ParamIDs::rndMorph, morphSec);
            rig.set (ParamIDs::rndAuto, 1.0f);
            rig.set (ParamIDs::rndInterval, 0.5f);
            rig.prepare();
            rig.proc->setRandomSeed (77);

            std::vector<long> rolls;
            int last = rig.proc->getRollCount();
            int modeChanges = 0;
            float lastMode = -1.0f;
            const auto out = rig.run (sine (sr, 220.0, 0.3), (long) (sr * 8.0), [&] (Rig& r, long pos, juce::MidiBuffer&)
            {
                const int now = r.proc->getRollCount();
                if (now != last)
                    rolls.push_back (pos - r.bs);
                last = now;
                const float m = r.proc->getRandomPlain (chouchou::epMode);
                if (lastMode >= 0.0f && ! juce::exactlyEqual (m, lastMode))
                    ++modeChanges;
                lastMode = m;
            });

            const long settle = (long) (sr * (morphSec + 0.12));
            double worstRatio = 0.0, worstTransition = 0.0;
            bool finite = true;
            for (auto v : out)
                finite = finite && std::isfinite (v);

            for (size_t k = 1; k + 1 < rolls.size(); ++k)
            {
                auto stepIn = [&] (long a, long b)
                {
                    double m = 0.0;
                    for (long i = std::max (1L, a); i < std::min ((long) out.size(), b); ++i)
                        m = std::max (m, (double) std::abs (out[(size_t) i] - out[(size_t) i - 1]));
                    return m;
                };
                const double before = stepIn (rolls[k - 1] + settle, rolls[k]);
                const double after = stepIn (rolls[k] + settle, rolls[k + 1]);
                const double transition = stepIn (rolls[k], rolls[k] + settle);
                const double allowed = 1.5 * std::max (before, after) + 0.02;
                worstRatio = std::max (worstRatio, transition / allowed);
                worstTransition = std::max (worstTransition, transition);
            }
            expect (finite && rolls.size() >= 14 && worstRatio < 1.0,
                    "draw every 0.5 s, all amounts 100%, morph " + fmt (morphSec, 1) + " s: no clicks",
                    std::to_string (rolls.size()) + " draws, " + std::to_string (modeChanges) + " Mode changes, worst transition step "
                        + fmt (worstTransition, 4) + " = " + fmt (worstRatio * 100.0, 0) + "% of allowed");
        }
    }

    void testRandomState()
    {
        std::cout << "\nRandom stage: save and reload\n";
        constexpr double sr = 48000.0;

        Rig a (sr);
        configureBoth (a);
        setAllAmounts (a, 60.0f);
        a.set (ParamIDs::rndBypass, 0.0f);
        a.set (ParamIDs::rndMorph, 0.0f);
        a.prepare();
        seededRoll (a, 42);
        a.run (saw (sr, 220.0, 0.3), (long) (sr * 0.3));

        juce::MemoryBlock block;
        a.proc->getStateInformation (block);

        Rig b (sr);
        b.proc->setStateInformation (block.getData(), (int) block.getSize());
        b.prepare();
        a.prepare();
        a.sample = 0;

        const auto outA = a.run (saw (sr, 220.0, 0.3), (long) sr);
        const auto outB = b.run (saw (sr, 220.0, 0.3), (long) sr);
        double maxErr = 0.0, maxValueErr = 0.0;
        for (size_t i = 0; i < outA.size(); ++i)
            maxErr = std::max (maxErr, (double) std::abs (outA[i] - outB[i]));
        std::string which;
        for (int i = 0; i < Proc::kNumRandom; ++i)
        {
            const double d = std::abs (a.proc->getRandomNormalised (i) - b.proc->getRandomNormalised (i));
            const double base = std::abs (a.proc->getMainValues()[(size_t) i] - b.proc->getMainValues()[(size_t) i]);
            if (d > 0.0 || base > 0.0)
                which += std::string (" ") + chouchou::engineParamInfo()[(size_t) i].id + " (random " + juce::String (d, 9).toStdString()
                         + ", base " + juce::String (base, 9).toStdString() + ")";
            maxValueErr = std::max (maxValueErr, d);
        }
        if (! which.empty())
            std::cout << "        differs:" << which << "\n";
        expect (maxErr == 0.0 && maxValueErr == 0.0, "reloaded state gives the same random stage and output",
                "max output diff " + juce::String (maxErr, 9).toStdString() + ", max value diff "
                    + juce::String (maxValueErr, 9).toStdString());
    }

    void testRandomAdopt()
    {
        std::cout << "\nRandom stage: Adopter\n";
        constexpr double sr = 48000.0;

        Rig rig (sr);
        configureBoth (rig);
        setAllAmounts (rig, 100.0f);
        rig.set (ParamIDs::rndBypass, 0.0f);
        rig.set (ParamIDs::rndMorph, 0.0f);
        rig.prepare();
        seededRoll (rig, 9);
        rig.run (saw (sr, 220.0, 0.3), (long) (sr * 0.2));

        const auto randomValues = rig.proc->getRandomValues();
        rig.proc->adoptRandom();
        const auto mainValues = rig.proc->getMainValues();

        double worst = 0.0;
        for (int i = 0; i < Proc::kNumRandom; ++i)
        {
            auto* p = rig.proc->getAPVTS().getParameter (chouchou::engineParamInfo()[(size_t) i].id);
            worst = std::max (worst, (double) std::abs (p->convertTo0to1 (mainValues[(size_t) i]) - p->convertTo0to1 (randomValues[(size_t) i])));
        }
        const bool bypassed = rig.proc->getAPVTS().getRawParameterValue (ParamIDs::rndBypass)->load() > 0.5f;
        expect (worst < 1.0e-5 && bypassed, "main knobs take the random values and the random stage is bypassed",
                "worst normalised difference " + juce::String (worst, 8).toStdString());
    }

    void testDisplayModel()
    {
        std::cout << "\nDisplay predictions match the sound\n";
        constexpr double sr = 48000.0;

        for (float balance : { 60.0f, -40.0f })
        for (bool protect : { false, true })
        {
            Rig rig (sr);
            rig.set (ParamIDs::mode, 1.0f);
            rig.set (ParamIDs::pitchSource, 0.0f);
            rig.set (ParamIDs::manualHz, 220.0f);
            rig.set (ParamIDs::balance, balance);
            rig.set (ParamIDs::protect, protect ? 1.0f : 0.0f);
            rig.prepare();
            std::vector<float> in;
            const auto out = tail (rig.run (saw (sr, 220.0, 0.5), (long) sr * 2, {}, &in), (size_t) sr);
            const auto inTail = tail (in, (size_t) sr);
            const auto settings = chouchou::EngineVoice::Settings::fromValues (rig.proc->getMainValues(), 1.0f);
            const auto predicted = chouchou::TimbreModel::rebalanceHarmonicDb (settings);
            double worst = 0.0;
            for (int k = 1; k <= 9; ++k)
            {
                const double measured = levelDb (out, sr, 220.0 * k) - levelDb (inTail, sr, 220.0 * k);
                worst = std::max (worst, std::abs (measured - predicted[(size_t) k - 1]));
            }
            expect (worst < 0.5, "Rebalance arrows, Balance " + fmt (balance, 0) + "%, protect " + (protect ? "on" : "off"),
                    "worst H1-H9 error " + fmt (worst, 3) + " dB");
        }

        for (bool autoDrive : { false, true })
        {
            Rig rig (sr);
            configureGenerate (rig, 100.0f, 50.0f, 1);
            rig.set (ParamIDs::autoDrive, autoDrive ? 1.0f : 0.0f);
            rig.prepare();
            const auto out = tail (rig.run (sine (sr, 220.0, 0.25), (long) (sr * 2.0)), (size_t) sr);
            const auto settings = chouchou::EngineVoice::Settings::fromValues (rig.proc->getMainValues(), 1.0f);
            const auto predicted = chouchou::TimbreModel::generateOutput (settings, rig.proc->getMainInputRms(),
                                                                          rig.proc->getMainAutoGain(), 220.0f, 1.0f);
            double worst = 0.0;
            for (int k = 1; k <= 5; ++k)
                worst = std::max (worst, std::abs (levelDb (out, sr, 220.0 * k) - juce::Decibels::gainToDecibels ((double) predicted[(size_t) k - 1])));
            expect (worst < 1.0, std::string ("Generate boxes, -12 dBFS sine, Drive auto ") + (autoDrive ? "on" : "off"),
                    "worst H1-H5 error " + fmt (worst, 3) + " dB, measured input RMS " + fmt (rig.proc->getMainInputRms(), 4));
        }

        {
            chouchou::EngineVoice::Settings s;
            s.gen.drive = 5.0f;
            s.gen.bias = 0.0f;
            s.gen.even = 1.0f;
            s.gen.odd = 1.0f;
            double worstEven = 0.0;
            for (int j = 0; j <= 100; ++j)
            {
                float full, oddPart, evenPart;
                chouchou::TimbreModel::shaperCurve (s, -1.0f + 0.02f * (float) j, full, oddPart, evenPart);
                worstEven = std::max (worstEven, (double) std::abs (evenPart));
            }
            expect (worstEven == 0.0, "shaper inset: even part is exactly zero at Biais 0", juce::String (worstEven, 9).toStdString());
        }
    }

    void testKnobEffect()
    {
        std::cout << "\nKnob focus panel: each knob changes only what it should\n";
        namespace TM = chouchou::TimbreModel;
        constexpr float rms = 0.177f, f0 = 220.0f;

        auto valuesFor = [] (std::initializer_list<std::pair<const char*, float>> settings)
        {
            Rig rig (48000.0);
            for (const auto& [id, value] : settings)
                rig.set (id, value);
            rig.prepare();
            return rig.proc->getMainValues();
        };
        auto delta = [] (const TM::KnobEffect& e, int k)
        {
            return TM::harmonicDb (e.current, k) - TM::harmonicDb (e.neutral, k);
        };

        {
            const auto values = valuesFor ({ { ParamIDs::even, 80.0f }, { ParamIDs::odd, 0.0f }, { ParamIDs::bias, 40.0f } });
            const auto e = TM::knobEffect (values, 1.0f, chouchou::epEven, rms, 1.0f, f0, 1.0f);
            const auto neutral = TM::waveformOf (e.neutral), input = TM::waveformOf (e.input);
            double waveDiff = 0.0, oddDelta = 0.0;
            for (int j = 0; j < TM::kWavePoints; ++j)
                waveDiff = std::max (waveDiff, (double) std::abs (neutral[(size_t) j] - input[(size_t) j]));
            for (int k : { 1, 3, 5, 7 })
                oddDelta = std::max (oddDelta, std::abs (delta (e, k)));
            expect (waveDiff < 1.0e-6, "Pairs (Impairs 0): dashed waveform is the input sine", "max diff " + fmt (waveDiff, 9));
            expect (oddDelta < 1.0e-6 && delta (e, 2) > 6.0, "Pairs: only even harmonics change",
                    "odd |delta| " + fmt (oddDelta, 9) + " dB, H2 +" + fmt (delta (e, 2), 1) + " dB");
        }

        {
            const auto values = valuesFor ({ { ParamIDs::even, 80.0f }, { ParamIDs::odd, 100.0f }, { ParamIDs::bias, 40.0f },
                                             { ParamIDs::drive, 4.0f } });
            const auto e = TM::knobEffect (values, 1.0f, chouchou::epOdd, rms, 1.0f, f0, 1.0f);
            double evenDelta = 0.0;
            for (int k : { 2, 4, 6, 8 })
                evenDelta = std::max (evenDelta, std::abs (delta (e, k)));
            expect (evenDelta < 1.0e-6 && std::abs (delta (e, 3)) > 3.0, "Impairs: only odd harmonics change",
                    "even |delta| " + fmt (evenDelta, 9) + " dB, H3 " + fmt (delta (e, 3), 1) + " dB");
        }

        for (float balance : { 60.0f, -40.0f })
        {
            const auto values = valuesFor ({ { ParamIDs::mode, 1.0f }, { ParamIDs::balance, balance }, { ParamIDs::protect, 1.0f } });
            const auto e = TM::knobEffect (values, 1.0f, chouchou::epBalance, rms, 1.0f, f0, 1.0f);
            const auto expected = TM::rebalanceHarmonicDb (chouchou::EngineVoice::Settings::fromValues (values, 1.0f));
            double worst = 0.0;
            for (int k = 1; k <= TM::kHarmonics; ++k)
                worst = std::max (worst, std::abs (delta (e, k) - expected[(size_t) k - 1]));
            expect (e.sawInput && worst < 0.01, "Balance " + fmt (balance, 0) + "%: panel bars equal the comb arrows",
                    "worst " + fmt (worst, 6) + " dB");
        }

        {
            const auto values = valuesFor ({ { ParamIDs::even, 80.0f }, { ParamIDs::odd, 50.0f }, { ParamIDs::bias, 40.0f } });
            const auto mixOff = TM::knobEffect (values, 0.6f, TM::focusMix, rms, 1.0f, f0, 1.0f);
            double worst = 0.0;
            for (int k = 1; k <= TM::kSpectrumHarmonics; ++k)
                worst = std::max (worst, std::abs (mixOff.neutral[(size_t) k - 1] - mixOff.input[(size_t) k - 1]));
            expect (worst < 1.0e-9, "Mix: dashed waveform is the dry input", "max diff " + fmt (worst, 12));

            const auto tone = TM::knobEffect (valuesFor ({ { ParamIDs::even, 80.0f }, { ParamIDs::tone, 1000.0f } }),
                                              1.0f, chouchou::epTone, rms, 1.0f, f0, 1.0f);
            expect (std::abs (delta (tone, 1)) < 1.0e-6 && delta (tone, 2) < -0.1 && delta (tone, 6) < -3.0,
                    "Tonalité: cuts added harmonics, more at the top, H1 untouched",
                    "H2 " + fmt (delta (tone, 2), 2) + " dB, H6 " + fmt (delta (tone, 6), 2) + " dB");

            const auto input = TM::knobEffect (values, 1.0f, TM::focusInput, rms, 1.0f, f0, 2.0f);
            const double h1Change = delta (input, 1);
            expect (std::abs (h1Change - 6.02) < 1.0, "Entrée +6 dB: compared with the signal before the gain",
                    "H1 " + fmt (h1Change, 2) + " dB");
        }
    }
}

// Renders the editor after feeding a saw through Both mode, to check the layout. With
// randomOn the random stage is active (overlays and knob ranges); randomPage shows its page.
static int writeSnapshot (const juce::File& file, bool randomOn, bool randomPage, int focus)
{
    constexpr double sr = 48000.0;
    Rig rig (sr);
    rig.set (ParamIDs::mode, 2.0f);
    rig.set (ParamIDs::odd, 30.0f);
    rig.set (ParamIDs::balance, 60.0f);
    if (randomOn)
    {
        rig.set (ParamIDs::rndBypass, 0.0f);
        rig.set (ParamIDs::rndMix, 70.0f);
        rig.set (ParamIDs::rndAmount (chouchou::epBalance).toRawUTF8(), 80.0f);
        rig.set (ParamIDs::rndAmount (chouchou::epMode).toRawUTF8(), 0.0f);
        rig.set (ParamIDs::rndAmount (chouchou::epPitchSource).toRawUTF8(), 0.0f);
        rig.set (ParamIDs::rndAmount (chouchou::epManualHz).toRawUTF8(), 0.0f);
    }
    rig.prepare();
    rig.proc->setRandomSeed (3);
    rig.proc->requestRoll();

    std::unique_ptr<juce::AudioProcessorEditor> editor (rig.proc->createEditor());
    if (auto* e = dynamic_cast<ChouchouEQtimbreAudioProcessorEditor*> (editor.get()))
    {
        e->showRandomPage (randomPage);
        e->setFocus (focus);
    }
    for (int frame = 0; frame < 20; ++frame)
    {
        rig.run (saw (sr, 220.0, 0.5), 2400);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    const bool ok = out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
    std::cout << (ok ? "Wrote " : "Could not write ") << file.getFullPathName() << "\n";
    return ok ? 0 : 1;
}

// Prints how strong the default sound is at different input levels.
static int printLevels()
{
    constexpr double sr = 48000.0;
    struct Preset { const char* name; float drive, comp, even, odd; };
    for (const auto& p : { Preset { "default (drive 2, comp 0, even 50, odd 0)", 2.0f, 0.0f, 50.0f, 0.0f },
                           Preset { "even 100", 2.0f, 0.0f, 100.0f, 0.0f },
                           Preset { "drive 5, even 100", 5.0f, 0.0f, 100.0f, 0.0f },
                           Preset { "drive 5, comp 100, even 100", 5.0f, 100.0f, 100.0f, 0.0f },
                           Preset { "drive 5, comp 100, even 100, odd 100", 5.0f, 100.0f, 100.0f, 100.0f } })
    {
    std::cout << "\n" << p.name << "\ninput dBFS | H2 vs H1 | H3 vs H1 | wet change (out - dry) vs dry\n";
    for (double inDb : { -30.0, -18.0, -12.0, -6.0, 0.0 })
    {
        Rig rig (sr);
        rig.set (ParamIDs::drive, p.drive);
        rig.set (ParamIDs::driveComp, p.comp);
        rig.set (ParamIDs::even, p.even);
        rig.set (ParamIDs::odd, p.odd);
        rig.prepare();
        std::vector<float> in;
        const double amp = juce::Decibels::decibelsToGain (inDb);
        const auto out = rig.run (sine (sr, 220.0, amp), (long) (sr * 1.5), {}, &in);
        const int lat = rig.proc->getLatencySamples();
        const auto o = tail (out, (size_t) sr);
        double diff = 0.0, dry = 0.0;
        for (size_t i = out.size() - (size_t) sr; i < out.size(); ++i)
        {
            const double d = in[i - (size_t) lat];
            diff += (out[i] - d) * (out[i] - d);
            dry += d * d;
        }
        const double h1 = levelDb (o, sr, 220.0);
        std::cout << fmt (inDb, 0) << " | " << fmt (levelDb (o, sr, 440.0) - h1) << " | "
                  << fmt (levelDb (o, sr, 660.0) - h1) << " | " << fmt (10.0 * std::log10 (diff / dry)) << " dB\n";
    }
    }
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc == 2 && juce::String (argv[1]) == "--levels")
        return printLevels();

    if ((argc == 3 || argc == 4) && juce::String (argv[1]).startsWith ("--snapshot"))
    {
        const juce::String mode (argv[1]);
        return writeSnapshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                              mode != "--snapshot", mode == "--snapshot-page",
                              argc == 4 ? juce::String (argv[3]).getIntValue() : (int) chouchou::epEven);
    }

    std::cout << "chouchouEQtimbre self-test\n";
    testGeneratePurity();
    testDriveZeroAndMixZero();
    testRebalance();
    testMidi();
    testAuto();
    testAutomation();
    testMidiTiming();
    testAutoStates();
    testRebalancerGlide();
    testAntiAlias();
    testMidiControllers();
    testOversamplingSwitch();
    testDriveComp();
    testTailAndSweeps();
    testTailImpulse();
    testAutoDriveAndRange();
    testRandomBypassExact();
    testRandomSerial();
    testRandomBypassToggle();
    testRandomReproducible();
    testRandomTriggers();
    testRandomRerollClicks();
    testRandomState();
    testRandomAdopt();
    testDisplayModel();
    testKnobEffect();

    std::cout << "\n" << (gFails == 0 ? "ALL PASSED" : std::to_string (gFails) + " FAILED") << "\n";
    return gFails == 0 ? 0 : 1;
}
