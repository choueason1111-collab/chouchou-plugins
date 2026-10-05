/*
  ==============================================================================

    chouchouPlugplugin — rack editor

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginScanSupport.h"

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
    styleBtn (clearButton, kDim);
    bypassButton.setColour (juce::ToggleButton::textColourId, kMuted);

    addAndMakeVisible (loadButton);
    addAndMakeVisible (openButton);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (clearButton);

    loadButton.onClick = [this] { openPluginChooser(); };
    openButton.onClick = [this] { openEditorWindow(); };
    clearButton.onClick = [this]
    {
        editorWindow.reset();
        chooserWindow.reset();
        editorKeepAlive.reset();
        processor.clearSlot (index);
    };
    bypassButton.onClick = [this]
    {
        processor.setSlotBypassed (index, bypassButton.getToggleState());
    };

    refresh();
}

void SlotRowComponent::deferDestroyWindow (std::unique_ptr<ClosableWindow>& window)
{
    // Never delete a DocumentWindow from inside closeButtonPressed.
    auto doomed = std::move (window);
    juce::MessageManager::callAsync ([doomed = std::move (doomed)]() mutable
    {
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

    openButton.setEnabled (loaded);
    clearButton.setEnabled (loaded);
    bypassButton.setEnabled (loaded);
    bypassButton.setToggleState (processor.isSlotBypassed (index), juce::dontSendNotification);

    if (! loaded)
    {
        editorWindow.reset();
        editorKeepAlive.reset();
    }
}

void SlotRowComponent::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    indexLabel.setBounds (r.removeFromLeft (36));
    r.removeFromLeft (6);

    clearButton.setBounds (r.removeFromRight (64).reduced (2));
    bypassButton.setBounds (r.removeFromRight (78).reduced (2));
    openButton.setBounds (r.removeFromRight (78).reduced (2));
    loadButton.setBounds (r.removeFromRight (64).reduced (2));
    r.removeFromRight (6);
    nameLabel.setBounds (r);
}

void SlotRowComponent::openPluginChooser()
{
    // Custom filtered list — no PluginListComponent Options / modal dialogs
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
            hint.setText ("Type to filter · double-click or Load · window X to close  ·  Insert Basic: prefer AU",
                          juce::dontSendNotification);
            addAndMakeVisible (hint);

            rebuildFiltered();
            setSize (720, 520);
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
            const auto meta = d.manufacturerName + "  ·  " + d.pluginFormatName;
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

    chooserWindow = std::make_unique<ClosableWindow> (
        "Load plugin into slot " + juce::String (index + 1), kPanel);
    chooserWindow->onClose = [this] { deferDestroyWindow (chooserWindow); };

    auto* content = new Content (processor);
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
        content->hint.setText ("Starting load — window will close; watch status on main UI.",
                               juce::dontSendNotification);

        editorWindow.reset();
        editorKeepAlive.reset();

        // Close chooser first so Insert Basic stays responsive while AU loads async.
        deferDestroyWindow (chooserWindow);
        juce::MessageManager::callAsync ([safeProc = juce::Component::SafePointer<SlotRowComponent> (this),
                                          desc, slot = index]
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

    chooserWindow->setContentOwned (content, true);
    chooserWindow->showAround (this, content->getWidth(), content->getHeight() + 28);
    content->search.grabKeyboardFocus();
}

void SlotRowComponent::openEditorWindow()
{
    editorKeepAlive = processor.getSlotInstanceShared (index);
    if (editorKeepAlive == nullptr)
        return;

    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (
        editorKeepAlive->hasEditor() ? editorKeepAlive->createEditorAndMakeActive()
                                     : nullptr);

    if (editor == nullptr)
    {
        editorKeepAlive.reset();
        processor.setUiStatus ("Slot " + juce::String (index + 1) + ": plugin has no UI.");
        return;
    }

    const auto title = "Slot " + juce::String (index + 1) + " — " + processor.getSlotName (index);
    editorWindow = std::make_unique<ClosableWindow> (title, kPanel);
    editorWindow->onClose = [this]
    {
        editorKeepAlive.reset();
        deferDestroyWindow (editorWindow);
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

    titleLabel.setText ("Scanning plugins — please wait", juce::dontSendNotification);
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
                                   ? "Scanning in-process — install Scanner helper for crash safety"
                                   : "Scanner helper missing — refusing in-process scan (host crash protection)"),
                        juce::dontSendNotification);

    if (! oop)
    {
        if (isChouChouRunningAsStandaloneApp())
            currentLabel.setText ("Tip: if the Standalone crashes, reopen and Scan again.\n"
                                  "The crashing plugin is auto-skipped next time.",
                                  juce::dontSendNotification);
        else
            currentLabel.setText ("Install:\n~/Library/Application Support/chouchou/"
                                  "chouchouPlugplugin/chouchouPlugplugin Scanner.app",
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

    countLabel.setText (juce::String (scannedCount) + " files checked  ·  "
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

    // Never use AlertWindow here — in Insert Basic / AU hosts the OK dialog
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
    titleLabel.setText ("chouchouPlugplugin", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f).withStyle ("Bold"));
    titleLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("10-slot serial rack  ·  same stereo I/O end-to-end  ·  Insert: layout auto-resync",
                           juce::dontSendNotification);
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

    scanHintLabel.setColour (juce::Label::textColourId, kMuted);
    scanHintLabel.setFont (juce::FontOptions (11.0f));
    scanHintLabel.setJustificationType (juce::Justification::centredRight);
    scanHintLabel.setText ("Pick format, then Scan.  No OK dialogs — use window X / Close.",
                           juce::dontSendNotification);
    addAndMakeVisible (scanHintLabel);

    for (int i = 0; i < NewProjectAudioProcessor::kNumSlots; ++i)
    {
        rows[(size_t) i] = std::make_unique<SlotRowComponent> (audioProcessor, i);
        addAndMakeVisible (*rows[(size_t) i]);
    }

    audioProcessor.addChangeListener (this);
    refreshAll();

    setSize (820, 640);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
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

    const auto status = audioProcessor.getUiStatus();
    if (status.isNotEmpty())
        scanHintLabel.setText (status, juce::dontSendNotification);
}

void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    auto panel = getLocalBounds().reduced (14).withTrimmedTop (88);
    g.setColour (kPanel);
    g.fillRoundedRectangle (panel.toFloat(), 10.0f);
}

void NewProjectAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);

    auto top = bounds.removeFromTop (28);
    titleLabel.setBounds (top.removeFromLeft (260));
    scanButton.setBounds (top.removeFromRight (72).reduced (0, 1));
    top.removeFromRight (8);
    formatBox.setBounds (top.removeFromRight (150).reduced (0, 1));

    subtitleLabel.setBounds (bounds.removeFromTop (20));
    scanHintLabel.setBounds (bounds.removeFromTop (18));
    bounds.removeFromTop (10);

    const int rowH = bounds.getHeight() / NewProjectAudioProcessor::kNumSlots;
    for (auto& row : rows)
    {
        auto area = bounds.removeFromTop (rowH).reduced (6, 3);
        row->setBounds (area);
    }
}
