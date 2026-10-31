#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
constexpr float olaGain = 2.0f / 3.0f;

juce::String bandId (int i, const char* field)
{
    return "b" + juce::String (i) + field;
}

float lerpMag (const std::vector<float>& m, float index, int bins)
{
    if (bins <= 1)
        return 0.0f;
    index = juce::jlimit (0.0f, (float) (bins - 1), index);
    const int i0 = (int) index;
    const int i1 = juce::jmin (bins - 1, i0 + 1);
    const float f = index - (float) i0;
    return m[(size_t) i0] * (1.0f - f) + m[(size_t) i1] * f;
}

void octaveSmooth (const std::vector<float>& mag, std::vector<float>& env, int bins)
{
    for (int i = 0; i < bins; ++i)
    {
        const int lo = juce::jmax (0, (int) std::floor ((double) i / 1.41421356237));
        const int hi = juce::jmin (bins - 1, (int) std::ceil ((double) i * 1.41421356237));
        float s = 0.0f;
        int n = 0;
        for (int k = lo; k <= hi; ++k)
        {
            s += mag[(size_t) k];
            ++n;
        }
        env[(size_t) i] = n > 0 ? s / (float) n : 0.0f;
    }
}

float pitchHzFromMag (const std::vector<float>& mag, int bins, double sr, int fftSize)
{
    const int lo = juce::jlimit (1, bins - 1, (int) std::floor (70.0 * fftSize / sr));
    const int hi = juce::jlimit (lo, bins - 1, (int) std::ceil (4000.0 * fftSize / sr));
    int peak = lo;
    for (int i = lo; i <= hi; ++i)
        if (mag[(size_t) i] > mag[(size_t) peak])
            peak = i;
    return (float) ((double) peak * sr / (double) fftSize);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout ChouchouFFTEQAudioProcessor::makeParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto flag = [] (const juce::String& id, const juce::String& name, bool def)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
    };
    auto knob = [] (const juce::String& id, const juce::String& name, float minV, float maxV, float def, float step = 0.001f)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (minV, maxV, step), def);
    };

    auto fr = [] (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); };

    p.push_back (flag ("eqOn", "EQ", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "eqEngine", 1 }, "Moteur EQ",
        juce::StringArray { "Original", "IIR", "FIR" }, 0));
    p.push_back (flag ("fftOn", "FFT", false));
    p.push_back (knob ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f));
    p.push_back (flag ("autoGain", "Auto Gain", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "fftSize", 1 }, fr ("Taille de fen\u00eatre"),
        juce::StringArray { "512", "1024", "2048", "4096" }, 1));

    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "scMode", 1 }, "Analyse Sidechain",
        juce::StringArray { fr ("Arr\u00eat"), fr ("Instantan\u00e9"), fr ("Temps r\u00e9el") }, 0));
    p.push_back (flag ("scCapture", "Capturer", false));
    p.push_back (flag ("scMatch", "Copier le spectre", false));
    p.push_back (knob ("scAmount", fr ("Quantit\u00e9"), 0.0f, 1.0f, 0.0f));
    p.push_back (flag ("scFollow", "Suivi de hauteur", false));
    p.push_back (flag ("scUseBands", "Bandes Sidechain", false));
    p.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "scBandN", 1 }, "Nombre de bandes Sidechain", 1, 1000, 12));

    p.push_back (knob ("pitch", "pitch", 0.25f, 4.0f, 1.0f));
    p.push_back (knob ("formant", "formant", 0.25f, 4.0f, 1.0f));
    p.push_back (knob ("offsetBins", "offsetBins", -128.0f, 128.0f, 0.0f, 0.1f));
    p.push_back (knob ("mulReel", "mulreel", 0.0f, 4.0f, 1.0f));
    p.push_back (knob ("mulImage", "mulimage", 0.0f, 4.0f, 1.0f));
    p.push_back (knob ("tilt", "tilt", -1.0f, 1.0f, 0.0f));
    p.push_back (knob ("contrast", "contrast", 0.25f, 4.0f, 1.0f));
    p.push_back (knob ("gate", "gate", 0.0f, 1.0f, 0.0f));
    p.push_back (knob ("blur", "blur", 0.0f, 32.0f, 0.0f, 0.1f));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "experiment", 1 }, "experiment",
        juce::StringArray { "0_chaine", "1_shift", "2_split", "3_offset", "4_write" }, 0));
    p.push_back (knob ("shift", "shift", 0.01f, 16.0f, 1.0f));
    p.push_back (knob ("shiftreel", "shiftreel", 0.01f, 16.0f, 1.0f));
    p.push_back (knob ("shiftimage", "shiftimage", 0.01f, 16.0f, 1.0f));
    p.push_back (flag ("freeze", "Geler", false));

    auto choice = [] (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def);
    };
    p.push_back (flag ("anOn", "Analyse", true));
    p.push_back (choice ("anFft", fr ("Taille FFT analyse"), juce::StringArray { "1024", "2048", "4096", "8192", "16384", "32768" }, 1));
    p.push_back (choice ("anWin", fr ("Fen\u00eatre"), juce::StringArray { "Hann", "Hamming", "Blackman", "Kaiser", "Rectangulaire" }, 0));
    p.push_back (knob ("anOverlap", "Recouvrement", 0.0f, 75.0f, 50.0f, 1.0f));
    p.push_back (knob ("anAvg", "Moyenne", 0.0f, 0.98f, 0.0f, 0.01f));
    p.push_back (knob ("anSmooth", "Lissage", 0.0f, 1.0f, 0.0f, 0.01f));
    p.push_back (choice ("anPad", "Zero padding", juce::StringArray { "1", "2", "4" }, 0));
    p.push_back (knob ("anRate", "Rafraichissement", 5.0f, 60.0f, 30.0f, 1.0f));
    p.push_back (choice ("anMode", "Mode spectre", juce::StringArray { fr ("Temps r\u00e9el"), "Peak hold", "Moyenne", "Gel", "Cascade" }, 0));
    p.push_back (knob ("anMin", fr ("Fr\u00e9quence min"), 20.0f, 5000.0f, 20.0f, 1.0f));
    p.push_back (knob ("anMax", fr ("Fr\u00e9quence max"), 1000.0f, 20000.0f, 20000.0f, 1.0f));
    p.push_back (knob ("anDb", "Plage dB", 20.0f, 120.0f, 80.0f, 1.0f));
    p.push_back (knob ("anRef", fr ("Niveau r\u00e9f\u00e9rence"), -60.0f, 12.0f, 0.0f, 0.1f));
    p.push_back (knob ("anAmp", "Plage amplitude", 0.1f, 4.0f, 1.0f, 0.01f));
    p.push_back (choice ("anFreqScale", fr ("\u00c9chelle fr\u00e9quence"), juce::StringArray { "Log", "Lineaire" }, 0));
    p.push_back (choice ("anAmpScale", "Echelle amplitude", juce::StringArray { "dB", "Lineaire" }, 0));
    p.push_back (choice ("anChan", "Canal", juce::StringArray { "Gauche", "Droite", "Mid", "Side", "Gauche+Droite" }, 4));
    p.push_back (flag ("anSnap", "Calage notes", false));
    p.push_back (flag ("anNotes", "Nom de note", true));
    p.push_back (flag ("anMeas", "Mesures", true));
    p.push_back (flag ("anDiff", "Difference", false));
    p.push_back (flag ("anSc", "Spectre sidechain", true));
    p.push_back (flag ("phOn", "Phase", false));
    p.push_back (knob ("phGain", "Gain affichage", 0.25f, 8.0f, 1.0f, 0.01f));
    p.push_back (knob ("phDecay", "Extinction", 0.0f, 1.0f, 0.35f, 0.01f));
    p.push_back (knob ("phPersist", "Persistance", 40.0f, 2000.0f, 400.0f, 1.0f));
    p.push_back (knob ("phRot", "Rotation", -180.0f, 180.0f, 0.0f, 1.0f));
    p.push_back (knob ("phZoom", "Zoom", 0.25f, 8.0f, 1.0f, 0.01f));
    p.push_back (knob ("phScaleX", "Echelle X", 0.25f, 4.0f, 1.0f, 0.01f));
    p.push_back (knob ("phScaleY", "Echelle Y", 0.25f, 4.0f, 1.0f, 0.01f));
    p.push_back (knob ("phThick", "Epaisseur", 1.0f, 4.0f, 1.5f, 0.1f));
    p.push_back (knob ("phBright", "Luminosite", 0.15f, 1.0f, 0.85f, 0.01f));
    p.push_back (flag ("phGrid", "Grille", true));
    p.push_back (flag ("phFreeze", "Figer phase", false));
    p.push_back (choice ("phMode", "Mode phase", juce::StringArray { "Lissajous", "Correlation", "Mid/Side" }, 0));
    p.push_back (choice ("phInput", "Entree phase", juce::StringArray { "Gauche/Droite", "Mid/Side" }, 0));

    p.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "bandCount", 1 }, "Bandes", 0, kMaxBands, 0));

    for (int i = 0; i < kMaxBands; ++i)
    {
        const auto n = juce::String (i + 1);
        p.push_back (flag (bandId (i, "on"), "Bande " + n, false));
        p.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { bandId (i, "type"), 1 }, "Type " + n,
            juce::StringArray { fr ("Cr\u00eate"), "Shelf bas", "Shelf haut", "Coupe bas", "Coupe haut" }, 0));
        p.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { bandId (i, "freq"), 1 }, fr ("Fr\u00e9quence ") + n,
            juce::NormalisableRange<float> (20.0f, 20000.0f, 0.01f, 0.25f), 1000.0f));
        p.push_back (knob (bandId (i, "gain"), "Gain " + n, -24.0f, 24.0f, 0.0f, 0.1f));
        p.push_back (knob (bandId (i, "q"), "Q " + n, 0.1f, 10.0f, 0.707f, 0.001f));
        p.push_back (knob (bandId (i, "slope"), "Pente " + n, 6.0f, 96.0f, 12.0f, 6.0f));
        p.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { bandId (i, "calc"), 1 }, "Calcul " + n,
            juce::StringArray { "Butterworth", "Q" }, 0));
        p.push_back (knob (bandId (i, "dly"), fr ("D\u00e9lai ") + n, 0.0f, 20.0f, 0.0f, 0.01f));
        p.push_back (knob (bandId (i, "pan"), "Pan " + n, -1.0f, 1.0f, 0.0f, 0.01f));
    }

    return { p.begin(), p.end() };
}

