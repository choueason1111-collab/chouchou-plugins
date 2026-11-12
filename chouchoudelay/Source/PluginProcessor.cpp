#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginScanSupport.h"

#include <cmath>

namespace
{
    constexpr const char* kStateTag = "chouchouDelay";
    constexpr const char* kEchoTag = "ECHO";
    constexpr const char* kSlotTag = "SLOT";
    constexpr const char* kInnerStateTag = "STATE";
    constexpr const char* kPluginListKey = "pluginList";
}

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

    pGateAmt = apvts.getRawParameterValue ("gateAmt");
    pGateThresh = apvts.getRawParameterValue ("gateThresh");
    pGateAtk = apvts.getRawParameterValue ("gateAtk");
    pGateRel = apvts.getRawParameterValue ("gateRel");
    pGateMix = apvts.getRawParameterValue ("gateMix");
    pGateMakeup = apvts.getRawParameterValue ("gateMakeup");
    pGateAutoGain = apvts.getRawParameterValue ("gateAutoGain");
    pGateFft = apvts.getRawParameterValue ("gateFft");

    pCompLowThresh = apvts.getRawParameterValue ("compLowThresh");
    pCompUpRatio = apvts.getRawParameterValue ("compUpRatio");
    pCompBoostAtk = apvts.getRawParameterValue ("compBoostAtk");
    pCompBoostRel = apvts.getRawParameterValue ("compBoostRel");
    pCompBoostSpeed = apvts.getRawParameterValue ("compBoostSpeed");
    pCompHighThresh = apvts.getRawParameterValue ("compHighThresh");
    pCompDownRatio = apvts.getRawParameterValue ("compDownRatio");
    pCompGrAtk = apvts.getRawParameterValue ("compGrAtk");
    pCompGrRel = apvts.getRawParameterValue ("compGrRel");
    pCompSpeed = apvts.getRawParameterValue ("compSpeed");
    pCompMix = apvts.getRawParameterValue ("compMix");
    pCompMakeup = apvts.getRawParameterValue ("compMakeup");
    pCompAutoGain = apvts.getRawParameterValue ("compAutoGain");
    pCompFft = apvts.getRawParameterValue ("compFft");

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
        pGateOn[(size_t) t] = apvts.getRawParameterValue (idGateOn (e));
        pCompOn[(size_t) t] = apvts.getRawParameterValue (idCompOn (e));
        pInsBypass[(size_t) t] = apvts.getRawParameterValue (idInsBypass (e));
    }

    if (! apvts.state.hasProperty ("intervalUnit"))
        apvts.state.setProperty ("intervalUnit", "ms", nullptr);

    juce::addDefaultFormatsToManager (formatManager);
    {
        juce::PropertiesFile::Options opt;
        opt.applicationName = "chouchouDelay";
        opt.filenameSuffix = "settings";
        opt.osxLibrarySubFolder = "Application Support";
        opt.folderName = "chouchou";
        appProperties.setStorageParameters (opt);
        if (auto* settings = appProperties.getUserSettings())
            if (auto saved = settings->getXmlValue (kPluginListKey))
                pluginList.recreateFromXml (*saved);
    }
    prepareChouChouPluginScanning (pluginList);
    pluginList.addChangeListener (this);
}

ChouchouDelayAudioProcessor::~ChouchouDelayAudioProcessor()
{
    aliveFlag->store (false, std::memory_order_release);
    pluginList.removeChangeListener (this);
    worker.stopThread (4000);
    {
        const juce::ScopedLock sl (insertLock);
        for (auto& fx : echoFx)
            releaseInsert (fx.insert);
    }
    freeAllStores();
}

int ChouchouDelayAudioProcessor::fftSizeChoiceToOrder (int choiceIndex) noexcept
{
    switch (juce::jlimit (0, 3, choiceIndex))
    {
        case 0:  return 9;
        case 1:  return 10;
        case 3:  return 12;
        default: return 11;
    }
}

