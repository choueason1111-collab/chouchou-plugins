#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    inline float readHermite (const float* buf, int len, int w, double delay) noexcept
    {
        double pos = (double) w - delay;
        if (pos < 0.0)
            pos += (double) len;

        int i1 = (int) pos;
        if (i1 >= len)
            i1 -= len;

        const float f = (float) (pos - std::floor (pos));
        if (f == 0.0f)
            return buf[i1];

        const int i0 = i1 == 0 ? len - 1 : i1 - 1;
        const int i2 = i1 + 1 >= len ? i1 + 1 - len : i1 + 1;
        const int i3 = i1 + 2 >= len ? i1 + 2 - len : i1 + 2;

        const float y0 = buf[i0], y1 = buf[i1], y2 = buf[i2], y3 = buf[i3];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    juce::String formatSeconds (double sec)
    {
        if (sec >= 1.0)
            return juce::String (sec, 3) + " s";
        if (sec >= 0.01)
            return juce::String (sec * 1000.0, 2) + " ms";
        if (sec >= 0.001)
            return juce::String (sec * 1000.0, 3) + " ms";
        return juce::String (sec * 1.0e6, 1) + " us";
    }

    double parseSeconds (juce::String text, double sampleRate)
    {
        text = text.trim().toLowerCase();
        const double v = text.getDoubleValue();

        if (text.contains ("smp") || text.contains ("sample") || text.endsWith ("sa"))
            return v / juce::jmax (1.0, sampleRate);
        if (text.contains ("us"))
            return v / 1.0e6;
        if (text.contains ("ms"))
            return v / 1000.0;
        if (text.endsWith ("s") || text.endsWith ("sec"))
            return v;
        return v / 1000.0;
    }
}

//==============================================================================
ChouchouDelayAudioProcessor::ChouchouDelayAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout (*this))
{
    pEchoCount = apvts.getRawParameterValue ("echoCount");
    pInterval  = apvts.getRawParameterValue ("interval");
    pDecay     = apvts.getRawParameterValue ("decay");
    pMix       = apvts.getRawParameterValue ("mix");
    pTempoMode = apvts.getRawParameterValue ("tempoMode");

    for (int t = 0; t < kMaxEchoes; ++t)
    {
        const int e = t + 1;
        pOn[(size_t) t]     = apvts.getRawParameterValue (idOn (e));
        pPitch[(size_t) t]  = apvts.getRawParameterValue (idPitch (e));
        pFine[(size_t) t]   = apvts.getRawParameterValue (idFine (e));
        pLevel[(size_t) t]  = apvts.getRawParameterValue (idLevel (e));
        pOffset[(size_t) t] = apvts.getRawParameterValue (idOffset (e));
        pPan[(size_t) t]    = apvts.getRawParameterValue (idPan (e));
        pRhythm[(size_t) t] = apvts.getRawParameterValue (idRhythm (e));
        pRhythmCount[(size_t) t] = apvts.getRawParameterValue (idRhythmCount (e));
        pTuplet[(size_t) t] = apvts.getRawParameterValue (idTuplet (e));
    }

    if (! apvts.state.hasProperty ("intervalUnit"))
        apvts.state.setProperty ("intervalUnit", "ms", nullptr);
}

ChouchouDelayAudioProcessor::~ChouchouDelayAudioProcessor()
{
    worker.stopThread (4000);
    freeAllStores();
}