ChouchouFFTEQAudioProcessor::ChouchouFFTEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
#endif
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
                          ),
#endif
      apvts (*this, nullptr, "PARAMS", makeParams())
{
    ensureSizes();
}

ChouchouFFTEQAudioProcessor::~ChouchouFFTEQAudioProcessor() = default;

void ChouchouFFTEQAudioProcessor::ensureSizes()
{
    for (auto& c : channels)
    {
        c.inRing.assign ((size_t) kMaxFftSize, 0.0f);
        c.outRing.assign ((size_t) kMaxFftSize, 0.0f);
        c.frame.assign ((size_t) kMaxFftSize, 0.0f);
        c.spectrum.assign ((size_t) (kMaxFftSize * 2), 0.0f);
        c.mag.assign ((size_t) kMaxBins, 0.0f);
        c.phase.assign ((size_t) kMaxBins, 0.0f);
        c.frozenMag.assign ((size_t) kMaxBins, 0.0f);
        c.frozenPhase.assign ((size_t) kMaxBins, 0.0f);
        c.dryDelay.assign ((size_t) kMaxFftSize + (size_t) chouchoueq::kFirDelay + 8, 0.0f);
        c.bypassDelay.assign ((size_t) kMaxFftSize, 0.0f);
    }

    for (auto* a : { &mainAnalyser, &scAnalyser })
    {
        a->ring.assign ((size_t) kMaxFftSize, 0.0f);
        a->spectrum.assign ((size_t) (kMaxFftSize * 2), 0.0f);
        a->mag.assign ((size_t) kMaxBins, 0.0f);
    }

    reference.assign ((size_t) kMaxBins, 0.0f);
    scLive.assign ((size_t) kMaxBins, 0.0f);
    mainLive.assign ((size_t) kMaxBins, 0.0f);
    envA.assign ((size_t) kMaxBins, 0.0f);
    envB.assign ((size_t) kMaxBins, 0.0f);
    tmpMag.assign ((size_t) kMaxBins, 0.0f);
    tmpPhase.assign ((size_t) kMaxBins, 0.0f);
    matchTarget.assign ((size_t) kMaxBins, 1.0f);
    matchDb.assign ((size_t) kMaxBins, 0.0f);
    scBandGain.assign (1000, 1.0f);
    scNodes.assign (1000, {});
    pubScBands.assign (1000, {});
    for (auto& bank : scFilt)
    {
        bank.assign (1000, {});
        for (auto& f : bank)
            f.setUnity();
    }
    pubMain.assign ((size_t) kMaxBins, 0.0f);
    pubSc.assign ((size_t) kMaxBins, 0.0f);
    pubRef.assign ((size_t) kMaxBins, 0.0f);
    pubMatch.assign ((size_t) kMaxBins, 0.0f);
}

