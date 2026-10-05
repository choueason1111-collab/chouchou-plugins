/*
  ==============================================================================

    Custom Standalone entry — can run as out-of-process plugin scan worker.

  ==============================================================================
*/

#include <JuceHeader.h>

#if JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include "PluginScanSupport.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include <mutex>
#include <queue>

namespace
{
    class PluginScannerSubprocess final : private juce::ChildProcessWorker,
                                          private juce::AsyncUpdater
    {
    public:
        PluginScannerSubprocess()
        {
            juce::addDefaultFormatsToManager (formatManager);
        }

        using ChildProcessWorker::initialiseFromCommandLine;

    private:
        void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
        {
            if (mb.isEmpty())
                return;

            const std::lock_guard<std::mutex> lock (mutex);

            if (const auto results = doScan (mb); ! results.isEmpty())
            {
                sendResults (results);
            }
            else
            {
                pendingBlocks.emplace (mb);
                triggerAsyncUpdate();
            }
        }

        void handleConnectionLost() override
        {
            juce::JUCEApplicationBase::quit();
        }

        void handleAsyncUpdate() override
        {
            for (;;)
            {
                const std::lock_guard<std::mutex> lock (mutex);

                if (pendingBlocks.empty())
                    return;

                sendResults (doScan (pendingBlocks.front()));
                pendingBlocks.pop();
            }
        }

        juce::OwnedArray<juce::PluginDescription> doScan (const juce::MemoryBlock& block)
        {
            juce::MemoryInputStream stream { block, false };
            const auto formatName = stream.readString();
            const auto identifier = stream.readString();

            juce::PluginDescription pd;
            pd.fileOrIdentifier = identifier;
            pd.uniqueId = pd.deprecatedUid = 0;

            juce::AudioPluginFormat* matchingFormat = nullptr;
            for (auto* format : formatManager.getFormats())
                if (format->getName() == formatName)
                    matchingFormat = format;

            juce::OwnedArray<juce::PluginDescription> results;

            if (matchingFormat != nullptr
                && (juce::MessageManager::getInstance()->isThisTheMessageThread()
                    || matchingFormat->requiresUnblockedMessageThreadDuringCreation (pd)))
            {
                matchingFormat->findAllTypesForFile (results, identifier);
            }

            return results;
        }

        void sendResults (const juce::OwnedArray<juce::PluginDescription>& results)
        {
            juce::XmlElement xml ("LIST");

            for (const auto& desc : results)
                xml.addChildElement (desc->createXml().release());

            const auto str = xml.toString();
            sendMessageToCoordinator ({ str.toRawUTF8(), str.getNumBytesAsUTF8() });
        }

        std::mutex mutex;
        std::queue<juce::MemoryBlock> pendingBlocks;
        juce::AudioPluginFormatManager formatManager;
    };

    //==============================================================================
    class ChouChouStandaloneApp final : public juce::JUCEApplication
    {
    public:
        ChouChouStandaloneApp()
        {
            juce::PropertiesFile::Options options;
            options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
            options.filenameSuffix      = ".settings";
            options.osxLibrarySubFolder = "Application Support";
            options.folderName          = {};
            appProperties.setStorageParameters (options);
        }

        const juce::String getApplicationName() override    { return juce::CharPointer_UTF8 (JucePlugin_Name); }
        const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
        bool moreThanOneInstanceAllowed() override          { return true; }

        void initialise (const juce::String& commandLine) override
        {
            auto scanner = std::make_unique<PluginScannerSubprocess>();

            if (scanner->initialiseFromCommandLine (commandLine, kChouChouScanProcessUID))
            {
                storedScanner = std::move (scanner);
                return; // worker mode: no UI
            }

            mainWindow.reset (createWindow());

            if (mainWindow != nullptr)
                mainWindow->setVisible (true);
            else
                pluginHolder = createPluginHolder();
        }

        void shutdown() override
        {
            pluginHolder = nullptr;
            mainWindow = nullptr;
            storedScanner = nullptr;
            appProperties.saveIfNeeded();
        }

        void systemRequestedQuit() override
        {
            if (pluginHolder != nullptr)
                pluginHolder->savePluginState();

            if (mainWindow != nullptr)
                mainWindow->pluginHolder->savePluginState();

            quit();
        }

    private:
        juce::StandaloneFilterWindow* createWindow()
        {
            if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
                return nullptr;

            return new juce::StandaloneFilterWindow (getApplicationName(),
                                                     juce::LookAndFeel::getDefaultLookAndFeel()
                                                         .findColour (juce::ResizableWindow::backgroundColourId),
                                                     createPluginHolder());
        }

        std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
        {
            const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
            return std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(),
                                                                   false,
                                                                   juce::String{},
                                                                   nullptr,
                                                                   channelConfig,
                                                                   false);
        }

        juce::ApplicationProperties appProperties;
        std::unique_ptr<PluginScannerSubprocess> storedScanner;
        std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
        std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
    };
}

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new ChouChouStandaloneApp();
}

#endif
