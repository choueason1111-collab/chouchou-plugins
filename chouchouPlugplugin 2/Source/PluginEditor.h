/*
  ==============================================================================

    chouchouPlugplugin2 — rack editor (10 slots)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>
#include <functional>
#include <memory>

//==============================================================================
class ClosableWindow  : public juce::DocumentWindow
{
public:
    ClosableWindow (const juce::String& name, juce::Colour bg)
        : DocumentWindow (name, bg,
                          DocumentWindow::closeButton
                            | DocumentWindow::minimiseButton)
    {
        // Native title bar + always-on-top helps Insert Basic / AU XPC hosts
        // deliver mouse events to floating windows.
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setAlwaysOnTop (true);
    }

    std::function<void()> onClose;

    void closeButtonPressed() override
    {
        if (onClose)
            onClose();
        else
            setVisible (false);
    }

    void showAround (juce::Component* anchor, int width, int height)
    {
        setSize (width, height);
        if (anchor != nullptr)
            centreAroundComponent (anchor, width, height);
        else
            centreWithSize (width, height);

        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
    }
};

//==============================================================================
/** Modal-style panel drawn inside the editor. Hosts like Audition can hide separate
    top-level windows behind their own plugin window, so pickers live in here instead. */
class OverlayPanel  : public juce::Component
{
public:
    OverlayPanel (const juce::String& title, std::unique_ptr<juce::Component> content);

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<int> panelBounds() const;

    juce::String titleText;
    juce::TextButton closeButton { "Close" };
    std::unique_ptr<juce::Component> content;
};

//==============================================================================
class SlotRowComponent  : public juce::Component
{
public:
    SlotRowComponent (NewProjectAudioProcessor& proc, int slotIndex);
    ~SlotRowComponent() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    /** Open the slot's plug-in UI (after an import bound audio to it). */
    void openPluginUi() { openEditorWindow(); }

private:
    void openPluginChooser();
    void openEditorWindow();
    void deferDestroyWindow (std::unique_ptr<ClosableWindow>& window);
    void closeChooser();
    void closeAllFloatingWindows();

    NewProjectAudioProcessor& processor;
    int index = 0;

    juce::Label indexLabel, nameLabel;
    juce::TextButton loadButton { "Load" };
    juce::TextButton openButton { "Open UI" };
    juce::TextButton araCaptureButton { "ARA Bind" };
    juce::TextButton araHostButton { "ARA Host" };
    juce::TextButton renderButton { "Render" };
    juce::ToggleButton bypassButton { "Bypass" };
    juce::TextButton clearButton { "Clear" };

    std::unique_ptr<ClosableWindow> editorWindow;
    std::unique_ptr<OverlayPanel> chooserOverlay;
    std::unique_ptr<ClosableWindow> araHostWindow;
    std::shared_ptr<juce::AudioPluginInstance> editorKeepAlive;
};

//==============================================================================
class ScanProgressWindow  : public ClosableWindow,
                            private juce::Timer
{
public:
    ScanProgressWindow (NewProjectAudioProcessor& proc,
                        juce::AudioPluginFormat& format,
                        std::function<void()> onFinished);

    ~ScanProgressWindow() override;

private:
    void timerCallback() override;
    void finish (bool cancelled);
    void closeAfterFinish();
    void resizedContent();

    NewProjectAudioProcessor& processor;
    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    std::function<void()> finishedCallback;

    juce::Component content;
    juce::Label titleLabel, currentLabel, countLabel;
    juce::TextButton cancelButton { "Cancel" };
    int scannedCount = 0;
    bool finishing = false;
};

//==============================================================================
/** Two horizontal bars (insert input / insert output) with decay; marks the locked source. */
class CaptureMeter  : public juce::Component
{
public:
    void setLevels (float inputPeak, float outputPeak, NewProjectAudioProcessor::CaptureSource source);
    void paint (juce::Graphics&) override;

private:
    float shownIn = 0.0f, shownOut = 0.0f;
    NewProjectAudioProcessor::CaptureSource lockedSource = NewProjectAudioProcessor::CaptureSource::Undecided;
};

//==============================================================================
class NewProjectAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        public juce::FileDragAndDropTarget,
                                        private juce::ComboBox::Listener,
                                        private juce::ChangeListener,
                                        private juce::Timer
{
public:
    explicit NewProjectAudioProcessorEditor (NewProjectAudioProcessor&);
    ~NewProjectAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void refreshAll();
    void updateCaptureBar();
    void updateGuide();
    void timerCallback() override;
    void comboBoxChanged (juce::ComboBox*) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void startScanForSelectedFormat();
    void openStandaloneHost();
    void toggleBridgeRec();
    void openInSpectraLayersPickWav();
    void promptSaveNamedCapture();

    NewProjectAudioProcessor& audioProcessor;

    juce::Label titleLabel, subtitleLabel;
    juce::ComboBox formatBox;
    juce::TextButton scanButton { "Scan" };
    juce::TextButton openStandaloneButton { "Standalone" };
    juce::TextButton copyReportButton { "Copy Report" };
    juce::Label scanHintLabel;

    // Capture bar — the only recording entry point.
    juce::TextButton recButton { "Rec" };
    juce::ToggleButton autoToggle { "Auto" };
    juce::TextButton importTrackButton { "Import Track" };
    bool dragHover = false;
    juce::TextButton saveTakeButton { "Save..." };
    juce::TextButton discardTakeButton { "Discard" };
    juce::TextButton openInSlButton { "Open in SL..." };
    CaptureMeter captureMeter;
    juce::Label captureInfoLabel, captureHintLabel;
    juce::Rectangle<int> captureBarArea, slotsArea;
    int silentRecordingTicks = 0;

    // Host probe row (diagnostics build).
    juce::TextButton probeLogButton { "Probe Log" };
    juce::TextButton probeClearButton { "Clear" };
    juce::TextButton probeMarkButton { "Mark" };
    juce::ToggleButton probeLatencyToggle { "Latency 1024" };
    juce::ToggleButton probeTailToggle { "Tail 2s" };
    juce::ToggleButton probeClickToggle { "Mark output" };
    juce::Label probeInfoLabel;

    std::array<std::unique_ptr<SlotRowComponent>, NewProjectAudioProcessor::kNumSlots> rows;
    std::unique_ptr<ScanProgressWindow> scanWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessorEditor)
};
