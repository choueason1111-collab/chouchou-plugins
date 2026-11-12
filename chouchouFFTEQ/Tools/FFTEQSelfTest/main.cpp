/*
  Each ChouchouFFTEQ function is exercised once.
*/

#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
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

void setParam (ChouchouFFTEQAudioProcessor& p, const char* id, float plain)
{
    auto* param = p.getAPVTS().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

void setParam (const std::unique_ptr<ChouchouFFTEQAudioProcessor>& p, const char* id, float plain)
{
    setParam (*p, id, plain);
}

std::vector<float> sine (int n, double hz, float amp = 0.25f)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = amp * (float) std::sin (2.0 * kPi * hz * (double) i / kSr);
    return x;
}

std::vector<float> harmonics (int n)
{
    std::vector<float> x ((size_t) n, 0.0f);
    const double f0 = 150.0;
    const float amps[] { 1.0f, 0.75f, 0.55f, 0.4f, 0.3f, 0.22f };
    for (int h = 0; h < 6; ++h)
        for (int i = 0; i < n; ++i)
            x[(size_t) i] += 0.08f * amps[h] * (float) std::sin (2.0 * kPi * f0 * (double) (h + 1) * (double) i / kSr);
    return x;
}

std::vector<float> twoTone (int n, double aHz, float aAmp, double bHz, float bAmp)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = aAmp * (float) std::sin (2.0 * kPi * aHz * i / kSr)
                      + bAmp * (float) std::sin (2.0 * kPi * bHz * i / kSr);
    return x;
}

struct Rig
{
    std::unique_ptr<ChouchouFFTEQAudioProcessor> p;
    bool sidechain = false;

    explicit Rig (bool withSidechain = false) : sidechain (withSidechain)
    {
        p = std::make_unique<ChouchouFFTEQAudioProcessor>();
        if (withSidechain)
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
            p->setBusesLayout (layout);
        }
    }

    void prepare() { p->prepareToPlay (kSr, 256); }

    std::vector<float> render (const std::vector<float>& in, const std::vector<float>* sc = nullptr)
    {
        const int n = (int) in.size();
        const int ch = sidechain ? 4 : 2;
        juce::AudioBuffer<float> buffer (ch, n);
        buffer.clear();
        for (int i = 0; i < n; ++i)
        {
            buffer.setSample (0, i, in[(size_t) i]);
            buffer.setSample (1, i, in[(size_t) i]);
            if (sidechain && sc != nullptr)
            {
                buffer.setSample (2, i, (*sc)[(size_t) i]);
                buffer.setSample (3, i, (*sc)[(size_t) i]);
            }
        }
        juce::MidiBuffer midi;
        p->processBlock (buffer, midi);
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = buffer.getSample (0, i);
        return out;
    }

    void renderStereo (const std::vector<float>& in, std::vector<float>& left, std::vector<float>& right)
    {
        const int n = (int) in.size();
        juce::AudioBuffer<float> buffer (2, n);
        for (int i = 0; i < n; ++i)
        {
            buffer.setSample (0, i, in[(size_t) i]);
            buffer.setSample (1, i, in[(size_t) i]);
        }
        juce::MidiBuffer midi;
        p->processBlock (buffer, midi);
        left.resize ((size_t) n);
        right.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            left[(size_t) i] = buffer.getSample (0, i);
            right[(size_t) i] = buffer.getSample (1, i);
        }
    }
};

double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b, int from)
{
    double m = 0.0;
    const int n = (int) std::min (a.size(), b.size());
    for (int i = from; i < n; ++i)
        m = std::max (m, (double) std::abs (a[(size_t) i] - b[(size_t) i]));
    return m;
}

double delayedError (const std::vector<float>& out, const std::vector<float>& in, int latency)
{
    double m = 0.0;
    for (int i = latency; i < (int) out.size(); ++i)
        m = std::max (m, (double) std::abs (out[(size_t) i] - in[(size_t) (i - latency)]));
    return m;
}

double rms (const std::vector<float>& x, int from)
{
    double s = 0.0;
    int n = 0;
    for (int i = from; i < (int) x.size(); ++i)
    {
        s += (double) x[(size_t) i] * (double) x[(size_t) i];
        ++n;
    }
    return n > 0 ? std::sqrt (s / (double) n) : 0.0;
}

double goertzel (const std::vector<float>& x, int from, int len, double hz)
{
    const double w = 2.0 * kPi * hz / kSr;
    const double coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < len; ++i)
    {
        const double s0 = (double) x[(size_t) (from + i)] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

double peakHz (const std::vector<float>& x, int from, int len, double lo, double hi, double step = 5.0)
{
    double bestHz = lo, best = -1.0;
    for (double h = lo; h <= hi; h += step)
    {
        const double e = goertzel (x, from, len, h);
        if (e > best)
        {
            best = e;
            bestHz = h;
        }
    }
    return bestHz;
}

int argmaxBin (const std::vector<float>& m)
{
    int k = 1;
    for (int i = 1; i < (int) m.size(); ++i)
        if (m[(size_t) i] > m[(size_t) k])
            k = i;
    return k;
}

float maxOf (const std::vector<float>& m)
{
    float v = 0.0f;
    for (float x : m)
        v = std::max (v, x);
    return v;
}

void armEqBand (Rig& r, int index, float freq, float gainDb, bool on = true)
{
    setParam (r.p, "eqOn", 1.0f);
    setParam (r.p, "bandCount", (float) (index + 1));
    const auto id = "b" + std::to_string (index);
    setParam (r.p, (id + "on").c_str(), on ? 1.0f : 0.0f);
    setParam (r.p, (id + "type").c_str(), 0.0f);
    setParam (r.p, (id + "freq").c_str(), freq);
    setParam (r.p, (id + "gain").c_str(), gainDb);
    setParam (r.p, (id + "q").c_str(), 2.0f);
}
} // namespace