void ChouchouFFTEQAudioProcessor::clearChan (Chan& c)
{
    std::fill (c.inRing.begin(), c.inRing.end(), 0.0f);
    std::fill (c.outRing.begin(), c.outRing.end(), 0.0f);
    std::fill (c.frame.begin(), c.frame.end(), 0.0f);
    std::fill (c.spectrum.begin(), c.spectrum.end(), 0.0f);
    std::fill (c.dryDelay.begin(), c.dryDelay.end(), 0.0f);
    std::fill (c.bypassDelay.begin(), c.bypassDelay.end(), 0.0f);
    c.writePos = c.hop = c.dryPos = c.bypassPos = 0;
    c.hasFreeze = false;
}

void ChouchouFFTEQAudioProcessor::applyFftSize (int choice, bool force)
{
    const int order = 9 + juce::jlimit (0, 3, choice);
    if (! force && order == fftOrder)
        return;

    fftOrder = order;
    fftSize = 1 << order;
    hopSize = fftSize / 4;
    nBins = fftSize / 2 + 1;
    for (auto& c : channels)
        clearChan (c);
    for (auto* a : { &mainAnalyser, &scAnalyser })
    {
        std::fill (a->ring.begin(), a->ring.end(), 0.0f);
        a->writePos = a->hop = 0;
    }
    std::fill (reference.begin(), reference.end(), 0.0f);
    referenceValid = false;
    lastCapture = false;
    inPower = wetPower = 1.0e-8f;
    dryDelayLength = fftSize;
    setLatencySamples (fftSize);
}

juce::dsp::FFT& ChouchouFFTEQAudioProcessor::currentFft() noexcept
{
    switch (fftOrder)
    {
        case 9:  return fft512;
        case 11: return fft2048;
        case 12: return fft4096;
        default: return fft1024;
    }
}

juce::dsp::WindowingFunction<float>& ChouchouFFTEQAudioProcessor::currentHann() noexcept
{
    switch (fftOrder)
    {
        case 9:  return hann512;
        case 11: return hann2048;
        case 12: return hann4096;
        default: return hann1024;
    }
}

void ChouchouFFTEQAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    agCoeff = 1.0f - std::exp (-1.0f / (0.03f * (float) currentSampleRate));
    eq.prepare (currentSampleRate);
    displayAnalyser.prepare (currentSampleRate);
    ensureSizes();
    applyFftSize ((int) apvts.getRawParameterValue ("fftSize")->load(), true);
}

void ChouchouFFTEQAudioProcessor::releaseResources() {}

void ChouchouFFTEQAudioProcessor::pumpDisplays()
{
    if (! displayOpen.load (std::memory_order_relaxed))
        return;

    static const int sizes[] { 1024, 2048, 4096, 8192, 16384, 32768 };
    const int fftChoice = juce::jlimit (0, 5, (int) std::lround (apvts.getRawParameterValue ("anFft")->load()));
    const int padChoice = juce::jlimit (0, 2, (int) std::lround (apvts.getRawParameterValue ("anPad")->load()));
    const int pad = padChoice == 2 ? 4 : (padChoice == 1 ? 2 : 1);
    displayAnalyser.configure (sizes[fftChoice],
                               (int) std::lround (apvts.getRawParameterValue ("anWin")->load()),
                               apvts.getRawParameterValue ("anOverlap")->load(),
                               apvts.getRawParameterValue ("anAvg")->load(),
                               apvts.getRawParameterValue ("anSmooth")->load(),
                               pad,
                               (int) std::lround (apvts.getRawParameterValue ("anChan")->load()),
                               (int) std::lround (apvts.getRawParameterValue ("anMode")->load()),
                               apvts.getRawParameterValue ("anSc")->load() > 0.5f);
    displayAnalyser.pump();
}

bool ChouchouFFTEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }
#endif
    return true;
#endif
}

const juce::String ChouchouFFTEQAudioProcessor::getName() const { return JucePlugin_Name; }
bool ChouchouFFTEQAudioProcessor::acceptsMidi() const { return false; }
bool ChouchouFFTEQAudioProcessor::producesMidi() const { return false; }
bool ChouchouFFTEQAudioProcessor::isMidiEffect() const { return false; }
double ChouchouFFTEQAudioProcessor::getTailLengthSeconds() const
{
    return currentSampleRate > 0.0 ? (double) fftSize / currentSampleRate : 0.0;
}
int ChouchouFFTEQAudioProcessor::getNumPrograms() { return 1; }
int ChouchouFFTEQAudioProcessor::getCurrentProgram() { return 0; }
void ChouchouFFTEQAudioProcessor::setCurrentProgram (int) {}
const juce::String ChouchouFFTEQAudioProcessor::getProgramName (int) { return {}; }
void ChouchouFFTEQAudioProcessor::changeProgramName (int, const juce::String&) {}

