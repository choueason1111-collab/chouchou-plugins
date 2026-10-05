/*
  ==============================================================================

    Launch Standalone with --wav / --session (message thread only).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace ChouChouStandaloneLauncher
{
    /** Resolve Release Standalone.app candidates. */
    juce::File findStandaloneApp();

    /** Open empty Standalone (secondary path). */
    bool openEmpty (juce::String& error);

    /** Open Standalone binding a WAV into SpectraLayers (`--wav`). */
    bool openWithWav (const juce::File& wav, juce::String& error);

    /** Open Standalone with a SpectraLayers session.json (`--session`). */
    bool openWithSession (const juce::File& sessionJson, juce::String& error);
}