juce::AudioProcessorValueTreeState::ParameterLayout
ChouchouDelayAudioProcessor::createLayout (ChouchouDelayAudioProcessor& self)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "echoCount", 1 }, "Echo Count", 1, kMaxEchoes, 4));

    juce::NormalisableRange<float> intervalRange ((float) kMinIntervalSeconds, (float) kMaxIntervalSeconds);
    intervalRange.setSkewForCentre (0.3f);

    auto* srPtr = &self.currentSampleRate;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "interval", 1 }, "Interval", intervalRange, 0.25f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("s")
            .withStringFromValueFunction ([] (float v, int) { return formatSeconds ((double) v); })
            .withValueFromStringFunction ([srPtr] (const juce::String& s)
                                          { return (float) parseSeconds (s, srPtr->load()); })));

    juce::NormalisableRange<float> decayRange ((float) kMinDecaySeconds, (float) kMaxDecaySeconds);
    decayRange.setSkewForCentre (2.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decay", 1 }, "Decay", decayRange, 3.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("s")
            .withStringFromValueFunction ([] (float v, int) { return formatSeconds ((double) v); })
            .withValueFromStringFunction ([srPtr] (const juce::String& s)
                                          {
                                              auto t = s.trim().toLowerCase();
                                              const double sec = (t.containsOnly ("0123456789.-+ ") && t.isNotEmpty())
                                                                     ? t.getDoubleValue()
                                                                     : parseSeconds (t, srPtr->load());
                                              return (float) juce::jlimit (kMinDecaySeconds, kMaxDecaySeconds, sec);
                                          })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "tempoMode", 1 }, "Tempo",
        juce::StringArray { "Manual", "Real-time", "Tap" },
        0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " %"; })));

    for (int e = 1; e <= kMaxEchoes; ++e)
    {
        const auto n = juce::String (e);

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { idOn (e), 1 }, "Echo " + n + " On", true));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { idLevel (e), 1 }, "Echo " + n + " Level",
            juce::NormalisableRange<float> (kLevelFloorDb, 6.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int)
                                              {
                                                  return v <= kLevelFloorDb ? juce::String ("-inf dB")
                                                                            : juce::String (v, 1) + " dB";
                                              })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { idPitch (e), 1 }, "Echo " + n + " Pitch",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("st")
                .withStringFromValueFunction ([] (float v, int)
                                              { return (v > 0 ? "+" : "") + juce::String ((int) v) + " st"; })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { idFine (e), 1 }, "Echo " + n + " Fine",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("ct")
                .withStringFromValueFunction ([] (float v, int)
                                              { return (v > 0 ? "+" : "") + juce::String ((int) v) + " ct"; })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { idOffset (e), 1 }, "Echo " + n + " Offset",
            juce::NormalisableRange<float> ((float) -kMaxOffsetPercent, (float) kMaxOffsetPercent, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction ([] (float v, int)
                                              { return (v > 0 ? "+" : "") + juce::String (v, 1) + " %"; })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { idPan (e), 1 }, "Echo " + n + " Pan",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int)
                                              {
                                                  if (v < -0.05f) return juce::String ("L ") + juce::String (-v, 2);
                                                  if (v > 0.05f)  return juce::String ("R ") + juce::String (v, 2);
                                                  return juce::String ("C");
                                              })));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { idRhythm (e), 1 }, "Echo " + n + " Rhythm",
            juce::StringArray { "Half", "Quarter", "8th", "16th", "32nd", "64th" },
            1));

        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { idRhythmCount (e), 1 }, "Echo " + n + " Count",
            1, 64, juce::jlimit (1, 64, e)));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { idTuplet (e), 1 }, "Echo " + n + " Tuplet",
            juce::StringArray { "Straight", "Triplet", "Quintuplet", "Sextuplet", "Septuplet" },
            0));
    }

    return layout;
}

//==============================================================================
const juce::String ChouchouDelayAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

int ChouchouDelayAudioProcessor::getEchoCount() const
{
    return juce::jlimit (1, kMaxEchoes, (int) std::lround (pEchoCount->load()));
}

double ChouchouDelayAudioProcessor::getIntervalSeconds() const
{
    return (double) pInterval->load();
}

double ChouchouDelayAudioProcessor::getIntervalSamplesExact() const
{
    return juce::jmax (1.0, getIntervalSeconds() * currentSampleRate.load());
}

bool ChouchouDelayAudioProcessor::isEchoOn (int echo) const
{
    return pOn[(size_t) (echo - 1)]->load() > 0.5f;
}

double ChouchouDelayAudioProcessor::getEchoOffsetPercent (int echo) const
{
    return (double) pOffset[(size_t) (echo - 1)]->load();
}

double ChouchouDelayAudioProcessor::getEchoOffsetSeconds (int echo) const
{
    return getEchoOffsetPercent (echo) / 100.0 * getIntervalSeconds();
}

