#include "EchoFx.h"
#include <cmath>

namespace
{
    constexpr float kEps = 1.0e-8f;
    constexpr float kGainFloor = 1.0e-4f;
    constexpr float kGainCeil = 32.0f;
    constexpr float kColaNorm = 2.0f / 3.0f;
    constexpr float kSpectralFullScale = 512.0f;
    constexpr float kSilencePeak = 1.0e-5f;
    constexpr float kMagRefFftSize = 2048.0f;
    constexpr float kBoostNoiseFloorDb = -90.0f;
    constexpr int kMinFftOrder = 9;
    constexpr int kMaxFftOrder = 12;

    float gateTimeMsToCoeff (float timeMs, float hopSeconds) noexcept
    {
        if (timeMs <= 0.0f)
            return 1.0f;
        return 1.0f - std::exp (-hopSeconds / (timeMs * 0.001f));
    }
}

//==============================================================================
void TapGateEngine::configureTransform (int order)
{
    order = juce::jlimit (kMinFftOrder, kMaxFftOrder, order);
    fftOrder = order;
    fftSize = 1 << fftOrder;
    hopSize = fftSize / 4;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
}

void TapGateEngine::resetChannel (ChannelState& ch)
{
    ch.inputFifo.assign ((size_t) fftSize, 0.0f);
    ch.ola.assign ((size_t) fftSize, 0.0f);
    ch.outFifo.assign ((size_t) hopSize, 0.0f);
    ch.fftData.assign ((size_t) fftSize * 2, 0.0f);
    ch.smoothedMag.assign ((size_t) (fftSize / 2 + 1), 0.0f);
    ch.dryDelay.assign ((size_t) fftSize, 0.0f);
    ch.window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        ch.window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) fftSize));
    ch.writePos = ch.outRead = ch.outAvailable = ch.dryWrite = 0;
}

void TapGateEngine::prepare (double sr, int order, int numChannels)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    agCoeff = 1.0f - std::exp (-1.0f / (0.03f * (float) juce::jmax (1.0, sampleRate)));
    inPower = wetPower = 1.0e-8f;
    configureTransform (order);
    channels.resize ((size_t) juce::jmax (1, numChannels));
    for (auto& ch : channels)
        resetChannel (ch);
}

void TapGateEngine::reset()
{
    inPower = wetPower = 1.0e-8f;
    for (auto& ch : channels)
        resetChannel (ch);
}

void TapGateEngine::setFftOrder (int order)
{
    if (juce::jlimit (kMinFftOrder, kMaxFftOrder, order) == fftOrder)
        return;
    configureTransform (order);
    for (auto& ch : channels)
        resetChannel (ch);
    inPower = wetPower = 1.0e-8f;
}

void TapGateEngine::processHop (ChannelState& ch, float amount, float thresholdLin, float mix,
                                float makeupLin, float attackCoeff, float releaseCoeff)
{
    if (fft == nullptr)
        return;

    for (int i = 0; i < fftSize; ++i)
        ch.fftData[(size_t) i] = ch.inputFifo[(size_t) i] * ch.window[(size_t) i];
    std::fill (ch.fftData.begin() + fftSize, ch.fftData.end(), 0.0f);
    fft->performRealOnlyForwardTransform (ch.fftData.data(), true);

    const int numBins = fftSize / 2;
    auto gateBin = [&] (int magIndex, float& re, float& im)
    {
        const float mag = std::sqrt (re * re + im * im);
        float& env = ch.smoothedMag[(size_t) magIndex];
        env += (mag > env ? attackCoeff : releaseCoeff) * (mag - env);
        const float relative = env / (thresholdLin + kEps);
        float gain = juce::jmin (std::pow (juce::jmax (relative, kEps), amount), 1.0f);
        gain = juce::jlimit (kGainFloor, kGainCeil, gain);
        gain = (1.0f - mix) + mix * gain;
        gain *= makeupLin;
        re *= gain;
        im *= gain;
    };

    {
        float re = ch.fftData[0], im = 0.0f;
        gateBin (0, re, im);
        ch.fftData[0] = re;
    }
    {
        float re = ch.fftData[1], im = 0.0f;
        gateBin (numBins, re, im);
        ch.fftData[1] = re;
    }
    for (int bin = 1; bin < numBins; ++bin)
    {
        const int reIndex = 2 * bin;
        float re = ch.fftData[(size_t) reIndex];
        float im = ch.fftData[(size_t) (reIndex + 1)];
        gateBin (bin, re, im);
        ch.fftData[(size_t) reIndex] = re;
        ch.fftData[(size_t) (reIndex + 1)] = im;
    }

    fft->performRealOnlyInverseTransform (ch.fftData.data());
    for (int i = 0; i < fftSize; ++i)
        ch.ola[(size_t) i] += ch.fftData[(size_t) i] * ch.window[(size_t) i] * kColaNorm;
    for (int i = 0; i < hopSize; ++i)
        ch.outFifo[(size_t) i] = ch.ola[(size_t) i];
    std::copy (ch.ola.begin() + hopSize, ch.ola.end(), ch.ola.begin());
    std::fill (ch.ola.end() - hopSize, ch.ola.end(), 0.0f);
    std::copy (ch.inputFifo.begin() + hopSize, ch.inputFifo.end(), ch.inputFifo.begin());
    ch.writePos = fftSize - hopSize;
    ch.outRead = 0;
    ch.outAvailable = hopSize;
}