float ChouchouFFTEQAudioProcessor::readDelay (std::vector<float>& d, int& pos, float x, int length) const noexcept
{
    length = juce::jlimit (1, (int) d.size(), length);
    if (pos < 0 || pos >= length)
        pos = 0;
    const float y = d[(size_t) pos];
    d[(size_t) pos] = x;
    pos = (pos + 1) % length;
    return y;
}

void ChouchouFFTEQAudioProcessor::readPolar (const std::vector<float>& spectrum, int idx, float& mag, float& phase) const noexcept
{
    float re, im;
    if (idx <= 0) { re = spectrum[0]; im = 0.0f; }
    else if (idx >= fftSize / 2) { re = spectrum[1]; im = 0.0f; }
    else
    {
        re = spectrum[(size_t) (2 * idx)];
        im = spectrum[(size_t) (2 * idx + 1)];
    }
    mag = std::sqrt (re * re + im * im);
    phase = std::atan2 (im, re);
}

void ChouchouFFTEQAudioProcessor::writePolar (std::vector<float>& spectrum, int idx, float mag, float phase) const noexcept
{
    const float re = mag * std::cos (phase);
    const float im = mag * std::sin (phase);
    if (idx <= 0) spectrum[0] = re;
    else if (idx >= fftSize / 2) spectrum[1] = re;
    else
    {
        spectrum[(size_t) (2 * idx)] = re;
        spectrum[(size_t) (2 * idx + 1)] = im;
    }
}

void ChouchouFFTEQAudioProcessor::processAnalysis (Analyser& a, std::vector<float>& dest)
{
    const int size = fftSize;
    auto& hann = currentHann();
    auto& fft = currentFft();

    for (int i = 0; i < size; ++i)
        a.spectrum[(size_t) i] = a.ring[(size_t) ((a.writePos + i) % size)];
    std::fill (a.spectrum.begin() + size, a.spectrum.begin() + size * 2, 0.0f);
    hann.multiplyWithWindowingTable (a.spectrum.data(), (size_t) size);
    fft.performRealOnlyForwardTransform (a.spectrum.data(), true);

    for (int i = 0; i < nBins; ++i)
    {
        float mag, phase;
        readPolar (a.spectrum, i, mag, phase);
        dest[(size_t) i] = mag;
    }
}

