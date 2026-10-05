/*
  ==============================================================================

    Out-of-process plugin scan worker + coordinator (Audition-safe).

  ==============================================================================
*/

#include "PluginScanSupport.h"
#include <queue>

namespace
{
    juce::File supportDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Application Support")
            .getChildFile ("chouchou")
            .getChildFile ("chouchouPlugplugin");
    }
}

juce::File getChouChouDeadMansPedalFile()
{
    supportDir().createDirectory();
    return supportDir().getChildFile ("DeadMansPedal");
}

juce::File findChouChouScannerExecutable()
{
   #if JUCE_WINDOWS
    // The Standalone .exe doubles as the scan worker: ourselves when we are the Standalone,
    // otherwise the copy installed in C:\Program Files\chouchou.
    if (isChouChouRunningAsStandaloneApp())
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    const auto installed = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                               .getChildFile ("chouchou/chouchouPlugplugin.exe");
    return installed.existsAsFile() ? installed : juce::File();
   #else
    // Preferred: helper installed next to user settings
    const auto helperApp = supportDir().getChildFile ("chouchouPlugplugin Scanner.app")
                              .getChildFile ("Contents/MacOS/chouchouPlugplugin");
    if (helperApp.existsAsFile())
        return helperApp;

    // Dev build Standalone
    // __FILE__ is this source file on the machine that built the plugin, so this finds the
    // project's Xcode build without writing the folder path into the code.
    const auto dev = juce::File (__FILE__).getParentDirectory().getParentDirectory()
                         .getChildFile ("Builds/MacOSX/build/Release/chouchouPlugplugin.app")
                         .getChildFile ("Contents/MacOS/chouchouPlugplugin");
    if (dev.existsAsFile())
        return dev;

    // If we are already the Standalone, use ourselves
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    if (self.getFileName().containsIgnoreCase ("chouchouPlugplugin") && self.existsAsFile())
    {
        // Avoid using the VST3/AU binary as a CLI worker — only .app MacOS binary
        if (self.getParentDirectory().getFileName() == "MacOS"
            && self.getParentDirectory().getParentDirectory().getFileName() == "Contents"
            && self.getParentDirectory().getParentDirectory().getParentDirectory().getFileName().endsWithIgnoreCase (".app"))
            return self;
    }

    return {};
   #endif
}

bool isChouChouRunningAsStandaloneApp()
{
   #if JUCE_WINDOWS
    return juce::JUCEApplicationBase::isStandaloneApp();
   #else
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    if (! self.existsAsFile())
        return false;

    // .../Something.app/Contents/MacOS/binary
    const auto macos = self.getParentDirectory();
    const auto contents = macos.getParentDirectory();
    const auto bundle = contents.getParentDirectory();
    return macos.getFileName() == "MacOS"
        && contents.getFileName() == "Contents"
        && bundle.getFileName().endsWithIgnoreCase (".app");
   #endif
}

//==============================================================================
class ChouChouScanSuperprocess final : private juce::ChildProcessCoordinator
{
public:
    explicit ChouChouScanSuperprocess (juce::File exeToLaunch)
    {
        if (exeToLaunch.existsAsFile())
            launchWorkerProcess (exeToLaunch, kChouChouScanProcessUID, 8000, 0);
    }

    enum class State { timeout, gotResult, connectionLost };

    struct Response
    {
        State state = State::timeout;
        std::unique_ptr<juce::XmlElement> xml;
    };

    Response getResponse()
    {
        std::unique_lock<std::mutex> lock { mutex };

        if (! condvar.wait_for (lock, std::chrono::milliseconds { 50 },
                                [&] { return gotResult || connectionLost; }))
            return { State::timeout, nullptr };

        const auto state = connectionLost ? State::connectionLost : State::gotResult;
        connectionLost = false;
        gotResult = false;
        return { state, std::move (pluginDescription) };
    }

    using ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        pluginDescription = juce::parseXML (mb.toString());
        gotResult = true;
        condvar.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        connectionLost = true;
        condvar.notify_one();
    }

    std::mutex mutex;
    std::condition_variable condvar;
    std::unique_ptr<juce::XmlElement> pluginDescription;
    bool connectionLost = false;
    bool gotResult = false;
};

//==============================================================================
class ChouChouSafePluginScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    bool findPluginTypesFor (juce::AudioPluginFormat& format,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        // Never scan ourselves inside the host.
        if (fileOrIdentifier.containsIgnoreCase ("chouchouPlugplugin"))
            return true;

        const auto exe = findChouChouScannerExecutable();

        // CRITICAL: never instantiate plugins in-process while hosted inside Insert /
        // Audition / another DAW. A crashing AU (e.g. dearVR license UI) would take
        // down the whole host. Only Standalone may fall back to in-process scan.
        if (! exe.existsAsFile())
        {
            if (! isChouChouRunningAsStandaloneApp())
                return false; // blacklist this file via KnownPluginList without loading it

            format.findAllTypesForFile (result, fileOrIdentifier);
            return true;
        }

        if (addPluginDescriptions (exe, format.getName(), fileOrIdentifier, result))
            return true;

        // Worker died → blacklist this file (KnownPluginList does that when we return false).
        // Do NOT fall back to in-process — same host-crash risk as above.
        superprocess = nullptr;
        return false;
    }

    void scanFinished() override { superprocess = nullptr; }

private:
    bool addPluginDescriptions (const juce::File& exe,
                                const juce::String& formatName,
                                const juce::String& fileOrIdentifier,
                                juce::OwnedArray<juce::PluginDescription>& result)
    {
        if (superprocess == nullptr)
            superprocess = std::make_unique<ChouChouScanSuperprocess> (exe);

        juce::MemoryBlock block;
        juce::MemoryOutputStream stream { block, true };
        stream.writeString (formatName);
        stream.writeString (fileOrIdentifier);

        if (! superprocess->sendMessageToWorker (block))
            return false;

        for (;;)
        {
            if (shouldExit())
                return true;

            const auto response = superprocess->getResponse();

            if (response.state == ChouChouScanSuperprocess::State::timeout)
                continue;

            if (response.xml != nullptr)
                for (const auto* item : response.xml->getChildIterator())
                {
                    auto desc = std::make_unique<juce::PluginDescription>();
                    if (desc->loadFromXml (*item))
                        result.add (std::move (desc));
                }

            // connectionLost ⇒ crashed while scanning this plugin
            return response.state == ChouChouScanSuperprocess::State::gotResult;
        }
    }

    std::unique_ptr<ChouChouScanSuperprocess> superprocess;
};

//==============================================================================
void prepareChouChouPluginScanning (juce::KnownPluginList& list)
{
    const auto pedal = getChouChouDeadMansPedalFile();
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (list, pedal);

    // Always blacklist ourselves (any path form).
    list.addToBlacklist ("chouchouPlugplugin");

    for (const auto& f : list.getTypes())
        if (f.fileOrIdentifier.containsIgnoreCase ("chouchouPlugplugin")
            || f.name.containsIgnoreCase ("chouchouPlugplugin"))
            list.addToBlacklist (f.fileOrIdentifier);

    installChouChouSafePluginScanner (list);
}

void installChouChouSafePluginScanner (juce::KnownPluginList& list)
{
    list.setCustomScanner (std::make_unique<ChouChouSafePluginScanner>());
}