void TapGateEngine::process (juce::AudioBuffer<float>& buffer, const GateParams& params)
{
    if (channels.empty() || buffer.getNumSamples() == 0)
        return;

    const float thresholdLin = juce::Decibels::decibelsToGain (params.thresholdDb) * kSpectralFullScale;
    const float makeupLin = juce::Decibels::decibelsToGain (params.makeupDb);
    const float hopSeconds = (float) hopSize / (float) juce::jmax (1.0, sampleRate);
    const float attackCoeff = gateTimeMsToCoeff (params.attackMs, hopSeconds);
    const float releaseCoeff = gateTimeMsToCoeff (params.releaseMs, hopSeconds);
    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) channels.size());

    float dryS[2] {}, wetS[2] {};
    const int linkedCh = juce::jmin (numCh, 2);

    for (int i = 0; i < numSamples; ++i)
    {
        for (int c = 0; c < linkedCh; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const float dryIn = data[i];
            const float delayedDry = ch.dryDelay[(size_t) ch.dryWrite];
            ch.dryDelay[(size_t) ch.dryWrite] = dryIn;
            ch.dryWrite = (ch.dryWrite + 1) % fftSize;
            ch.inputFifo[(size_t) ch.writePos] = dryIn;
            ++ch.writePos;

            float wet = 0.0f;
            if (ch.outAvailable > 0)
            {
                wet = ch.outFifo[(size_t) ch.outRead];
                ++ch.outRead;
                --ch.outAvailable;
            }
            dryS[c] = delayedDry;
            wetS[c] = wet;
            if (ch.writePos == fftSize)
                processHop (ch, params.amount, thresholdLin, params.mix, makeupLin,
                            attackCoeff, releaseCoeff);
        }

        float g = 1.0f;
        if (params.autoGain)
        {
            float ip = 0.0f, wp = 0.0f;
            for (int c = 0; c < linkedCh; ++c)
            {
                ip += dryS[c] * dryS[c];
                wp += wetS[c] * wetS[c];
            }
            const float inv = 1.0f / (float) juce::jmax (1, linkedCh);
            inPower += agCoeff * (ip * inv - inPower);
            wetPower += agCoeff * (wp * inv - wetPower);
            g = std::sqrt (juce::jmax (inPower, 1.0e-12f) / juce::jmax (wetPower, 1.0e-12f));
            g = juce::jlimit (0.03f, 30.0f, g);
        }

        for (int c = 0; c < linkedCh; ++c)
            buffer.setSample (c, i, wetS[c] * g);
    }
}

//==============================================================================
float TapCompEngine::timeMsToCoeff (float timeMs, float hopSeconds) noexcept
{
    if (timeMs <= 0.0f || hopSeconds <= 0.0f)
        return 1.0f;
    const float t = juce::jmax (timeMs * 0.001f, hopSeconds * 0.25f);
    return 1.0f - std::exp (-hopSeconds / t);
}