bool ChouchouDelayAudioProcessor::isSelfDescription (const juce::PluginDescription& pd)
{
    if (pd.name.containsIgnoreCase (JucePlugin_Name))
        return true;
    if (pd.fileOrIdentifier.containsIgnoreCase ("chouchouDelay"))
        return true;
    if (pd.manufacturerName.containsIgnoreCase (JucePlugin_Manufacturer)
        && pd.name.containsIgnoreCase ("Delay"))
        return true;
    if (pd.uniqueId != 0 && pd.uniqueId == (int) JucePlugin_PluginCode)
        return true;
    return false;
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

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { idGateOn (e), 1 }, "Echo " + n + " Gate", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { idCompOn (e), 1 }, "Echo " + n + " Comp", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { idInsBypass (e), 1 }, "Echo " + n + " Insert Bypass", false));
    }

    // Global Gate
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gateAmt", 1 }, "Gate Amount",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f, 0.25f }, 0.8f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gateThresh", 1 }, "Gate Threshold",
        juce::NormalisableRange<float> { -120.0f, 12.0f, 0.1f }, -40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dBFS")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gateAtk", 1 }, "Gate Attack",
        juce::NormalisableRange<float> { 0.0f, 500.0f, 0.01f, 0.35f }, 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gateRel", 1 }, "Gate Release",
        juce::NormalisableRange<float> { 0.0f, 2000.0f, 0.01f, 0.3f }, 80.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gateMix", 1 }, "Gate Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gateMakeup", 1 }, "Gate Makeup",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "gateAutoGain", 1 }, "Gate Auto Gain", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "gateFft", 1 }, "Gate FFT",
        juce::StringArray { "512", "1024", "2048", "4096" }, 0));

    // Global Comp
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compLowThresh", 1 }, "Comp Boost Thresh",
        juce::NormalisableRange<float> { -120.0f, 0.0f, 0.1f }, -40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dBFS")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compUpRatio", 1 }, "Comp Boost Ratio",
        juce::NormalisableRange<float> { 1.0f, 20.0f, 0.01f, 0.4f }, 2.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compBoostAtk", 1 }, "Comp Boost Attack",
        juce::NormalisableRange<float> { 0.0f, 500.0f, 0.01f, 0.35f }, 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compBoostRel", 1 }, "Comp Boost Release",
        juce::NormalisableRange<float> { 0.0f, 2000.0f, 0.01f, 0.3f }, 80.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compBoostSpeed", 1 }, "Comp Boost Speed",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compHighThresh", 1 }, "Comp Thresh",
        juce::NormalisableRange<float> { -60.0f, 0.0f, 0.1f }, -12.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dBFS")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compDownRatio", 1 }, "Comp Ratio",
        juce::NormalisableRange<float> { 1.0f, 20.0f, 0.01f, 0.4f }, 4.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compGrAtk", 1 }, "Comp Attack",
        juce::NormalisableRange<float> { 0.0f, 500.0f, 0.01f, 0.35f }, 10.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compGrRel", 1 }, "Comp Release",
        juce::NormalisableRange<float> { 0.0f, 2000.0f, 0.01f, 0.3f }, 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compSpeed", 1 }, "Comp Speed",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compMix", 1 }, "Comp Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "compMakeup", 1 }, "Comp Makeup",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "compAutoGain", 1 }, "Comp Auto Gain", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "compFft", 1 }, "Comp FFT",
        juce::StringArray { "512", "1024", "2048", "4096" }, 0));

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
    double insertTail = 0.0;
    {
        const juce::ScopedLock sl (insertLock);
        for (int e = 1; e <= getEchoCount(); ++e)
            if (auto inst = echoFx[(size_t) (e - 1)].insert.instance)
                if (! echoFx[(size_t) (e - 1)].insert.bypassed)
                    insertTail = juce::jmax (insertTail, inst->getTailLengthSeconds());
    }
    return maxT + kPitchWindowSeconds + insertTail
         + (double) reportedFxLatency.load() / juce::jmax (1.0, currentSampleRate.load());
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
void ChouchouDelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    worker.stopThread (4000);
    freeAllStores();

    currentSampleRate.store (sampleRate > 0.0 ? sampleRate : 48000.0);
    preparedBlockSize = juce::jmax (16, samplesPerBlock);
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

    prepareEchoFx (sr, preparedBlockSize, chans);
    worker.startThread();
}

