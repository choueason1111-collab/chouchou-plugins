/*
  ==============================================================================

    Out-of-process plugin scan worker + coordinator (Audition-safe).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <condition_variable>
#include <memory>
#include <mutex>

constexpr const char* kChouChouScanProcessUID = "ChouChouPlugScan1";

juce::File getChouChouDeadMansPedalFile();
juce::File findChouChouScannerExecutable();
bool isChouChouRunningAsStandaloneApp();

/** Call once at processor startup. */
void prepareChouChouPluginScanning (juce::KnownPluginList& list);

/** Installs CustomScanner that prefers out-of-process scanning via Standalone.app. */
void installChouChouSafePluginScanner (juce::KnownPluginList& list);