float TapCompEngine::speedToEngageCoeff (float speed0to100, float hopSeconds) noexcept
{
    const float s = juce::jlimit (0.0f, 100.0f, speed0to100) * 0.01f;
    const float engageMs = 2.0f + 398.0f * std::pow (0.05f, s);
    return timeMsToCoeff (engageMs, hopSeconds);
}

int TapCompEngine::attackMsToDelayHops (float attackMs, float hopSeconds) noexcept
{
    if (attackMs <= 0.0f || hopSeconds <= 0.0f)
        return 0;
    return (int) std::ceil ((double) (attackMs * 0.001f) / (double) hopSeconds);
}

float TapCompEngine::boostTargetGain (float magLin, float lowLin, float upRatio) noexcept
{
    const float noiseFloorLin = juce::Decibels::decibelsToGain (kBoostNoiseFloorDb) * kSpectralFullScale;
    if (upRatio <= 1.0f + 1.0e-4f)
        return 1.0f;
    if (magLin < noiseFloorLin || magLin >= lowLin)
        return 1.0f;
    const float envDb = juce::Decibels::gainToDecibels (magLin / kSpectralFullScale, -160.0f);
    const float lowDb = juce::Decibels::gainToDecibels (lowLin / kSpectralFullScale, -160.0f);
    return juce::Decibels::decibelsToGain ((lowDb - envDb) * (1.0f - 1.0f / upRatio));
}

float TapCompEngine::grTargetGain (float magLin, float highLin, float downRatio) noexcept
{
    if (downRatio <= 1.0f + 1.0e-4f)
        return 1.0f;
    if (magLin <= highLin)
        return 1.0f;
    const float envDb = juce::Decibels::gainToDecibels (magLin / kSpectralFullScale, -160.0f);
    const float highDb = juce::Decibels::gainToDecibels (highLin / kSpectralFullScale, -160.0f);
    return juce::Decibels::decibelsToGain ((highDb - envDb) * (1.0f - 1.0f / downRatio));
}

void TapCompEngine::configureTransform (int order)
{
    order = juce::jlimit (kMinFftOrder, kMaxFftOrder, order);
    fftOrder = order;
    fftSize = 1 << fftOrder;
    hopSize = fftSize / 4;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
}

void TapCompEngine::resetChannel (ChannelState& ch)
{
    const int bins = fftSize / 2 + 1;
    ch.inputFifo.assign ((size_t) fftSize, 0.0f);
    ch.ola.assign ((size_t) fftSize, 0.0f);
    ch.outFifo.assign ((size_t) hopSize, 0.0f);
    ch.fftData.assign ((size_t) fftSize * 2, 0.0f);
    ch.window.resize ((size_t) fftSize);
    ch.boostGain.assign ((size_t) bins, 1.0f);
    ch.grGain.assign ((size_t) bins, 1.0f);
    ch.boostDelayHops.assign ((size_t) bins, -1);
    ch.grDelayHops.assign ((size_t) bins, -1);
    ch.dryDelay.assign ((size_t) fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        ch.window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) fftSize));
    ch.writePos = ch.outRead = ch.outAvailable = ch.dryWrite = 0;
}

void TapCompEngine::prepare (double sr, int order, int numChannels)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    agCoeff = 1.0f - std::exp (-1.0f / (0.03f * (float) juce::jmax (1.0, sampleRate)));
    inPower = wetPower = 1.0e-8f;
    configureTransform (order);
    channels.resize ((size_t) juce::jmax (1, numChannels));
    for (auto& ch : channels)
        resetChannel (ch);
}

void TapCompEngine::reset()
{
    inPower = wetPower = 1.0e-8f;
    for (auto& ch : channels)
        resetChannel (ch);
}

void TapCompEngine::setFftOrder (int order)
{
    if (juce::jlimit (kMinFftOrder, kMaxFftOrder, order) == fftOrder)
        return;
    configureTransform (order);
    for (auto& ch : channels)
        resetChannel (ch);
    inPower = wetPower = 1.0e-8f;
}

