/*
  Offline stage self-test for chouchouCompressor.

  Instantiates the real AudioProcessor and drives synthetic audio through
  processBlock to verify Stage 1 BOOST / Stage 2 COMP / Stage 3 MIX.
*/

#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
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
        {
            const float norm = param->convertTo0to1 (value);
            param->setValueNotifyingHost (norm);
        }
    }

    void setDefaultsForIsolation (NewProjectAudioProcessor& p)
    {
        // Bypass both dynamics stages; full wet; zero makeup.
        setParam (p, "upratio", 1.0f);
        setParam (p, "downratio", 1.0f);
        setParam (p, "mix", 1.0f);
        setParam (p, "makeup", 0.0f);
        setParam (p, "boostspeed", 50.0f);
        setParam (p, "compspeed", 50.0f);
        setParam (p, "boostatk", 0.0f);
        setParam (p, "boostrel", 0.0f);
        setParam (p, "gratk", 0.0f);
        setParam (p, "grrel", 0.0f);
        setParam (p, "lowthresh", -50.0f);
        setParam (p, "highthresh", -18.0f);
        setParam (p, "fftsize", 2.0f); // 2048
    }

    float rmsOf (const juce::AudioBuffer<float>& buf)
    {
        double acc = 0.0;
        const int n = buf.getNumSamples() * buf.getNumChannels();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                acc += (double) d[i] * (double) d[i];
        }
        return n > 0 ? (float) std::sqrt (acc / (double) n) : 0.0f;
    }

    float peakOf (const juce::AudioBuffer<float>& buf)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buf.getMagnitude (ch, 0, buf.getNumSamples()));
        return peak;
    }

    /** Fill with sine. amp is linear peak. */
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

    void fillSilence (juce::AudioBuffer<float>& buf)
    {
        buf.clear();
    }

    /** Run enough blocks for STFT latency + settling. */
    void render (NewProjectAudioProcessor& p,
                 juce::AudioBuffer<float>& io,
                 int totalSamples,
                 double sr,
                 float hz,
                 float amp)
    {
        const int block = io.getNumSamples();
        juce::MidiBuffer midi;
        double phase = 0.0;
        int done = 0;

        while (done < totalSamples)
        {
            fillSine (io, sr, hz, amp, phase);
            p.processBlock (io, midi);
            done += block;
        }
    }

    /** Last-block RMS after warming up with a constant sine. */
    float steadyRms (NewProjectAudioProcessor& p,
                     juce::AudioBuffer<float>& io,
                     double sr,
                     float hz,
                     float amp,
                     int warmBlocks = 64)
    {
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int b = 0; b < warmBlocks; ++b)
        {
            fillSine (io, sr, hz, amp, phase);
            p.processBlock (io, midi);
        }

        fillSine (io, sr, hz, amp, phase);
        p.processBlock (io, midi);
        return rmsOf (io);
    }

    float steadyPeak (NewProjectAudioProcessor& p,
                      juce::AudioBuffer<float>& io,
                      double sr,
                      float hz,
                      float amp,
                      int warmBlocks = 64)
    {
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int b = 0; b < warmBlocks; ++b)
        {
            fillSine (io, sr, hz, amp, phase);
            p.processBlock (io, midi);
        }

        fillSine (io, sr, hz, amp, phase);
        p.processBlock (io, midi);
        return peakOf (io);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    NewProjectAudioProcessor proc;
    proc.prepareToPlay (sr, blockSize);
    proc.applyFftSizeFromParameterLocked (true);

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;

    std::cout << "\n=== chouchouCompressor offline stage self-test ===\n";
    std::cout << "FFT=" << proc.getFftSize()
              << " hop=" << proc.getHopSize()
              << " latency=" << proc.getLatencySamples() << "\n\n";

    //--------------------------------------------------------------------------
    std::cout << "[A] Stage independence (Ratio=1 bypass)\n";
    {
        setDefaultsForIsolation (proc);
        proc.reset();

        const float dryRms = [] (double sampleRate)
        {
            // Reference RMS of amp 0.25 sine ≈ 0.25/sqrt(2)
            juce::ignoreUnused (sampleRate);
            return 0.25f / std::sqrt (2.0f);
        } (sr);

        const float outRms = steadyRms (proc, buf, sr, 1000.0f, 0.25f);
        const float errDb = juce::Decibels::gainToDecibels (outRms / dryRms, -120.0f);
        expect (std::abs (errDb) < 1.5f,
                "Both ratios=1 → near unity wet (STFT)",
                "err=" + juce::String (errDb, 2).toStdString() + " dB");
    }

    //--------------------------------------------------------------------------
    std::cout << "\n[B] Stage 1 BOOST only\n";
    {
        setDefaultsForIsolation (proc);
        setParam (proc, "downratio", 1.0f);   // comp off
        setParam (proc, "upratio", 8.0f);     // boost on
        setParam (proc, "lowthresh", -20.0f); // quiet content below -20 should rise
        setParam (proc, "boostatk", 0.0f);
        setParam (proc, "boostrel", 0.0f);
        proc.reset();

        // Quiet sine (~ -32 dBFS peak)
        const float quietAmp = juce::Decibels::decibelsToGain (-32.0f);
        const float outPeak = steadyPeak (proc, buf, sr, 1000.0f, quietAmp, 96);
        const float outDb = juce::Decibels::gainToDecibels (outPeak, -120.0f);
        const float boostMeter = proc.getMeterBoostDb();

        expect (outDb > -32.0f + 3.0f,
                "Quiet sine is boosted upward",
                "in=-32 dBFS out=" + juce::String (outDb, 1).toStdString() + " dBFS");
        expect (boostMeter > 1.0f,
                "BOOST meter reports gain",
                "meter=" + juce::String (boostMeter, 1).toStdString() + " dB");
        expect (proc.getMeterGrDb() < 0.5f,
                "COMP meter stays idle while Comp Ratio=1",
                "gr=" + juce::String (proc.getMeterGrDb(), 2).toStdString());

        std::vector<float> specMag, specGr, specBoost;
        double specSr = 0.0;
        int specN = 0;
        proc.copySpectrum (specMag, specGr, specBoost, specSr, specN);
        const int bin1k = specN > 0 ? juce::jlimit (0, (int) specBoost.size() - 1,
                                                    (int) std::lround (1000.0 / sr * (double) specN)) : 0;
        const int bin8k = specN > 0 ? juce::jlimit (0, (int) specBoost.size() - 1,
                                                    (int) std::lround (8000.0 / sr * (double) specN)) : 0;
        expect (! specBoost.empty() && specBoost[(size_t) bin1k] > 0.5f && specBoost[(size_t) bin8k] < 0.5f,
                "The spectrum shows expansion at 1000 Hz and not at 8 kHz",
                "1k " + std::to_string (specBoost.empty() ? 0.0f : specBoost[(size_t) bin1k])
                    + " 8k " + std::to_string (specBoost.empty() ? 0.0f : specBoost[(size_t) bin8k]));
    }

    //--------------------------------------------------------------------------
    std::cout << "\n[C] Stage 2 COMP only\n";
    {
        setDefaultsForIsolation (proc);
        setParam (proc, "upratio", 1.0f);      // boost off
        setParam (proc, "downratio", 8.0f);    // comp on
        setParam (proc, "highthresh", -18.0f);
        setParam (proc, "gratk", 0.0f);
        setParam (proc, "grrel", 0.0f);
        proc.reset();

        // Loud sine (~ -6 dBFS peak) — above -18 thresh
        const float loudAmp = juce::Decibels::decibelsToGain (-6.0f);
        const float outPeak = steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96);
        const float outDb = juce::Decibels::gainToDecibels (outPeak, -120.0f);
        const float grMeter = proc.getMeterGrDb();

        expect (outDb < -6.0f - 1.5f,
                "Loud sine is compressed downward",
                "in=-6 dBFS out=" + juce::String (outDb, 1).toStdString() + " dBFS");
        expect (grMeter > 1.0f,
                "GR meter reports reduction",
                "meter=" + juce::String (grMeter, 1).toStdString() + " dB");
        expect (proc.getMeterBoostDb() < 0.5f,
                "BOOST meter stays idle while Boost Ratio=1",
                "boost=" + juce::String (proc.getMeterBoostDb(), 2).toStdString());
    }

    //--------------------------------------------------------------------------
    std::cout << "\n[D] Boost thresh does not fake Comp activity\n";
    {
        setDefaultsForIsolation (proc);
        setParam (proc, "upratio", 4.0f);
        setParam (proc, "downratio", 1.0f); // comp fully bypassed
        setParam (proc, "lowthresh", -40.0f);
        proc.reset();
        (void) steadyPeak (proc, buf, sr, 1000.0f, juce::Decibels::decibelsToGain (-50.0f), 64);

        const float grBefore = proc.getMeterGrDb();

        setParam (proc, "lowthresh", -15.0f); // move boost thresh a lot
        (void) steadyPeak (proc, buf, sr, 1000.0f, juce::Decibels::decibelsToGain (-50.0f), 64);
        const float grAfter = proc.getMeterGrDb();

        expect (grBefore < 0.5f && grAfter < 0.5f,
                "Moving Boost Thresh with Comp Ratio=1 never lights GR",
                "grBefore=" + juce::String (grBefore, 2).toStdString()
                    + " grAfter=" + juce::String (grAfter, 2).toStdString());
    }

    //--------------------------------------------------------------------------
    std::cout << "\n[E] Stage 3 MIX\n";
    {
        setDefaultsForIsolation (proc);
        setParam (proc, "upratio", 1.0f);
        setParam (proc, "downratio", 8.0f);
        setParam (proc, "highthresh", -18.0f);
        setParam (proc, "makeup", 0.0f);
        setParam (proc, "gratk", 0.0f);
        setParam (proc, "grrel", 0.0f);

        // Wet-only compressed level
        setParam (proc, "mix", 1.0f);
        proc.reset();
        const float wetPeak = steadyPeak (proc, buf, sr, 1000.0f,
                                          juce::Decibels::decibelsToGain (-6.0f), 96);

        // Dry-only — should be ~ -6 dBFS (delayed), not compressed
        setParam (proc, "mix", 0.0f);
        proc.reset();
        const float dryPeak = steadyPeak (proc, buf, sr, 1000.0f,
                                          juce::Decibels::decibelsToGain (-6.0f), 96);
        const float dryDb = juce::Decibels::gainToDecibels (dryPeak, -120.0f);

        expect (std::abs (dryDb - (-6.0f)) < 1.5f,
                "Mix=0% ≈ delayed dry (no compression)",
                "dryOut=" + juce::String (dryDb, 1).toStdString() + " dBFS");
        expect (wetPeak < dryPeak * 0.85f,
                "Mix=100% is quieter than Mix=0% under Comp",
                "wet=" + juce::String (juce::Decibels::gainToDecibels (wetPeak, -120.0f), 1).toStdString()
                    + " dry=" + juce::String (dryDb, 1).toStdString());

        // Makeup on wet only: Mix=0 + Makeup=+12 must NOT boost dry
        setParam (proc, "mix", 0.0f);
        setParam (proc, "makeup", 12.0f);
        proc.reset();
        const float dryWithMakeup = steadyPeak (proc, buf, sr, 1000.0f,
                                                juce::Decibels::decibelsToGain (-12.0f), 96);
        const float dryWithMakeupDb = juce::Decibels::gainToDecibels (dryWithMakeup, -120.0f);
        expect (std::abs (dryWithMakeupDb - (-12.0f)) < 1.5f,
                "Mix=0% ignores Makeup (+12 dB)",
                "out=" + juce::String (dryWithMakeupDb, 1).toStdString() + " dBFS");

        setParam (proc, "mix", 1.0f);
        setParam (proc, "upratio", 1.0f);
        setParam (proc, "downratio", 1.0f);
        setParam (proc, "makeup", 12.0f);
        proc.reset();
        const float wetMakeup = steadyPeak (proc, buf, sr, 1000.0f,
                                            juce::Decibels::decibelsToGain (-12.0f), 96);
        const float wetMakeupDb = juce::Decibels::gainToDecibels (wetMakeup, -120.0f);
        expect (wetMakeupDb > -12.0f + 9.0f,
                "Mix=100% applies Makeup (+12 dB)",
                "out=" + juce::String (wetMakeupDb, 1).toStdString() + " dBFS");
    }

    //--------------------------------------------------------------------------
    std::cout << "\n[F] Serial Boost→Comp (loud after boost can engage Comp)\n";
    {
        setDefaultsForIsolation (proc);
        // Quiet tone: boost alone lifts it; with high ratio boost + low comp thresh,
        // post-boost level can cross into compression.
        setParam (proc, "upratio", 10.0f);
        setParam (proc, "lowthresh", -12.0f);
        setParam (proc, "downratio", 1.0f);
        setParam (proc, "boostatk", 0.0f);
        setParam (proc, "boostrel", 0.0f);
        proc.reset();

        const float amp = juce::Decibels::decibelsToGain (-24.0f);
        const float boostOnlyPeak = steadyPeak (proc, buf, sr, 1000.0f, amp, 96);
        const float boostOnlyDb = juce::Decibels::gainToDecibels (boostOnlyPeak, -120.0f);

        setParam (proc, "downratio", 12.0f);
        setParam (proc, "highthresh", -20.0f); // below boosted level if boost worked
        setParam (proc, "gratk", 0.0f);
        setParam (proc, "grrel", 0.0f);
        proc.reset();
        const float bothPeak = steadyPeak (proc, buf, sr, 1000.0f, amp, 96);
        const float bothDb = juce::Decibels::gainToDecibels (bothPeak, -120.0f);

        expect (boostOnlyDb > -24.0f + 2.0f,
                "Boost-only raises quiet tone",
                "out=" + juce::String (boostOnlyDb, 1).toStdString() + " dBFS");
        expect (bothDb < boostOnlyDb - 0.5f,
                "Adding Comp after Boost reduces level vs boost-only",
                "boostOnly=" + juce::String (boostOnlyDb, 1).toStdString()
                    + " both=" + juce::String (bothDb, 1).toStdString());
        expect (proc.getMeterGrDb() > 0.5f,
                "GR meter active when Comp engages post-boost",
                "gr=" + juce::String (proc.getMeterGrDb(), 1).toStdString());
    }

    //--------------------------------------------------------------------------
    std::cout << "\n[G] Silence / mute meters\n";
    {
        setDefaultsForIsolation (proc);
        setParam (proc, "upratio", 4.0f);
        setParam (proc, "lowthresh", -30.0f);
        proc.reset();
        (void) steadyPeak (proc, buf, sr, 1000.0f, juce::Decibels::decibelsToGain (-40.0f), 32);

        for (int i = 0; i < 32; ++i)
        {
            fillSilence (buf);
            proc.processBlock (buf, midi);
        }

        expect (proc.getMeterBoostDb() < 0.2f && proc.getMeterGrDb() < 0.2f,
                "Meters clear on sustained silence",
                "boost=" + juce::String (proc.getMeterBoostDb(), 2).toStdString()
                    + " gr=" + juce::String (proc.getMeterGrDb(), 2).toStdString());
    }

    //--------------------------------------------------------------------------
    // Threshold vs input peak-dBFS (pure sine).
    // Boost/Comp thresholds are per-bin dBFS; for a peak-sine the OUTPUT level
    // should move toward (Boost) or away downward from (Comp) that thresh.
    std::cout << "\n[H] Threshold ↔ input dBFS correspondence (pure sine)\n";
    {
        constexpr float kToneDb = -24.0f;
        const float amp = juce::Decibels::decibelsToGain (kToneDb);

        setDefaultsForIsolation (proc);
        setParam (proc, "downratio", 1.0f);
        setParam (proc, "upratio", 20.0f);
        setParam (proc, "boostatk", 0.0f);
        setParam (proc, "boostrel", 0.0f);

        // Thresh above input → pull output up toward thresh
        setParam (proc, "lowthresh", -12.0f);
        proc.reset();
        const float outPull = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, amp, 128), -120.0f);
        expect (outPull > kToneDb + 6.0f && outPull < -12.0f + 4.0f,
                "Boost thresh -12 pulls -24 dBFS sine upward toward -12",
                "out=" + juce::String (outPull, 1).toStdString() + " dBFS");

        // Thresh below input → main tone stays (~unity for that bin)
        setParam (proc, "lowthresh", -36.0f);
        proc.reset();
        const float outStay = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, amp, 128), -120.0f);
        expect (std::abs (outStay - kToneDb) < 2.0f,
                "Boost thresh -36 leaves -24 dBFS sine near unity",
                "out=" + juce::String (outStay, 1).toStdString() + " dBFS");

        // Meter: engage vs idle for main-tone-relevant bins
        setParam (proc, "lowthresh", -12.0f);
        proc.reset();
        (void) steadyPeak (proc, buf, sr, 1000.0f, amp, 96);
        const float meterEngage = proc.getMeterBoostDb();

        setParam (proc, "lowthresh", -36.0f);
        proc.reset();
        (void) steadyPeak (proc, buf, sr, 1000.0f, amp, 96);
        const float meterIdle = proc.getMeterBoostDb();

        expect (meterEngage > 3.0f,
                "BOOST meter active when thresh is above input",
                "meter=" + juce::String (meterEngage, 1).toStdString() + " dB");
        expect (meterIdle < 1.0f,
                "BOOST meter idle when thresh is below input (main tone)",
                "meter=" + juce::String (meterIdle, 2).toStdString() + " dB");

        // Comp
        setDefaultsForIsolation (proc);
        setParam (proc, "upratio", 1.0f);
        setParam (proc, "downratio", 20.0f);
        setParam (proc, "gratk", 0.0f);
        setParam (proc, "grrel", 0.0f);

        constexpr float kLoudDb = -6.0f;
        const float loudAmp = juce::Decibels::decibelsToGain (kLoudDb);

        setParam (proc, "highthresh", -18.0f);
        proc.reset();
        const float outComp = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 128), -120.0f);
        expect (outComp < kLoudDb - 4.0f,
                "Comp thresh -18 pulls -6 dBFS sine downward",
                "out=" + juce::String (outComp, 1).toStdString() + " dBFS");

        setParam (proc, "highthresh", 0.0f);
        proc.reset();
        const float outNoComp = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 128), -120.0f);
        expect (std::abs (outNoComp - kLoudDb) < 1.5f,
                "Comp thresh 0 dBFS leaves -6 dBFS sine alone",
                "out=" + juce::String (outNoComp, 1).toStdString() + " dBFS");
    }

    proc.releaseResources();

    std::cout << "\n[I] Separate expand/compress tessituras (1 Hz-96 kHz) and waveform\n";
    {
        auto setAllTess = [] (NewProjectAudioProcessor& p, bool on)
        {
            for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
            {
                setParam (p, ("tessExp" + std::to_string (i)).c_str(), on ? 1.0f : 0.0f);
                setParam (p, ("tessComp" + std::to_string (i)).c_str(), on ? 1.0f : 0.0f);
            }
        };
        auto setAllExp = [] (NewProjectAudioProcessor& p, bool on)
        {
            for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
                setParam (p, ("tessExp" + std::to_string (i)).c_str(), on ? 1.0f : 0.0f);
        };
        auto setAllComp = [] (NewProjectAudioProcessor& p, bool on)
        {
            for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
                setParam (p, ("tessComp" + std::to_string (i)).c_str(), on ? 1.0f : 0.0f);
        };

        expect (proc.getAPVTS().getParameter ("tessExp5") != nullptr
                    && proc.getAPVTS().getParameter ("tessComp5") != nullptr,
                "Separate Expand and Compress tessitura parameters exist");
        expect (proc.getAPVTS().getParameter ("tess0") == nullptr,
                "Legacy shared tessitura parameters are gone");

        setDefaultsForIsolation (proc);
        setParam (proc, "upratio", 1.0f);
        setParam (proc, "downratio", 1.0f);
        setAllTess (proc, true);
        proc.reset();
        const float loudAmp = juce::Decibels::decibelsToGain (-6.0f);
        const float bypassDb = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96), -120.0f);

        setParam (proc, "downratio", 8.0f);
        setParam (proc, "highthresh", -18.0f);
        setParam (proc, "gratk", 0.0f);
        setParam (proc, "grrel", 0.0f);
        setAllTess (proc, false);
        setParam (proc, "tessComp5", 1.0f); // 1-2 kHz, contains 1000 Hz
        proc.reset();
        const float inBandDb = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96), -120.0f);

        setAllTess (proc, false);
        setParam (proc, "tessComp0", 1.0f); // 1-60 Hz, does not contain 1000 Hz
        proc.reset();
        const float outBandDb = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96), -120.0f);

        expect (inBandDb < bypassDb - 1.5f,
                "Compress tessitura 1-2 kHz compresses a 1000 Hz sine",
                "bypass " + std::to_string (bypassDb) + " in " + std::to_string (inBandDb));
        expect (std::abs (outBandDb - bypassDb) < 2.0f,
                "A compress tessitura outside the tone does not compress it",
                "bypass " + std::to_string (bypassDb) + " out " + std::to_string (outBandDb));
        expect (proc.getTessituraGrDb (0) < 0.5f,
                "The unused compress tessitura reports no reduction",
                "gr " + std::to_string (proc.getTessituraGrDb (0)));

        setAllTess (proc, false);
        setParam (proc, "tessComp5", 1.0f);
        proc.reset();
        (void) steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96);

        std::vector<float> waveIn, waveOut, waveGr;
        proc.copyWaveform (waveIn, waveOut, waveGr);
        float inComp = 0.0f, outComp = 0.0f, grPeak = 0.0f;
        int compPoints = 0;
        for (size_t i = 0; i < waveIn.size(); ++i)
        {
            if (waveGr[i] <= 0.5f)
                continue;
            ++compPoints;
            inComp = std::max (inComp, std::abs (waveIn[i]));
            outComp = std::max (outComp, std::abs (waveOut[i]));
            grPeak = std::max (grPeak, waveGr[i]);
        }
        std::vector<float> specMag, specGr;
        double specSr = 0.0;
        int specN = 0;
        proc.copySpectrum (specMag, specGr, specSr, specN);
        const int bin1k = specN > 0 ? juce::jlimit (0, (int) specGr.size() - 1,
                                                    (int) std::lround (1000.0 / sr * (double) specN)) : 0;
        const int bin8k = specN > 0 ? juce::jlimit (0, (int) specGr.size() - 1,
                                                    (int) std::lround (8000.0 / sr * (double) specN)) : 0;
        expect (! specGr.empty() && specGr[(size_t) bin1k] > 0.5f && specGr[(size_t) bin8k] < 0.5f,
                "The spectrum shows compression at 1000 Hz and not at 8 kHz",
                "1k " + std::to_string (specGr.empty() ? 0.0f : specGr[(size_t) bin1k])
                    + " 8k " + std::to_string (specGr.empty() ? 0.0f : specGr[(size_t) bin8k]));

        expect (! waveIn.empty() && compPoints > 8 && grPeak > 0.5f && outComp < inComp,
                "Waveform shows the input, the output, and where compression happens",
                "n " + std::to_string (waveIn.size()) + " points " + std::to_string (compPoints)
                    + " in " + std::to_string (inComp) + " out " + std::to_string (outComp)
                    + " gr " + std::to_string (grPeak));
        expect (proc.getTessituraGrDb (5) > 0.5f,
                "The selected compress tessitura reports the reduction",
                "gr " + std::to_string (proc.getTessituraGrDb (5)));

        // Expand-only band: compress row off must not reduce, expand row on must boost.
        setParam (proc, "downratio", 8.0f);
        setParam (proc, "highthresh", -18.0f);
        setParam (proc, "upratio", 8.0f);
        setParam (proc, "lowthresh", -3.0f);
        setParam (proc, "boostatk", 0.0f);
        setParam (proc, "boostrel", 0.0f);
        setParam (proc, "gratk", 0.0f);
        setParam (proc, "grrel", 0.0f);
        setAllExp (proc, false);
        setAllComp (proc, false);
        setParam (proc, "tessExp5", 1.0f); // expand 1-2 kHz only
        proc.reset();
        (void) steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96);
        std::vector<float> sepMag, sepGr, sepBoost;
        proc.copySpectrum (sepMag, sepGr, sepBoost, specSr, specN);
        expect (proc.getTessituraBoostDb (5) > 0.5f
                    && proc.getTessituraGrDb (5) < 0.5f
                    && ! sepBoost.empty()
                    && sepBoost[(size_t) bin1k] > 0.5f
                    && (sepGr.empty() || sepGr[(size_t) bin1k] < 0.5f),
                "Expand tessitura alone expands without compressing",
                "boost " + std::to_string (proc.getTessituraBoostDb (5))
                    + " gr " + std::to_string (proc.getTessituraGrDb (5)));

        setAllExp (proc, false);
        setAllComp (proc, false);
        setParam (proc, "tessComp5", 1.0f); // compress 1-2 kHz only
        setParam (proc, "upratio", 1.0f);
        proc.reset();
        (void) steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96);
        proc.copySpectrum (sepMag, sepGr, sepBoost, specSr, specN);
        expect (proc.getTessituraGrDb (5) > 0.5f
                    && proc.getTessituraBoostDb (5) < 0.5f
                    && ! sepGr.empty()
                    && sepGr[(size_t) bin1k] > 0.5f,
                "Compress tessitura alone compresses without expanding",
                "gr " + std::to_string (proc.getTessituraGrDb (5))
                    + " boost " + std::to_string (proc.getTessituraBoostDb (5)));

        setParam (proc, "downratio", 1.0f);
        setParam (proc, "upratio", 8.0f);
        setParam (proc, "lowthresh", -3.0f);
        setParam (proc, "boostatk", 0.0f);
        setParam (proc, "boostrel", 0.0f);
        setAllTess (proc, false);
        setParam (proc, "tessExp5", 1.0f);
        proc.reset();
        (void) steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96);
        std::vector<float> expMag, expGr, expBoost;
        proc.copySpectrum (expMag, expGr, expBoost, specSr, specN);
        const int expBin1k = specN > 0 ? juce::jlimit (0, (int) expBoost.size() - 1,
                                                       (int) std::lround (1000.0 / sr * (double) specN)) : 0;
        const int expBin8k = specN > 0 ? juce::jlimit (0, (int) expBoost.size() - 1,
                                                       (int) std::lround (8000.0 / sr * (double) specN)) : 0;
        expect (proc.getTessituraBoostDb (5) > 0.5f
                    && ! expBoost.empty()
                    && expBoost[(size_t) expBin1k] > 0.5f
                    && expBoost[(size_t) expBin8k] < 0.5f,
                "Expand tessitura 1-2 kHz expands a 1000 Hz sine and not 8 kHz",
                "band " + std::to_string (proc.getTessituraBoostDb (5))
                    + " 1k " + std::to_string (expBoost.empty() ? 0.0f : expBoost[(size_t) expBin1k]));

        setAllTess (proc, false);
        setParam (proc, "tessExp0", 1.0f);
        proc.reset();
        (void) steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96);
        proc.copySpectrum (expMag, expGr, expBoost, specSr, specN);
        expect (proc.getTessituraBoostDb (5) < 0.5f
                    && (expBoost.empty() || expBoost[(size_t) expBin1k] < 0.5f),
                "An expand tessitura outside the tone does not expand it",
                "band " + std::to_string (proc.getTessituraBoostDb (5))
                    + " 1k " + std::to_string (expBoost.empty() ? 0.0f : expBoost[(size_t) expBin1k]));
    }

    //--------------------------------------------------------------------------
    std::cout << "\n[J] Auto Gain matches wet level to dry after compression\n";
    {
        expect (proc.getAPVTS().getParameter ("autoGain") != nullptr,
                "Auto Gain parameter exists");

        // Re-enable every expand/compress tessitura — section [I] may have left only one on.
        for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
        {
            setParam (proc, ("tessExp" + std::to_string (i)).c_str(), 1.0f);
            setParam (proc, ("tessComp" + std::to_string (i)).c_str(), 1.0f);
        }

        setDefaultsForIsolation (proc);
        setParam (proc, "upratio", 1.0f);
        setParam (proc, "downratio", 12.0f);
        setParam (proc, "highthresh", -18.0f);
        setParam (proc, "gratk", 0.0f);
        setParam (proc, "grrel", 0.0f);
        setParam (proc, "mix", 1.0f);
        setParam (proc, "makeup", 0.0f);

        const float amp = juce::Decibels::decibelsToGain (-6.0f);

        setParam (proc, "autoGain", 0.0f);
        proc.reset();
        const float compressed = steadyPeak (proc, buf, sr, 1000.0f, amp, 120);
        const float compressedDb = juce::Decibels::gainToDecibels (compressed, -120.0f);

        setParam (proc, "autoGain", 1.0f);
        proc.reset();
        const float ag = steadyPeak (proc, buf, sr, 1000.0f, amp, 200);
        const float agDb = juce::Decibels::gainToDecibels (ag, -120.0f);

        expect (compressedDb < -6.0f - 1.5f,
                "Without Auto Gain, compressed tone is quieter",
                "out=" + juce::String (compressedDb, 1).toStdString() + " dBFS");
        expect (std::abs (agDb - (-6.0f)) < 3.0f,
                "With Auto Gain, output ≈ input level",
                "out=" + juce::String (agDb, 1).toStdString() + " dBFS");
        expect (ag > compressed * 1.15f,
                "Auto Gain raises level vs compressed-only",
                "ag=" + juce::String (agDb, 1).toStdString()
                    + " comp=" + juce::String (compressedDb, 1).toStdString());

        // Expander still works with Auto Gain off
        setParam (proc, "autoGain", 0.0f);
        setParam (proc, "downratio", 1.0f);
        setParam (proc, "upratio", 8.0f);
        setParam (proc, "lowthresh", -12.0f);
        setParam (proc, "boostatk", 0.0f);
        setParam (proc, "boostrel", 0.0f);
        proc.reset();
        const float quiet = juce::Decibels::decibelsToGain (-24.0f);
        const float boosted = steadyPeak (proc, buf, sr, 1000.0f, quiet, 96);
        expect (juce::Decibels::gainToDecibels (boosted, -120.0f) > -24.0f + 2.0f,
                "Expander still raises quiet tone (Auto Gain off)",
                "out=" + juce::String (juce::Decibels::gainToDecibels (boosted, -120.0f), 1).toStdString()
                    + " dBFS");
    }

    //--------------------------------------------------------------------------
    std::cout << "\n[K] Spectrum axis 1 Hz-96 kHz and extreme tessituras\n";
    {
        expect (NewProjectAudioProcessor::kSpectrumMinHz == 1.0f
                    && NewProjectAudioProcessor::kSpectrumMaxHz == 96000.0f,
                "Spectrum axis constants are 1 Hz and 96 kHz",
                "min=" + std::to_string (NewProjectAudioProcessor::kSpectrumMinHz)
                    + " max=" + std::to_string (NewProjectAudioProcessor::kSpectrumMaxHz));

        auto* expLo = proc.getAPVTS().getParameter ("tessExp0");
        auto* expHi = proc.getAPVTS().getParameter ("tessExp9");
        auto* compLo = proc.getAPVTS().getParameter ("tessComp0");
        auto* compHi = proc.getAPVTS().getParameter ("tessComp9");
        expect (expLo != nullptr && expHi != nullptr && compLo != nullptr && compHi != nullptr,
                "Extreme expand/compress tessitura parameters exist");
        if (expLo != nullptr && expHi != nullptr)
        {
            expect (expLo->getName (64).containsIgnoreCase ("1-60")
                        && expHi->getName (64).containsIgnoreCase ("20-96"),
                    "Tessitura names cover 1-60 Hz and 20-96 kHz",
                    "lo=" + expLo->getName (64).toStdString()
                        + " hi=" + expHi->getName (64).toStdString());
        }

        // At 48 kHz, only the audible mid band must affect 1 kHz — not the 20-96 kHz row.
        for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
        {
            setParam (proc, ("tessExp" + std::to_string (i)).c_str(), 0.0f);
            setParam (proc, ("tessComp" + std::to_string (i)).c_str(), 0.0f);
        }
        setDefaultsForIsolation (proc);
        setParam (proc, "upratio", 1.0f);
        setParam (proc, "downratio", 8.0f);
        setParam (proc, "highthresh", -18.0f);
        setParam (proc, "gratk", 0.0f);
        setParam (proc, "grrel", 0.0f);
        setParam (proc, "tessComp9", 1.0f); // 20-96 kHz only
        proc.reset();
        const float loudAmp = juce::Decibels::decibelsToGain (-6.0f);
        const float outHiOnly = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96), -120.0f);
        expect (std::abs (outHiOnly - (-6.0f)) < 2.0f
                    && proc.getTessituraGrDb (9) < 0.5f
                    && proc.getTessituraGrDb (5) < 0.5f,
                "At 48 kHz, 20-96 kHz compress band leaves 1 kHz alone",
                "out=" + juce::String (outHiOnly, 1).toStdString()
                    + " gr9=" + juce::String (proc.getTessituraGrDb (9), 2).toStdString());

        setParam (proc, "tessComp9", 0.0f);
        setParam (proc, "tessComp0", 1.0f); // 1-60 Hz only
        proc.reset();
        const float outLoOnly = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 1000.0f, loudAmp, 96), -120.0f);
        expect (std::abs (outLoOnly - (-6.0f)) < 2.0f,
                "At 48 kHz, 1-60 Hz compress band leaves 1 kHz alone",
                "out=" + juce::String (outLoOnly, 1).toStdString());

        // 192 kHz: the 20-96 kHz band can hear a 40 kHz tone.
        constexpr double srHi = 192000.0;
        constexpr int blockHi = 512;
        NewProjectAudioProcessor procHi;
        procHi.prepareToPlay (srHi, blockHi);
        procHi.applyFftSizeFromParameterLocked (true);
        juce::AudioBuffer<float> bufHi (2, blockHi);

        for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
        {
            setParam (procHi, ("tessExp" + std::to_string (i)).c_str(), 0.0f);
            setParam (procHi, ("tessComp" + std::to_string (i)).c_str(), 0.0f);
        }
        setDefaultsForIsolation (procHi);
        setParam (procHi, "upratio", 1.0f);
        setParam (procHi, "downratio", 1.0f);
        setParam (procHi, "mix", 1.0f);
        setParam (procHi, "tessComp9", 1.0f);
        procHi.reset();
        const float amp40 = juce::Decibels::decibelsToGain (-6.0f);
        const float bypass40 = juce::Decibels::gainToDecibels (
            steadyPeak (procHi, bufHi, srHi, 40000.0f, amp40, 160), -120.0f);

        setParam (procHi, "downratio", 10.0f);
        setParam (procHi, "highthresh", -18.0f);
        setParam (procHi, "gratk", 0.0f);
        setParam (procHi, "grrel", 0.0f);
        procHi.reset();
        const float compressed40 = juce::Decibels::gainToDecibels (
            steadyPeak (procHi, bufHi, srHi, 40000.0f, amp40, 160), -120.0f);

        expect (compressed40 < bypass40 - 1.5f && procHi.getTessituraGrDb (9) > 0.5f,
                "At 192 kHz, 20-96 kHz band compresses a 40 kHz sine",
                "bypass=" + juce::String (bypass40, 1).toStdString()
                    + " out=" + juce::String (compressed40, 1).toStdString()
                    + " gr9=" + juce::String (procHi.getTessituraGrDb (9), 1).toStdString());

        setParam (procHi, "tessComp9", 0.0f);
        setParam (procHi, "tessComp5", 1.0f); // 1-2 kHz only
        procHi.reset();
        const float midOnly40 = juce::Decibels::gainToDecibels (
            steadyPeak (procHi, bufHi, srHi, 40000.0f, amp40, 160), -120.0f);
        expect (std::abs (midOnly40 - bypass40) < 2.5f && procHi.getTessituraGrDb (9) < 0.5f,
                "At 192 kHz, 1-2 kHz band does not compress 40 kHz",
                "bypass=" + juce::String (bypass40, 1).toStdString()
                    + " out=" + juce::String (midOnly40, 1).toStdString());

        std::vector<float> magHi, grHi, boostHi;
        double pubSr = 0.0;
        int pubN = 0;
        procHi.copySpectrum (magHi, grHi, boostHi, pubSr, pubN);
        const float binHz = pubN > 0 ? (float) pubSr / (float) pubN : 0.0f;
        const float topBinHz = pubN > 1 ? binHz * (float) (pubN / 2) : 0.0f;
        expect (std::abs (pubSr - srHi) < 1.0
                    && topBinHz >= 90000.0f,
                "At 192 kHz the published spectrum reaches near 96 kHz",
                "sr=" + juce::String (pubSr, 0).toStdString()
                    + " topBin=" + juce::String (topBinHz, 0).toStdString() + " Hz");

        // Expand-only on the low band at 48 kHz still works after the high-SR processor.
        for (int i = 0; i < NewProjectAudioProcessor::kTessituras; ++i)
        {
            setParam (proc, ("tessExp" + std::to_string (i)).c_str(), 0.0f);
            setParam (proc, ("tessComp" + std::to_string (i)).c_str(), 0.0f);
        }
        setDefaultsForIsolation (proc);
        setParam (proc, "downratio", 1.0f);
        setParam (proc, "upratio", 8.0f);
        setParam (proc, "lowthresh", -12.0f);
        setParam (proc, "boostatk", 0.0f);
        setParam (proc, "boostrel", 0.0f);
        setParam (proc, "tessExp0", 1.0f); // 1-60 Hz expand only
        proc.reset();
        const float subQuiet = juce::Decibels::decibelsToGain (-24.0f);
        const float subOut = juce::Decibels::gainToDecibels (
            steadyPeak (proc, buf, sr, 40.0f, subQuiet, 128), -120.0f);
        expect (subOut > -24.0f + 2.0f && proc.getTessituraBoostDb (0) > 0.5f,
                "1-60 Hz expand band boosts a 40 Hz quiet sine",
                "out=" + juce::String (subOut, 1).toStdString()
                    + " boost0=" + juce::String (proc.getTessituraBoostDb (0), 1).toStdString());

        procHi.releaseResources();
    }

    proc.releaseResources();

    std::cout << "\n=== Result: " << (gFails == 0 ? "ALL PASS" : "FAILURES")
              << " (" << gFails << " failed) ===\n\n";
    return gFails == 0 ? 0 : 1;
}