int main()
{
    std::cout << "chouchouFFTEQ\n";
    const int n = 24000;
    const auto tone440 = sine (n, 440.0);
    const auto tone300 = sine (n, 300.0);
    const auto tone2000 = sine (n, 2000.0);
    const int tail = 12000;

    {
        Rig r;
        r.prepare();
        const auto out = r.render (tone440);
        const int latency = r.p->getLatencySamples();
        const double err = delayedError (out, tone440, latency);
        expect (latency == 1024 && err < 1.0e-5, "neutre : sortie = entree retardee",
                "lat " + std::to_string (latency) + " err " + std::to_string (err));
    }

    {
        Rig r;
        setParam (r.p, "fftSize", 2.0f);
        r.prepare();
        const auto out = r.render (tone440);
        const double err = delayedError (out, tone440, r.p->getLatencySamples());
        expect (r.p->getFftSize() == 2048 && err < 1.0e-5, "neutre aussi en FFT 2048",
                "err " + std::to_string (err));
    }

    {
        Rig boosted;
        armEqBand (boosted, 0, 440.0f, 18.0f);
        boosted.prepare();
        const auto loud = boosted.render (tone440);

        Rig flat;
        armEqBand (flat, 0, 440.0f, 0.0f);
        flat.prepare();
        const auto unity = flat.render (tone440);

        Rig off;
        armEqBand (off, 0, 440.0f, 18.0f, false);
        off.prepare();
        const auto bypassed = off.render (tone440);

        Rig plain;
        plain.prepare();
        const auto dry = plain.render (tone440);

        const double loudR = rms (loud, tail) / rms (dry, tail);
        const double unityErr = maxAbsDiff (unity, dry, 1024);
        const double offErr = maxAbsDiff (bypassed, dry, 1024);
        expect (loudR > 3.0, "EQ crete active augmente le sinus", "rapport " + std::to_string (loudR));
        expect (unityErr < 1.0e-4, "EQ a 0 dB ne change rien", "err " + std::to_string (unityErr));
        expect (offErr < 1.0e-4, "bande EQ coupee ne change rien", "err " + std::to_string (offErr));
    }

    {
        Rig onlyHi;
        setParam (onlyHi.p, "eqOn", 1.0f);
        setParam (onlyHi.p, "bandCount", 2.0f);
        setParam (onlyHi.p, "b0on", 0.0f);
        setParam (onlyHi.p, "b0freq", 300.0f);
        setParam (onlyHi.p, "b0gain", 18.0f);
        setParam (onlyHi.p, "b1on", 1.0f);
        setParam (onlyHi.p, "b1type", 0.0f);
        setParam (onlyHi.p, "b1freq", 2000.0f);
        setParam (onlyHi.p, "b1gain", 18.0f);
        setParam (onlyHi.p, "b1q", 2.0f);
        onlyHi.prepare();
        const auto hi = onlyHi.render (tone2000);
        const auto lo = onlyHi.render (tone300);
        Rig plain;
        plain.prepare();
        const double hiR = rms (hi, tail) / rms (plain.render (tone2000), tail);
        const double loR = rms (lo, tail) / rms (plain.render (tone300), tail);
        expect (hiR > 3.0 && loR < 1.4, "la seconde bande agit seule",
                "haut " + std::to_string (hiR) + " bas " + std::to_string (loR));
    }

    {
        Rig r (true);
        setParam (r.p, "scMode", 2.0f);
        r.prepare();
        const auto sc = sine (n, 1000.0);
        r.render (tone440, &sc);
        std::vector<float> mainMag, scMag, refMag;
        r.p->copySpectra (mainMag, scMag, refMag);
        const double mainHz = (double) argmaxBin (mainMag) * kSr / 1024.0;
        const double scHz = (double) argmaxBin (scMag) * kSr / 1024.0;
        expect (r.p->getAnalysisHops() > 0 && maxOf (mainMag) > 1.0e-4f && maxOf (scMag) > 1.0e-4f
                    && std::abs (mainHz - 440.0) < 80.0 && std::abs (scHz - 1000.0) < 80.0,
                "spectres principal et Sidechain visibles",
                "main " + std::to_string (mainHz) + " sc " + std::to_string (scHz));
    }

    auto fftRender = [&] (const std::vector<float>& in, const char* id, float value)
    {
        Rig r;
        setParam (r.p, "fftOn", 1.0f);
        setParam (r.p, "mix", 1.0f);
        if (id != nullptr)
            setParam (r.p, id, value);
        r.prepare();
        return r.render (in);
    };

    const auto fftBase = fftRender (tone440, nullptr, 0.0f);
    const int specFrom = 8000;
    const int specLen = 4096;

    {
        const auto moved = fftRender (tone440, "pitch", 2.0f);
        const auto back = fftRender (tone440, "pitch", 1.0f);
        const double baseHz = peakHz (fftBase, specFrom, specLen, 80, 1200);
        const double movedHz = peakHz (moved, specFrom, specLen, 80, 1200);
        expect (std::abs (movedHz - baseHz) > 80.0, "pitch different de 1 decale la hauteur",
                std::to_string (baseHz) + " -> " + std::to_string (movedHz));
        expect (maxAbsDiff (back, fftBase, 1024) < 1.0e-4, "pitch remis a 1 retrouve l'identite");
    }

    {
        const auto harm = harmonics (n);
        const auto baseH = fftRender (harm, nullptr, 0.0f);
        const auto formed = fftRender (harm, "formant", 2.0f);
        const auto back = fftRender (harm, "formant", 1.0f);
        const double f0Base = peakHz (baseH, specFrom, specLen, 120, 190, 2);
        const double f0Form = peakHz (formed, specFrom, specLen, 120, 190, 2);
        const double e200 = goertzel (baseH, specFrom, specLen, 300.0);
        const double e200b = goertzel (formed, specFrom, specLen, 300.0);
        const double e900 = goertzel (baseH, specFrom, specLen, 900.0);
        const double e900b = goertzel (formed, specFrom, specLen, 900.0);
        const double ratioBase = e900 / std::max (e200, 1.0e-12);
        const double ratioForm = e900b / std::max (e200b, 1.0e-12);
        expect (std::abs (ratioForm - ratioBase) / std::max (ratioBase, 1.0e-9) > 0.15
                    && std::abs (f0Form - f0Base) < 30.0,
                "formant change l'enveloppe sans le meme decalage de hauteur",
                "fondamental " + std::to_string (f0Base) + " -> " + std::to_string (f0Form)
                    + " rapport " + std::to_string (ratioBase) + " -> " + std::to_string (ratioForm));
        expect (maxAbsDiff (back, baseH, 1024) < 1.0e-3, "formant remis a 1 retrouve l'identite");
    }

    {
        const auto moved = fftRender (tone440, "offsetBins", 20.0f);
        const auto back = fftRender (tone440, "offsetBins", 0.0f);
        const double baseHz = peakHz (fftBase, specFrom, specLen, 80, 2000);
        const double movedHz = peakHz (moved, specFrom, specLen, 80, 2500);
        expect (std::abs (movedHz - baseHz) > 150.0, "offsetBins deplace le pic",
                std::to_string (baseHz) + " -> " + std::to_string (movedHz));
        expect (maxAbsDiff (back, fftBase, 1024) < 1.0e-4, "offsetBins remis a 0 retrouve l'identite");
    }

    {
        const auto louder = fftRender (tone440, "mulReel", 3.0f);
        const auto back = fftRender (tone440, "mulReel", 1.0f);
        const double ratio = rms (louder, tail) / std::max (rms (fftBase, tail), 1.0e-9);
        expect (ratio > 1.5, "gain reel modifie le niveau", "rapport " + std::to_string (ratio));
        expect (maxAbsDiff (back, fftBase, 1024) < 1.0e-4, "gain reel remis a 1 retrouve l'identite");
    }

    {
        const auto changed = fftRender (tone440, "mulImage", 0.0f);
        const auto back = fftRender (tone440, "mulImage", 1.0f);
        expect (maxAbsDiff (changed, fftBase, tail) > 0.01, "gain imaginaire modifie le signal");
        expect (maxAbsDiff (back, fftBase, 1024) < 1.0e-4, "gain imaginaire remis a 1 retrouve l'identite");
    }

    {
        Rig off;
        off.prepare();
        const auto plain = off.render (tone440);
        Rig moved;
        setParam (moved.p, "fftOn", 0.0f);
        setParam (moved.p, "mulReel", 3.0f);
        moved.prepare();
        const auto louder = moved.render (tone440);
        const double ratio = rms (louder, tail) / std::max (rms (plain, tail), 1.0e-9);
        expect (ratio < 1.05, "la case FFT eteinte coupe mulreel",
                "rapport " + std::to_string (ratio));

        Rig armed;
        setParam (armed.p, "fftOn", 1.0f);
        setParam (armed.p, "mulReel", 3.0f);
        armed.prepare();
        const auto on = armed.render (tone440);
        const double onRatio = rms (on, tail) / std::max (rms (plain, tail), 1.0e-9);
        expect (onRatio > 1.5, "la case FFT allumee laisse passer mulreel",
                "rapport " + std::to_string (onRatio));

        Rig shiftedOff;
        setParam (shiftedOff.p, "fftOn", 0.0f);
        setParam (shiftedOff.p, "experiment", 1.0f);
        setParam (shiftedOff.p, "shift", 2.0f);
        shiftedOff.prepare();
        const auto stayed = shiftedOff.render (tone440);
        expect (maxAbsDiff (stayed, plain, tail) < 0.02, "experience sans case FFT ne change pas le son");
    }

    {
        Rig shifted;
        setParam (shifted.p, "fftOn", 1.0f);
        setParam (shifted.p, "experiment", 1.0f);
        setParam (shifted.p, "shift", 2.0f);
        shifted.prepare();
        const auto moved = shifted.render (tone440);
        const double baseHz = peakHz (fftBase, specFrom, specLen, 80, 1200);
        const double movedHz = peakHz (moved, specFrom, specLen, 80, 1200);
        expect (std::abs (movedHz - baseHz) > 80.0, "1_shift decale la hauteur",
                std::to_string (baseHz) + " -> " + std::to_string (movedHz));
    }

    {
        Rig split;
        setParam (split.p, "fftOn", 1.0f);
        setParam (split.p, "experiment", 2.0f);
        setParam (split.p, "shiftimage", 0.5f);
        split.prepare();
        const auto moved = split.render (tone440);
        expect (maxAbsDiff (moved, fftBase, tail) > 0.01, "2_split avec shiftimage change le signal");
    }

    {
        Rig written;
        setParam (written.p, "fftOn", 1.0f);
        setParam (written.p, "experiment", 4.0f);
        setParam (written.p, "shift", 0.5f);
        written.prepare();
        const auto moved = written.render (tone440);
        const double baseHz = peakHz (fftBase, specFrom, specLen, 80, 1200);
        const double movedHz = peakHz (moved, specFrom, specLen, 80, 2000);
        expect (std::abs (movedHz - baseHz) > 80.0, "4_write deplace l'energie",
                std::to_string (baseHz) + " -> " + std::to_string (movedHz));
    }

    {
        const auto tones = twoTone (n, 200.0, 0.2f, 2000.0, 0.2f);
        const auto baseT = fftRender (tones, nullptr, 0.0f);
        const auto tilted = fftRender (tones, "tilt", 1.0f);
        const auto back = fftRender (tones, "tilt", 0.0f);
        const double relBase = goertzel (baseT, specFrom, specLen, 2000.0) / std::max (goertzel (baseT, specFrom, specLen, 200.0), 1.0e-12);
        const double relTilt = goertzel (tilted, specFrom, specLen, 2000.0) / std::max (goertzel (tilted, specFrom, specLen, 200.0), 1.0e-12);
        expect (relTilt > relBase * 1.3, "inclinaison penche le spectre",
                std::to_string (relBase) + " -> " + std::to_string (relTilt));
        expect (maxAbsDiff (back, baseT, 1024) < 1.0e-4, "inclinaison remise a 0 retrouve l'identite");
    }

    {
        const auto changed = fftRender (tone440, "contrast", 3.0f);
        const auto back = fftRender (tone440, "contrast", 1.0f);
        expect (maxAbsDiff (changed, fftBase, tail) > 0.01, "contraste modifie le signal");
        expect (maxAbsDiff (back, fftBase, 1024) < 1.0e-4, "contraste remis a 1 retrouve l'identite");
    }

    {
        const auto pair = twoTone (n, 440.0, 0.4f, 1800.0, 0.03f);
        const auto openG = fftRender (pair, nullptr, 0.0f);
        const auto gated = fftRender (pair, "gate", 0.35f);
        const auto back = fftRender (pair, "gate", 0.0f);
        const double weakOpen = goertzel (openG, specFrom, specLen, 1800.0);
        const double weakGate = goertzel (gated, specFrom, specLen, 1800.0);
        expect (weakGate < weakOpen * 0.5, "gate coupe le partiel faible",
                std::to_string (weakOpen) + " -> " + std::to_string (weakGate));
        expect (maxAbsDiff (back, openG, 1024) < 1.0e-4, "gate remis a 0 retrouve l'identite");
    }

    {
        const auto blurred = fftRender (tone440, "blur", 12.0f);
        const auto back = fftRender (tone440, "blur", 0.0f);
        const double focusBase = goertzel (fftBase, specFrom, specLen, 440.0) / std::max (goertzel (fftBase, specFrom, specLen, 560.0), 1.0e-9);
        const double focusBlur = goertzel (blurred, specFrom, specLen, 440.0) / std::max (goertzel (blurred, specFrom, specLen, 560.0), 1.0e-9);
        expect (focusBlur < focusBase * 0.7, "flou etale l'energie",
                std::to_string (focusBase) + " -> " + std::to_string (focusBlur));
        expect (maxAbsDiff (back, fftBase, 1024) < 1.0e-4, "flou remis a 0 retrouve l'identite");
    }

    {
        const auto harm = harmonics (n);
        const auto both = fftRender (harm, nullptr, 0.0f);
        Rig pitchOnly;
        setParam (pitchOnly.p, "fftOn", 1.0f);
        setParam (pitchOnly.p, "pitch", 1.5f);
        pitchOnly.prepare();
        const auto pOnly = pitchOnly.render (harm);
        Rig formOnly;
        setParam (formOnly.p, "fftOn", 1.0f);
        setParam (formOnly.p, "formant", 2.0f);
        formOnly.prepare();
        const auto fOnly = formOnly.render (harm);
        Rig together;
        setParam (together.p, "fftOn", 1.0f);
        setParam (together.p, "pitch", 1.5f);
        setParam (together.p, "formant", 2.0f);
        together.prepare();
        const auto combo = together.render (harm);
        juce::ignoreUnused (both);
        expect (maxAbsDiff (combo, pOnly, tail) > 0.005 && maxAbsDiff (combo, fOnly, tail) > 0.005,
                "pitch et formant ensemble different de chacun seul");
    }

    {
        Rig held;
        setParam (held.p, "fftOn", 1.0f);
        held.prepare();
        held.render (sine (16000, 440.0));
        setParam (held.p, "freeze", 1.0f);
        held.render (sine (2000, 440.0));
        const auto frozen = held.render (sine (16000, 2000.0));
        const double frozenHz = peakHz (frozen, 4000, 4096, 80, 3000);
        setParam (held.p, "freeze", 0.0f);
        const auto released = held.render (sine (16000, 2000.0));
        const double releasedHz = peakHz (released, 4000, 4096, 80, 3000);
        expect (std::abs (frozenHz - 440.0) < 80.0, "geler garde l'ancien spectre", std::to_string (frozenHz));
        expect (std::abs (releasedHz - 2000.0) < 120.0, "sans gel le spectre suit l'entree", std::to_string (releasedHz));
    }

    {
        Rig snap (true);
        setParam (snap.p, "scMode", 1.0f);
        snap.prepare();
        const auto scLow = sine (12000, 1000.0);
        const auto main = sine (12000, 440.0);
        snap.p->pulseCapture();
        snap.render (main, &scLow);
        const auto scHigh = sine (12000, 3000.0);
        snap.render (main, &scHigh);
        std::vector<float> mainMag, scMag, refMag;
        snap.p->copySpectra (mainMag, scMag, refMag);
        const double ref = (double) argmaxBin (refMag) * kSr / 1024.0;
        const double live = (double) argmaxBin (scMag) * kSr / 1024.0;
        expect (std::abs (ref - 1000.0) < 100.0 && std::abs (live - 3000.0) < 120.0,
                "analyse instantanee garde la reference",
                "ref " + std::to_string (ref) + " live " + std::to_string (live));
    }

    {
        Rig live (true);
        setParam (live.p, "scMode", 2.0f);
        live.prepare();
        const auto main = sine (12000, 440.0);
        const auto scA = sine (12000, 1000.0);
        const auto scB = sine (12000, 3000.0);
        live.render (main, &scA);
        live.render (main, &scB);
        std::vector<float> mainMag, scMag, refMag;
        live.p->copySpectra (mainMag, scMag, refMag);
        const double ref = (double) argmaxBin (refMag) * kSr / 1024.0;
        expect (std::abs (ref - 3000.0) < 120.0, "analyse temps reel suit le Sidechain", std::to_string (ref));
    }

    {
        const auto harm = harmonics (n);
        const auto sc = sine (n, 800.0, 0.5f);
        Rig off (true);
        setParam (off.p, "fftOn", 1.0f);
        setParam (off.p, "scMode", 2.0f);
        setParam (off.p, "scMatch", 1.0f);
        setParam (off.p, "scAmount", 0.0f);
        off.prepare();
        const auto dryMatch = off.render (harm, &sc);

        Rig plain;
        setParam (plain.p, "fftOn", 1.0f);
        plain.prepare();
        const auto noMatch = plain.render (harm);
        expect (maxAbsDiff (dryMatch, noMatch, tail) < 0.02, "quantite 0 ne copie pas le spectre");

        Rig bare;
        setParam (bare.p, "fftOn", 1.0f);
        setParam (bare.p, "scMatch", 1.0f);
        setParam (bare.p, "scAmount", 1.0f);
        setParam (bare.p, "scMode", 2.0f);
        bare.prepare();
        const auto unplugged = bare.render (harm);
        expect (maxAbsDiff (unplugged, noMatch, tail) < 0.02, "Sidechain non branche ne change rien");

        Rig copied (true);
        setParam (copied.p, "fftOn", 1.0f);
        setParam (copied.p, "scMode", 2.0f);
        setParam (copied.p, "scMatch", 1.0f);
        setParam (copied.p, "scAmount", 1.0f);
        copied.prepare();
        const auto shaped = copied.render (harm, &sc);
        const double relOff = goertzel (dryMatch, specFrom, specLen, 800.0) / std::max (goertzel (dryMatch, specFrom, specLen, 200.0), 1.0e-12);
        const double relOn = goertzel (shaped, specFrom, specLen, 800.0) / std::max (goertzel (shaped, specFrom, specLen, 200.0), 1.0e-12);
        expect (relOn > relOff * 1.25, "la copie rapproche l'enveloppe du Sidechain",
                std::to_string (relOff) + " -> " + std::to_string (relOn));

        std::vector<float> curve;
        int shownBands = -1;
        copied.p->copyMatch (curve, shownBands);
        float extreme = 0.0f;
        for (float v : curve)
            extreme = std::max (extreme, std::abs (v));
        expect (extreme > 0.3f && shownBands == 0, "sans bandes la copie s'affiche sur le spectre",
                "bandes " + std::to_string (shownBands) + " ecart " + std::to_string (extreme));

        auto sideRatio = [&] (int bandCount)
        {
            Rig r (true);
            setParam (r.p, "fftOn", 1.0f);
            setParam (r.p, "scMode", 2.0f);
            setParam (r.p, "scMatch", 1.0f);
            setParam (r.p, "scAmount", 1.0f);
            setParam (r.p, "scUseBands", 1.0f);
            setParam (r.p, "scBandN", (float) bandCount);
            r.prepare();
            const auto out = r.render (harm, &sc);
            const double ratio = goertzel (out, specFrom, specLen, 800.0)
                                 / std::max (goertzel (out, specFrom, specLen, 200.0), 1.0e-12);
            std::vector<float> drawn;
            int bands = -1;
            r.p->copyMatch (drawn, bands);
            float draw = 0.0f;
            for (float v : drawn)
                draw = std::max (draw, std::abs (v));
            return std::tuple<double, int, float> { ratio, bands, draw };
        };
        const auto few = sideRatio (1);
        const auto many = sideRatio (64);
        expect (std::get<1> (many) == 64 && std::get<2> (many) > 0.3f,
                "les bandes imitent le Sidechain et s'affichent",
                "n " + std::to_string (std::get<1> (many)) + " ecart " + std::to_string (std::get<2> (many)));
        expect (std::get<0> (many) > std::get<0> (few) * 8.0 && std::get<0> (many) < relOn * 8.0,
                "plus de bandes imite mieux le Sidechain",
                "1 " + std::to_string (std::get<0> (few)) + " 64 " + std::to_string (std::get<0> (many))
                    + " spectral " + std::to_string (relOn));
    }

    {
        auto followPeak = [] (float mainHz, bool follow)
        {
            Rig r (true);
            setParam (r.p, "fftOn", 1.0f);
            setParam (r.p, "scMode", 2.0f);
            setParam (r.p, "scMatch", 1.0f);
            setParam (r.p, "scAmount", 1.0f);
            setParam (r.p, "scFollow", follow ? 1.0f : 0.0f);
            r.prepare();
            const auto main = sine (16000, mainHz);
            const auto sc = sine (16000, 1000.0, 0.5f);
            r.render (main, &sc);
            return (double) r.p->getMatchPeakHz();
        };
        const double low = followPeak (250.0f, true);
        const double high = followPeak (500.0f, true);
        const double absLow = followPeak (250.0f, false);
        const double absHigh = followPeak (500.0f, false);
        expect (high > low + 80.0, "le suivi de hauteur decale le gabarit",
                std::to_string (low) + " -> " + std::to_string (high));
        expect (std::abs (absLow - absHigh) < 80.0, "sans suivi le gabarit reste sur les memes frequences",
                std::to_string (absLow) + " / " + std::to_string (absHigh));
    }

    {
        const auto harm = harmonics (n);
        const auto sc = sine (n, 800.0, 0.5f);

        Rig bandsNoFft (true);
        setParam (bandsNoFft.p, "fftOn", 0.0f);
        setParam (bandsNoFft.p, "scMode", 2.0f);
        setParam (bandsNoFft.p, "scMatch", 1.0f);
        setParam (bandsNoFft.p, "scAmount", 1.0f);
        setParam (bandsNoFft.p, "scUseBands", 1.0f);
        setParam (bandsNoFft.p, "scBandN", 64.0f);
        bandsNoFft.prepare();
        const auto banded = bandsNoFft.render (harm, &sc);
        const double bandRatio = goertzel (banded, specFrom, specLen, 800.0)
                                 / std::max (goertzel (banded, specFrom, specLen, 200.0), 1.0e-12);

        Rig plainNoFft;
        setParam (plainNoFft.p, "fftOn", 0.0f);
        plainNoFft.prepare();
        const auto dryNoFft = plainNoFft.render (harm);
        const double dryRatio = goertzel (dryNoFft, specFrom, specLen, 800.0)
                                / std::max (goertzel (dryNoFft, specFrom, specLen, 200.0), 1.0e-12);
        expect (bandRatio > dryRatio * 4.0,
                "les bandes Sidechain agissent sans case FFT",
                std::to_string (dryRatio) + " -> " + std::to_string (bandRatio));

        setParam (bandsNoFft.p, "scMatch", 0.0f);
        const auto afterOff = bandsNoFft.render (harm, &sc);
        expect (maxAbsDiff (afterOff, dryNoFft, tail) < 0.05,
                "couper Copy spectrum arrete les bandes tout de suite",
                "diff " + std::to_string (maxAbsDiff (afterOff, dryNoFft, tail)));

        Rig stick (true);
        setParam (stick.p, "fftOn", 0.0f);
        setParam (stick.p, "scMode", 2.0f);
        setParam (stick.p, "scMatch", 1.0f);
        setParam (stick.p, "scAmount", 1.0f);
        setParam (stick.p, "scUseBands", 1.0f);
        setParam (stick.p, "scBandN", 32.0f);
        stick.prepare();
        stick.render (harm, &sc);
        setParam (stick.p, "scUseBands", 0.0f);
        setParam (stick.p, "scMatch", 0.0f);
        const auto cleared = stick.render (harm, &sc);
        expect (maxAbsDiff (cleared, dryNoFft, tail) < 0.05,
                "quitter Bandes puis Copy ne laisse aucun filtre IIR",
                "diff " + std::to_string (maxAbsDiff (cleared, dryNoFft, tail)));

        Rig snapMatch (true);
        setParam (snapMatch.p, "fftOn", 0.0f);
        setParam (snapMatch.p, "scMode", 1.0f);
        setParam (snapMatch.p, "scMatch", 1.0f);
        setParam (snapMatch.p, "scAmount", 1.0f);
        setParam (snapMatch.p, "scUseBands", 1.0f);
        setParam (snapMatch.p, "scBandN", 48.0f);
        snapMatch.prepare();
        snapMatch.p->pulseCapture();
        snapMatch.render (harm, &sc); // Capture lands at the end of this block.
        const auto captured = snapMatch.render (harm, &sc);
        const double withCable = goertzel (captured, specFrom, specLen, 800.0)
                                 / std::max (goertzel (captured, specFrom, specLen, 200.0), 1.0e-12);
        const auto silentSc = std::vector<float> ((size_t) n, 0.0f);
        const auto kept = snapMatch.render (harm, &silentSc);
        const double afterUnplug = goertzel (kept, specFrom, specLen, 800.0)
                                   / std::max (goertzel (kept, specFrom, specLen, 200.0), 1.0e-12);
        expect (withCable > dryRatio * 4.0 && afterUnplug > dryRatio * 4.0,
                "Snapshot garde la limite apres Capture sans Sidechain",
                std::to_string (withCable) + " / " + std::to_string (afterUnplug));

        setParam (snapMatch.p, "scMode", 0.0f);
        const auto modeOff = snapMatch.render (harm, &sc);
        expect (maxAbsDiff (modeOff, dryNoFft, tail) < 0.05,
                "Sidechain Off annule la limite de frequence",
                "diff " + std::to_string (maxAbsDiff (modeOff, dryNoFft, tail)));

        Rig shrink (true);
        setParam (shrink.p, "fftOn", 0.0f);
        setParam (shrink.p, "scMode", 2.0f);
        setParam (shrink.p, "scMatch", 1.0f);
        setParam (shrink.p, "scAmount", 1.0f);
        setParam (shrink.p, "scUseBands", 1.0f);
        setParam (shrink.p, "scBandN", 128.0f);
        shrink.prepare();
        shrink.render (harm, &sc);
        setParam (shrink.p, "scBandN", 1.0f);
        shrink.render (harm, &sc);
        std::vector<float> drawn;
        int bands = -1;
        shrink.p->copyMatch (drawn, bands);
        expect (bands == 1, "reduire le nombre de bandes Sidechain met a jour l'affichage",
                "n=" + std::to_string (bands));

        Rig spectralNoFft (true);
        setParam (spectralNoFft.p, "fftOn", 0.0f);
        setParam (spectralNoFft.p, "scMode", 2.0f);
        setParam (spectralNoFft.p, "scMatch", 1.0f);
        setParam (spectralNoFft.p, "scAmount", 1.0f);
        setParam (spectralNoFft.p, "scUseBands", 0.0f);
        spectralNoFft.prepare();
        spectralNoFft.render (harm, &sc); // First block learns the Sidechain reference.
        const auto spectral = spectralNoFft.render (harm, &sc);
        const double spectralRatio = goertzel (spectral, specFrom, specLen, 800.0)
                                     / std::max (goertzel (spectral, specFrom, specLen, 200.0), 1.0e-12);
        expect (spectralRatio > dryRatio * 4.0,
                "la copie spectrale complete marche aussi sans case FFT",
                std::to_string (dryRatio) + " -> " + std::to_string (spectralRatio));
    }

    {
        Rig wet;
        armEqBand (wet, 0, 440.0f, 12.0f);
        setParam (wet.p, "mix", 1.0f);
        wet.prepare();
        const auto full = wet.render (tone440);

        Rig dry;
        armEqBand (dry, 0, 440.0f, 12.0f);
        setParam (dry.p, "mix", 0.0f);
        dry.prepare();
        const auto onlyDry = dry.render (tone440);
        expect (delayedError (onlyDry, tone440, dry.p->getLatencySamples()) < 1.0e-4,
                "Dry/Wet 0 % = entree retardee");

        Rig mid;
        armEqBand (mid, 0, 440.0f, 12.0f);
        setParam (mid.p, "mix", 0.5f);
        mid.prepare();
        const auto half = mid.render (tone440);
        double midErr = 0.0;
        for (int i = 1024; i < n; ++i)
        {
            const float expectS = 0.5f * (onlyDry[(size_t) i] + full[(size_t) i]);
            midErr = std::max (midErr, (double) std::abs (half[(size_t) i] - expectS));
        }
        expect (midErr < 1.0e-3 && maxAbsDiff (full, onlyDry, tail) > 0.02,
                "Dry/Wet 50 % est entre le sec et le traite", "err " + std::to_string (midErr));
    }

    {
        Rig plain;
        armEqBand (plain, 0, 440.0f, 18.0f);
        setParam (plain.p, "autoGain", 0.0f);
        plain.prepare();
        const auto boosted = plain.render (tone440);

        Rig comp;
        armEqBand (comp, 0, 440.0f, 18.0f);
        setParam (comp.p, "autoGain", 1.0f);
        comp.prepare();
        const auto leveled = comp.render (tone440);

        Rig unity;
        unity.prepare();
        const double inR = rms (unity.render (tone440), tail);
        const double boostR = rms (boosted, tail) / inR;
        const double levelR = rms (leveled, tail) / inR;
        expect (boostR > 4.0 && levelR > 0.75 && levelR < 1.25,
                "Auto Gain ramene le niveau de l'EQ vers l'entree",
                "sans " + std::to_string (boostR) + " avec " + std::to_string (levelR));

        std::vector<float> burst ((size_t) n, 0.0f);
        for (int i = 4000; i < n; ++i)
            burst[(size_t) i] = tone440[(size_t) i];
        const auto burstOut = comp.render (burst);
        const int arrive = 4000 + comp.p->getLatencySamples();
        double spike = 0.0;
        for (int i = arrive; i < arrive + 1500 && i < n; ++i)
            spike = std::max (spike, (double) std::abs (burstOut[(size_t) i]));
        const double steady = rms (leveled, tail);
        expect (spike < steady * 4.0,
                "Auto Gain ne sature pas l'attaque",
                "pic " + std::to_string (spike) + " stable " + std::to_string (steady));
    }

    {
        Rig eqOnly;
        armEqBand (eqOnly, 0, 440.0f, 12.0f);
        setParam (eqOnly.p, "fftOn", 0.0f);
        eqOnly.prepare();
        const auto eqOut = eqOnly.render (tone440);

        Rig fftOnly;
        setParam (fftOnly.p, "fftOn", 1.0f);
        setParam (fftOnly.p, "pitch", 2.0f);
        fftOnly.prepare();
        const auto fftOut = fftOnly.render (tone440);

        Rig neither;
        neither.prepare();
        const auto plain = neither.render (tone440);
        expect (maxAbsDiff (eqOut, plain, tail) > 0.02 && maxAbsDiff (fftOut, plain, tail) > 0.02
                    && maxAbsDiff (eqOut, fftOut, tail) > 0.02,
                "couper une seule section laisse l'autre agir");
    }

    {
        Rig plain;
        plain.prepare();
        const auto dry = plain.render (tone440);
        const double dryR = rms (dry, tail);

        Rig iir;
        armEqBand (iir, 0, 440.0f, 18.0f);
        setParam (iir.p, "eqEngine", 1.0f);
        iir.prepare();
        const auto iirOut = iir.render (tone440);
        expect (rms (iirOut, tail) / dryR > 3.0, "IIR crete augmente le sinus",
                "rapport " + std::to_string (rms (iirOut, tail) / dryR));

        Rig iirFlat;
        armEqBand (iirFlat, 0, 440.0f, 0.0f);
        setParam (iirFlat.p, "eqEngine", 1.0f);
        iirFlat.prepare();
        expect (maxAbsDiff (iirFlat.render (tone440), dry, tail) < 1.0e-3, "IIR a 0 dB ne change rien");

        Rig fir;
        armEqBand (fir, 0, 440.0f, 18.0f);
        setParam (fir.p, "eqEngine", 2.0f);
        fir.prepare();
        const auto firOut = fir.render (tone440);
        expect (rms (firOut, tail) / dryR > 2.5, "FIR crete augmente le sinus",
                "rapport " + std::to_string (rms (firOut, tail) / dryR)
                    + " latence " + std::to_string (fir.p->getLatencySamples()));
        expect (fir.p->getLatencySamples() == 1024 + 511, "FIR ajoute son delai a la latence");

        Rig firId;
        setParam (firId.p, "eqOn", 1.0f);
        setParam (firId.p, "eqEngine", 2.0f);
        firId.prepare();
        const auto firPlain = firId.render (tone440);
        expect (firId.p->getLatencySamples() == 1024 + 511
                    && delayedError (firPlain, tone440, firId.p->getLatencySamples()) < 1.0e-3,
                "FIR sans bande reste l'original retarde",
                "err " + std::to_string (delayedError (firPlain, tone440, firId.p->getLatencySamples())));

        std::vector<float> impulse ((size_t) n, 0.0f);
        impulse[0] = 1.0f;
        Rig firHit;
        armEqBand (firHit, 0, 440.0f, 18.0f);
        setParam (firHit.p, "eqEngine", 2.0f);
        firHit.prepare();
        const auto firIr = firHit.render (impulse);
        int firPeak = 0;
        for (int i = 1; i < (int) firIr.size(); ++i)
            if (std::abs (firIr[(size_t) i]) > std::abs (firIr[(size_t) firPeak]))
                firPeak = i;
        double pre = 0.0;
        for (int i = std::max (0, firPeak - 220); i < firPeak - 40; ++i)
            pre = std::max (pre, (double) std::abs (firIr[(size_t) i]));

        Rig origHit;
        armEqBand (origHit, 0, 440.0f, 18.0f);
        setParam (origHit.p, "eqEngine", 0.0f);
        origHit.prepare();
        const auto origIr = origHit.render (impulse);
        int origPeak = 0;
        for (int i = 1; i < (int) origIr.size(); ++i)
            if (std::abs (origIr[(size_t) i]) > std::abs (origIr[(size_t) origPeak]))
                origPeak = i;
        double origBefore = 0.0;
        for (int i = std::max (0, origPeak - 80); i < origPeak - 8; ++i)
            origBefore = std::max (origBefore, (double) std::abs (origIr[(size_t) i]));

        expect (pre > 0.002 && origBefore < 1.0e-4,
                "FIR a une pre-reponse, l'original non",
                "fir " + std::to_string (pre) + " original " + std::to_string (origBefore));
        expect (maxAbsDiff (firOut, iirOut, tail) > 0.01,
                "FIR, IIR et l'original peuvent etre compares");
    }

    {
        auto phaseOf = [] (const std::vector<float>& x, int from, int len, double hz)
        {
            const double w = 2.0 * kPi * hz / kSr;
            double s = 0.0, c = 0.0;
            for (int i = 0; i < len; ++i)
            {
                const double t = (double) (from + i);
                s += (double) x[(size_t) (from + i)] * std::sin (w * t);
                c += (double) x[(size_t) (from + i)] * std::cos (w * t);
            }
            return std::atan2 (s, c);
        };

        Rig plain;
        plain.prepare();
        const auto dry = plain.render (tone440);
        const double dryR = rms (dry, tail);

        Rig flat;
        armEqBand (flat, 0, 440.0f, 0.0f);
        setParam (flat.p, "b0dly", 12.0f);
        flat.prepare();
        const auto flatOut = flat.render (tone440);
        expect (delayedError (flatOut, tone440, flat.p->getLatencySamples()) < 1.0e-3,
                "delai sur une bande a 0 dB ne change pas le son");

        Rig still;
        armEqBand (still, 0, 440.0f, 18.0f);
        setParam (still.p, "b0dly", 0.0f);
        still.prepare();
        const auto noDly = still.render (tone440);

        Rig moved;
        armEqBand (moved, 0, 440.0f, 18.0f);
        setParam (moved.p, "b0dly", 8.0f);
        moved.prepare();
        const auto withDly = moved.render (tone440);
        double dphi = std::abs (phaseOf (withDly, tail, 4096, 440.0) - phaseOf (noDly, tail, 4096, 440.0));
        if (dphi > kPi)
            dphi = 2.0 * kPi - dphi;
        expect (rms (withDly, tail) / dryR > 3.0 && dphi > 0.2,
                "le delai de bande change la phase et garde le boost",
                "phase " + std::to_string (dphi) + " rapport " + std::to_string (rms (withDly, tail) / dryR));
        expect (maxAbsDiff (withDly, noDly, tail) > 0.01,
                "un delai nul et un delai positif ne sonnent pas pareil");
    }

    {
        Rig plain;
        plain.prepare();
        const auto dry = plain.render (tone440);

        Rig watch;
        watch.p->setDisplayOpen (true);
        setParam (watch.p, "anOn", 1.0f);
        setParam (watch.p, "anFft", 5.0f);
        setParam (watch.p, "anOverlap", 75.0f);
        setParam (watch.p, "phOn", 1.0f);
        watch.prepare();
        const auto watched = watch.render (tone440);
        watch.p->pumpDisplays();
        expect (maxAbsDiff (dry, watched, 0) < 1.0e-4,
                "l'analyse et la phase ne modifient pas le son");

        Rig centre;
        armEqBand (centre, 0, 440.0f, 18.0f);
        setParam (centre.p, "b0pan", 0.0f);
        centre.prepare();
        std::vector<float> cL, cR;
        centre.renderStereo (tone440, cL, cR);

        Rig left;
        armEqBand (left, 0, 440.0f, 18.0f);
        setParam (left.p, "b0pan", -1.0f);
        left.prepare();
        std::vector<float> lL, lR;
        left.renderStereo (tone440, lL, lR);

        Rig right;
        armEqBand (right, 0, 440.0f, 18.0f);
        setParam (right.p, "b0pan", 1.0f);
        right.prepare();
        std::vector<float> rL, rR;
        right.renderStereo (tone440, rL, rR);

        const double dryRms = rms (dry, tail);
        expect (std::abs (rms (cL, tail) - rms (cR, tail)) / dryRms < 0.05,
                "pan centre : les deux canaux restent ensemble");
        expect (rms (lL, tail) > dryRms * 3.0 && rms (lR, tail) < dryRms * 1.2,
                "pan gauche : seule la gauche est boostee",
                "L " + std::to_string (rms (lL, tail) / dryRms) + " R " + std::to_string (rms (lR, tail) / dryRms));
        expect (rms (rR, tail) > dryRms * 3.0 && rms (rL, tail) < dryRms * 1.2,
                "pan droite : seule la droite est boostee",
                "L " + std::to_string (rms (rL, tail) / dryRms) + " R " + std::to_string (rms (rR, tail) / dryRms));
    }

    {
        auto cutLevel = [&] (int type, float slope, int calc, float q, double toneHz)
        {
            Rig r;
            setParam (r.p, "eqOn", 1.0f);
            setParam (r.p, "bandCount", 1.0f);
            setParam (r.p, "b0on", 1.0f);
            setParam (r.p, "b0type", (float) type);
            setParam (r.p, "b0freq", 1000.0f);
            setParam (r.p, "b0q", q);
            setParam (r.p, "b0slope", slope);
            setParam (r.p, "b0calc", (float) calc);
            r.prepare();
            return rms (r.render (sine (n, toneHz)), tail);
        };

        const double low12 = cutLevel (3, 12.0f, 0, 0.707f, 100.0);
        const double low48 = cutLevel (3, 48.0f, 0, 0.707f, 100.0);
        const double lowPass = cutLevel (3, 12.0f, 0, 0.707f, 4000.0);
        const double high12 = cutLevel (4, 12.0f, 0, 0.707f, 4000.0);
        const double high48 = cutLevel (4, 48.0f, 0, 0.707f, 4000.0);
        const double qMode = cutLevel (3, 24.0f, 1, 4.0f, 1000.0);
        const double butter = cutLevel (3, 24.0f, 0, 4.0f, 1000.0);
        expect (low48 < low12 * 0.25 && lowPass > low12 * 4.0,
                "coupe bas : une pente plus forte en dB/oct coupe davantage les graves",
                "12 " + std::to_string (low12) + " 48 " + std::to_string (low48) + " aigu " + std::to_string (lowPass));
        expect (high48 < high12 * 0.25,
                "coupe haut : une pente plus forte en dB/oct coupe davantage les aigus",
                "12 " + std::to_string (high12) + " 48 " + std::to_string (high48));
        expect (std::abs (qMode - butter) / std::max (butter, 1.0e-9) > 0.05,
                "le calcul Q ne sonne pas comme Butterworth",
                "Q " + std::to_string (qMode) + " Butterworth " + std::to_string (butter));
    }

    // --- [ULTRA] @ 96 kHz ---
    {
        constexpr double kUltraSr = 96000.0;
        constexpr int kUltraN = 48000;
        constexpr int kUltraTail = 24000;
        constexpr int kSpecFrom = 8192;
        constexpr int kSpecLen = 16384;

        auto goertzelAt = [&] (const std::vector<float>& x, double hz)
        {
            const double w = 2.0 * kPi * hz / kUltraSr;
            const double coeff = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;
            for (int i = 0; i < kSpecLen; ++i)
            {
                const double s0 = (double) x[(size_t) (kSpecFrom + i)] + coeff * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            return s1 * s1 + s2 * s2 - coeff * s1 * s2;
        };

        auto toneAt = [&] (int length, double aHz, float aAmp, double bHz, float bAmp)
        {
            std::vector<float> x ((size_t) length);
            for (int i = 0; i < length; ++i)
                x[(size_t) i] = aAmp * (float) std::sin (2.0 * kPi * aHz * (double) i / kUltraSr)
                              + bAmp * (float) std::sin (2.0 * kPi * bHz * (double) i / kUltraSr);
            return x;
        };

        struct UltraRig
        {
            std::unique_ptr<ChouchouFFTEQAudioProcessor> p;
            UltraRig() { p = std::make_unique<ChouchouFFTEQAudioProcessor>(); }
            void prepare() { p->prepareToPlay (kUltraSr, 512); }
            std::vector<float> render (const std::vector<float>& in)
            {
                const int m = (int) in.size();
                juce::AudioBuffer<float> buffer (2, m);
                for (int i = 0; i < m; ++i)
                {
                    buffer.setSample (0, i, in[(size_t) i]);
                    buffer.setSample (1, i, in[(size_t) i]);
                }
                juce::MidiBuffer midi;
                p->processBlock (buffer, midi);
                std::vector<float> out ((size_t) m);
                for (int i = 0; i < m; ++i)
                    out[(size_t) i] = buffer.getSample (0, i);
                return out;
            }
        };

        const auto duo = toneAt (kUltraN, 1000.0, 0.2f, 30000.0, 0.2f);
        std::cout << "\n[ULTRA] 96 kHz ultrasound matrix\n";

        {
            UltraRig base;
            setParam (base.p, "fftOn", 1.0f);
            setParam (base.p, "fftSize", 2.0f); // 2048
            setParam (base.p, "mix", 1.0f);
            base.prepare();
            const auto dry = base.render (duo);

            UltraRig armedOff;
            setParam (armedOff.p, "fftOn", 1.0f);
            setParam (armedOff.p, "fftSize", 2.0f);
            setParam (armedOff.p, "mix", 1.0f);
            setParam (armedOff.p, "usOn", 0.0f);
            setParam (armedOff.p, "usGain", 12.0f);
            setParam (armedOff.p, "usTilt", 1.0f);
            setParam (armedOff.p, "usGate", 0.5f);
            setParam (armedOff.p, "usSolo", 1.0f);
            armedOff.prepare();
            const auto stayed = armedOff.render (duo);
            expect (maxAbsDiff (stayed, dry, kUltraTail) < 0.02,
                    "usOn off : identite meme avec Gain/Tilt/Gate/Solo armes");
        }

        {
            UltraRig base;
            setParam (base.p, "fftOn", 1.0f);
            setParam (base.p, "fftSize", 2.0f);
            setParam (base.p, "mix", 1.0f);
            setParam (base.p, "usOn", 1.0f);
            setParam (base.p, "usFloor", 18000.0f);
            setParam (base.p, "usGain", 0.0f);
            base.prepare();
            const auto flat = base.render (duo);
            const double e1 = goertzelAt (flat, 1000.0);
            const double e30 = goertzelAt (flat, 30000.0);

            UltraRig up;
            setParam (up.p, "fftOn", 1.0f);
            setParam (up.p, "fftSize", 2.0f);
            setParam (up.p, "mix", 1.0f);
            setParam (up.p, "usOn", 1.0f);
            setParam (up.p, "usFloor", 18000.0f);
            setParam (up.p, "usGain", 12.0f);
            up.prepare();
            const auto louder = up.render (duo);
            expect (goertzelAt (louder, 30000.0) > e30 * 2.0,
                    "usGain +12 dB monte 30 kHz",
                    std::to_string (e30) + " -> " + std::to_string (goertzelAt (louder, 30000.0)));
            expect (std::abs (goertzelAt (louder, 1000.0) - e1) / std::max (e1, 1.0e-12) < 0.25,
                    "usGain +12 dB laisse 1 kHz stable");

            UltraRig down;
            setParam (down.p, "fftOn", 1.0f);
            setParam (down.p, "fftSize", 2.0f);
            setParam (down.p, "mix", 1.0f);
            setParam (down.p, "usOn", 1.0f);
            setParam (down.p, "usFloor", 18000.0f);
            setParam (down.p, "usGain", -12.0f);
            down.prepare();
            const auto quieter = down.render (duo);
            expect (goertzelAt (quieter, 30000.0) < e30 * 0.5,
                    "usGain -12 dB baisse 30 kHz");
            expect (std::abs (goertzelAt (quieter, 1000.0) - e1) / std::max (e1, 1.0e-12) < 0.25,
                    "usGain -12 dB laisse 1 kHz stable");
        }

        {
            UltraRig pos;
            setParam (pos.p, "fftOn", 1.0f);
            setParam (pos.p, "fftSize", 2.0f);
            setParam (pos.p, "mix", 1.0f);
            setParam (pos.p, "usOn", 1.0f);
            setParam (pos.p, "usFloor", 18000.0f);
            setParam (pos.p, "usTilt", 1.0f);
            pos.prepare();
            const auto hiTilt = pos.render (duo);

            UltraRig neg;
            setParam (neg.p, "fftOn", 1.0f);
            setParam (neg.p, "fftSize", 2.0f);
            setParam (neg.p, "mix", 1.0f);
            setParam (neg.p, "usOn", 1.0f);
            setParam (neg.p, "usFloor", 18000.0f);
            setParam (neg.p, "usTilt", -1.0f);
            neg.prepare();
            const auto loTilt = neg.render (duo);
            const double rPos = goertzelAt (hiTilt, 40000.0) / std::max (goertzelAt (hiTilt, 20000.0), 1.0e-12);
            const double rNeg = goertzelAt (loTilt, 40000.0) / std::max (goertzelAt (loTilt, 20000.0), 1.0e-12);
            expect (rPos > rNeg * 1.2,
                    "usTilt +1 vs -1 penche la zone ultrason",
                    std::to_string (rNeg) + " -> " + std::to_string (rPos));
        }

        {
            const auto pair = toneAt (kUltraN, 25000.0, 0.35f, 35000.0, 0.03f);
            UltraRig openG;
            setParam (openG.p, "fftOn", 1.0f);
            setParam (openG.p, "fftSize", 2.0f);
            setParam (openG.p, "mix", 1.0f);
            setParam (openG.p, "usOn", 1.0f);
            setParam (openG.p, "usFloor", 18000.0f);
            setParam (openG.p, "usGate", 0.0f);
            openG.prepare();
            const auto open = openG.render (pair);

            UltraRig gated;
            setParam (gated.p, "fftOn", 1.0f);
            setParam (gated.p, "fftSize", 2.0f);
            setParam (gated.p, "mix", 1.0f);
            setParam (gated.p, "usOn", 1.0f);
            setParam (gated.p, "usFloor", 18000.0f);
            setParam (gated.p, "usGate", 0.35f);
            gated.prepare();
            const auto closed = gated.render (pair);
            expect (goertzelAt (closed, 35000.0) < goertzelAt (open, 35000.0) * 0.5,
                    "usGate attenue le bin ultrason faible");
            expect (goertzelAt (closed, 25000.0) > goertzelAt (open, 25000.0) * 0.5,
                    "usGate conserve le bin ultrason fort");
        }

        {
            UltraRig solo;
            setParam (solo.p, "fftOn", 1.0f);
            setParam (solo.p, "fftSize", 2.0f);
            setParam (solo.p, "mix", 1.0f);
            setParam (solo.p, "usOn", 1.0f);
            setParam (solo.p, "usFloor", 18000.0f);
            setParam (solo.p, "usSolo", 1.0f);
            solo.prepare();
            const auto out = solo.render (duo);
            expect (goertzelAt (out, 1000.0) < goertzelAt (out, 30000.0) * 0.15,
                    "usSolo attenue 1 kHz et garde 30 kHz");
        }

        {
            UltraRig highFloor;
            setParam (highFloor.p, "fftOn", 1.0f);
            setParam (highFloor.p, "fftSize", 2.0f);
            setParam (highFloor.p, "mix", 1.0f);
            setParam (highFloor.p, "usOn", 1.0f);
            setParam (highFloor.p, "usFloor", 35000.0f);
            setParam (highFloor.p, "usGain", 12.0f);
            highFloor.prepare();
            const auto out = highFloor.render (duo);

            UltraRig flat;
            setParam (flat.p, "fftOn", 1.0f);
            setParam (flat.p, "fftSize", 2.0f);
            setParam (flat.p, "mix", 1.0f);
            setParam (flat.p, "usOn", 1.0f);
            setParam (flat.p, "usFloor", 35000.0f);
            setParam (flat.p, "usGain", 0.0f);
            flat.prepare();
            const auto base = flat.render (duo);
            expect (std::abs (goertzelAt (out, 30000.0) - goertzelAt (base, 30000.0))
                        / std::max (goertzelAt (base, 30000.0), 1.0e-12) < 0.3,
                    "usFloor 35 kHz : gain ultrason ne touche plus 30 kHz");
        }

        {
            UltraRig base;
            setParam (base.p, "fftOn", 1.0f);
            setParam (base.p, "fftSize", 2.0f);
            setParam (base.p, "mix", 1.0f);
            setParam (base.p, "usOn", 1.0f);
            setParam (base.p, "usFloor", 18000.0f);
            setParam (base.p, "usGain", 0.0f);
            setParam (base.p, "usTilt", 0.0f);
            setParam (base.p, "usGate", 0.0f);
            setParam (base.p, "usSolo", 0.0f);
            base.prepare();
            const auto flat = base.render (duo);

            UltraRig reset;
            setParam (reset.p, "fftOn", 1.0f);
            setParam (reset.p, "fftSize", 2.0f);
            setParam (reset.p, "mix", 1.0f);
            setParam (reset.p, "usOn", 1.0f);
            setParam (reset.p, "usGain", 12.0f);
            setParam (reset.p, "usTilt", 0.5f);
            setParam (reset.p, "usGate", 0.2f);
            setParam (reset.p, "usSolo", 1.0f);
            setParam (reset.p, "usGain", 0.0f);
            setParam (reset.p, "usTilt", 0.0f);
            setParam (reset.p, "usGate", 0.0f);
            setParam (reset.p, "usSolo", 0.0f);
            reset.prepare();
            expect (maxAbsDiff (reset.render (duo), flat, kUltraTail) < 0.02,
                    "reset params ultrason retrouve l'identite");
        }

        {
            UltraRig plain;
            setParam (plain.p, "fftOn", 0.0f);
            setParam (plain.p, "experiment", 5.0f);
            setParam (plain.p, "shift", 0.5f);
            plain.prepare();
            const auto dry = plain.render (duo);

            UltraRig offFft;
            setParam (offFft.p, "fftOn", 0.0f);
            setParam (offFft.p, "experiment", 5.0f);
            setParam (offFft.p, "shift", 0.5f);
            setParam (offFft.p, "usOn", 1.0f);
            offFft.prepare();
            expect (maxAbsDiff (offFft.render (duo), dry, kUltraTail) < 0.02,
                    "5_ultra sans FFT : identite");

            UltraRig moved;
            setParam (moved.p, "fftOn", 1.0f);
            setParam (moved.p, "fftSize", 2.0f);
            setParam (moved.p, "mix", 1.0f);
            setParam (moved.p, "experiment", 5.0f);
            setParam (moved.p, "usFloor", 18000.0f);
            setParam (moved.p, "shift", 0.5f);
            moved.prepare();
            const auto changed = moved.render (duo);

            UltraRig unity;
            setParam (unity.p, "fftOn", 1.0f);
            setParam (unity.p, "fftSize", 2.0f);
            setParam (unity.p, "mix", 1.0f);
            setParam (unity.p, "experiment", 5.0f);
            setParam (unity.p, "usFloor", 18000.0f);
            setParam (unity.p, "shift", 1.0f);
            unity.prepare();
            const auto same = unity.render (duo);

            UltraRig chain;
            setParam (chain.p, "fftOn", 1.0f);
            setParam (chain.p, "fftSize", 2.0f);
            setParam (chain.p, "mix", 1.0f);
            setParam (chain.p, "experiment", 0.0f);
            chain.prepare();
            const auto base = chain.render (duo);

            expect (maxAbsDiff (changed, base, kUltraTail) > 0.01,
                    "5_ultra + shift!=1 change le signal HF");
            expect (maxAbsDiff (same, base, kUltraTail) < 0.05,
                    "5_ultra + shift=1 proche identite");
        }

        {
            UltraRig eq;
            setParam (eq.p, "eqOn", 1.0f);
            setParam (eq.p, "bandCount", 1.0f);
            setParam (eq.p, "b0on", 1.0f);
            setParam (eq.p, "b0type", 0.0f);
            setParam (eq.p, "b0freq", 30000.0f);
            setParam (eq.p, "b0gain", 12.0f);
            setParam (eq.p, "b0q", 2.0f);
            eq.prepare();
            expect (std::abs (eq.p->getAPVTS().getRawParameterValue ("b0freq")->load() - 30000.0f) < 1.0f,
                    "band freq accepte 30000 Hz");
            const auto tone30 = toneAt (kUltraN, 30000.0, 0.2f, 1000.0, 0.0f);
            (void) eq.render (tone30); // bake EQ coeffs via processBlock
            expect (std::abs (eq.p->responseDb (30000.0)) > 6.0f,
                    "peak 30 kHz : responseDb non nul",
                    std::to_string (eq.p->responseDb (30000.0)));

            UltraRig flatEq;
            setParam (flatEq.p, "eqOn", 1.0f);
            setParam (flatEq.p, "bandCount", 1.0f);
            setParam (flatEq.p, "b0on", 1.0f);
            setParam (flatEq.p, "b0type", 0.0f);
            setParam (flatEq.p, "b0freq", 30000.0f);
            setParam (flatEq.p, "b0gain", 0.0f);
            setParam (flatEq.p, "b0q", 2.0f);
            flatEq.prepare();
            const double flatRms = rms (flatEq.render (tone30), kUltraTail);
            const double boostRms = rms (eq.render (tone30), kUltraTail);
            expect (boostRms > flatRms * 2.0,
                    "peak 30 kHz booste le sinus ultrason",
                    std::to_string (flatRms) + " -> " + std::to_string (boostRms));

            auto* anMin = dynamic_cast<juce::AudioParameterFloat*> (eq.p->getAPVTS().getParameter ("anMin"));
            auto* anMax = dynamic_cast<juce::AudioParameterFloat*> (eq.p->getAPVTS().getParameter ("anMax"));
            expect (anMax != nullptr && anMax->getNormalisableRange().end >= 60000.0f,
                    "anMax range atteint 60 kHz");
            expect (anMin != nullptr && anMin->getNormalisableRange().start <= 1.0f
                    && anMin->getNormalisableRange().end >= 1000.0f,
                    "anMin permet 1 Hz .. >= 1 kHz");

            UltraRig view;
            setParam (view.p, "anMin", 1000.0f);
            setParam (view.p, "anMax", 60000.0f);
            setParam (view.p, "anFreqScale", 1.0f);
            view.prepare();
            expect (std::abs (view.p->getAPVTS().getRawParameterValue ("anMin")->load() - 1000.0f) < 1.0f,
                    "affichage 1 kHz applicable");
            expect (std::abs (view.p->getAPVTS().getRawParameterValue ("anMax")->load() - 60000.0f) < 1.0f,
                    "affichage 60 kHz applicable");

            // Edit EQ peak inside 1–60 kHz window (25 kHz)
            UltraRig edit;
            setParam (edit.p, "eqOn", 1.0f);
            setParam (edit.p, "bandCount", 1.0f);
            setParam (edit.p, "b0on", 1.0f);
            setParam (edit.p, "b0type", 0.0f);
            setParam (edit.p, "b0freq", 25000.0f);
            setParam (edit.p, "b0gain", 12.0f);
            setParam (edit.p, "b0q", 2.0f);
            setParam (edit.p, "anMin", 1000.0f);
            setParam (edit.p, "anMax", 60000.0f);
            edit.prepare();
            const auto tone25 = toneAt (kUltraN, 25000.0, 0.2f, 2000.0, 0.0f);
            UltraRig flat25;
            setParam (flat25.p, "eqOn", 1.0f);
            setParam (flat25.p, "bandCount", 1.0f);
            setParam (flat25.p, "b0on", 1.0f);
            setParam (flat25.p, "b0freq", 25000.0f);
            setParam (flat25.p, "b0gain", 0.0f);
            flat25.prepare();
            const double flat25Rms = rms (flat25.render (tone25), kUltraTail);
            const double boost25Rms = rms (edit.render (tone25), kUltraTail);
            expect (boost25Rms > flat25Rms * 2.0,
                    "edition peak 25 kHz dans fenetre 1–60 kHz",
                    std::to_string (flat25Rms) + " -> " + std::to_string (boost25Rms));
            expect (std::abs (edit.p->responseDb (25000.0)) > 6.0f,
                    "responseDb editable a 25 kHz");
        }

        {
            // Ghost-peak regression: one boost must not mirror above Nyquist.
            UltraRig one;
            setParam (one.p, "eqOn", 1.0f);
            setParam (one.p, "bandCount", 1.0f);
            setParam (one.p, "b0on", 1.0f);
            setParam (one.p, "b0type", 0.0f);
            setParam (one.p, "b0freq", 17000.0f);
            setParam (one.p, "b0gain", 12.0f);
            setParam (one.p, "b0q", 2.0f);
            one.prepare();
            (void) one.render (toneAt (kUltraN, 17000.0, 0.2f, 1000.0, 0.0f));
            const float atPeak = one.p->responseDb (17000.0);
            const float atMirror31k = one.p->responseDb (31000.0);
            const float at65k = one.p->responseDb (65000.0);
            const float at85k = one.p->responseDb (85000.0);
            expect (atPeak > 6.0f, "un boost a 17 kHz a une vraie crete",
                    std::to_string (atPeak));
            expect (std::abs (atMirror31k) < 1.0f
                    && std::abs (at65k) < 0.1f
                    && std::abs (at85k) < 0.1f,
                    "pas de pics fantomes au-dessus de Nyquist",
                    "31k=" + std::to_string (atMirror31k)
                    + " 65k=" + std::to_string (at65k)
                    + " 85k=" + std::to_string (at85k));
        }
    }

    std::cout << (gFails == 0 ? "TOUT PASSE\n" : ("ECHECS " + std::to_string (gFails) + "\n"));
    return gFails == 0 ? 0 : 1;
}