void TapCompEngine::processHop (ChannelState& ch, const HopParams& hp)
{
    if (fft == nullptr)
        return;

    float framePeak = 0.0f;
    for (int i = 0; i < fftSize; ++i)
        framePeak = juce::jmax (framePeak, std::abs (ch.inputFifo[(size_t) i]));
    const bool forceUnity = framePeak < kSilencePeak;

    for (int i = 0; i < fftSize; ++i)
        ch.fftData[(size_t) i] = ch.inputFifo[(size_t) i] * ch.window[(size_t) i];
    std::fill (ch.fftData.begin() + fftSize, ch.fftData.end(), 0.0f);
    fft->performRealOnlyForwardTransform (ch.fftData.data(), true);

    const int numBins = fftSize / 2;
    auto binMag = [&] (float re, float im) -> float
    {
        return std::sqrt (re * re + im * im) * (kMagRefFftSize / (float) fftSize) + kEps;
    };

    auto processBin = [&] (int magIndex, float& re, float& im)
    {
        const float mag = binMag (re, im);
        float& b = ch.boostGain[(size_t) magIndex];
        float& g = ch.grGain[(size_t) magIndex];
        int& bDelay = ch.boostDelayHops[(size_t) magIndex];
        int& gDelay = ch.grDelayHops[(size_t) magIndex];

        float boostTarget = 1.0f;
        if (! forceUnity)
            boostTarget = juce::jlimit (1.0f, kGainCeil,
                                        boostTargetGain (mag, hp.boostThreshLin, hp.boostRatio));
        if (boostTarget <= 1.001f)
        {
            bDelay = -1;
            b += hp.boostRelCoeff * (1.0f - b);
        }
        else
        {
            if (bDelay < 0)
                bDelay = hp.boostAttackDelayHops;
            if (bDelay > 0)
                --bDelay;
            else
                b += hp.boostSpeedCoeff * (boostTarget - b);
        }
        if (std::abs (b - 1.0f) < 1.0e-4f)
            b = 1.0f;

        float grTarget = 1.0f;
        if (! forceUnity)
            grTarget = juce::jlimit (kGainFloor, 1.0f,
                                     grTargetGain (mag * b, hp.compThreshLin, hp.compRatio));
        if (grTarget >= 0.999f)
        {
            gDelay = -1;
            g += hp.compRelCoeff * (1.0f - g);
        }
        else
        {
            if (gDelay < 0)
                gDelay = hp.compAttackDelayHops;
            if (gDelay > 0)
                --gDelay;
            else
                g += hp.compSpeedCoeff * (grTarget - g);
        }
        if (std::abs (g - 1.0f) < 1.0e-4f)
            g = 1.0f;

        const float gain = b * g * hp.makeupLin;
        re *= gain;
        im *= gain;
    };

    {
        float re = ch.fftData[0], im = 0.0f;
        processBin (0, re, im);
        ch.fftData[0] = re;
    }
    {
        float re = ch.fftData[1], im = 0.0f;
        processBin (numBins, re, im);
        ch.fftData[1] = re;
    }
    for (int bin = 1; bin < numBins; ++bin)
    {
        const int reIndex = 2 * bin;
        float re = ch.fftData[(size_t) reIndex];
        float im = ch.fftData[(size_t) (reIndex + 1)];
        processBin (bin, re, im);
        ch.fftData[(size_t) reIndex] = re;
        ch.fftData[(size_t) (reIndex + 1)] = im;
    }

    fft->performRealOnlyInverseTransform (ch.fftData.data());
    for (int i = 0; i < fftSize; ++i)
        ch.ola[(size_t) i] += ch.fftData[(size_t) i] * ch.window[(size_t) i] * kColaNorm;
    for (int i = 0; i < hopSize; ++i)
        ch.outFifo[(size_t) i] = ch.ola[(size_t) i];
    std::copy (ch.ola.begin() + hopSize, ch.ola.end(), ch.ola.begin());
    std::fill (ch.ola.end() - hopSize, ch.ola.end(), 0.0f);
    std::copy (ch.inputFifo.begin() + hopSize, ch.inputFifo.end(), ch.inputFifo.begin());
    ch.writePos = fftSize - hopSize;
    ch.outRead = 0;
    ch.outAvailable = hopSize;
}