void ChouchouDelayAudioProcessor::releaseResources()
{
    fxPrepared = false;
}

void ChouchouDelayAudioProcessor::prepareEchoFx (double sr, int blockSize, int numChannels)
{
    const int ch = juce::jlimit (1, 2, numChannels);
    const int gateOrder = fftSizeChoiceToOrder ((int) std::lround (pGateFft->load()));
    const int compOrder = fftSizeChoiceToOrder ((int) std::lround (pCompFft->load()));

    wetSumBuf.setSize (ch, blockSize, false, false, true);
    insertScratch.setSize (ch, blockSize, false, false, true);

    for (auto& fx : echoFx)
    {
        fx.gate.prepare (sr, gateOrder, ch);
        fx.comp.prepare (sr, compOrder, ch);
        fx.buf.setSize (ch, blockSize, false, false, true);
        fx.buf.clear();

        const juce::ScopedLock sl (insertLock);
        if (fx.insert.instance != nullptr)
        {
            configureInsertBuses (*fx.insert.instance, ch);
            prepareInsertInstance (*fx.insert.instance, sr, blockSize);
        }
    }

    dryDelaySize = 0;
    dryDelayWrite = 0;
    dryDelayBuf.setSize (0, 0);
    fxPrepared = true;
    updateReportedLatency();
}

void ChouchouDelayAudioProcessor::applyFxFftSizes()
{
    const int gateOrder = fftSizeChoiceToOrder ((int) std::lround (pGateFft->load()));
    const int compOrder = fftSizeChoiceToOrder ((int) std::lround (pCompFft->load()));
    bool changed = false;
    for (auto& fx : echoFx)
    {
        const int prevG = fx.gate.getFftSize();
        const int prevC = fx.comp.getFftSize();
        fx.gate.setFftOrder (gateOrder);
        fx.comp.setFftOrder (compOrder);
        changed = changed || prevG != fx.gate.getFftSize() || prevC != fx.comp.getFftSize();
    }
    if (changed)
        updateReportedLatency();
}

void ChouchouDelayAudioProcessor::updateReportedLatency()
{
    int lat = 0;
    const int count = getEchoCount();
    for (int t = 0; t < count; ++t)
    {
        if (pGateOn[(size_t) t]->load() > 0.5f)
            lat = juce::jmax (lat, echoFx[(size_t) t].gate.getLatency());
        if (pCompOn[(size_t) t]->load() > 0.5f)
            lat = juce::jmax (lat, echoFx[(size_t) t].comp.getLatency());

        const juce::ScopedLock sl (insertLock);
        if (echoFx[(size_t) t].insert.instance != nullptr && ! echoFx[(size_t) t].insert.bypassed
            && pInsBypass[(size_t) t]->load() <= 0.5f)
            lat = juce::jmax (lat, echoFx[(size_t) t].insert.instance->getLatencySamples());
    }

    if (lat != dryDelaySize)
    {
        dryDelaySize = lat;
        dryDelayWrite = 0;
        const int ch = juce::jmax (1, wetSumBuf.getNumChannels());
        dryDelayBuf.setSize (ch, juce::jmax (1, lat), false, true, true);
        dryDelayBuf.clear();
    }

    reportedFxLatency.store (lat);
    setLatencySamples (lat);
}

