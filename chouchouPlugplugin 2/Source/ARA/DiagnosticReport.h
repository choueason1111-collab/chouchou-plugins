#pragma once

#include "HostMode.h"
#include <JuceHeader.h>

//==============================================================================
/** Fixed block the user can Copy / paste into chat for debugging. */
struct ChouChouDiagnosticReport
{
    ChouChouHostMode mode = ChouChouHostMode::AuditionCaptureMode;
    ChouChouCaptureState captureState = ChouChouCaptureState::Idle;
    ChouChouAraHostState araHostState = ChouChouAraHostState::Idle;
    juce::String errorCode;   // e.g. INNER_LOAD_REJECTED
    juce::String message;
    juce::String hostProcess;
    juce::String sourceWav;
    juce::String sessionJson;
    juce::String pluginName;
    juce::String pluginUid;
    uint64_t sessionGeneration = 0;
    uint64_t documentGeneration = 0;

    juce::String toPasteableBlock() const
    {
        juce::String s;
        s << "=== chouchou diagnostic ===\n";
        s << "time: " << juce::Time::getCurrentTime().toString (true, true, true, true) << "\n";
        s << "hostProcess: " << (hostProcess.isNotEmpty() ? hostProcess : juce::PluginHostType().getHostDescription()) << "\n";
        s << "mode: " << toString (mode) << "\n";
        s << "captureState: " << toString (captureState) << "\n";
        s << "araHostState: " << (int) araHostState << "\n";
        s << "errorCode: " << (errorCode.isNotEmpty() ? errorCode : "(none)") << "\n";
        s << "message: " << message << "\n";
        s << "paths: sourceWav=" << (sourceWav.isNotEmpty() ? sourceWav : "(none)")
          << " sessionJson=" << (sessionJson.isNotEmpty() ? sessionJson : "(none)") << "\n";
        s << "plugin: name=" << (pluginName.isNotEmpty() ? pluginName : "(none)")
          << " uid=" << (pluginUid.isNotEmpty() ? pluginUid : "(none)") << "\n";
        s << "generation: session=" << juce::String (sessionGeneration)
          << " document=" << juce::String (documentGeneration) << "\n";
        s << "=== end ===\n";
        return s;
    }

    void copyToClipboard() const
    {
        juce::SystemClipboard::copyTextToClipboard (toPasteableBlock());
    }
};