void ChouchouFFTEQAudioProcessor::processEffectFrame (Chan& c, const float* scMag)
{
    const int size = fftSize;
    const int bins = nBins;
    auto& hann = currentHann();
    auto& fft = currentFft();

    for (int i = 0; i < size; ++i)
        c.frame[(size_t) i] = c.inRing[(size_t) ((c.writePos + i) % size)];

    hann.multiplyWithWindowingTable (c.frame.data(), (size_t) size);
    std::fill_n (c.spectrum.begin(), (size_t) (size * 2), 0.0f);
    std::copy_n (c.frame.begin(), (size_t) size, c.spectrum.begin());
    fft.performRealOnlyForwardTransform (c.spectrum.data(), true);

    for (int i = 0; i < bins; ++i)
        readPolar (c.spectrum, i, c.mag[(size_t) i], c.phase[(size_t) i]);

    const bool fftOn = apvts.getRawParameterValue ("fftOn")->load() > 0.5f;
    const bool freeze = fftOn && apvts.getRawParameterValue ("freeze")->load() > 0.5f;
    if (freeze)
    {
        if (! c.hasFreeze)
        {
            std::copy_n (c.mag.begin(), (size_t) bins, c.frozenMag.begin());
            std::copy_n (c.phase.begin(), (size_t) bins, c.frozenPhase.begin());
            c.hasFreeze = true;
        }
        std::copy_n (c.frozenMag.begin(), (size_t) bins, c.mag.begin());
        std::copy_n (c.frozenPhase.begin(), (size_t) bins, c.phase.begin());
    }
    else
        c.hasFreeze = false;

    const float amount = apvts.getRawParameterValue ("scAmount")->load();
    const bool match = apvts.getRawParameterValue ("scMatch")->load() > 0.5f
                       && amount > 0.0001f
                       && referenceValid
                       && scMag != nullptr;
    if (match)
    {
        octaveSmooth (c.mag, envA, bins);
        octaveSmooth (reference, envB, bins);
        const bool follow = apvts.getRawParameterValue ("scFollow")->load() > 0.5f;
        float scale = 1.0f;
        if (follow)
        {
            const float pMain = juce::jmax (1.0f, pitchHzFromMag (c.mag, bins, currentSampleRate, fftSize));
            const float pRef = juce::jmax (1.0f, pitchHzFromMag (reference, bins, currentSampleRate, fftSize));
            scale = pRef / pMain;
        }

        int peak = 1;
        float peakTemplate = 0.0f;
        for (int i = 0; i < bins; ++i)
        {
            const float src = (float) i * scale;
            const float scE = lerpMag (envB, src, bins);
            const float mE = juce::jmax (envA[(size_t) i], 1.0e-8f);
            const float ratio = juce::jlimit (0.05f, 20.0f, scE / mE);
            matchTarget[(size_t) i] = 1.0f + amount * (ratio - 1.0f);
            if (i > 0 && scE > peakTemplate)
            {
                peakTemplate = scE;
                peak = i;
            }
        }

        const bool useBands = apvts.getRawParameterValue ("scUseBands")->load() > 0.5f;
        const int bandN = juce::jlimit (1, 1000, (int) std::lround (apvts.getRawParameterValue ("scBandN")->load()));
        if (useBands)
        {
            if (&c == &channels[0])
            {
                const float nyquist = (float) currentSampleRate * 0.5f;
                const float fLo = 30.0f;
                const float fHi = juce::jmax (fLo + 1.0f, juce::jmin (nyquist * 0.9f, 18000.0f));
                const float logSpan = std::log (fHi / fLo);
                const int n = bandN;
                const float octaves = std::log2 (fHi / fLo);
                const float bw = juce::jmax (0.04f, octaves / (float) juce::jmax (1, n));
                const float q = juce::jlimit (0.25f, 12.0f, 1.0f / (2.0f * std::sinh (0.5f * std::log (2.0f) * bw)));
                std::fill_n (matchDb.begin(), (size_t) bins, 0.0f);
                for (int b = 0; b < n; ++b)
                {
                    const float t = n == 1 ? 0.5f : (float) b / (float) (n - 1);
                    const float hz = juce::jlimit (20.0f, nyquist * 0.95f, fLo * std::exp (logSpan * t));
                    const float bin = hz * (float) fftSize / (float) currentSampleRate;
                    const float g = juce::jmax (lerpMag (matchTarget, bin, bins), 1.0e-8f);
                    scBandGain[(size_t) b] = 20.0f * std::log10 (g);
                    scNodes[(size_t) b] = { hz, 0.0f, q };
                }
                for (int pass = 0; pass < 2; ++pass)
                {
                    for (int b = 0; b < n; ++b)
                    {
                        const float hz = scNodes[(size_t) b].hz;
                        float sum = 0.0f;
                        const int p0 = juce::jmax (0, b - 8);
                        const int p1 = juce::jmin (n - 1, b + 8);
                        for (int p = p0; p <= p1; ++p)
                            if (p != b)
                                sum += scFilt[0][(size_t) p].responseDb (hz, currentSampleRate);
                        const float db = juce::jlimit (-24.0f, 24.0f, scBandGain[(size_t) b] - sum);
                        scNodes[(size_t) b].db = db;
                        const auto designed = chouchoueq::Biquad::peak (currentSampleRate, hz, q, db);
                        for (auto& bank : scFilt)
                            bank[(size_t) b].copyCoeffs (designed);
                    }
                }
                for (int b = 0; b < n; ++b)
                {
                    const int shown = juce::jlimit (0, bins - 1,
                                                    (int) std::lround ((double) scNodes[(size_t) b].hz * (double) fftSize / currentSampleRate));
                    matchDb[(size_t) shown] = scNodes[(size_t) b].db;
                }
                scFiltCount = n;
                matchBandCount = n;
            }
        }
        else
        {
            for (int i = 0; i < bins; ++i)
            {
                const float g = matchTarget[(size_t) i];
                c.mag[(size_t) i] *= g;
                if (&c == &channels[0])
                    matchDb[(size_t) i] = 20.0f * std::log10 (juce::jmax (g, 1.0e-8f));
            }
            if (&c == &channels[0])
            {
                matchBandCount = 0;
                scFiltCount = 0;
            }
        }

        if (&c == &channels[0])
            matchPeakHz.store ((float) ((double) peak * currentSampleRate / (double) fftSize),
                               std::memory_order_relaxed);
    }
    else if (&c == &channels[0])
    {
        matchPeakHz.store (0.0f, std::memory_order_relaxed);
        std::fill_n (matchDb.begin(), (size_t) bins, 0.0f);
        matchBandCount = 0;
        scFiltCount = 0;
    }

    const float pitch = fftOn ? apvts.getRawParameterValue ("pitch")->load() : 1.0f;
    const float formant = fftOn ? apvts.getRawParameterValue ("formant")->load() : 1.0f;
    const float offset = fftOn ? apvts.getRawParameterValue ("offsetBins")->load() : 0.0f;
    const int experiment = fftOn ? (int) apvts.getRawParameterValue ("experiment")->load() : 0;
    const float shift = fftOn ? apvts.getRawParameterValue ("shift")->load() : 1.0f;
    const float shiftreel = fftOn ? apvts.getRawParameterValue ("shiftreel")->load() : 1.0f;
    const float shiftimage = fftOn ? apvts.getRawParameterValue ("shiftimage")->load() : 1.0f;

    auto peekSnap = [&] (float pos, float& reel, float& image)
    {
        if (pos < 0.0f || pos >= (float) bins)
        {
            reel = 0.0f;
            image = 0.0f;
            return;
        }
        if (pos >= (float) (bins - 1))
        {
            reel = tmpMag[(size_t) (bins - 1)];
            image = tmpPhase[(size_t) (bins - 1)];
            return;
        }
        const int i0 = (int) pos;
        const float frac = pos - (float) i0;
        reel = tmpMag[(size_t) i0] * (1.0f - frac) + tmpMag[(size_t) (i0 + 1)] * frac;
        image = tmpPhase[(size_t) i0] * (1.0f - frac) + tmpPhase[(size_t) (i0 + 1)] * frac;
    };

    if (experiment == 0)
    {
        for (int i = 0; i < bins; ++i)
        {
            const float src = (float) i * pitch;
            tmpMag[(size_t) i] = lerpMag (c.mag, src, bins);
            tmpPhase[(size_t) i] = lerpMag (c.phase, src, bins);
        }
        c.mag.swap (tmpMag);
        c.phase.swap (tmpPhase);

        if (std::abs (formant - 1.0f) > 0.0001f)
        {
            octaveSmooth (c.mag, envA, bins);
            for (int i = 0; i < bins; ++i)
            {
                const float shifted = lerpMag (envA, (float) i / formant, bins);
                const float e = juce::jmax (envA[(size_t) i], 1.0e-8f);
                c.mag[(size_t) i] *= shifted / e;
            }
        }

        if (std::abs (offset) > 0.0001f)
        {
            for (int i = 0; i < bins; ++i)
            {
                tmpMag[(size_t) i] = lerpMag (c.mag, (float) i - offset, bins);
                tmpPhase[(size_t) i] = lerpMag (c.phase, (float) i - offset, bins);
            }
            std::copy_n (tmpMag.begin(), (size_t) bins, c.mag.begin());
            std::copy_n (tmpPhase.begin(), (size_t) bins, c.phase.begin());
        }
    }
    else if (experiment == 4)
    {
        std::fill_n (tmpMag.begin(), (size_t) bins, 0.0f);
        std::fill_n (tmpPhase.begin(), (size_t) bins, 0.0f);
        for (int idx = 0; idx < bins; ++idx)
        {
            const float reel = c.mag[(size_t) idx];
            const float image = c.phase[(size_t) idx];
            int writeidx = (int) std::lround ((double) idx * (double) shift);
            writeidx %= bins;
            if (writeidx < 0)
                writeidx += bins;
            tmpMag[(size_t) writeidx] += reel * std::cos (image);
            tmpPhase[(size_t) writeidx] += reel * std::sin (image);
        }
        for (int idx = 0; idx < bins; ++idx)
        {
            const float re = tmpMag[(size_t) idx];
            const float im = tmpPhase[(size_t) idx];
            c.mag[(size_t) idx] = std::sqrt (re * re + im * im);
            c.phase[(size_t) idx] = std::atan2 (im, re);
        }
    }
    else
    {
        std::copy_n (c.mag.begin(), (size_t) bins, tmpMag.begin());
        std::copy_n (c.phase.begin(), (size_t) bins, tmpPhase.begin());
        for (int idx = 0; idx < bins; ++idx)
        {
            float reel = tmpMag[(size_t) idx];
            float image = tmpPhase[(size_t) idx];
            if (experiment == 1)
                peekSnap ((float) idx * shift, reel, image);
            else if (experiment == 2)
            {
                float magFromR = 0.0f, phaseUnused = 0.0f;
                float magUnused = 0.0f, phaseFromI = 0.0f;
                peekSnap ((float) idx * shiftreel, magFromR, phaseUnused);
                peekSnap ((float) idx * shiftimage, magUnused, phaseFromI);
                reel = magFromR;
                image = phaseFromI;
            }
            else
                peekSnap ((float) idx + offset, reel, image);
            c.mag[(size_t) idx] = reel;
            c.phase[(size_t) idx] = image;
        }
    }

    if (experiment != 0 && std::abs (formant - 1.0f) > 0.0001f)
    {
        octaveSmooth (c.mag, envA, bins);
        for (int i = 0; i < bins; ++i)
        {
            const float shifted = lerpMag (envA, (float) i / formant, bins);
            const float e = juce::jmax (envA[(size_t) i], 1.0e-8f);
            c.mag[(size_t) i] *= shifted / e;
        }
    }

    const float tilt = fftOn ? apvts.getRawParameterValue ("tilt")->load() : 0.0f;
    const float contrast = fftOn ? apvts.getRawParameterValue ("contrast")->load() : 1.0f;
    const float gate = fftOn ? apvts.getRawParameterValue ("gate")->load() : 0.0f;
    const float blur = fftOn ? apvts.getRawParameterValue ("blur")->load() : 0.0f;

    if (std::abs (tilt) > 0.0001f)
    {
        for (int i = 0; i < bins; ++i)
        {
            const float t = bins > 1 ? (float) i / (float) (bins - 1) : 0.5f;
            c.mag[(size_t) i] *= std::exp (tilt * (t - 0.5f) * 4.0f);
        }
    }

    if (std::abs (contrast - 1.0f) > 0.0001f)
    {
        float mean = 0.0f;
        for (int i = 0; i < bins; ++i)
            mean += c.mag[(size_t) i];
        mean = juce::jmax (mean / (float) bins, 1.0e-8f);
        for (int i = 0; i < bins; ++i)
            c.mag[(size_t) i] = mean * std::pow (juce::jmax (c.mag[(size_t) i], 0.0f) / mean, contrast);
    }

    if (gate > 0.0001f)
    {
        float peak = 0.0f;
        for (int i = 0; i < bins; ++i)
            peak = juce::jmax (peak, c.mag[(size_t) i]);
        const float thr = gate * peak;
        for (int i = 0; i < bins; ++i)
            if (c.mag[(size_t) i] < thr)
                c.mag[(size_t) i] = 0.0f;
    }

    const int radius = juce::jlimit (0, 64, (int) std::lround (blur));
    if (radius > 0)
    {
        for (int i = 0; i < bins; ++i)
        {
            float re = 0.0f, im = 0.0f;
            int count = 0;
            for (int d = -radius; d <= radius; ++d)
            {
                const int k = juce::jlimit (0, bins - 1, i + d);
                re += c.mag[(size_t) k] * std::cos (c.phase[(size_t) k]);
                im += c.mag[(size_t) k] * std::sin (c.phase[(size_t) k]);
                ++count;
            }
            const float inv = 1.0f / (float) count;
            tmpMag[(size_t) i] = std::sqrt (re * re + im * im) * inv;
            tmpPhase[(size_t) i] = std::atan2 (im, re);
        }
        std::copy_n (tmpMag.begin(), (size_t) bins, c.mag.begin());
        std::copy_n (tmpPhase.begin(), (size_t) bins, c.phase.begin());
    }

    const float mulReel = fftOn ? apvts.getRawParameterValue ("mulReel")->load() : 1.0f;
    const float mulImage = fftOn ? apvts.getRawParameterValue ("mulImage")->load() : 1.0f;
    for (int i = 0; i < bins; ++i)
    {
        const float re = c.mag[(size_t) i] * std::cos (c.phase[(size_t) i]) * mulReel;
        const float im = c.mag[(size_t) i] * std::sin (c.phase[(size_t) i]) * mulImage;
        const float mag = std::sqrt (re * re + im * im);
        const float phase = std::atan2 (im, re);
        writePolar (c.spectrum, i, mag, phase);
    }

    fft.performRealOnlyInverseTransform (c.spectrum.data());
    for (int i = 0; i < size; ++i)
        c.frame[(size_t) i] = c.spectrum[(size_t) i];
    hann.multiplyWithWindowingTable (c.frame.data(), (size_t) size);
    for (int i = 0; i < size; ++i)
        c.outRing[(size_t) ((c.writePos + i) % size)] += c.frame[(size_t) i] * olaGain;
}

void ChouchouFFTEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    applyFftSize ((int) apvts.getRawParameterValue ("fftSize")->load(), false);

    const int numSamples = buffer.getNumSamples();
    const int bufCh = buffer.getNumChannels();
    int numIn = getMainBusNumInputChannels();
    int numOut = getMainBusNumOutputChannels();
    if (numIn <= 0 && bufCh > 0)
        numIn = juce::jmin (bufCh, 2);
    if (numOut <= 0)
        numOut = juce::jmax (numIn, 1);

    for (int i = numIn; i < numOut && i < bufCh; ++i)
        buffer.clear (i, 0, numSamples);

    int scCh = 0;
    const float* scPtrs[2] { nullptr, nullptr };
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled() && bufCh > 0)
    {
        const int offset = scBus->getChannelIndexInProcessBlockBuffer (0);
        scCh = juce::jmin (scBus->getNumberOfChannels(), bufCh - offset);
        if (scCh > 0)
        {
            auto scBuffer = getBusBuffer (buffer, true, 1);
            scPtrs[0] = scBuffer.getReadPointer (0);
            scPtrs[1] = scBuffer.getReadPointer (juce::jmin (1, scCh - 1));
        }
        else
            scCh = 0;
    }

    const bool eqOn = apvts.getRawParameterValue ("eqOn")->load() > 0.5f;
    const bool fftOn = apvts.getRawParameterValue ("fftOn")->load() > 0.5f;
    const float mix = apvts.getRawParameterValue ("mix")->load();
    const bool autoGain = apvts.getRawParameterValue ("autoGain")->load() > 0.5f;
    const int scMode = (int) apvts.getRawParameterValue ("scMode")->load();
    const bool capture = apvts.getRawParameterValue ("scCapture")->load() > 0.5f;
    const bool matchOn = apvts.getRawParameterValue ("scMatch")->load() > 0.5f
                         && apvts.getRawParameterValue ("scAmount")->load() > 0.0001f
                         && scMode > 0
                         && scCh > 0;
    const bool spectral = fftOn || matchOn;

    chouchoueq::BandSettings settings[(size_t) kMaxBands];
    const int bandCount = (int) apvts.getRawParameterValue ("bandCount")->load();
    for (int i = 0; i < kMaxBands; ++i)
    {
        settings[i].on = apvts.getRawParameterValue (bandId (i, "on"))->load() > 0.5f;
        settings[i].type = (int) apvts.getRawParameterValue (bandId (i, "type"))->load();
        settings[i].freq = apvts.getRawParameterValue (bandId (i, "freq"))->load();
        settings[i].gainDb = apvts.getRawParameterValue (bandId (i, "gain"))->load();
        settings[i].q = apvts.getRawParameterValue (bandId (i, "q"))->load();
        settings[i].slope = apvts.getRawParameterValue (bandId (i, "slope"))->load();
        settings[i].cutCalc = (int) apvts.getRawParameterValue (bandId (i, "calc"))->load();
        settings[i].delayMs = apvts.getRawParameterValue (bandId (i, "dly"))->load();
        settings[i].pan = apvts.getRawParameterValue (bandId (i, "pan"))->load();
    }
    eq.setEngine ((int) apvts.getRawParameterValue ("eqEngine")->load());
    eq.update (settings, bandCount, eqOn);

    const int totalLatency = fftSize + eq.latencySamples();
    if (totalLatency != dryDelayLength || getLatencySamples() != totalLatency)
    {
        dryDelayLength = totalLatency;
        setLatencySamples (totalLatency);
        for (auto& c : channels)
        {
            std::fill (c.dryDelay.begin(), c.dryDelay.end(), 0.0f);
            c.dryPos = 0;
        }
    }

    const int numCh = juce::jmin (2, numIn > 0 ? numIn : bufCh, bufCh);
    const bool risingCapture = capture && ! lastCapture;
    lastCapture = capture;

    const bool watch = displayOpen.load (std::memory_order_relaxed);
    const bool anOn = watch && apvts.getRawParameterValue ("anOn")->load() > 0.5f;
    const bool phOn = watch && apvts.getRawParameterValue ("phOn")->load() > 0.5f;
    if (anOn || phOn)
    {
        displayL.resize ((size_t) numSamples);
        displayR.resize ((size_t) numSamples);
        displaySc.resize ((size_t) numSamples);
        const float* inL = numCh > 0 ? buffer.getReadPointer (0) : nullptr;
        const float* inR = numCh > 1 ? buffer.getReadPointer (1) : inL;
        for (int i = 0; i < numSamples; ++i)
        {
            displayL[(size_t) i] = inL != nullptr ? inL[i] : 0.0f;
            displayR[(size_t) i] = inR != nullptr ? inR[i] : displayL[(size_t) i];
            displaySc[(size_t) i] = scPtrs[0] != nullptr ? scPtrs[0][i] : 0.0f;
        }
    }

    for (int n = 0; n < numSamples; ++n)
    {
        float shapedForAnalysis = 0.0f;
        float scSample = 0.0f;
        float dryS[2] {}, wetS[2] {};

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            auto& c = channels[(size_t) ch];
            const float in = data[n];
            const float dry = readDelay (c.dryDelay, c.dryPos, in, dryDelayLength);
            const float shaped = eq.process (ch, in);

            if (ch == 0)
            {
                shapedForAnalysis = shaped;
                scSample = scPtrs[0] != nullptr ? scPtrs[0][n] : 0.0f;
            }

            float wet;
            if (spectral)
            {
                wet = c.outRing[(size_t) c.writePos];
                c.outRing[(size_t) c.writePos] = 0.0f;
                c.inRing[(size_t) c.writePos] = shaped;
                c.writePos = (c.writePos + 1) % fftSize;
            }
            else
                wet = readDelay (c.bypassDelay, c.bypassPos, shaped, fftSize);

            const int bandBank = scFiltCount;
            if (bandBank > 0)
            {
                auto& bank = scFilt[(size_t) ch];
                for (int b = 0; b < bandBank; ++b)
                    wet = bank[(size_t) b].process (wet);
            }

            dryS[ch] = dry;
            wetS[ch] = wet;
        }

        // The wet signal is already delayed by the FFT (and by the FIR, when that
        // engine is on). The reference has to be the dry signal delayed by the
        // same amount. Comparing it to the live input pegs the gain while the
        // delay line is still empty, so the first notes come out far too loud.
        if (autoGain)
        {
            float ip = 0.0f, wp = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                ip += dryS[ch] * dryS[ch];
                wp += wetS[ch] * wetS[ch];
            }
            const float inv = 1.0f / (float) juce::jmax (1, numCh);
            inPower += agCoeff * (ip * inv - inPower);
            wetPower += agCoeff * (wp * inv - wetPower);
            float g = 1.0f;
            if (inPower > 1.0e-8f || wetPower > 1.0e-8f)
                g = std::sqrt (inPower / juce::jmax (wetPower, 1.0e-12f));
            g = juce::jlimit (0.03f, 30.0f, g);
            for (int ch = 0; ch < numCh; ++ch)
                wetS[ch] *= g;
        }
        else
            inPower = wetPower = 1.0e-8f;

        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer (ch)[n] = dryS[ch] + mix * (wetS[ch] - dryS[ch]);

        if (spectral)
        {
            // One shared hop counter, stored on channel 0. Channel writes already advanced writePos.
            auto& clock = channels[0];
            if (++clock.hop >= hopSize)
            {
                clock.hop = 0;
                for (int ch = 0; ch < numCh; ++ch)
                    processEffectFrame (channels[(size_t) ch], scCh > 0 ? scLive.data() : nullptr);
            }
        }

        mainAnalyser.ring[(size_t) mainAnalyser.writePos] = shapedForAnalysis;
        scAnalyser.ring[(size_t) scAnalyser.writePos] = scSample;
        mainAnalyser.writePos = (mainAnalyser.writePos + 1) % fftSize;
        scAnalyser.writePos = mainAnalyser.writePos;
        if (++mainAnalyser.hop >= hopSize)
        {
            mainAnalyser.hop = 0;
            scAnalyser.writePos = mainAnalyser.writePos;
            processAnalysis (mainAnalyser, mainLive);
            if (scCh > 0)
                processAnalysis (scAnalyser, scLive);
            else
                std::fill_n (scLive.begin(), (size_t) nBins, 0.0f);

            if (scMode == 2 && scCh > 0)
            {
                std::copy_n (scLive.begin(), (size_t) nBins, reference.begin());
                referenceValid = true;
            }
            else if (scMode == 1 && scCh > 0 && risingCapture && n == 0)
            {
                std::copy_n (scLive.begin(), (size_t) nBins, reference.begin());
                referenceValid = true;
            }

            if (spectrumMutex.try_lock())
            {
                std::copy_n (mainLive.begin(), (size_t) nBins, pubMain.begin());
                std::copy_n (scLive.begin(), (size_t) nBins, pubSc.begin());
                std::copy_n (reference.begin(), (size_t) nBins, pubRef.begin());
                std::copy_n (matchDb.begin(), (size_t) nBins, pubMatch.begin());
                pubMatchBands = matchBandCount;
                if (matchBandCount > 0)
                    std::copy_n (scNodes.begin(), (size_t) matchBandCount, pubScBands.begin());
                spectrumMutex.unlock();
            }
            analysisHops.fetch_add (1, std::memory_order_relaxed);
        }
    }

    displayAnalyser.setEnabled (anOn);
    phaseCapture.setEnabled (phOn);
    if (anOn)
        displayAnalyser.pushBlock (displayL.data(), displayR.data(), displaySc.data(), numSamples);
    if (phOn && numCh > 1)
        phaseCapture.pushBlock (displayL.data(), displayR.data(), numSamples);

    // Snapshot can be requested at any time; catch a rising edge that fell inside the block
    // after the first sample by copying the spectrum gathered during this block.
    if (scMode == 1 && scCh > 0 && risingCapture)
    {
        std::copy_n (scLive.begin(), (size_t) nBins, reference.begin());
        referenceValid = true;
        if (spectrumMutex.try_lock())
        {
            std::copy_n (reference.begin(), (size_t) nBins, pubRef.begin());
            spectrumMutex.unlock();
        }
    }
}

