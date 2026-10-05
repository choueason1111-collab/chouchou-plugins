/*
  ==============================================================================

    Launch Standalone with --wav / --session (message thread only).

  ==============================================================================
*/

#include "Audition/StandaloneLauncher.h"

namespace ChouChouStandaloneLauncher
{
    juce::File findStandaloneApp()
    {
       #if JUCE_WINDOWS
        const juce::File candidates[] = {
            juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                .getChildFile ("chouchou/chouchouPlugplugin2.exe"),
            juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                .getChildFile ("chouchou/chouchouPlugplugin2.exe"),
        };

        for (const auto& app : candidates)
            if (app.existsAsFile())
                return app;
       #else
        const juce::File candidates[] = {
            juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                .getChildFile ("Application Support/chouchou/chouchouPlugplugin/chouchouPlugplugin2.app"),
            // The project's CMake build, found from this source file's location (Source/Audition/).
            juce::File (__FILE__).getParentDirectory().getParentDirectory().getParentDirectory()
                .getChildFile ("build_cmake/chouchouPlugplugin2_artefacts/Release/Standalone/chouchouPlugplugin2.app"),
            juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                .getChildFile ("Applications/chouchouPlugplugin2.app"),
        };

        for (const auto& app : candidates)
            if (app.isDirectory())
                return app;
       #endif

        return {};
    }

    static bool launchWithArgv (const juce::StringArray& extraArgs, juce::String& error)
    {
        const auto app = findStandaloneApp();
        if (! app.exists())
        {
           #if JUCE_WINDOWS
            error = "Standalone app not found. Put chouchouPlugplugin2.exe in C:\\Program Files\\chouchou, then try again.";
           #else
            error = "Standalone app not found. Build Release Standalone, then try again.";
           #endif
            return false;
        }

       #if JUCE_MAC
        juce::StringArray cmd;
        cmd.add ("/usr/bin/open");
        cmd.add ("-n");
        cmd.add ("-a");
        cmd.add (app.getFullPathName());

        if (! extraArgs.isEmpty())
        {
            cmd.add ("--args");
            cmd.addArray (extraArgs);
        }

        juce::ChildProcess proc;
        if (! proc.start (cmd))
        {
            error = "Failed to launch Standalone.";
            return false;
        }
       #else
        juce::String args;
        for (const auto& a : extraArgs)
            args << a.quoted() << " ";

        if (! app.startAsProcess (args.trim()))
        {
            error = "Failed to launch Standalone.";
            return false;
        }
       #endif

        error.clear();
        return true;
    }

    bool openEmpty (juce::String& error)
    {
        return launchWithArgv ({}, error);
    }

    bool openWithWav (const juce::File& wav, juce::String& error)
    {
        if (! wav.existsAsFile())
        {
            error = "WAV not found: " + wav.getFullPathName();
            return false;
        }

        return launchWithArgv ({ "--wav", wav.getFullPathName() }, error);
    }

    bool openWithSession (const juce::File& sessionJson, juce::String& error)
    {
        if (! sessionJson.existsAsFile())
        {
            error = "Session not found: " + sessionJson.getFullPathName();
            return false;
        }

        return launchWithArgv ({ "--session", sessionJson.getFullPathName() }, error);
    }
}