void ChouchouDelayAudioProcessor::processEchoFx (int tapIndex, int numSamples, int numChannels, juce::MidiBuffer& midi)
{
    auto& fx = echoFx[(size_t) tapIndex];
    auto& buf = fx.buf;
    if (buf.getNumSamples() < numSamples || buf.getNumChannels() < numChannels)
        buf.setSize (numChannels, numSamples, false, false, true);

    if (pGateOn[(size_t) tapIndex]->load() > 0.5f)
    {
        GateParams gp;
        gp.amount = pGateAmt->load();
        gp.thresholdDb = pGateThresh->load();
        gp.attackMs = pGateAtk->load();
        gp.releaseMs = pGateRel->load();
        gp.mix = pGateMix->load();
        gp.makeupDb = pGateMakeup->load();
        gp.autoGain = pGateAutoGain->load() > 0.5f;
        fx.gate.process (buf, gp);
    }

    if (pCompOn[(size_t) tapIndex]->load() > 0.5f)
    {
        CompParams cp;
        cp.boostThreshDb = pCompLowThresh->load();
        cp.boostRatio = pCompUpRatio->load();
        cp.boostAtkMs = pCompBoostAtk->load();
        cp.boostRelMs = pCompBoostRel->load();
        cp.boostSpeed = pCompBoostSpeed->load();
        cp.compThreshDb = pCompHighThresh->load();
        cp.compRatio = pCompDownRatio->load();
        cp.compAtkMs = pCompGrAtk->load();
        cp.compRelMs = pCompGrRel->load();
        cp.compSpeed = pCompSpeed->load();
        cp.mix = pCompMix->load();
        cp.makeupDb = pCompMakeup->load();
        cp.autoGain = pCompAutoGain->load() > 0.5f;
        fx.comp.process (buf, cp);
    }

    std::shared_ptr<juce::AudioPluginInstance> insert;
    bool bypassed = true;
    {
        const juce::ScopedLock sl (insertLock);
        insert = fx.insert.instance;
        bypassed = fx.insert.bypassed || pInsBypass[(size_t) tapIndex]->load() > 0.5f;
    }

    if (insert != nullptr && ! bypassed)
        processHostedInsert (*insert, buf, midi, insertScratch);
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

void ChouchouDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
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

    if (fxPrepared)
        applyFxFftSizes();

    updateTapTargets (*store, false);
    mixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, pMix->load() / 100.0f));
    if (getTempoMode() == 1)
        analyseTempo (buffer.getReadPointer (0), chans > 1 ? buffer.getReadPointer (1) : nullptr, numSamples, chans);

    if (wetSumBuf.getNumChannels() < chans || wetSumBuf.getNumSamples() < numSamples)
        wetSumBuf.setSize (chans, numSamples, false, false, true);
    wetSumBuf.clear();

    for (int t = 0; t < kMaxEchoes; ++t)
    {
        auto& buf = echoFx[(size_t) t].buf;
        if (buf.getNumChannels() < chans || buf.getNumSamples() < numSamples)
            buf.setSize (chans, numSamples, false, false, true);
        buf.clear();
    }

    float* io[2] = { buffer.getWritePointer (0), chans > 1 ? buffer.getWritePointer (1) : nullptr };
    float* line[2] = { store->ch[0].get(), store->numChannels > 1 ? store->ch[1].get() : store->ch[0].get() };
    const int storeCh = store->numChannels;
    const int len = store->length;
    const double maxDelay = (double) len - 4.0;
    int w = (int) (store->written % len);

    // Snapshot dry input (before we overwrite buffer at mix stage)
    juce::HeapBlock<float> drySnap[2];
    for (int c = 0; c < chans; ++c)
    {
        drySnap[c].allocate ((size_t) numSamples, false);
        juce::FloatVectorOperations::copy (drySnap[c].get(), io[c], numSamples);
    }

    for (int s = 0; s < numSamples; ++s)
    {
        float in[2] = { drySnap[0][s], chans > 1 ? drySnap[1][s] : 0.0f };
        line[0][w] = in[0];
        if (storeCh > 1)
            line[1][w] = chans > 1 ? in[1] : in[0];

        for (int t = 0; t < kMaxEchoes; ++t)
        {
            auto& tp = taps[(size_t) t];
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

            float* echoL = echoFx[(size_t) t].buf.getWritePointer (0);
            float* echoR = chans > 1 ? echoFx[(size_t) t].buf.getWritePointer (1) : nullptr;
            const float panGain[2] = { chans > 1 ? tp.panL : 1.0f, tp.panR };

            if (tp.ratio == 1.0)
            {
                const double d = juce::jlimit (1.0, maxDelay, tp.delay);
                echoL[s] += tp.gain * panGain[0] * readHermite (line[0], len, w, d);
                if (echoR != nullptr)
                    echoR[s] += tp.gain * panGain[1] * readHermite (line[1], len, w, d);
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
                    echoL[s] += hg * panGain[0] * readHermite (line[0], len, w, d);
                    if (echoR != nullptr)
                        echoR[s] += hg * panGain[1] * readHermite (line[1], len, w, d);
                }

                tp.phase += tp.phaseInc;
                if (tp.phase >= 1.0) tp.phase -= 1.0;
                else if (tp.phase < 0.0) tp.phase += 1.0;
            }
        }

        if (++w == len)
            w = 0;
    }

    store->written += numSamples;
    publishedWritten.store (store->written, std::memory_order_release);

    const int echoCount = getEchoCount();
    juce::MidiBuffer emptyMidi;
    for (int t = 0; t < echoCount; ++t)
    {
        const bool needsFx = pGateOn[(size_t) t]->load() > 0.5f
                          || pCompOn[(size_t) t]->load() > 0.5f
                          || isEchoInsertLoaded (t + 1);
        if (needsFx)
            processEchoFx (t, numSamples, chans, emptyMidi);

        for (int c = 0; c < chans; ++c)
            wetSumBuf.addFrom (c, 0, echoFx[(size_t) t].buf, c, 0, numSamples);
    }

    // Align dry with max FX latency when any FX is active
    const int lat = dryDelaySize;
    for (int s = 0; s < numSamples; ++s)
    {
        float delayedDry[2] {};
        for (int c = 0; c < chans; ++c)
        {
            if (lat > 0 && dryDelayBuf.getNumSamples() >= lat)
            {
                delayedDry[c] = dryDelayBuf.getSample (c, dryDelayWrite);
                dryDelayBuf.setSample (c, dryDelayWrite, drySnap[c][s]);
            }
            else
                delayedDry[c] = drySnap[c][s];
        }
        if (lat > 0)
            dryDelayWrite = (dryDelayWrite + 1) % lat;

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
            io[c][s] = delayedDry[c] * dryG + wetSumBuf.getSample (c, s) * wetG;
    }
}