void ChouchouFFTEQAudioProcessor::copySpectra (std::vector<float>& mainMag,
                                              std::vector<float>& sidechainMag,
                                              std::vector<float>& referenceMag) const
{
    std::lock_guard<std::mutex> lock (spectrumMutex);
    mainMag.assign (pubMain.begin(), pubMain.begin() + nBins);
    sidechainMag.assign (pubSc.begin(), pubSc.begin() + nBins);
    referenceMag.assign (pubRef.begin(), pubRef.begin() + nBins);
}

void ChouchouFFTEQAudioProcessor::copyMatch (std::vector<float>& gainDb, int& bandCount) const
{
    std::lock_guard<std::mutex> lock (spectrumMutex);
    gainDb.assign (pubMatch.begin(), pubMatch.begin() + nBins);
    bandCount = pubMatchBands;
}

void ChouchouFFTEQAudioProcessor::copyScBands (std::vector<ScBand>& bands) const
{
    std::lock_guard<std::mutex> lock (spectrumMutex);
    bands.assign (pubScBands.begin(), pubScBands.begin() + juce::jlimit (0, 1000, pubMatchBands));
}

bool ChouchouFFTEQAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ChouchouFFTEQAudioProcessor::createEditor()
{
    return new ChouchouFFTEQEditor (*this);
}

void ChouchouFFTEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ChouchouFFTEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChouchouFFTEQAudioProcessor();
}
