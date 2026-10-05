#pragma once

#include <JuceHeader.h>

//==============================================================================
/** Shipping modes — do not mix audio pipelines. */
enum class ChouChouHostMode
{
    AuditionCaptureMode,       // Bridge only: capture → session → Standalone
    EmbeddedARAExperimentMode, // Dev flag: Non-SAM test ARA lifecycle only
    StandaloneFullARAMode      // Full ARA Host + SpectraLayers UI
};

enum class ChouChouCaptureState
{
    Idle,
    Armed,
    Capturing,
    Overflowed,
    Finalizing,
    WavReady,
    Incomplete,
    Failed
};

enum class ChouChouAraHostState
{
    Idle,
    FactoryCreating,
    FactoryReady,
    DocumentCreating,
    DocumentReady,
    AudioSourceReady,
    RegionReady,
    PluginInstanceReady,
    PluginEditorAllowed,
    Failed
};

//==============================================================================
/** Prefer calling with an explicit standalone flag from the processor. */
inline ChouChouHostMode detectChouChouHostMode (bool runningAsStandaloneApp) noexcept
{
    if (runningAsStandaloneApp)
        return ChouChouHostMode::StandaloneFullARAMode;

    if (juce::PluginHostType().isAdobeAudition())
        return ChouChouHostMode::AuditionCaptureMode;

    // Other DAWs: same shipping policy as Audition Bridge — no inner ARA loads.
    return ChouChouHostMode::AuditionCaptureMode;
}

inline ChouChouHostMode detectChouChouHostMode() noexcept
{
    // Fallback when standalone helper is not linked yet — Audition check only.
    if (juce::PluginHostType().isAdobeAudition())
        return ChouChouHostMode::AuditionCaptureMode;

    return ChouChouHostMode::AuditionCaptureMode;
}

inline const char* toString (ChouChouHostMode m) noexcept
{
    switch (m)
    {
        case ChouChouHostMode::AuditionCaptureMode:       return "AuditionCaptureMode";
        case ChouChouHostMode::EmbeddedARAExperimentMode: return "EmbeddedARAExperimentMode";
        case ChouChouHostMode::StandaloneFullARAMode:     return "StandaloneFullARAMode";
    }
    return "Unknown";
}

inline const char* toString (ChouChouCaptureState s) noexcept
{
    switch (s)
    {
        case ChouChouCaptureState::Idle:        return "Idle";
        case ChouChouCaptureState::Armed:       return "Armed";
        case ChouChouCaptureState::Capturing:   return "Capturing";
        case ChouChouCaptureState::Overflowed:  return "Overflowed";
        case ChouChouCaptureState::Finalizing:  return "Finalizing";
        case ChouChouCaptureState::WavReady:    return "WavReady";
        case ChouChouCaptureState::Incomplete:  return "Incomplete";
        case ChouChouCaptureState::Failed:      return "Failed";
    }
    return "Unknown";
}

/** Inner ARA plug-ins (e.g. SpectraLayers) may load in every host. Loading them inside
    Audition has crashed it before, so save the session before trying. */
inline bool allowsInnerAraPluginLoad (ChouChouHostMode) noexcept
{
    return true;
}