//==============================================================================
juce::AudioProcessorEditor* ChouchouDelayAudioProcessor::createEditor()
{
    return new ChouchouDelayAudioProcessorEditor (*this);
}

bool ChouchouDelayAudioProcessor::isEchoGateOn (int echo) const
{
    if (echo < 1 || echo > kMaxEchoes) return false;
    return pGateOn[(size_t) (echo - 1)]->load() > 0.5f;
}

bool ChouchouDelayAudioProcessor::isEchoCompOn (int echo) const
{
    if (echo < 1 || echo > kMaxEchoes) return false;
    return pCompOn[(size_t) (echo - 1)]->load() > 0.5f;
}

bool ChouchouDelayAudioProcessor::isEchoInsertBypassed (int echo) const
{
    if (echo < 1 || echo > kMaxEchoes) return true;
    if (pInsBypass[(size_t) (echo - 1)]->load() > 0.5f)
        return true;
    const juce::ScopedLock sl (insertLock);
    return echoFx[(size_t) (echo - 1)].insert.bypassed;
}

bool ChouchouDelayAudioProcessor::isEchoInsertLoaded (int echo) const
{
    if (echo < 1 || echo > kMaxEchoes) return false;
    const juce::ScopedLock sl (insertLock);
    return echoFx[(size_t) (echo - 1)].insert.instance != nullptr;
}

juce::String ChouchouDelayAudioProcessor::getEchoInsertName (int echo) const
{
    if (echo < 1 || echo > kMaxEchoes) return {};
    const juce::ScopedLock sl (insertLock);
    const auto& slot = echoFx[(size_t) (echo - 1)].insert;
    if (slot.instance == nullptr)
        return {};
    auto name = slot.description.name;
    if (name.isEmpty())
        name = slot.instance->getName();
    return name;
}

