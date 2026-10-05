/*
  Offline ARA self-test — no Audition required.

  Steps:
    1) Locate SpectraLayers.vst3
    2) findAllTypesForFile + OnlyARA / hasARAExtension repair check
    3) createPluginInstance → maybeWrapWithARAHost
    4) Wait for async ARA DocumentController
    5) Assign a generated WAV as AudioSource
    6) prepareToPlay + a few processBlock calls
    7) Tear down cleanly

  Exit 0 = all PASS. Exit 1 = FAIL (or missing SpectraLayers).
*/

#include <JuceHeader.h>
#include "ARAHostSupport.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

namespace
{
    int gFails = 0;

    void expect (bool ok, const std::string& name, const std::string& detail = {})
    {
        if (ok)
        {
            std::cout << "  PASS  " << name << "\n";
            return;
        }

        ++gFails;
        std::cout << "  FAIL  " << name;
        if (! detail.empty())
            std::cout << "  (" << detail << ")";
        std::cout << "\n";
    }

    juce::File spectraLayersVST3()
    {
        const juce::File system { "/Library/Audio/Plug-Ins/VST3/SpectraLayers.vst3" };
        if (system.isDirectory() || system.existsAsFile())
            return system;

        const auto user = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                              .getChildFile ("Library/Audio/Plug-Ins/VST3/SpectraLayers.vst3");
        if (user.isDirectory() || user.existsAsFile())
            return user;

        return {};
    }

    juce::File writeTestWav()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("chouchouARASelfTest");
        dir.createDirectory();
        auto file = dir.getChildFile ("tone_440_2s.wav");
        file.deleteFile();

        constexpr double sr = 48000.0;
        constexpr int n = (int) (sr * 2.0);
        juce::AudioBuffer<float> buf (2, n);
        double phase = 0.0;
        const double w = juce::MathConstants<double>::twoPi * 440.0 / sr;

        for (int i = 0; i < n; ++i)
        {
            const float s = 0.2f * (float) std::sin (phase);
            phase += w;
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
        if (out == nullptr)
            return {};

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (out.release(), sr, 2u, 24, {}, 0));
        if (writer == nullptr)
            return {};

        if (! writer->writeFromAudioSampleBuffer (buf, 0, n))
            return {};

