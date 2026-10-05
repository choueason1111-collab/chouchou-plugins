/*
  ==============================================================================

    chouchouPlugplugin — rack editor (10 slots)

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
class SlotRowComponent  : public juce::Component
{
public:
    SlotRowComponent (NewProjectAudioProcessor& proc, int slotIndex);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();

private:
    void openPluginChooser();
    void openEditorWindow();
    void deferDestroyWindow (std::unique_ptr<ClosableWindow>& window);

    NewProjectAudioProcessor& processor;
    int index = 0;

    juce::Label indexLabel, nameLabel;
    juce::TextButton loadButton { "Load" };
    juce::TextButton openButton { "Open UI" };
    juce::ToggleButton bypassButton { "Bypass" };
    juce::TextButton clearButton { "Clear" };

    std::unique_ptr<ClosableWindow> editorWindow;
    std::unique_ptr<ClosableWindow> chooserWindow;
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
class NewProjectAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::ComboBox::Listener,
                                        private juce::ChangeListener
{
public:
    explicit NewProjectAudioProcessorEditor (NewProjectAudioProcessor&);
    ~NewProjectAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshAll();
    void comboBoxChanged (juce::ComboBox*) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void startScanForSelectedFormat();

    NewProjectAudioProcessor& audioProcessor;

    juce::Label titleLabel, subtitleLabel;
    juce::ComboBox formatBox;
    juce::TextButton scanButton { "Scan" };
    juce::Label scanHintLabel;

    std::array<std::unique_ptr<SlotRowComponent>, NewProjectAudioProcessor::kNumSlots> rows;
    std::unique_ptr<ScanProgressWindow> scanWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessorEditor)
};
