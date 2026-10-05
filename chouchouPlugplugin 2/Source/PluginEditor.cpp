/*
  ==============================================================================

    chouchouPlugplugin2 - rack editor

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginScanSupport.h"
#include "ARA/HostMode.h"
#include "Audition/StandaloneLauncher.h"

namespace
{
    const juce::Colour kBg     { 0xff121416 };
    const juce::Colour kPanel  { 0xff1e2226 };
    const juce::Colour kRow    { 0xff282c31 };
    const juce::Colour kAccent { 0xffc4a574 };
    const juce::Colour kText   { 0xffebe8e3 };
    const juce::Colour kMuted  { 0xff8f8a84 };
    const juce::Colour kDim    { 0xff3a3f45 };
    const juce::Colour kOk     { 0xff7ec8a3 };
}

//==============================================================================
SlotRowComponent::SlotRowComponent (NewProjectAudioProcessor& proc, int slotIndex)
    : processor (proc), index (slotIndex)
{
    indexLabel.setText (juce::String (index + 1).paddedLeft ('0', 2), juce::dontSendNotification);
    indexLabel.setJustificationType (juce::Justification::centred);
    indexLabel.setColour (juce::Label::textColourId, kAccent);
    indexLabel.setFont (juce::FontOptions (16.0f).withStyle ("Bold"));
    addAndMakeVisible (indexLabel);

    nameLabel.setColour (juce::Label::textColourId, kText);
    nameLabel.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (nameLabel);

    auto styleBtn = [] (juce::TextButton& b, juce::Colour fill)
    {
        b.setColour (juce::TextButton::buttonColourId, fill);
        b.setColour (juce::TextButton::textColourOffId, kText);
    };

    styleBtn (loadButton, kDim);
    styleBtn (openButton, kDim);
    styleBtn (araCaptureButton, kDim);
    styleBtn (araHostButton, kDim);
    styleBtn (renderButton, kDim);
    styleBtn (clearButton, kDim);
    bypassButton.setColour (juce::ToggleButton::textColourId, kMuted);

    addAndMakeVisible (loadButton);
    addAndMakeVisible (openButton);
    addAndMakeVisible (araCaptureButton);
    addAndMakeVisible (araHostButton);
    addChildComponent (renderButton);
    renderButton.setTooltip ("Write SpectraLayers' result for the whole clip to <name>_SL.wav (DAW stopped).");
    renderButton.onClick = [this] { processor.renderSlotToWavAsync (index); };
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (clearButton);

    loadButton.onClick = [this] { openPluginChooser(); };
    openButton.onClick = [this] { openEditorWindow(); };
    // Recording lives only on the editor's Capture bar. This button just binds audio to an
    // ARA slot outside Bridge mode (take in memory, or a WAV picked from disk).
    araCaptureButton.onClick = [this]
    {
        if (! allowsInnerAraPluginLoad (processor.getHostMode()))
            return;

        const auto captured = processor.getLiveCapture().getNumSamplesCaptured();

        if (captured <= 0)
        {
            // The file is placed at the DAW cursor position at the moment it is picked.
            auto chooser = std::make_shared<juce::FileChooser> (
                "Choose audio for SpectraLayers (placed at the DAW cursor)",
                processor.getDefaultCaptureDirectory(),
                "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf");

            juce::Component::SafePointer<SlotRowComponent> safe (this);
            chooser->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe, chooser] (const juce::FileChooser& fc)
                {
                    if (safe == nullptr)
                        return;

                    auto& proc = safe->processor;
                    auto file = fc.getResult();
                    if (! file.existsAsFile())
                    {
                        proc.setUiStatus ("ARA bind cancelled.");
                        return;
                    }

                    proc.importFileToSpectraLayers (file, proc.getLastHostTimeSamples());
                });
            return;
        }

        juce::String err;
        if (! processor.bindTakeToSpectraLayers (err))
            processor.setUiStatus (err.isNotEmpty() ? err : "Bind failed.  [Copy Report]");
    };
    araHostButton.onClick = [this]
    {
        if (araHostWindow != nullptr)
        {
            araHostWindow->toFront (true);
            return;
        }

        auto ed = processor.createARAHostControlEditor (index);
        if (ed == nullptr)
        {
            processor.setUiStatus ("No ARA host controls for this slot.");
            return;
        }

        editorKeepAlive = processor.getSlotInstanceShared (index);
        araHostWindow = std::make_unique<ClosableWindow> ("ARA Host " + juce::String (index + 1), kPanel);
        juce::Component::SafePointer<SlotRowComponent> safe (this);
        araHostWindow->onClose = [safe]
        {
            if (safe != nullptr)
                safe->deferDestroyWindow (safe->araHostWindow);
        };
        ed->setSize (juce::jmax (480, ed->getWidth()), juce::jmax (220, ed->getHeight()));
        araHostWindow->setContentOwned (ed.release(), true);
        araHostWindow->showAround (this, araHostWindow->getWidth(), araHostWindow->getHeight());
    };
    clearButton.onClick = [this]
    {
        closeAllFloatingWindows();
        processor.clearSlot (index);
    };
    bypassButton.onClick = [this]
    {
        processor.setSlotBypassed (index, bypassButton.getToggleState());
    };

    refresh();
}

SlotRowComponent::~SlotRowComponent()
{
    // Tear down floating windows before `this` dies - onClose lambdas capture this.
    closeAllFloatingWindows();
}

//==============================================================================
OverlayPanel::OverlayPanel (const juce::String& title, std::unique_ptr<juce::Component> c)
    : titleText (title), content (std::move (c))
{
    setWantsKeyboardFocus (true);
    closeButton.setColour (juce::TextButton::buttonColourId, kDim);
    closeButton.setColour (juce::TextButton::textColourOffId, kText);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    if (content != nullptr)
        addAndMakeVisible (*content);
}

juce::Rectangle<int> OverlayPanel::panelBounds() const
{
    return getLocalBounds().reduced (24);
}

void OverlayPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    auto panel = panelBounds();
    g.setColour (kPanel);
    g.fillRoundedRectangle (panel.toFloat(), 10.0f);
    g.setColour (kText);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (titleText, panel.reduced (14, 0).removeFromTop (38), juce::Justification::centredLeft);
}

void OverlayPanel::resized()
{
    auto panel = panelBounds();
    auto header = panel.removeFromTop (38).reduced (10, 6);
    closeButton.setBounds (header.removeFromRight (72));
    if (content != nullptr)
        content->setBounds (panel.reduced (4));
}

void OverlayPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! panelBounds().contains (e.getPosition()) && onClose)
        onClose();
}

bool OverlayPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}

//==============================================================================
void SlotRowComponent::closeChooser()
{
    if (chooserOverlay == nullptr)
        return;

    // Never delete the overlay from inside one of its own button callbacks.
    std::shared_ptr<OverlayPanel> doomed (chooserOverlay.release());
    doomed->onClose = nullptr;
    doomed->setVisible (false);
    juce::MessageManager::callAsync ([doomed] {});
}

void SlotRowComponent::closeAllFloatingWindows()
{
    if (editorWindow != nullptr)
        editorWindow->onClose = nullptr;
    if (araHostWindow != nullptr)
        araHostWindow->onClose = nullptr;

    editorWindow.reset();
    chooserOverlay.reset();
    araHostWindow.reset();
    editorKeepAlive.reset();
}

void SlotRowComponent::deferDestroyWindow (std::unique_ptr<ClosableWindow>& window)
{
    // Never delete a DocumentWindow from inside closeButtonPressed.
    // Do not capture `this` - only the window unique_ptr.
    auto doomed = std::move (window);
    juce::MessageManager::callAsync ([doomed = std::move (doomed)]() mutable
    {
        if (doomed != nullptr)
            doomed->onClose = nullptr;
        doomed.reset();
    });
}

void SlotRowComponent::paint (juce::Graphics& g)
{
    g.setColour (kRow);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f);
}

void SlotRowComponent::refresh()
{
    const bool loaded = processor.isSlotLoaded (index);
    nameLabel.setText (processor.getSlotName (index), juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, loaded ? kOk : kMuted);

    const bool ara = loaded && processor.isSlotARA (index);
    const juce::PluginHostType host;

    openButton.setEnabled (loaded);
    openButton.setButtonText ("Open UI");
    openButton.setTooltip (ara && host.isAdobeAudition()
                               ? "Audition: Option-click to try the SpectraLayers UI here (blank SAM window seen before)."
                               : juce::String());

    // Only one recording entry exists (the Capture bar); slots never start/stop recording.
    const bool showAraBind = ara && allowsInnerAraPluginLoad (processor.getHostMode());
    if (araCaptureButton.isVisible() != showAraBind)
    {
        araCaptureButton.setVisible (showAraBind);
        resized();
    }
    araCaptureButton.setEnabled (showAraBind);
    araHostButton.setEnabled (ara);

    double regionStart = 0.0, regionLength = 0.0;
    const bool bound = ara && processor.getSlotRegion (index, regionStart, regionLength);
    if (renderButton.isVisible() != bound)
    {
        renderButton.setVisible (bound);
        resized();
    }
    renderButton.setEnabled (bound && ! processor.isRenderRunning());
    renderButton.setButtonText (processor.isRenderRunning() ? "..." : "Render");
    clearButton.setEnabled (loaded);
    bypassButton.setEnabled (loaded);
    bypassButton.setToggleState (processor.isSlotBypassed (index), juce::dontSendNotification);

    if (showAraBind)
    {
        const auto n = processor.getLiveCapture().getNumSamplesCaptured();
        araCaptureButton.setButtonText ("ARA Bind");
        araCaptureButton.setTooltip (
            n > 0 ? ("Send the current take (" + juce::String (n) + " samples) to SpectraLayers at its recorded position")
                  : "No take in memory - pick an audio file (placed at the DAW cursor)");
    }

    if (! loaded)
        closeAllFloatingWindows();
}

void SlotRowComponent::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    indexLabel.setBounds (r.removeFromLeft (36));
    r.removeFromLeft (6);

    clearButton.setBounds (r.removeFromRight (56).reduced (1));
    bypassButton.setBounds (r.removeFromRight (68).reduced (1));
    araHostButton.setBounds (r.removeFromRight (78).reduced (1));
    if (renderButton.isVisible())
        renderButton.setBounds (r.removeFromRight (64).reduced (1));
    if (araCaptureButton.isVisible())
        araCaptureButton.setBounds (r.removeFromRight (80).reduced (1));
    openButton.setBounds (r.removeFromRight (72).reduced (1));
    loadButton.setBounds (r.removeFromRight (56).reduced (1));
    r.removeFromRight (4);
    nameLabel.setBounds (r);
}

void SlotRowComponent::openPluginChooser()
{
    // Custom filtered list - no PluginListComponent Options / modal dialogs
    // (those hang Insert Basic). Keyword search matches name / maker / format.
    struct Content final : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::TextEditor::Listener
    {
        Content (NewProjectAudioProcessor& proc)
            : processor (proc)
        {
            search.setTextToShowWhenEmpty ("Search keyword (name / maker / format)...", kMuted);
            search.setColour (juce::TextEditor::backgroundColourId, kDim);
            search.setColour (juce::TextEditor::textColourId, kText);
            search.setColour (juce::TextEditor::outlineColourId, kMuted);
            search.setColour (juce::TextEditor::focusedOutlineColourId, kAccent);
            search.setColour (juce::CaretComponent::caretColourId, kAccent);
            search.addListener (this);
            addAndMakeVisible (search);

            formatFilter.addItem ("All formats", 1);
            formatFilter.addItem ("AudioUnit", 2);
            formatFilter.addItem ("VST3", 3);
            formatFilter.setSelectedId (1, juce::dontSendNotification);
            formatFilter.setColour (juce::ComboBox::backgroundColourId, kDim);
            formatFilter.setColour (juce::ComboBox::textColourId, kText);
            formatFilter.setColour (juce::ComboBox::outlineColourId, kMuted);
            formatFilter.onChange = [this] { rebuildFiltered(); };
            addAndMakeVisible (formatFilter);

            list.setModel (this);
            list.setColour (juce::ListBox::backgroundColourId, kBg);
            list.setColour (juce::ListBox::outlineColourId, kDim);
            list.setRowHeight (28);
            list.setMultipleSelectionEnabled (false);
            addAndMakeVisible (list);

            loadButton.setColour (juce::TextButton::buttonColourId, kAccent);
            loadButton.setColour (juce::TextButton::textColourOffId, kBg);
            addAndMakeVisible (loadButton);

            countLabel.setColour (juce::Label::textColourId, kMuted);
            countLabel.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (countLabel);

            hint.setColour (juce::Label::textColourId, kMuted);
            hint.setFont (juce::FontOptions (12.0f));
            hint.setText ("Type to filter | double-click or Load | Close / Esc to dismiss  |  Insert Basic: prefer AU",
                          juce::dontSendNotification);
            addAndMakeVisible (hint);

            rebuildFiltered();
            setSize (920, 520);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            auto top = r.removeFromTop (30);
            formatFilter.setBounds (top.removeFromRight (130));
            top.removeFromRight (8);
            search.setBounds (top);
            r.removeFromTop (8);

            auto bottom = r.removeFromBottom (36);
            hint.setBounds (r.removeFromBottom (22));
            r.removeFromBottom (4);
            countLabel.setBounds (bottom.removeFromLeft (bottom.getWidth() - 110));
            loadButton.setBounds (bottom.removeFromRight (100).reduced (0, 4));
            list.setBounds (r);
        }

        int getNumRows() override { return filtered.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (! juce::isPositiveAndBelow (row, filtered.size()))
                return;

            if (selected)
                g.fillAll (kAccent.withAlpha (0.28f));
            else if (row % 2 != 0)
                g.fillAll (kRow.withAlpha (0.45f));

            const auto& d = filtered.getReference (row);
            g.setColour (kText);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (d.name, 8, 0, w - 16, h, juce::Justification::centredLeft, true);

            g.setColour (kMuted);
            g.setFont (juce::FontOptions (11.0f));
            const auto meta = d.manufacturerName + "  |  " + d.pluginFormatName;
            g.drawText (meta, 8, 0, w - 16, h, juce::Justification::centredRight, true);
        }

        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            if (onChoose)
                onChoose (row);
        }

        void textEditorTextChanged (juce::TextEditor&) override { rebuildFiltered(); }
        void textEditorReturnKeyPressed (juce::TextEditor&) override
        {
            if (filtered.size() == 1 && onChoose)
                onChoose (0);
        }

        void rebuildFiltered()
        {
            filtered.clear();
            const auto query = search.getText().trim();
            const int fmtId = formatFilter.getSelectedId();

            for (const auto& d : processor.getPluginList().getTypes())
            {
                if (fmtId == 2 && ! d.pluginFormatName.containsIgnoreCase ("AudioUnit")
                    && ! d.pluginFormatName.containsIgnoreCase ("AU"))
                    continue;
                if (fmtId == 3 && ! d.pluginFormatName.containsIgnoreCase ("VST3"))
                    continue;

                if (query.isNotEmpty())
                {
                    const bool hit = d.name.containsIgnoreCase (query)
                                  || d.manufacturerName.containsIgnoreCase (query)
                                  || d.pluginFormatName.containsIgnoreCase (query)
                                  || d.category.containsIgnoreCase (query)
                                  || d.descriptiveName.containsIgnoreCase (query);
                    if (! hit)
                        continue;
                }

                filtered.add (d);
            }

            countLabel.setText (juce::String (filtered.size()) + " / "
                                    + juce::String (processor.getPluginList().getTypes().size())
                                    + " plugins",
                                juce::dontSendNotification);
            list.updateContent();
            list.deselectAllRows();
            if (filtered.size() > 0)
                list.selectRow (0);
        }

        std::function<void(int)> onChoose;
        NewProjectAudioProcessor& processor;
        juce::Array<juce::PluginDescription> filtered;
        juce::TextEditor search;
        juce::ComboBox formatFilter;
        juce::ListBox list { "plugins", this };
        juce::TextButton loadButton { "Load" };
        juce::Label countLabel, hint;
    };

    closeChooser();

    auto contentOwner = std::make_unique<Content> (processor);
    auto* content = contentOwner.get();
    content->loadButton.onClick = [this, content]
    {
        const int row = content->list.getSelectedRow();
        if (! juce::isPositiveAndBelow (row, content->filtered.size()))
        {
            processor.setUiStatus ("Select a plugin (or type a keyword), then Load.");
            return;
        }

        const auto desc = content->filtered.getReference (row);
        content->loadButton.setEnabled (false);
        content->loadButton.setButtonText ("...");
        content->hint.setText ("Starting load - window will close; watch status on main UI.",
                               juce::dontSendNotification);

        editorWindow.reset();
        editorKeepAlive.reset();

        // Close chooser first so Insert Basic stays responsive while AU loads async.
        closeChooser();
        // Built outside the capture list: MSVC reads `this` in a nested lambda's init-capture
        // as the enclosing lambda.
        juce::Component::SafePointer<SlotRowComponent> safeProc (this);
        juce::MessageManager::callAsync ([safeProc, desc, slot = index]
        {
            if (safeProc == nullptr)
                return;
            safeProc->processor.loadPluginAsync (slot, desc);
        });
    };
    content->onChoose = [content] (int row)
    {
        if (juce::isPositiveAndBelow (row, content->filtered.size()))
            content->list.selectRow (row);
        content->loadButton.triggerClick();
    };

    chooserOverlay = std::make_unique<OverlayPanel> ("Load plugin into slot " + juce::String (index + 1),
                                                     std::move (contentOwner));
    chooserOverlay->onClose = [safe = juce::Component::SafePointer<SlotRowComponent> (this)]
    {
        if (safe != nullptr)
            safe->closeChooser();
    };

    juce::Component* host = findParentComponentOfClass<juce::AudioProcessorEditor>();
    if (host == nullptr)
        host = getTopLevelComponent();

    chooserOverlay->setBounds (host->getLocalBounds());
    host->addAndMakeVisible (*chooserOverlay);
    chooserOverlay->toFront (true);
    content->search.grabKeyboardFocus();
}

void SlotRowComponent::openEditorWindow()
{
    editorKeepAlive = processor.getSlotInstanceShared (index);
    if (editorKeepAlive == nullptr)
        return;

    // Product law: Audition = Bridge only. Opening SpectraLayers UI here triggers
    // SAM not initialized -> misleading "install SAM" dialog -> blank Qt window.
    // Same binary reaches Ready in Standalone / Pro Tools.
    if (processor.isSlotARA (index))
    {
        const juce::PluginHostType host;

        if (araHostWindow == nullptr)
            araHostButton.triggerClick();
        else
            araHostWindow->toFront (true);

        // Option-click opts in to trying the UI inside Audition anyway.
        if (host.isAdobeAudition() && ! juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown())
        {
            processor.setUiStatus (
                "Audition: SpectraLayers UI has shown a blank SAM window here before. "
                "Option-click Open UI to try anyway, or use Open in SL... (Standalone).");
            editorKeepAlive.reset();
            return; // never createEditor under Audition for ARA slots
        }

        processor.setUiStatus ("ARA Host mode: opening SpectraLayers UI...");
    }

    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (
        editorKeepAlive->hasEditor() ? editorKeepAlive->createEditorAndMakeActive()
                                     : nullptr);

    if (editor == nullptr)
    {
        if (! processor.isSlotARA (index))
        {
            editorKeepAlive.reset();
            processor.setUiStatus ("Slot " + juce::String (index + 1) + ": plugin has no UI.");
        }
        else
        {
            editorKeepAlive.reset();
            processor.setUiStatus (
                "SpectraLayers UI did not open (not Ready). Stay on ARA Host panel or use Standalone.");
        }
        return;
    }

    const auto title = "Slot " + juce::String (index + 1) + " - " + processor.getSlotName (index);
    editorWindow = std::make_unique<ClosableWindow> (title, kPanel);
    editorWindow->onClose = [safe = juce::Component::SafePointer<SlotRowComponent> (this)]
    {
        if (safe != nullptr)
        {
            safe->editorKeepAlive.reset();
            safe->deferDestroyWindow (safe->editorWindow);
        }
    };
    editorWindow->setContentOwned (editor.release(), true);
    editorWindow->showAround (this, editorWindow->getWidth(), editorWindow->getHeight());
}

//==============================================================================
ScanProgressWindow::ScanProgressWindow (NewProjectAudioProcessor& proc,
                                        juce::AudioPluginFormat& format,
                                        std::function<void()> onFinished)
    : ClosableWindow ("Scanning " + format.getName() + "...", kPanel),
      processor (proc),
      finishedCallback (std::move (onFinished))
{
    onClose = [this] { finish (true); };

    titleLabel.setText ("Scanning plugins - please wait", juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, kText);
    titleLabel.setFont (juce::FontOptions (16.0f).withStyle ("Bold"));
    titleLabel.setJustificationType (juce::Justification::centredLeft);

    currentLabel.setText ("Starting...", juce::dontSendNotification);
    currentLabel.setColour (juce::Label::textColourId, kAccent);
    currentLabel.setFont (juce::FontOptions (13.0f));
    currentLabel.setJustificationType (juce::Justification::centredLeft);

    countLabel.setText ("0 files checked", juce::dontSendNotification);
    countLabel.setColour (juce::Label::textColourId, kMuted);
    countLabel.setFont (juce::FontOptions (12.0f));

    cancelButton.setColour (juce::TextButton::buttonColourId, kDim);
    cancelButton.setColour (juce::TextButton::textColourOffId, kText);
    cancelButton.onClick = [this] { finish (true); };

    content.addAndMakeVisible (titleLabel);
    content.addAndMakeVisible (currentLabel);
    content.addAndMakeVisible (countLabel);
    content.addAndMakeVisible (cancelButton);
    content.setSize (520, 160);
    setContentNonOwned (&content, true);

    scanner = std::make_unique<juce::PluginDirectoryScanner> (
        processor.getPluginList(),
        format,
        format.getDefaultLocationsToSearch(),
        true,
        getChouChouDeadMansPedalFile());

    const auto exe = findChouChouScannerExecutable();
    const bool oop = exe.existsAsFile();

    titleLabel.setText (oop
                            ? "Scanning out-of-process (crash-safe)"
                            : (isChouChouRunningAsStandaloneApp()
                                   ? "Scanning in-process - install Scanner helper for crash safety"
                                   : "Scanner helper missing - refusing in-process scan (host crash protection)"),
                        juce::dontSendNotification);

    if (! oop)
    {
        if (isChouChouRunningAsStandaloneApp())
            currentLabel.setText ("Tip: if the Standalone crashes, reopen and Scan again.\n"
                                  "The crashing plugin is auto-skipped next time.",
                                  juce::dontSendNotification);
        else
            currentLabel.setText ("Install:\n~/Library/Application Support/chouchou/"
                                  "chouchouPlugplugin/chouchouPlugplugin2 Scanner.app",
                                  juce::dontSendNotification);
    }

    setSize (540, 200);
    showAround (nullptr, 540, 200);

    startTimerHz (20);
}

ScanProgressWindow::~ScanProgressWindow()
{
    stopTimer();
}

void ScanProgressWindow::resizedContent()
{
    auto r = content.getLocalBounds().reduced (16);
    titleLabel.setBounds (r.removeFromTop (24));
    r.removeFromTop (10);
    currentLabel.setBounds (r.removeFromTop (48));
    r.removeFromTop (8);
    countLabel.setBounds (r.removeFromTop (20));
    cancelButton.setBounds (r.removeFromBottom (32).removeFromRight (100));
}

void ScanProgressWindow::timerCallback()
{
    if (finishing || scanner == nullptr)
        return;

    resizedContent();

    juce::String name;
    const bool more = scanner->scanNextFile (true, name);

    if (name.isNotEmpty() || more)
        ++scannedCount;

    if (name.isNotEmpty())
        currentLabel.setText ("Now scanning:\n" + name, juce::dontSendNotification);
    else
        currentLabel.setText ("Now scanning:\n(next file...)", juce::dontSendNotification);

    countLabel.setText (juce::String (scannedCount) + " files checked  |  "
                            + juce::String (processor.getPluginList().getTypes().size())
                            + " plugins found",
                        juce::dontSendNotification);

    if (! more)
        finish (false);
}

void ScanProgressWindow::finish (bool cancelled)
{
    if (finishing)
        return;

    finishing = true;
    stopTimer();
    scanner.reset();

    const int found = processor.getPluginList().getTypes().size();

    // Never use AlertWindow here - in Insert Basic / AU hosts the OK dialog
    // often cannot be dismissed. Show the result on this window and let Close/X
    // destroy it via finishedCallback.
    setName (cancelled ? "Scan cancelled" : "Scan complete");
    titleLabel.setText (cancelled ? "Scan cancelled" : "Scan complete",
                        juce::dontSendNotification);
    currentLabel.setText (cancelled
                              ? ("Stopped early.\nPlugins in list: " + juce::String (found))
                              : ("Done.\nPlugins found: " + juce::String (found)),
                          juce::dontSendNotification);
    countLabel.setText (juce::String (scannedCount) + " files checked",
                        juce::dontSendNotification);

    cancelButton.setButtonText ("Close");
    cancelButton.onClick = [this] { closeAfterFinish(); };
    onClose = [this] { closeAfterFinish(); };

    toFront (true);
}

void ScanProgressWindow::closeAfterFinish()
{
    auto cb = std::move (finishedCallback);
    finishedCallback = nullptr;

    if (cb)
        cb();
}

//==============================================================================
NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor (NewProjectAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    titleLabel.setText ("chouchouPlugplugin2", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f).withStyle ("Bold"));
    titleLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (titleLabel);

    {
        if (isChouChouRunningAsStandaloneApp())
            subtitleLabel.setText (
                "Mode: StandaloneFullARAMode  |  SpectraLayers UI OK  |  Open in SL... picks a WAV",
                juce::dontSendNotification);
        else
            subtitleLabel.setText (
                "Drop audio here -> SpectraLayers  |  Live: Import Track  |  Rec / Auto optional  |  Play = hear result, Render = WAV",
                juce::dontSendNotification);
    }
    subtitleLabel.setColour (juce::Label::textColourId, kMuted);
    subtitleLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (subtitleLabel);

    formatBox.setTextWhenNothingSelected ("Format...");
    formatBox.setColour (juce::ComboBox::backgroundColourId, kDim);
    formatBox.setColour (juce::ComboBox::textColourId, kText);
    formatBox.setColour (juce::ComboBox::outlineColourId, kMuted);
    formatBox.setColour (juce::ComboBox::arrowColourId, kAccent);

    auto& formats = audioProcessor.getFormatManager();
    for (int i = 0; i < formats.getNumFormats(); ++i)
        if (auto* f = formats.getFormat (i))
            formatBox.addItem (f->getName(), i + 1);

    if (formatBox.getNumItems() > 0)
        formatBox.setSelectedItemIndex (0, juce::dontSendNotification);

    formatBox.addListener (this);
    addAndMakeVisible (formatBox);

    scanButton.setColour (juce::TextButton::buttonColourId, kAccent);
    scanButton.setColour (juce::TextButton::textColourOffId, kBg);
    scanButton.onClick = [this] { startScanForSelectedFormat(); };
    addAndMakeVisible (scanButton);

    recButton.setColour (juce::TextButton::buttonColourId, kDim);
    recButton.setColour (juce::TextButton::textColourOffId, kText);
    recButton.onClick = [this] { toggleBridgeRec(); };
    addAndMakeVisible (recButton);

    autoToggle.setColour (juce::ToggleButton::textColourId, kText);
    autoToggle.setColour (juce::ToggleButton::tickColourId, kAccent);
    autoToggle.setTooltip ("One take: records from the next DAW Play until Stop, then sends it to SpectraLayers.");
    autoToggle.onClick = [this] { audioProcessor.setAutoCapture (autoToggle.getToggleState()); };
    addAndMakeVisible (autoToggle);

    importTrackButton.setColour (juce::TextButton::buttonColourId, kAccent);
    importTrackButton.setColour (juce::TextButton::textColourOffId, kBg);
    importTrackButton.setTooltip ("Send the selected Arrangement audio clip's file to SpectraLayers, aligned "
                                  "(needs the chouchouLink Control Surface).");
    importTrackButton.onClick = [this] { audioProcessor.requestLiveImportTrack(); };
    addChildComponent (importTrackButton);
    importTrackButton.setVisible (juce::PluginHostType().isAbletonLive());

    saveTakeButton.setColour (juce::TextButton::buttonColourId, kAccent);
    saveTakeButton.setColour (juce::TextButton::textColourOffId, kBg);
    saveTakeButton.setTooltip ("Choose a folder and file name for the take.");
    saveTakeButton.onClick = [this] { promptSaveNamedCapture(); };
    addAndMakeVisible (saveTakeButton);

    discardTakeButton.setColour (juce::TextButton::buttonColourId, kDim);
    discardTakeButton.setColour (juce::TextButton::textColourOffId, kText);
    discardTakeButton.setTooltip ("Throw away the unsaved take so you can record again.");
    discardTakeButton.onClick = [this]
    {
        audioProcessor.discardBridgeTake();
        audioProcessor.setUiStatus ("Take discarded.");
        updateCaptureBar();
    };
    addAndMakeVisible (discardTakeButton);

    openInSlButton.setColour (juce::TextButton::buttonColourId, kDim);
    openInSlButton.setColour (juce::TextButton::textColourOffId, kText);
    openInSlButton.setTooltip ("Pick a WAV, then launch Standalone with SpectraLayers bound to it.");
    openInSlButton.onClick = [this] { openInSpectraLayersPickWav(); };
    addAndMakeVisible (openInSlButton);

    addAndMakeVisible (captureMeter);

    captureInfoLabel.setColour (juce::Label::textColourId, kText);
    captureInfoLabel.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (captureInfoLabel);

    captureHintLabel.setColour (juce::Label::textColourId, kAccent);
    captureHintLabel.setFont (juce::FontOptions (11.5f));
    addAndMakeVisible (captureHintLabel);

    for (auto* b : { &probeLogButton, &probeClearButton, &probeMarkButton })
    {
        b->setColour (juce::TextButton::buttonColourId, kDim);
        b->setColour (juce::TextButton::textColourOffId, kText);
        addAndMakeVisible (*b);
    }
    probeLogButton.setTooltip ("Show the host probe log in Finder.");
    probeLogButton.onClick = []
    {
        auto f = ChouChouHostProbe::getLogFile();
        if (f.existsAsFile())
            f.revealToUser();
        else
            f.getParentDirectory().revealToUser();
    };
    probeClearButton.setTooltip ("Empty the probe log (shared by all chouchou instances).");
    probeClearButton.onClick = [] { ChouChouHostProbe::clearLog(); };
    probeMarkButton.setTooltip ("Write a numbered MARK line - press before each test step.");
    probeMarkButton.onClick = [this]
    {
        if (auto* p = audioProcessor.getHostProbe())
            p->mark();
    };

    auto* probe = audioProcessor.getHostProbe();
    for (auto* t : { &probeLatencyToggle, &probeTailToggle, &probeClickToggle })
    {
        t->setColour (juce::ToggleButton::textColourId, kText);
        t->setColour (juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible (*t);
    }
    if (probe != nullptr)
    {
        probeLatencyToggle.setToggleState (probe->expLatency.load(), juce::dontSendNotification);
        probeTailToggle.setToggleState (probe->expTail.load(), juce::dontSendNotification);
        probeClickToggle.setToggleState (probe->expMarkOutput.load(), juce::dontSendNotification);
    }
    probeLatencyToggle.setTooltip ("Report 1024 samples extra latency (no real delay) - does the host compensate?");
    probeLatencyToggle.onClick = [this] { audioProcessor.setProbeLatencyExperiment (probeLatencyToggle.getToggleState()); };
    probeTailToggle.setTooltip ("Report 2 s extra tail - does Apply / Export process longer?");
    probeTailToggle.onClick = [this] { audioProcessor.setProbeTailExperiment (probeTailToggle.getToggleState()); };
    probeClickToggle.setTooltip ("Put a click at sample 0 of every processing run - shows where output lands.");
    probeClickToggle.onClick = [this]
    {
        if (auto* p = audioProcessor.getHostProbe())
        {
            p->expMarkOutput.store (probeClickToggle.getToggleState());
            p->logEvent ("EXPERIMENT markOutput=" + juce::String ((int) probeClickToggle.getToggleState()));
        }
    };

    probeInfoLabel.setColour (juce::Label::textColourId, kMuted);
    probeInfoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (probeInfoLabel);

    openStandaloneButton.setColour (juce::TextButton::buttonColourId, kDim);
    openStandaloneButton.setColour (juce::TextButton::textColourOffId, kText);
    openStandaloneButton.setTooltip ("Opens empty Standalone (secondary). Prefer Open in SL...");
    openStandaloneButton.onClick = [this] { openStandaloneHost(); };
    openStandaloneButton.setEnabled (! isChouChouRunningAsStandaloneApp());
    addAndMakeVisible (openStandaloneButton);

    copyReportButton.setColour (juce::TextButton::buttonColourId, kDim);
    copyReportButton.setColour (juce::TextButton::textColourOffId, kText);
    copyReportButton.setTooltip ("Copy last error/diagnostic block to clipboard - paste into chat.");
    copyReportButton.onClick = [this]
    {
        audioProcessor.copyLastDiagnosticToClipboard();
        auto r = audioProcessor.getLastDiagnostic();
        if (r.message.isEmpty() && r.errorCode.isEmpty())
            audioProcessor.setUiStatus ("No diagnostic yet - trigger Capture/Load failure first.");
        else
            audioProcessor.setUiStatus ("Diagnostic copied to clipboard - paste it in chat.");
    };
    addAndMakeVisible (copyReportButton);

    scanHintLabel.setColour (juce::Label::textColourId, kMuted);
    scanHintLabel.setFont (juce::FontOptions (11.0f));
    scanHintLabel.setJustificationType (juce::Justification::centredRight);
    scanHintLabel.setText ("SpectraLayers = VST3 (not AU). Pick format -> Scan.  X / Close to dismiss.",
                           juce::dontSendNotification);
    addAndMakeVisible (scanHintLabel);

    for (int i = 0; i < NewProjectAudioProcessor::kNumSlots; ++i)
    {
        rows[(size_t) i] = std::make_unique<SlotRowComponent> (audioProcessor, i);
        addAndMakeVisible (*rows[(size_t) i]);
    }

    audioProcessor.addChangeListener (this);
    refreshAll();

    setSize (920, 750);
    startTimerHz (10);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
    stopTimer();
    if (auto* p = audioProcessor.getHostProbe())
        p->logEvent ("EDITOR closed");
    audioProcessor.removeChangeListener (this);
    formatBox.removeListener (this);
    scanWindow.reset();
}

void NewProjectAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshAll();
}

void NewProjectAudioProcessorEditor::comboBoxChanged (juce::ComboBox*) {}

void NewProjectAudioProcessorEditor::startScanForSelectedFormat()
{
    if (scanWindow != nullptr)
    {
        scanWindow->toFront (true);
        return;
    }

    const int id = formatBox.getSelectedId();
    if (id <= 0)
        return;

    auto* format = audioProcessor.getFormatManager().getFormat (id - 1);
    if (format == nullptr)
        return;

    scanButton.setEnabled (false);
    formatBox.setEnabled (false);

    juce::Component::SafePointer<NewProjectAudioProcessorEditor> safe (this);

    scanWindow = std::make_unique<ScanProgressWindow> (
        audioProcessor,
        *format,
        [safe]
        {
            if (safe == nullptr)
                return;

            const int found = safe->audioProcessor.getPluginList().getTypes().size();
            safe->scanHintLabel.setText ("Last scan: " + juce::String (found) + " plugins in list.",
                                         juce::dontSendNotification);
            safe->scanWindow.reset();
            safe->scanButton.setEnabled (true);
            safe->formatBox.setEnabled (true);
            safe->refreshAll();
        });
}

void NewProjectAudioProcessorEditor::refreshAll()
{
    for (auto& row : rows)
        if (row != nullptr)
            row->refresh();

    // In Standalone, Rec still works; Open in SL binds in-process instead of launching.
    openInSlButton.setButtonText (isChouChouRunningAsStandaloneApp()
                                      ? "Open WAV in SL..."
                                      : "Open in SL...");

    const auto status = audioProcessor.getUiStatus();
    if (status.isNotEmpty())
        scanHintLabel.setText (status, juce::dontSendNotification);

    updateCaptureBar();
}

void NewProjectAudioProcessorEditor::timerCallback()
{
    const int openSlot = audioProcessor.takePendingOpenUiSlot();
    if (juce::isPositiveAndBelow (openSlot, NewProjectAudioProcessor::kNumSlots) && rows[(size_t) openSlot] != nullptr)
        rows[(size_t) openSlot]->openPluginUi();

    autoToggle.setToggleState (audioProcessor.isAutoCaptureEnabled(), juce::dontSendNotification);
    updateCaptureBar();
    updateGuide();
}

static juce::String u8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }

void NewProjectAudioProcessorEditor::updateGuide()
{
    const bool live = juce::PluginHostType().isAbletonLive();
    const int slot = audioProcessor.findFirstARASlot();
    double start = 0.0, length = 0.0;
    const bool bound = slot >= 0 && audioProcessor.getSlotRegion (slot, start, length);

    juce::String text;
    if (audioProcessor.isRenderRunning())
        text = u8 ("輸出中… 完成後 Finder 會顯示 _SL.wav，拖回音軌即可。");
    else if (audioProcessor.isAutoCaptureEnabled())
        text = u8 ("Auto 已開：在 DAW 按播放開始錄，按停止後自動送進 SpectraLayers。");
    else if (bound)
        text = u8 ("2. 在 SpectraLayers 編輯  →  3. DAW 播到 ")
             + juce::String (start, 2) + " - " + juce::String (start + length, 2)
             + u8 (" 秒就會聽到結果；或停止後按該格 Render 輸出 WAV。");
    else
        text = u8 ("1. 把 DAW 游標放在 clip 開頭，再把音檔拖進這個視窗")
             + (live ? u8 ("（或選取 Arrangement clip 按 Import Track）") : juce::String())
             + u8 ("；也可開 Auto 錄一段。");

    captureHintLabel.setText (text, juce::dontSendNotification);
}

bool NewProjectAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;m4a;caf;ogg"))
            return true;
    return false;
}

void NewProjectAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHover = true;
    repaint();
}

void NewProjectAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void NewProjectAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();

    for (const auto& f : files)
    {
        const juce::File file (f);
        if (! file.hasFileExtension ("wav;aif;aiff;flac;mp3;m4a;caf;ogg"))
            continue;

        // Option held while dropping = place the file at the very start of the timeline.
        const bool atZero = juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown();
        audioProcessor.importFileToSpectraLayers (file, atZero ? 0 : audioProcessor.getLastHostTimeSamples());
        return;
    }
}

void NewProjectAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover)
        return;

    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    g.setColour (kAccent);
    g.drawRoundedRectangle (getLocalBounds().reduced (10).toFloat(), 12.0f, 3.0f);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText (u8 ("放開 → 送進 SpectraLayers（位置 = 目前 DAW 游標；按住 Option = 0 秒）"),
                getLocalBounds(), juce::Justification::centred);
}

static juce::String formatCaptureTime (int64_t samples, double sampleRate)
{
    const double secs = sampleRate > 0.0 ? (double) samples / sampleRate : 0.0;
    const int mins = (int) (secs / 60.0);
    return juce::String::formatted ("%02d:%04.1f", mins, secs - mins * 60.0);
}

static juce::String formatPeakDb (float peak)
{
    if (peak < LiveCaptureBuffer::kSilenceThreshold)
        return "-inf dB";
    return juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dB";
}

static juce::String describeCaptureSource (NewProjectAudioProcessor::CaptureSource s)
{
    using S = NewProjectAudioProcessor::CaptureSource;
    switch (s)
    {
        case S::Output: return "Source: Output (insert return)";
        case S::Input:  return "Source: Input (pre-FX at insert)";
        case S::Undecided: break;
    }
    return "Source: waiting for signal";
}

void NewProjectAudioProcessorEditor::updateCaptureBar()
{
    using UX = NewProjectAudioProcessor::CaptureUxState;
    const auto state  = audioProcessor.getCaptureUxState();
    const auto source = audioProcessor.getCaptureSource();
    const float inPeak  = audioProcessor.getLiveInputPeak();
    const float outPeak = audioProcessor.getLiveOutputPeak();
    const auto& cap = audioProcessor.getLiveCapture();

    captureMeter.setLevels (inPeak, outPeak, state == UX::Idle ? NewProjectAudioProcessor::CaptureSource::Undecided
                                                               : source);

    const bool liveSilent = juce::jmax (inPeak, outPeak) < LiveCaptureBuffer::kSilenceThreshold;
    silentRecordingTicks = (state == UX::Recording && liveSilent) ? silentRecordingTicks + 1 : 0;

    recButton.setButtonText (state == UX::Recording ? "Stop" : "Rec");
    recButton.setColour (juce::TextButton::buttonColourId,
                         state == UX::Recording ? juce::Colour (0xffc04848) : juce::Colour (0xff7a3030));
    recButton.setEnabled (state != UX::ReadyToSave);
    recButton.setTooltip (state == UX::ReadyToSave
                              ? "Save... or Discard the current take before recording again."
                              : "Rec = start recording at this insert; Stop = choose folder & name.");

    saveTakeButton.setEnabled (state == UX::ReadyToSave);
    discardTakeButton.setEnabled (state == UX::ReadyToSave);

    const auto duration = formatCaptureTime (cap.getNumSamplesCaptured(), cap.getSampleRate());
    juce::String info;
    auto colour = kText;

    switch (state)
    {
        case UX::Recording:
            info = "REC " + duration + "  |  " + describeCaptureSource (source)
                 + "  |  hold " + formatPeakDb (cap.getPeakAbs());
            if (silentRecordingTicks >= 10)
            {
                info = "REC " + duration + "  |  No signal at the insert - is the track playing / chouchou on the right track?";
                colour = juce::Colour (0xffff6060);
            }
            else
            {
                colour = juce::Colour (0xffff9090);
            }
            break;

        case UX::ReadyToSave:
            info = "Take ready " + duration + "  |  " + describeCaptureSource (source)
                 + "  |  peak " + formatPeakDb (cap.getPeakAbs());
            if (cap.didOverflow())
            {
                info << "  |  Buffer full (" << NewProjectAudioProcessor::kMaxCaptureSeconds << " s) - Save... or Discard";
                colour = juce::Colour (0xffffb050);
            }
            else
            {
                colour = kAccent;
            }
            break;

        case UX::Idle:
            info = "Idle  |  In " + formatPeakDb (inPeak) + "  /  Out " + formatPeakDb (outPeak)
                 + "  |  Optional: Rec / Auto";
            colour = kText;
            break;
    }

    captureInfoLabel.setText (info, juce::dontSendNotification);
    captureInfoLabel.setColour (juce::Label::textColourId, colour);

    if (auto* probe = audioProcessor.getHostProbe())
    {
        const auto track = probe->getTrackName();
        probeInfoLabel.setText ("Probe " + probe->getInstanceId() + "  |  " + probe->getHostDescription()
                                    + "  |  track: " + (track.isNotEmpty() ? track : juce::String ("(host did not say)"))
                                    + "  |  Desktop/" + ChouChouHostProbe::getLogFile().getFileName(),
                                juce::dontSendNotification);
    }
}

void NewProjectAudioProcessorEditor::toggleBridgeRec()
{
    using UX = NewProjectAudioProcessor::CaptureUxState;
    const auto state = audioProcessor.getCaptureUxState();
    juce::String err;

    if (state == UX::Recording)
    {
        if (audioProcessor.stopBridgeRecording (err))
        {
            // With SpectraLayers in the rack the take goes straight in; otherwise ask where to save.
            if (audioProcessor.findFirstARASlot() >= 0)
            {
                if (! audioProcessor.bindTakeToSpectraLayers (err))
                    audioProcessor.setUiStatus (err + "  Take kept - Save... or Discard.");
            }
            else
            {
                promptSaveNamedCapture();
            }
        }
    }
    else if (state == UX::Idle)
    {
        audioProcessor.startBridgeRecording (err);
    }

    updateCaptureBar();
}

void NewProjectAudioProcessorEditor::promptSaveNamedCapture()
{
    if (audioProcessor.getCaptureUxState() != NewProjectAudioProcessor::CaptureUxState::ReadyToSave)
        return;

    auto startDir = audioProcessor.getLastCaptureWav().getParentDirectory();
    if (! startDir.isDirectory())
        startDir = audioProcessor.getDefaultCaptureDirectory();

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    auto chooser = std::make_shared<juce::FileChooser> (
        "Save capture WAV for SpectraLayers",
        startDir.getChildFile ("capture_" + stamp + ".wav"),
        "*.wav");

    juce::Component::SafePointer<NewProjectAudioProcessorEditor> safe (this);

    chooser->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe, chooser] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;

            auto& proc = safe->audioProcessor;
            auto file = fc.getResult();
            if (file == juce::File{})
            {
                proc.setUiStatus ("Save cancelled - take kept. Save... to retry or Discard.");
                safe->updateCaptureBar();
                return;
            }

            juce::String err;
            if (! proc.finalizeNamedCapture (file, err))
                proc.setUiStatus ((err.isNotEmpty() ? err : juce::String ("Save failed."))
                                  + "  Take kept - Save... to retry.  [Copy Report]");
            safe->updateCaptureBar();
        });
}

void NewProjectAudioProcessorEditor::openInSpectraLayersPickWav()
{
    auto start = audioProcessor.getLastCaptureWav();
    if (start == juce::File{})
        start = audioProcessor.getDefaultCaptureDirectory();

    auto chooser = std::make_shared<juce::FileChooser> (
        "Choose WAV for SpectraLayers",
        start,
        "*.wav");

    juce::Component::SafePointer<NewProjectAudioProcessorEditor> safe (this);

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, chooser] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;

            auto& audioProcessor = safe->audioProcessor;
            auto file = fc.getResult();
            if (! file.existsAsFile())
            {
                audioProcessor.setUiStatus ("Open in SpectraLayers cancelled.");
                return;
            }

            if (isChouChouRunningAsStandaloneApp())
            {
                audioProcessor.beginSpectraLayersOpenWithWav (file);
                return;
            }

            juce::String err;
            const auto sidecar = juce::File (file.getFullPathName() + ".session.json");
            const bool ok = sidecar.existsAsFile()
                                ? ChouChouStandaloneLauncher::openWithSession (sidecar, err)
                                : ChouChouStandaloneLauncher::openWithWav (file, err);

            if (! ok)
                audioProcessor.setUiStatus (err.isNotEmpty() ? err : "Launch failed.  [Copy Report]");
            else
                audioProcessor.setUiStatus (
                    "Launched Standalone with " + file.getFileName()
                    + " -> SpectraLayers (Scan VST3 first if SL is missing).");
        });
}

void NewProjectAudioProcessorEditor::openStandaloneHost()
{
    if (isChouChouRunningAsStandaloneApp())
    {
        audioProcessor.setUiStatus ("Already running as Standalone.");
        return;
    }

    juce::String err;
    if (! ChouChouStandaloneLauncher::openEmpty (err))
        audioProcessor.setUiStatus (err);
    else
        audioProcessor.setUiStatus (
            "Opened empty Standalone - prefer Open in SL... to bind a WAV.");
}

void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kPanel);
    g.fillRoundedRectangle (captureBarArea.toFloat(), 8.0f);
    g.fillRoundedRectangle (slotsArea.toFloat(), 10.0f);
}

void NewProjectAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);

    auto top = bounds.removeFromTop (28);
    titleLabel.setBounds (top.removeFromLeft (180));
    scanButton.setBounds (top.removeFromRight (64).reduced (0, 1));
    top.removeFromRight (4);
    copyReportButton.setBounds (top.removeFromRight (92).reduced (0, 1));
    top.removeFromRight (4);
    openStandaloneButton.setBounds (top.removeFromRight (88).reduced (0, 1));
    top.removeFromRight (6);
    formatBox.setBounds (top.removeFromRight (130).reduced (0, 1));

    subtitleLabel.setBounds (bounds.removeFromTop (20));
    bounds.removeFromTop (6);

    captureBarArea = bounds.removeFromTop (78).expanded (2, 0);
    {
        auto bar = captureBarArea.reduced (10, 8);
        auto controls = bar.removeFromTop (34);
        recButton.setBounds (controls.removeFromLeft (64));
        controls.removeFromLeft (4);
        autoToggle.setBounds (controls.removeFromLeft (60));
        controls.removeFromLeft (4);
        saveTakeButton.setBounds (controls.removeFromLeft (72).reduced (0, 3));
        controls.removeFromLeft (4);
        discardTakeButton.setBounds (controls.removeFromLeft (72).reduced (0, 3));
        controls.removeFromLeft (10);
        openInSlButton.setBounds (controls.removeFromRight (110).reduced (0, 3));
        controls.removeFromRight (6);
        if (importTrackButton.isVisible())
        {
            importTrackButton.setBounds (controls.removeFromRight (104).reduced (0, 3));
            controls.removeFromRight (6);
        }
        captureMeter.setBounds (controls.removeFromLeft (140).reduced (0, 2));
        controls.removeFromLeft (10);
        captureInfoLabel.setBounds (controls);

        bar.removeFromTop (4);
        captureHintLabel.setBounds (bar);
    }

    bounds.removeFromTop (4);
    {
        auto probeRow = bounds.removeFromTop (24);
        probeLogButton.setBounds (probeRow.removeFromLeft (76).reduced (0, 1));
        probeRow.removeFromLeft (4);
        probeClearButton.setBounds (probeRow.removeFromLeft (52).reduced (0, 1));
        probeRow.removeFromLeft (4);
        probeMarkButton.setBounds (probeRow.removeFromLeft (52).reduced (0, 1));
        probeRow.removeFromLeft (8);
        probeLatencyToggle.setBounds (probeRow.removeFromLeft (104));
        probeTailToggle.setBounds (probeRow.removeFromLeft (70));
        probeClickToggle.setBounds (probeRow.removeFromLeft (100));
        probeRow.removeFromLeft (6);
        probeInfoLabel.setBounds (probeRow);
    }

    bounds.removeFromTop (4);
    scanHintLabel.setBounds (bounds.removeFromTop (18));
    bounds.removeFromTop (6);

    slotsArea = bounds.expanded (2, 0);

    const int rowH = bounds.getHeight() / NewProjectAudioProcessor::kNumSlots;
    for (auto& row : rows)
    {
        auto area = bounds.removeFromTop (rowH).reduced (6, 3);
        row->setBounds (area);
    }
}

//==============================================================================
void CaptureMeter::setLevels (float inputPeak, float outputPeak, NewProjectAudioProcessor::CaptureSource source)
{
    constexpr float decay = 0.7f;
    shownIn  = juce::jmax (inputPeak,  shownIn  * decay);
    shownOut = juce::jmax (outputPeak, shownOut * decay);
    lockedSource = source;
    repaint();
}

void CaptureMeter::paint (juce::Graphics& g)
{
    using S = NewProjectAudioProcessor::CaptureSource;
    constexpr float minDb = -60.0f;

    auto area = getLocalBounds().toFloat();
    const float rowH = area.getHeight() / 2.0f;

    auto drawBar = [&] (juce::Rectangle<float> r, const char* name, float peak, bool isSource)
    {
        auto labelArea = r.removeFromLeft (30.0f);
        g.setColour (isSource ? kAccent : kMuted);
        g.setFont (juce::FontOptions (10.5f, isSource ? juce::Font::bold : juce::Font::plain));
        g.drawText (name, labelArea, juce::Justification::centredLeft);

        auto track = r.reduced (0.0f, 2.5f);
        g.setColour (kDim);
        g.fillRoundedRectangle (track, 2.0f);

        const float db = peak > 0.0f ? juce::Decibels::gainToDecibels (peak, minDb) : minDb;
        const float frac = juce::jlimit (0.0f, 1.0f, (db - minDb) / -minDb);
        if (frac > 0.0f)
        {
            const auto fillColour = db > -1.0f ? juce::Colour (0xffe05050)
                                  : db > -12.0f ? juce::Colour (0xffe0c050)
                                                : kAccent;
            g.setColour (isSource ? fillColour : fillColour.withAlpha (0.55f));
            g.fillRoundedRectangle (track.withWidth (track.getWidth() * frac), 2.0f);
        }
    };

    drawBar (area.removeFromTop (rowH), "IN",  shownIn,  lockedSource == S::Input);
    drawBar (area,                      "OUT", shownOut, lockedSource == S::Output);
}