void ChouchouDelayAudioProcessor::releaseInsert (EchoInsertSlot& slot)
{
    if (slot.instance != nullptr)
    {
        slot.instance->releaseResources();
        slot.instance.reset();
    }
    slot.description = {};
    slot.bypassed = false;
}

uint64_t ChouchouDelayAudioProcessor::beginInsertLoad (int echo)
{
    auto& slot = echoFx[(size_t) (echo - 1)].insert;
    slot.loadGeneration = nextLoadId.fetch_add (1, std::memory_order_relaxed);
    return slot.loadGeneration;
}

bool ChouchouDelayAudioProcessor::isInsertLoadCurrent (int echo, uint64_t requestId) const
{
    return echoFx[(size_t) (echo - 1)].insert.loadGeneration == requestId;
}

void ChouchouDelayAudioProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto* settings = appProperties.getUserSettings())
        if (auto xml = pluginList.createXml())
            settings->setValue (kPluginListKey, xml.get());
}

void ChouchouDelayAudioProcessor::setUiStatus (const juce::String& text)
{
    const juce::ScopedLock sl (statusLock);
    uiStatus = text;
}

juce::String ChouchouDelayAudioProcessor::getUiStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return uiStatus;
}

void ChouchouDelayAudioProcessor::loadEchoInsertAsync (int echo, const juce::PluginDescription& desc)
{
    if (echo < 1 || echo > kMaxEchoes)
        return;
    if (isSelfDescription (desc))
    {
        setUiStatus ("Cannot load chouchouDelay inside itself.");
        return;
    }

    const double sr = currentSampleRate.load() > 0.0 ? currentSampleRate.load() : 44100.0;
    const int bs = preparedBlockSize > 0 ? preparedBlockSize : 512;
    uint64_t requestId = 0;
    {
        const juce::ScopedLock sl (insertLock);
        requestId = beginInsertLoad (echo);
    }

    auto life = aliveFlag;
    const int echoIndex = echo;
    formatManager.createPluginInstanceAsync (
        desc, sr, bs,
        [life, this, echoIndex, requestId, pluginName = desc.name]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (! life->load (std::memory_order_acquire))
                return;
            if (error.isNotEmpty() || instance == nullptr)
            {
                setUiStatus ("Load failed: " + (error.isNotEmpty() ? error : pluginName));
                return;
            }
            if (! configureInsertBuses (*instance, juce::jmax (1, getMainBusNumOutputChannels())))
            {
                setUiStatus ("Load failed: bus config for " + pluginName);
                return;
            }
            auto shared = std::shared_ptr<juce::AudioPluginInstance> (std::move (instance));
            {
                const juce::ScopedLock sl (insertLock);
                if (! isInsertLoadCurrent (echoIndex, requestId))
                    return;
                auto& slot = echoFx[(size_t) (echoIndex - 1)].insert;
                releaseInsert (slot);
                slot.description = shared->getPluginDescription();
                slot.instance = std::move (shared);
                slot.bypassed = false;
                if (fxPrepared)
                    prepareInsertInstance (*slot.instance, currentSampleRate.load(), preparedBlockSize);
            }
            updateReportedLatency();
            setUiStatus ("Loaded on echo " + juce::String (echoIndex) + ": " + pluginName);
        });
}

bool ChouchouDelayAudioProcessor::loadEchoInsertBlocking (int echo,
                                                          const juce::PluginDescription& desc,
                                                          juce::String& error)
{
    error.clear();
    if (echo < 1 || echo > kMaxEchoes)
    {
        error = "Invalid echo";
        return false;
    }
    if (isSelfDescription (desc))
    {
        error = "Cannot load chouchouDelay inside itself.";
        return false;
    }

    const double sr = currentSampleRate.load() > 0.0 ? currentSampleRate.load() : 44100.0;
    const int bs = preparedBlockSize > 0 ? preparedBlockSize : 512;
    juce::String createError;
    auto instance = formatManager.createPluginInstance (desc, sr, bs, createError);
    if (instance == nullptr)
    {
        error = createError.isNotEmpty() ? createError : "createPluginInstance failed";
        return false;
    }
    if (! configureInsertBuses (*instance, juce::jmax (1, getMainBusNumOutputChannels())))
    {
        error = "Could not configure plugin buses";
        return false;
    }
    auto shared = std::shared_ptr<juce::AudioPluginInstance> (std::move (instance));
    {
        const juce::ScopedLock sl (insertLock);
        beginInsertLoad (echo);
        auto& slot = echoFx[(size_t) (echo - 1)].insert;
        releaseInsert (slot);
        slot.description = shared->getPluginDescription();
        slot.instance = std::move (shared);
        slot.bypassed = false;
        if (fxPrepared)
            prepareInsertInstance (*slot.instance, sr, bs);
    }
    updateReportedLatency();
    return true;
}

