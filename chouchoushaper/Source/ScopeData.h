/*
  ==============================================================================

    chouchouShaper — scope data passed from the audio thread to the editor

      One ScopeColumn summarises kScopeColumnSamples samples. Columns are pushed
      in real time order; the editor re-aligns output (latency later) and the
      spectral gain (half the latency later) with the input.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>

constexpr int kScopeColumnSamples = 64;

struct ScopeColumn
{
    float inMin = 0, inMax = 0;     // main input before the time stage (channel 0)
    float outMin = 0, outMax = 0;   // final output (channel 0)
    float scMin = 0, scMax = 0;     // filtered sidechain (channel 0), 0 when unused
    bool  scActive = false;         // a stage is detecting from the sidechain

    // Time stage, averaged over the column (dB, scaled by Dry/Wet; 0 when off).
    float tRatio = 0, tAttack = 0, tSustain = 0, tTotal = 0;

    // Spectral stage, energy-weighted over bins at the latest hop (dB, scaled by Dry/Wet).
    float sRatio = 0, sAttack = 0, sSustain = 0, sTotal = 0;
};

/** Single-producer / single-consumer column queue. A full queue drops new columns. */
class ScopeFifo
{
public:
    explicit ScopeFifo (int capacity = 8192) : fifo (capacity), data ((size_t) capacity) {}

    void push (const ScopeColumn& c) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0)
            data[(size_t) s1] = c;
        else if (n2 > 0)
            data[(size_t) s2] = c;
        fifo.finishedWrite (n1 + n2);
    }

    /** Calls fn (const ScopeColumn&) for every queued column, oldest first. */
    template <typename Fn>
    int drain (Fn&& fn)
    {
        const int ready = fifo.getNumReady();
        int s1, n1, s2, n2;
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) fn (data[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) fn (data[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    void clear() noexcept { fifo.reset(); }

private:
    juce::AbstractFifo fifo;
    std::vector<ScopeColumn> data;
};