void TapCompEngine::process (juce::AudioBuffer<float>& buffer, const CompParams& params)
{
    if (channels.empty() || buffer.getNumSamples() == 0)
        return;

    const float hopSeconds = (float) hopSize / (float) juce::jmax (1.0, sampleRate);
    HopParams hp;
    hp.boostThreshLin = juce::Decibels::decibelsToGain (params.boostThreshDb) * kSpectralFullScale;
    hp.boostRatio = params.boostRatio;
    hp.boostAttackDelayHops = attackMsToDelayHops (params.boostAtkMs, hopSeconds);
    hp.boostSpeedCoeff = speedToEngageCoeff (params.boostSpeed, hopSeconds);
    hp.boostRelCoeff = timeMsToCoeff (params.boostRelMs, hopSeconds);
    hp.compThreshLin = juce::Decibels::decibelsToGain (params.compThreshDb) * kSpectralFullScale;
    hp.compRatio = params.compRatio;
    hp.compAttackDelayHops = attackMsToDelayHops (params.compAtkMs, hopSeconds);
    hp.compSpeedCoeff = speedToEngageCoeff (params.compSpeed, hopSeconds);
    hp.compRelCoeff = timeMsToCoeff (params.compRelMs, hopSeconds);
    hp.makeupLin = juce::Decibels::decibelsToGain (params.makeupDb);

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    float dryS[2] {}, wetS[2] {};
    const int linkedCh = juce::jmin (numCh, 2);
    const float mix = juce::jlimit (0.0f, 1.0f, params.mix);

    for (int i = 0; i < numSamples; ++i)
    {
        for (int c = 0; c < linkedCh; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const float dryIn = data[i];
            const float delayedDry = ch.dryDelay[(size_t) ch.dryWrite];
            ch.dryDelay[(size_t) ch.dryWrite] = dryIn;
            ch.dryWrite = (ch.dryWrite + 1) % fftSize;
            ch.inputFifo[(size_t) ch.writePos] = dryIn;
            ++ch.writePos;

            float wet = 0.0f;
            if (ch.outAvailable > 0)
            {
                wet = ch.outFifo[(size_t) ch.outRead];
                ++ch.outRead;
                --ch.outAvailable;
            }
            dryS[c] = delayedDry;
            wetS[c] = wet;
            if (ch.writePos == fftSize)
                processHop (ch, hp);
        }

        float g = 1.0f;
        if (params.autoGain)
        {
            float ip = 0.0f, wp = 0.0f;
            for (int c = 0; c < linkedCh; ++c)
            {
                ip += dryS[c] * dryS[c];
                wp += wetS[c] * wetS[c];
            }
            const float inv = 1.0f / (float) juce::jmax (1, linkedCh);
            inPower += agCoeff * (ip * inv - inPower);
            wetPower += agCoeff * (wp * inv - wetPower);
            g = std::sqrt (juce::jmax (inPower, 1.0e-12f) / juce::jmax (wetPower, 1.0e-12f));
            g = juce::jlimit (0.03f, 30.0f, g);
        }

        for (int c = 0; c < linkedCh; ++c)
        {
            const float wet = wetS[c] * g;
            buffer.setSample (c, i, dryS[c] * (1.0f - mix) + wet * mix);
        }
    }
}

//==============================================================================
bool configureInsertBuses (juce::AudioPluginInstance& plugin, int hostChannels)
{
    const int hostCh = juce::jmax (1, hostChannels);
    const auto set = hostCh >= 2 ? juce::AudioChannelSet::stereo()
                                 : juce::AudioChannelSet::mono();

    auto tryMainIO = [&plugin] (const juce::AudioChannelSet& ioSet, bool disableExtras) -> bool
    {
        auto layout = plugin.getBusesLayout();
        if (layout.inputBuses.isEmpty() && layout.outputBuses.isEmpty())
            return false;
        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = ioSet;
        if (! layout.outputBuses.isEmpty())
            layout.outputBuses.getReference (0) = ioSet;
        if (disableExtras)
        {
            for (int i = 1; i < layout.inputBuses.size(); ++i)
                layout.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();
            for (int i = 1; i < layout.outputBuses.size(); ++i)
                layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();
        }
        return plugin.checkBusesLayoutSupported (layout) && plugin.setBusesLayout (layout);
    };

    if (tryMainIO (set, true) || tryMainIO (set, false))
        return true;

    if (hostCh < 2)
        return tryMainIO (juce::AudioChannelSet::stereo(), true)
            || tryMainIO (juce::AudioChannelSet::stereo(), false);

    plugin.disableNonMainBuses();
    const int inCh = plugin.getMainBusNumInputChannels();
    const int outCh = plugin.getMainBusNumOutputChannels();
    if ((outCh == hostCh) && (inCh == 0 || inCh == hostCh))
        return true;
    return hostCh < 2 && outCh >= 2 && (inCh == 0 || inCh >= 2);
}