double ChouchouDelayAudioProcessor::getEchoPitchSemitones (int echo) const
{
    const auto t = (size_t) (echo - 1);
    return (double) pPitch[t]->load() + (double) pFine[t]->load() / 100.0;
}

juce::String ChouchouDelayAudioProcessor::rhythmName (int index)
{
    static const char* names[] = { "Half", "Quarter", "8th", "16th", "32nd", "64th" };
    return names[juce::jlimit (0, kRhythmCount - 1, index)];
}

double ChouchouDelayAudioProcessor::rhythmBeats (int index)
{
    static const double beats[] = { 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
    return beats[juce::jlimit (0, kRhythmCount - 1, index)];
}

juce::String ChouchouDelayAudioProcessor::tupletName (int index)
{
    static const char* names[] = { "Straight", "Triplet", "Quintuplet", "Sextuplet", "Septuplet" };
    return names[juce::jlimit (0, kTupletCount - 1, index)];
}

double ChouchouDelayAudioProcessor::tupletFactor (int index)
{
    // Straight, 3 in the time of 2, then 5, 6 and 7 in the time of 4.
    static const double factor[] = { 1.0, 2.0 / 3.0, 4.0 / 5.0, 4.0 / 6.0, 4.0 / 7.0 };
    return factor[juce::jlimit (0, kTupletCount - 1, index)];
}

int ChouchouDelayAudioProcessor::getTempoMode() const
{
    return juce::jlimit (0, 2, (int) std::lround (pTempoMode->load()));
}

float ChouchouDelayAudioProcessor::getCurrentBpm() const
{
    const int mode = getTempoMode();
    const float bpm = mode == 1 ? detectedBpm.load() : tapBpm.load();
    return juce::jlimit (30.0f, 300.0f, bpm);
}

float ChouchouDelayAudioProcessor::getEchoPan (int echo) const
{
    return pPan[(size_t) (echo - 1)]->load();
}

int ChouchouDelayAudioProcessor::getEchoRhythm (int echo) const
{
    return juce::jlimit (0, kRhythmCount - 1, (int) std::lround (pRhythm[(size_t) (echo - 1)]->load()));
}

int ChouchouDelayAudioProcessor::getEchoRhythmCount (int echo) const
{
    return juce::jlimit (1, 64, (int) std::lround (pRhythmCount[(size_t) (echo - 1)]->load()));
}

int ChouchouDelayAudioProcessor::getEchoTuplet (int echo) const
{
    return juce::jlimit (0, kTupletCount - 1, (int) std::lround (pTuplet[(size_t) (echo - 1)]->load()));
}

void ChouchouDelayAudioProcessor::tapTempo() noexcept
{
    const auto now = (juce::int64) juce::Time::getMillisecondCounter();
    const auto prev = lastTapMs.exchange (now);
    if (prev <= 0 || now - prev > 2000 || now - prev < 50)
    {
        tapIntervals = 0;
        return;
    }

    tapHist[(size_t) tapHistPos] = (double) (now - prev);
    tapHistPos = (tapHistPos + 1) % (int) tapHist.size();
    if (tapIntervals < (int) tapHist.size())
        ++tapIntervals;

    double sum = 0.0;
    for (int i = 0; i < tapIntervals; ++i)
        sum += tapHist[(size_t) i];
    tapBpm.store (juce::jlimit (30.0f, 300.0f, (float) (60000.0 / (sum / (double) tapIntervals))));
}

double ChouchouDelayAudioProcessor::getEchoDelaySamples (int echo) const
{
    const double sr = currentSampleRate.load();
    const double pct = getEchoOffsetPercent (echo) / 100.0;
    double seconds;
    if (getTempoMode() == 0)
        seconds = ((double) echo + pct) * getIntervalSeconds();
    else
    {
        const double beat = 60.0 / (double) getCurrentBpm();
        const double unit = beat * rhythmBeats (getEchoRhythm (echo)) * tupletFactor (getEchoTuplet (echo));
        seconds = ((double) getEchoRhythmCount (echo) + pct) * unit;
    }
    return juce::jmax (1.0, (double) std::llround (seconds * sr));
}

double ChouchouDelayAudioProcessor::getEchoTimeSeconds (int echo) const
{
    return getEchoDelaySamples (echo) / currentSampleRate.load();
}

float ChouchouDelayAudioProcessor::getEchoDecayGain (int echo) const
{
    const double decay = juce::jmax (kMinDecaySeconds, (double) pDecay->load());
    return (float) std::pow (10.0, (kDecayTargetDb / 20.0) * getEchoTimeSeconds (echo) / decay);
}

float ChouchouDelayAudioProcessor::getEchoLevelGain (int echo) const
{
    const float db = pLevel[(size_t) (echo - 1)]->load();
    return db <= kLevelFloorDb ? 0.0f : juce::Decibels::decibelsToGain (db);
}

float ChouchouDelayAudioProcessor::getEchoTotalGain (int echo) const
{
    if (echo > getEchoCount() || ! isEchoOn (echo))
        return 0.0f;
    const float g = getEchoDecayGain (echo) * getEchoLevelGain (echo);
    return g < kSilenceGain ? 0.0f : g;
}

double ChouchouDelayAudioProcessor::getTailLengthSeconds() const
{
    double maxT = 0.0;
    for (int e = 1; e <= getEchoCount(); ++e)
        maxT = juce::jmax (maxT, getEchoTimeSeconds (e));
    return maxT + kPitchWindowSeconds;
}

//==============================================================================
int ChouchouDelayAudioProcessor::computeBufferLength (int echoCapacity, double sampleRate)
{
    const double span = ((double) echoCapacity + kMaxOffsetPercent / 100.0) * kMaxIntervalSeconds + kMarginSeconds;
    return (int) std::ceil (span * sampleRate) + 16;
}

juce::int64 ChouchouDelayAudioProcessor::getBufferBytes() const
{
    return (juce::int64) activeLength.load() * (juce::int64) activeChannels.load() * (juce::int64) sizeof (float);
}

bool ChouchouDelayAudioProcessor::isBufferResizePending() const
{
    return pending.load() != nullptr || activeCapacity.load() != getEchoCount();
}

std::unique_ptr<ChouchouDelayAudioProcessor::DelayStore>
ChouchouDelayAudioProcessor::makeStore (int echoCapacity, double sampleRate, int numChannels)
{
    auto s = std::make_unique<DelayStore>();
    s->numChannels = juce::jlimit (1, 2, numChannels);
    s->length = computeBufferLength (echoCapacity, sampleRate);
    s->capacityEchoes = echoCapacity;
    for (int c = 0; c < s->numChannels; ++c)
        s->ch[(size_t) c].calloc ((size_t) s->length);
    return s;
}

void ChouchouDelayAudioProcessor::freeAllStores()
{
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    activePtr.store (nullptr);
    delete active;
    active = nullptr;
    activeCapacity.store (0);
    activeLength.store (0);
    activeChannels.store (0);
}

void ChouchouDelayAudioProcessor::ResizeWorker::run()
{
    while (! threadShouldExit())
    {
        owner.workerTick();
        wait (25);
    }
}

void ChouchouDelayAudioProcessor::workerTick()
{
    delete retired.exchange (nullptr, std::memory_order_acq_rel);

    if (pending.load (std::memory_order_acquire) != nullptr)
        return;

    DelayStore* a = activePtr.load (std::memory_order_acquire);
    if (a == nullptr)
        return;

    const int desired = getEchoCount();
    if (desired == a->capacityEchoes)
        return;

    const double sr = currentSampleRate.load();
    auto b = makeStore (desired, sr, a->numChannels);
    if (worker.threadShouldExit())
        return;

    // Copy the newest history. The audio thread only overwrites the oldest part of `a`,
    // so staying `safety` samples away from it keeps the copied region untouched.
    const juce::int64 w0 = publishedWritten.load (std::memory_order_acquire);
    const juce::int64 safety = (juce::int64) (2.0 * sr);
    juce::int64 hist = juce::jmin ((juce::int64) b->length, (juce::int64) a->length - safety, w0);

    if (hist > 0)
    {
        for (int c = 0; c < b->numChannels; ++c)
        {
            const float* src = a->ch[(size_t) c].get();
            float* dst = b->ch[(size_t) c].get();
            juce::int64 abs = w0 - hist;
            int si = (int) (abs % a->length);
            int di = (int) (abs % b->length);
            juce::int64 remaining = hist;

            while (remaining > 0)
            {
                const int chunk = (int) juce::jmin (remaining, (juce::int64) (a->length - si), (juce::int64) (b->length - di));
                std::memcpy (dst + di, src + si, sizeof (float) * (size_t) chunk);
                remaining -= chunk;
                si += chunk; if (si >= a->length) si = 0;
                di += chunk; if (di >= b->length) di = 0;
            }
        }
    }

    b->syncedTo = w0;
    b->written = w0;
    pending.store (b.release(), std::memory_order_release);
}

void ChouchouDelayAudioProcessor::adoptPendingStore()
{
    DelayStore* p = pending.load (std::memory_order_acquire);
    if (p == nullptr || retired.load (std::memory_order_acquire) != nullptr || active == nullptr)
        return;

    DelayStore* a = active;

    // Bring over the samples written while the worker was copying.
    const juce::int64 from = juce::jmax (p->syncedTo, a->written - (juce::int64) juce::jmin (a->length, p->length));
    for (int c = 0; c < p->numChannels; ++c)
    {
        const float* src = a->ch[(size_t) juce::jmin (c, a->numChannels - 1)].get();
        float* dst = p->ch[(size_t) c].get();
        for (juce::int64 abs = from; abs < a->written; ++abs)
            dst[abs % p->length] = src[abs % a->length];
    }

    p->written = a->written;
    active = p;
    activePtr.store (p, std::memory_order_release);
    activeCapacity.store (p->capacityEchoes);
    activeLength.store (p->length);
    activeChannels.store (p->numChannels);
    pending.store (nullptr, std::memory_order_release);
    retired.store (a, std::memory_order_release);
    worker.notify();
}

//==============================================================================
void ChouchouDelayAudioProcessor::prepareToPlay (double sampleRate, int)
{
    worker.stopThread (4000);
    freeAllStores();

    currentSampleRate.store (sampleRate > 0.0 ? sampleRate : 48000.0);
    const double sr = currentSampleRate.load();

    const int chans = juce::jlimit (1, 2, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    active = makeStore (getEchoCount(), sr, chans).release();
    activePtr.store (active);
    activeCapacity.store (active->capacityEchoes);
    activeLength.store (active->length);
    activeChannels.store (active->numChannels);
    publishedWritten.store (0);

    for (auto& t : taps)
        t = TapState {};
    updateTapTargets (*active, true);

    mixSmoothed.reset (sr, 0.03);
    mixSmoothed.setCurrentAndTargetValue (pMix->load() / 100.0f);

    envRing.assign (800, 0.0f);
    envWork.assign (800, 0.0f);
    envPos = envCount = hopFill = envSince = 0;
    hopSum = 0.0f;

    worker.startThread();
}

void ChouchouDelayAudioProcessor::releaseResources()
{
}

bool ChouchouDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void ChouchouDelayAudioProcessor::updateTapTargets (const DelayStore& store, bool snap)
{
    const double sr = currentSampleRate.load();
    const int count = getEchoCount();
    const double decay = juce::jmax (kMinDecaySeconds, (double) pDecay->load());
    const double maxDelay = (double) store.length - 4.0;
    const double wMax = kPitchWindowSeconds * sr;
    const int gainRampLen  = juce::jmax (1, (int) (kGainRampSeconds * sr));
    const int delayRampLen = juce::jmax (1, (int) (kDelayRampSeconds * sr));

    for (int t = 0; t < kMaxEchoes; ++t)
    {
        const int e = t + 1;
        auto& tp = taps[(size_t) t];

        double targetDelay = getEchoDelaySamples (e);
        const double semis = getEchoPitchSemitones (e);
        const double ratio = std::pow (2.0, semis / 12.0);
        const bool pitched = std::abs (semis) > 1.0e-6;
        const double window = juce::jmin (wMax, juce::jmax (32.0, 2.0 * (targetDelay - 2.0)));

        float g = 0.0f;
        if (e <= count && pOn[(size_t) t]->load() > 0.5f)
        {
            const float lvlDb = pLevel[(size_t) t]->load();
            const float lvl = lvlDb <= kLevelFloorDb ? 0.0f : juce::Decibels::decibelsToGain (lvlDb);
            g = (float) std::pow (10.0, (kDecayTargetDb / 20.0) * (targetDelay / sr) / decay) * lvl;
            if (g < kSilenceGain)
                g = 0.0f;
        }

        const double need = targetDelay + (pitched ? window * 0.5 : 0.0);
        if (need > maxDelay)
        {
            g = 0.0f;
            targetDelay = juce::jmax (1.0, juce::jmin (targetDelay, maxDelay - wMax * 0.5));
        }

        const float pan = juce::jlimit (-1.0f, 1.0f, pPan[(size_t) t]->load());
        tp.panL = juce::jmin (1.0f, 1.0f - pan);
        tp.panR = juce::jmin (1.0f, 1.0f + pan);

        tp.ratio = pitched ? ratio : 1.0;
        tp.window = window;
        tp.phaseInc = pitched ? (1.0 - ratio) / window : 0.0;

        if (snap)
        {
            tp.gain = tp.gainTarget = g;
            tp.gainRamp = 0;
        }
        else if (g != tp.gainTarget)
        {
            tp.gainTarget = g;
            tp.gainRamp = gainRampLen;
            tp.gainStep = (g - tp.gain) / (float) gainRampLen;
        }

        const bool silentNow = tp.gain == 0.0f && tp.gainRamp == 0;
        if (snap || silentNow)
        {
            tp.delay = tp.delayTarget = targetDelay;
            tp.delayRamp = 0;
        }
        else if (targetDelay != tp.delayTarget)
        {
            tp.delayTarget = targetDelay;
            tp.delayRamp = delayRampLen;
            tp.delayStep = (targetDelay - tp.delay) / (double) delayRampLen;
        }
    }
}

void ChouchouDelayAudioProcessor::analyseTempo (const float* left, const float* right, int numSamples, int numChannels)
{
    if (envRing.empty() || left == nullptr || numSamples <= 0)
        return;

    const int hop = juce::jmax (1, (int) std::lround (currentSampleRate.load() / 200.0));
    const int len = (int) envRing.size();

    for (int i = 0; i < numSamples; ++i)
    {
        float s = std::abs (left[i]);
        if (numChannels > 1 && right != nullptr)
            s = 0.5f * (s + std::abs (right[i]));
        hopSum += s;
        if (++hopFill < hop)
            continue;

        envRing[(size_t) envPos] = hopSum / (float) hop;
        envPos = (envPos + 1) % len;
        if (envCount < len)
            ++envCount;
        hopSum = 0.0f;
        hopFill = 0;
        if (++envSince >= 50 && envCount > 300)
        {
            envSince = 0;
            estimateBpm();
        }
    }
}

void ChouchouDelayAudioProcessor::estimateBpm()
{
    const int n = envCount;
    const int len = (int) envRing.size();
    if (n < 300 || (int) envWork.size() < n)
        return;

    double mean = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const int idx = (envPos - n + i + len * 2) % len;
        envWork[(size_t) i] = envRing[(size_t) idx];
        mean += envWork[(size_t) i];
    }
    mean /= (double) n;

    float energy = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        envWork[(size_t) i] -= (float) mean;
        energy += envWork[(size_t) i] * envWork[(size_t) i];
    }
    if (energy < 1.0e-6f)
        return;

    const int lagMin = 50;
    const int lagMax = juce::jmin (n / 2, 400);
    float best = 0.0f;
    int bestLag = 0;
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        float c = 0.0f;
        const int m = n - lag;
        for (int i = 0; i < m; ++i)
            c += envWork[(size_t) i] * envWork[(size_t) (i + lag)];
        if (c > best)
        {
            best = c;
            bestLag = lag;
        }
    }
    if (bestLag < lagMin || best < energy * 0.12f)
        return;

    if (bestLag / 2 >= lagMin)
    {
        float half = 0.0f;
        const int lag2 = bestLag / 2;
        for (int i = 0; i < n - lag2; ++i)
            half += envWork[(size_t) i] * envWork[(size_t) (i + lag2)];
        if (half > best * 0.72f)
            bestLag = lag2;
    }

    const float bpm = 60.0f * 200.0f / (float) bestLag;
    const float prev = detectedBpm.load();
    detectedBpm.store (juce::jlimit (30.0f, 300.0f, prev + 0.45f * (bpm - prev)));
}

void ChouchouDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, numSamples);

    adoptPendingStore();

    DelayStore* store = active;
    const int chans = juce::jmin (buffer.getNumChannels(), 2);
    if (store == nullptr || numSamples == 0 || chans == 0)
        return;

    updateTapTargets (*store, false);
    mixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, pMix->load() / 100.0f));
    if (getTempoMode() == 1)
        analyseTempo (buffer.getReadPointer (0), chans > 1 ? buffer.getReadPointer (1) : nullptr, numSamples, chans);

    float* io[2] = { buffer.getWritePointer (0), chans > 1 ? buffer.getWritePointer (1) : nullptr };
    float* line[2] = { store->ch[0].get(), store->numChannels > 1 ? store->ch[1].get() : store->ch[0].get() };
    const int storeCh = store->numChannels;
    const int len = store->length;
    const double maxDelay = (double) len - 4.0;
    int w = (int) (store->written % len);

    for (int s = 0; s < numSamples; ++s)
    {
        float in[2] = { io[0][s], chans > 1 ? io[1][s] : 0.0f };
        line[0][w] = in[0];
        if (storeCh > 1)
            line[1][w] = chans > 1 ? in[1] : in[0];

        float wet[2] = { 0.0f, 0.0f };

        for (auto& tp : taps)
        {
            if (tp.gainRamp > 0)
            {
                tp.gain += tp.gainStep;
                if (--tp.gainRamp == 0)
                    tp.gain = tp.gainTarget;
            }
            if (tp.delayRamp > 0)
            {
                tp.delay += tp.delayStep;
                if (--tp.delayRamp == 0)
                    tp.delay = tp.delayTarget;
            }

            if (tp.gain == 0.0f)
                continue;

            const float panGain[2] = { chans > 1 ? tp.panL : 1.0f, tp.panR };
            if (tp.ratio == 1.0)
            {
                const double d = juce::jlimit (1.0, maxDelay, tp.delay);
                for (int c = 0; c < chans; ++c)
                    wet[c] += tp.gain * panGain[c] * readHermite (line[c], len, w, d);
            }
            else
            {
                double ph[2] = { tp.phase, tp.phase + 0.5 };
                if (ph[1] >= 1.0) ph[1] -= 1.0;

                for (int k = 0; k < 2; ++k)
                {
                    const double d = juce::jlimit (1.0, maxDelay, tp.delay + (ph[k] - 0.5) * tp.window);
                    const float sn = (float) std::sin (juce::MathConstants<double>::pi * ph[k]);
                    const float hg = tp.gain * sn * sn;
                    for (int c = 0; c < chans; ++c)
                        wet[c] += hg * panGain[c] * readHermite (line[c], len, w, d);
                }

                tp.phase += tp.phaseInc;
                if (tp.phase >= 1.0) tp.phase -= 1.0;
                else if (tp.phase < 0.0) tp.phase += 1.0;
            }
        }

        const float m = mixSmoothed.getNextValue();
        float dryG, wetG;
        if (m <= 0.0f)      { dryG = 1.0f; wetG = 0.0f; }
        else if (m >= 1.0f) { dryG = 0.0f; wetG = 1.0f; }
        else
        {
            dryG = std::cos (m * juce::MathConstants<float>::halfPi);
            wetG = std::sin (m * juce::MathConstants<float>::halfPi);
        }

        for (int c = 0; c < chans; ++c)
            io[c][s] = in[c] * dryG + wet[c] * wetG;

        if (++w == len)
            w = 0;
    }

    store->written += numSamples;
    publishedWritten.store (store->written, std::memory_order_release);
}

//==============================================================================
juce::AudioProcessorEditor* ChouchouDelayAudioProcessor::createEditor()
{
    return new ChouchouDelayAudioProcessorEditor (*this);
}

void ChouchouDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ChouchouDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChouchouDelayAudioProcessor();
}