        return file;
    }

    bool pumpUntil (const std::function<bool()>& ready, int timeoutMs)
    {
        const auto start = juce::Time::getMillisecondCounter();
        auto* mm = juce::MessageManager::getInstance();

        while (juce::Time::getMillisecondCounter() - start < (juce::uint32) timeoutMs)
        {
            if (ready())
                return true;

            if (mm != nullptr)
                mm->runDispatchLoopUntil (50);
            else
                std::this_thread::sleep_for (std::chrono::milliseconds (50));
        }

        return ready();
    }

    class Harness final : public juce::JUCEApplicationBase
    {
    public:
        const juce::String getApplicationName() override { return "chouchouARASelfTest"; }
        const juce::String getApplicationVersion() override { return "1.0"; }
        bool moreThanOneInstanceAllowed() override { return false; }
        void anotherInstanceStarted (const juce::String&) override {}
        void suspended() override {}
        void resumed() override {}
        void systemRequestedQuit() override { quit(); }
        void memoryWarningReceived() override {}
        void unhandledException (const std::exception*, const juce::String&, int) override {}

        void initialise (const juce::String&) override
        {
            const int code = runTests();
            setApplicationReturnValue (code);
            quit();
        }

        void shutdown() override {}

    private:
        int runTests()
        {
            std::cout << "\n=== chouchouARASelfTest (SpectraLayers + embedded ARA) ===\n";

           #if ! (JUCE_PLUGINHOST_ARA && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX))
            expect (false, "JUCE_PLUGINHOST_ARA enabled");
            return 1;
           #endif

            juce::AudioPluginFormatManager formats;
            juce::addDefaultFormatsToManager (formats);

            const auto pluginFile = spectraLayersVST3();
            expect (pluginFile.getFullPathName().isNotEmpty(),
                    "SpectraLayers.vst3 present",
                    pluginFile.getFullPathName().toStdString());
            if (pluginFile.getFullPathName().isEmpty())
                return 1;

            std::cout << "  info  path: " << pluginFile.getFullPathName() << "\n";

            juce::AudioPluginFormat* vst3 = nullptr;
            for (auto* f : formats.getFormats())
                if (f->getName() == "VST3")
                    vst3 = f;

            expect (vst3 != nullptr, "VST3 format available");
            if (vst3 == nullptr)
                return 1;

            juce::OwnedArray<juce::PluginDescription> types;
            vst3->findAllTypesForFile (types, pluginFile.getFullPathName());
            expect (types.size() > 0, "findAllTypesForFile returned descriptions",
                    "count=" + std::to_string (types.size()));
            if (types.isEmpty())
                return 1;

            auto desc = *types.getFirst();
            std::cout << "  info  name=" << desc.name
                      << "  category=" << desc.category
                      << "  hasARAExtension(raw)=" << (int) desc.hasARAExtension << "\n";

            expect (pluginDescriptionHasARA (desc),
                    "pluginDescriptionHasARA detects SpectraLayers/OnlyARA");

            // CRITICAL regression: Audition scan leaves hasARAExtension=0.
            // Do NOT promote the flag — factory must still start (Capture fix).
            expect (! desc.hasARAExtension
                        || desc.category.containsIgnoreCase ("ARA"),
                    "raw description is OnlyARA-style (flag may be 0)");
            desc.hasARAExtension = false;
            std::cout << "  info  forcing hasARAExtension=0 to simulate Audition scan result\n";

            juce::String err;
            auto instance = formats.createPluginInstance (desc, 48000.0, 512, err);
            expect (instance != nullptr, "createPluginInstance with flag=0", err.toStdString());
            if (instance == nullptr)
                return 1;

            std::cout << "  info  instance.hasARAExtension="
                      << (int) instance->getPluginDescription().hasARAExtension
                      << "  category=" << instance->getPluginDescription().category << "\n";

            instance = maybeWrapWithARAHost (std::move (instance));
            auto* ara = asARAWrapper (instance.get());
            expect (ara != nullptr, "maybeWrapWithARAHost despite hasARAExtension=0");
            if (ara == nullptr)
                return 1;

            std::cout << "  info  waiting for async ARA factory / DocumentController...\n";
            const bool ready = pumpUntil ([&] { return ara->isARADocumentReady(); }, 20000);
            expect (ready, "ARA DocumentController ready with flag=0 (Capture prerequisite)");
            if (! ready)
                return 1;

            const auto wav = writeTestWav();
            expect (wav.existsAsFile(), "wrote test WAV", wav.getFullPathName().toStdString());
            if (! wav.existsAsFile())
                return 1;

            // Simulate ARA Capture → assignFileToARA (same path as the button)
            juce::String assignErr;
            const bool assigned = assignFileToARA (*ara, wav, assignErr);
            expect (assigned, "ARA Capture path: assignFileToARA", assignErr.toStdString());

            pumpUntil ([&] { return ara->hasBoundAudioSource(); }, 5000);
            expect (ara->hasBoundAudioSource(), "PlaybackRegion Context bound after Capture",
                    ara->getAssignedAudioSourceFile().getFileName().toStdString());

            // Also exercise live-capture helper (empty → must fail clearly)
            {
                LiveCaptureBuffer empty;
                empty.prepare (48000.0, 8);
                juce::String liveErr;
                const bool liveOk = assignLiveCaptureToARA (*ara, empty, liveErr);
                expect (! liveOk, "empty live capture fails loudly");
                expect (liveErr.containsIgnoreCase ("capture") || liveErr.containsIgnoreCase ("audio"),
                        "empty capture error mentions capture/audio",
                        liveErr.toStdString());
            }

            // Fill a live buffer and Capture again
            {
                LiveCaptureBuffer live;
                live.prepare (48000.0, 8);
                juce::AudioBuffer<float> chunk (2, 2048);
                for (int i = 0; i < 2048; ++i)
                {
                    const float s = 0.1f * std::sin (0.05f * (float) i);
                    chunk.setSample (0, i, s);
                    chunk.setSample (1, i, s);
                }
                for (int n = 0; n < 40; ++n)
                    live.push (chunk);

                expect (live.getNumSamplesCaptured() > 0, "liveCapture has samples",
                        std::to_string (live.getNumSamplesCaptured()));

                juce::String liveErr;
                const bool liveOk = assignLiveCaptureToARA (*ara, live, liveErr);
                expect (liveOk, "assignLiveCaptureToARA (button path)", liveErr.toStdString());
                expect (ara->hasBoundAudioSource(), "bound after live Capture");
            }

            ara->prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;

            bool processed = true;
            for (int i = 0; i < 8; ++i)
            {
                buffer.clear();
                try
                {
                    ara->processBlock (buffer, midi);
                }
                catch (...)
                {
                    processed = false;
                    break;
                }
            }

            expect (processed, "processBlock x8 without throw");
            ara->releaseResources();
            ara->clearAudioSourceFile();
            instance.reset();

            std::cout << "\n=== result: " << (gFails == 0 ? "ALL PASS" : "FAILED")
                      << " (" << gFails << " failures) ===\n\n";
            return gFails == 0 ? 0 : 1;
        }
    };
}

START_JUCE_APPLICATION (Harness)