void prepareInsertInstance (juce::AudioPluginInstance& plugin, double sr, int blockSize)
{
    const int inCh = juce::jmax (1, plugin.getMainBusNumInputChannels() > 0
                                         ? plugin.getMainBusNumInputChannels()
                                         : plugin.getTotalNumInputChannels());
    const int outCh = juce::jmax (1, plugin.getMainBusNumOutputChannels() > 0
                                         ? plugin.getMainBusNumOutputChannels()
                                         : plugin.getTotalNumOutputChannels());
    plugin.setPlayConfigDetails (inCh, outCh, sr, blockSize);
    plugin.setRateAndBufferSizeDetails (sr, blockSize);
    plugin.prepareToPlay (sr, blockSize);
    plugin.suspendProcessing (false);
}

void processHostedInsert (juce::AudioPluginInstance& plugin,
                          juce::AudioBuffer<float>& rackBuffer,
                          juce::MidiBuffer& midi,
                          juce::AudioBuffer<float>& scratch)
{
    const int numSamples = rackBuffer.getNumSamples();
    const int rackCh = rackBuffer.getNumChannels();
    int mainIn = plugin.getMainBusNumInputChannels();
    int mainOut = plugin.getMainBusNumOutputChannels();
    int totalIn = plugin.getTotalNumInputChannels();
    int totalOut = plugin.getTotalNumOutputChannels();

    if (mainIn <= 0)
        mainIn = juce::jmax (1, juce::jmin (rackCh, totalIn > 0 ? totalIn : rackCh));
    if (mainOut <= 0)
        mainOut = juce::jmax (1, juce::jmin (rackCh, totalOut > 0 ? totalOut : rackCh));
    if (totalIn <= 0)
        totalIn = mainIn;
    if (totalOut <= 0)
        totalOut = mainOut;

    const int totalCh = juce::jmax (totalIn, totalOut, 1);

    if (mainIn == rackCh && mainOut == rackCh && totalIn == rackCh && totalOut == rackCh)
    {
        plugin.processBlock (rackBuffer, midi);
        return;
    }

    scratch.setSize (totalCh, numSamples, false, false, true);
    scratch.clear();

    if (mainIn == 1 && rackCh >= 1)
    {
        scratch.copyFrom (0, 0, rackBuffer, 0, 0, numSamples);
        if (rackCh > 1)
        {
            scratch.addFrom (0, 0, rackBuffer, 1, 0, numSamples);
            scratch.applyGain (0, 0, numSamples, 0.5f);
        }
    }
    else if (mainIn >= 2)
    {
        scratch.copyFrom (0, 0, rackBuffer, 0, 0, numSamples);
        if (rackCh > 1)
            scratch.copyFrom (1, 0, rackBuffer, 1, 0, numSamples);
        else
            scratch.copyFrom (1, 0, rackBuffer, 0, 0, numSamples);
    }

    plugin.processBlock (scratch, midi);

    if (mainOut <= 0)
        return;

    if (mainOut == 1)
    {
        rackBuffer.copyFrom (0, 0, scratch, 0, 0, numSamples);
        if (rackCh > 1)
            rackBuffer.copyFrom (1, 0, scratch, 0, 0, numSamples);
    }
    else if (rackCh == 1)
    {
        rackBuffer.copyFrom (0, 0, scratch, 0, 0, numSamples);
        rackBuffer.addFrom (0, 0, scratch, 1, 0, numSamples);
        rackBuffer.applyGain (0, 0, numSamples, 0.5f);
    }
    else
    {
        rackBuffer.copyFrom (0, 0, scratch, 0, 0, numSamples);
        rackBuffer.copyFrom (1, 0, scratch, 1, 0, numSamples);
    }
}