void ChouchouDelayAudioProcessor::setEchoInsertForTest (int echo,
                                                        std::shared_ptr<juce::AudioPluginInstance> instance)
{
    if (echo < 1 || echo > kMaxEchoes)
        return;
    {
        const juce::ScopedLock sl (insertLock);
        beginInsertLoad (echo);
        auto& slot = echoFx[(size_t) (echo - 1)].insert;
        releaseInsert (slot);
        if (instance != nullptr)
        {
            configureInsertBuses (*instance, juce::jmax (1, getMainBusNumOutputChannels()));
            slot.description = instance->getPluginDescription();
            slot.instance = std::move (instance);
            slot.bypassed = false;
            if (fxPrepared)
                prepareInsertInstance (*slot.instance, currentSampleRate.load(), preparedBlockSize);
        }
    }
    updateReportedLatency();
}

void ChouchouDelayAudioProcessor::clearEchoInsert (int echo)
{
    if (echo < 1 || echo > kMaxEchoes)
        return;
    {
        const juce::ScopedLock sl (insertLock);
        beginInsertLoad (echo);
        releaseInsert (echoFx[(size_t) (echo - 1)].insert);
    }
    updateReportedLatency();
}

void ChouchouDelayAudioProcessor::setEchoInsertBypassed (int echo, bool shouldBypass)
{
    if (echo < 1 || echo > kMaxEchoes)
        return;
    if (auto* p = apvts.getParameter (idInsBypass (echo)))
        p->setValueNotifyingHost (shouldBypass ? 1.0f : 0.0f);
    {
        const juce::ScopedLock sl (insertLock);
        echoFx[(size_t) (echo - 1)].insert.bypassed = shouldBypass;
    }
    updateReportedLatency();
}

std::unique_ptr<juce::AudioProcessorEditor> ChouchouDelayAudioProcessor::createEchoInsertEditor (int echo)
{
    if (echo < 1 || echo > kMaxEchoes)
        return {};
    const juce::ScopedLock sl (insertLock);
    if (auto inst = echoFx[(size_t) (echo - 1)].insert.instance)
        if (inst->hasEditor())
            return std::unique_ptr<juce::AudioProcessorEditor> (inst->createEditorAndMakeActive());
    return {};
}

std::shared_ptr<juce::AudioPluginInstance> ChouchouDelayAudioProcessor::getEchoInsertShared (int echo) const
{
    if (echo < 1 || echo > kMaxEchoes)
        return {};
    const juce::ScopedLock sl (insertLock);
    return echoFx[(size_t) (echo - 1)].insert.instance;
}

void ChouchouDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root (kStateTag);
    if (auto params = apvts.copyState().createXml())
    {
        params->setTagName ("PARAMS");
        root.addChildElement (params.release());
    }

    std::array<std::shared_ptr<juce::AudioPluginInstance>, kMaxEchoes> snap;
    std::array<juce::PluginDescription, kMaxEchoes> descs;
    std::array<bool, kMaxEchoes> bypass {};
    {
        const juce::ScopedLock sl (insertLock);
        for (int i = 0; i < kMaxEchoes; ++i)
        {
            snap[(size_t) i] = echoFx[(size_t) i].insert.instance;
            descs[(size_t) i] = echoFx[(size_t) i].insert.description;
            bypass[(size_t) i] = echoFx[(size_t) i].insert.bypassed;
        }
    }

    for (int i = 0; i < kMaxEchoes; ++i)
    {
        if (snap[(size_t) i] == nullptr)
            continue;
        auto* echoXml = root.createNewChildElement (kEchoTag);
        echoXml->setAttribute ("index", i + 1);
        auto* slotXml = echoXml->createNewChildElement (kSlotTag);
        slotXml->setAttribute ("bypassed", bypass[(size_t) i] ? 1 : 0);
        auto desc = descs[(size_t) i];
        if (desc.name.isEmpty())
            desc = snap[(size_t) i]->getPluginDescription();
        if (auto descXml = desc.createXml())
            slotXml->addChildElement (descXml.release());
        juce::MemoryBlock inner;
        snap[(size_t) i]->getStateInformation (inner);
        auto* stateNode = slotXml->createNewChildElement (kInnerStateTag);
        stateNode->addTextElement (inner.toBase64Encoding());
    }

    copyXmlToBinary (root, destData);
}

void ChouchouDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    // Legacy: APVTS-only state
    if (xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        return;
    }

    if (! xml->hasTagName (kStateTag))
        return;

    if (auto* params = xml->getChildByName ("PARAMS"))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    const uint64_t thisRestore = restoreGeneration.fetch_add (1, std::memory_order_relaxed) + 1;
    {
        const juce::ScopedLock sl (insertLock);
        for (int i = 0; i < kMaxEchoes; ++i)
        {
            beginInsertLoad (i + 1);
            releaseInsert (echoFx[(size_t) i].insert);
        }
    }
    updateReportedLatency();

    const double sr = currentSampleRate.load() > 0.0 ? currentSampleRate.load() : 44100.0;
    const int bs = preparedBlockSize > 0 ? preparedBlockSize : 512;
    auto life = aliveFlag;

    for (auto* echoXml : xml->getChildWithTagNameIterator (kEchoTag))
    {
        const int echo = echoXml->getIntAttribute ("index", -1);
        if (echo < 1 || echo > kMaxEchoes)
            continue;
        auto* slotXml = echoXml->getChildByName (kSlotTag);
        if (slotXml == nullptr)
            continue;
        const bool bypassed = slotXml->getIntAttribute ("bypassed", 0) != 0;
        auto* pluginNode = slotXml->getChildByName ("PLUGIN");
        if (pluginNode == nullptr)
            continue;
        juce::PluginDescription pd;
        if (! pd.loadFromXml (*pluginNode) || isSelfDescription (pd))
            continue;

        juce::MemoryBlock innerState;
        if (auto* stateNode = slotXml->getChildByName (kInnerStateTag))
            innerState.fromBase64Encoding (stateNode->getAllSubText());

        uint64_t requestId = 0;
        {
            const juce::ScopedLock sl (insertLock);
            requestId = beginInsertLoad (echo);
        }

        formatManager.createPluginInstanceAsync (
            pd, sr, bs,
            [life, this, echo, bypassed, innerState, requestId, thisRestore]
            (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
            {
                if (! life->load (std::memory_order_acquire))
                    return;
                if (restoreGeneration.load (std::memory_order_relaxed) != thisRestore)
                    return;
                if (error.isNotEmpty() || instance == nullptr)
                    return;
                if (! configureInsertBuses (*instance, juce::jmax (1, getMainBusNumOutputChannels())))
                    return;
                if (! innerState.isEmpty())
                    instance->setStateInformation (innerState.getData(), (int) innerState.getSize());
                auto shared = std::shared_ptr<juce::AudioPluginInstance> (std::move (instance));
                {
                    const juce::ScopedLock sl (insertLock);
                    if (! isInsertLoadCurrent (echo, requestId))
                        return;
                    auto& slot = echoFx[(size_t) (echo - 1)].insert;
                    releaseInsert (slot);
                    slot.description = shared->getPluginDescription();
                    slot.instance = std::move (shared);
                    slot.bypassed = bypassed;
                    if (fxPrepared)
                        prepareInsertInstance (*slot.instance, currentSampleRate.load(), preparedBlockSize);
                }
                updateReportedLatency();
            });
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChouchouDelayAudioProcessor();
}
