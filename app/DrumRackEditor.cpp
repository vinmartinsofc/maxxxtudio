#include "DrumRackEditor.h"
#include "AppModalDialog.h"
#include "Theme.h"

namespace
{
bool isAudioSampleFile (const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".wave" || extension == ".aif"
        || extension == ".aiff" || extension == ".flac" || extension == ".ogg";
}

juce::File audioFileFromDrag (const juce::var& description)
{
    const auto value = description.toString();
    return value.startsWith ("audio:") ? juce::File (value.substring (6)) : juce::File();
}

juce::String pluginIdentifierFromDrag (const juce::var& description)
{
    const auto value = description.toString();
    return value.startsWith ("plugin:") ? value.substring (7) : juce::String();
}

bool isSharpPad (int pad)
{
    return pad == 1 || pad == 3 || pad == 6 || pad == 8 || pad == 10 || pad == 13;
}
}

DrumRackEditor::DrumRackEditor (DrumRackProcessor& owner)
    : juce::AudioProcessorEditor (owner), processor (owner)
{
    title.setText ("Drum Master - 15 MIDI Keys", juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centredLeft);
    title.setColour (juce::Label::textColourId, theme::text());
    selectedPadLabel.setColour (juce::Label::textColourId, theme::text());
    selectedPadLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));

    addAndMakeVisible (title);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);
    addAndMakeVisible (selectedPadLabel);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearSelectedButton);
    addAndMakeVisible (modeLabel);
    addAndMakeVisible (effectsLabel);
    addAndMakeVisible (effectBox);
    addAndMakeVisible (editEffectButton);
    addAndMakeVisible (removeEffectButton);
    addAndMakeVisible (bypassEffectButton);
    addAndMakeVisible (moveEffectUpButton);
    addAndMakeVisible (moveEffectDownButton);
    addAndMakeVisible (gainLabel);
    addAndMakeVisible (pitchLabel);
    addAndMakeVisible (startLabel);
    addAndMakeVisible (endLabel);
    addAndMakeVisible (gainSlider);
    addAndMakeVisible (pitchSlider);
    addAndMakeVisible (startSlider);
    addAndMakeVisible (endSlider);
    addAndMakeVisible (reverseButton);
    addAndMakeVisible (loopButton);
    addAndMakeVisible (modeBox);

    modeBox.addItem ("One-Shot", 1);
    modeBox.addItem ("Classic", 2);
    modeBox.setTooltip ("One-Shot ignores note-off; Classic can sustain loops while the note is held.");
    gainSlider.setRange (0.0, 4.0, 0.01);
    pitchSlider.setRange (-48.0, 48.0, 1.0);
    startSlider.setRange (0.0, 1000.0, 1.0);
    endSlider.setRange (0.0, 1000.0, 1.0);
    gainSlider.setDoubleClickReturnValue (true, 1.0);
    pitchSlider.setDoubleClickReturnValue (true, 0.0);
    startSlider.setDoubleClickReturnValue (true, 0.0);
    endSlider.setDoubleClickReturnValue (true, 1000.0);
    for (auto* slider : { &gainSlider, &pitchSlider, &startSlider, &endSlider })
    {
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, true, 70, 20);
    }
    bypassEffectButton.setClickingTogglesState (true);
    moveEffectUpButton.setTooltip ("Move the selected effect up one position in the chain.");
    moveEffectDownButton.setTooltip ("Move the selected effect down one position in the chain.");

    clearButton.onClick = [this]
    {
        juce::Component::SafePointer<DrumRackEditor> safeThis (this);
        AppModalDialog::show (
            *this, "Clear all pads?",
            "This removes the samples from all 15 pads. Use Undo to restore them.",
            AppModalDialog::Icon::Warning,
            { { "Clear All", 1, AppModalDialog::ButtonStyle::Primary },
              { "Cancel", 0, AppModalDialog::ButtonStyle::Tertiary } },
            [safeThis] (int result)
            {
                if (safeThis == nullptr || result == 0)
                    return;
                safeThis->processor.clearPads();
                safeThis->refreshPadLabels();
                safeThis->refreshPadControls();
                safeThis->refreshHistoryButtons();
            });
    };
    undoButton.onClick = [this]
    {
        if (processor.undo())
        {
            refreshPadLabels();
            refreshPadControls();
            refreshHistoryButtons();
        }
    };
    redoButton.onClick = [this]
    {
        if (processor.redo())
        {
            refreshPadLabels();
            refreshPadControls();
            refreshHistoryButtons();
        }
    };
    loadButton.onClick = [this] { chooseSample(); };
    clearSelectedButton.onClick = [this] { clearSelectedPad(); };
    effectBox.onChange = [this]
    {
        if (updatingEffectSelection)
            return;
        selectedEffect = effectBox.getSelectedId() - 1;
        refreshPadEffectButtons();
    };
    editEffectButton.onClick = [this]
    {
        if (selectedEffect >= 0 && onPadEffectEditorRequested)
            onPadEffectEditorRequested (selectedPad, selectedEffect);
    };
    removeEffectButton.onClick = [this]
    {
        if (selectedEffect >= 0 && processor.removePadEffect (selectedPad, selectedEffect))
        {
            selectedEffect = -1;
            refreshPadEffects();
            if (onPadEffectsChanged)
                onPadEffectsChanged (selectedPad);
        }
    };
    bypassEffectButton.onClick = [this]
    {
        if (selectedEffect >= 0)
        {
            processor.setPadEffectBypassed (selectedPad, selectedEffect,
                                             bypassEffectButton.getToggleState());
            refreshPadEffects();
        }
    };
    moveEffectUpButton.onClick = [this]
    {
        if (selectedEffect > 0 && processor.movePadEffect (selectedPad, selectedEffect, -1))
        {
            --selectedEffect;
            refreshPadEffects();
            if (onPadEffectsChanged)
                onPadEffectsChanged (selectedPad);
        }
    };
    moveEffectDownButton.onClick = [this]
    {
        const auto effects = processor.padEffects (selectedPad);
        if (selectedEffect >= 0 && selectedEffect + 1 < static_cast<int> (effects.size())
            && processor.movePadEffect (selectedPad, selectedEffect, 1))
        {
            ++selectedEffect;
            refreshPadEffects();
            if (onPadEffectsChanged)
                onPadEffectsChanged (selectedPad);
        }
    };

    for (int index = 0; index < DrumRackProcessor::visiblePadCount; ++index)
    {
        auto button = std::make_unique<juce::TextButton>();
        button->onClick = [this, index] { selectPad (index); };
        button->setMouseClickGrabsKeyboardFocus (false);
        button->setColour (juce::TextButton::buttonColourId, theme::control());
        button->setColour (juce::TextButton::textColourOffId, theme::text());
        addAndMakeVisible (*button);
        pads[static_cast<std::size_t> (index)] = std::move (button);
    }

    gainSlider.onValueChange = [this] { applyPadSettings(); };
    pitchSlider.onValueChange = [this] { applyPadSettings(); };
    startSlider.onValueChange = [this] { applyPadSettings(); };
    endSlider.onValueChange = [this] { applyPadSettings(); };
    reverseButton.onClick = [this] { applyPadSettings(); };
    loopButton.onClick = [this] { applyPadSettings(); };
    modeBox.onChange = [this] { applyPadSettings(); };

    refreshPadLabels();
    refreshPadControls();
    refreshHistoryButtons();
    setSize (900, 560);
    addKeyListenerRecursively (*this);
    startTimerHz (30);
}

void DrumRackEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (theme::background());
}

void DrumRackEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (34);
    title.setBounds (header.removeFromLeft (260));
    clearButton.setBounds (header.removeFromRight (112));
    header.removeFromRight (6);
    redoButton.setBounds (header.removeFromRight (76));
    header.removeFromRight (4);
    undoButton.setBounds (header.removeFromRight (82));
    area.removeFromTop (8);

    auto controls = area.removeFromRight (320);
    area.removeFromRight (12);
    auto grid = area.reduced (2);
    constexpr std::array<int, 9> naturalPads { 0, 2, 4, 5, 7, 9, 11, 12, 14 };
    constexpr std::array<int, 6> sharpPads { 1, 3, 6, 8, 10, 13 };
    const int keyWidth = grid.getWidth() / static_cast<int> (naturalPads.size());
    const int rowHeight = grid.getHeight() / 2;
    for (std::size_t column = 0; column < naturalPads.size(); ++column)
    {
        const auto pad = static_cast<std::size_t> (naturalPads[column]);
        pads[pad]->setBounds (grid.getX() + static_cast<int> (column) * keyWidth + 2,
                              grid.getY() + rowHeight + 2, keyWidth - 4, rowHeight - 4);
    }
    for (std::size_t index = 0; index < sharpPads.size(); ++index)
    {
        const auto pad = static_cast<std::size_t> (sharpPads[index]);
        const auto semitone = DrumRackProcessor::computerMidiKeyboardMap[pad].second;
        const int whiteKeyColumn = semitone == 1 ? 0
                                 : semitone == 3 ? 1
                                 : semitone == 6 ? 3
                                 : semitone == 8 ? 4
                                 : semitone == 10 ? 5 : 7;
        const int sharpWidth = keyWidth * 2 / 3;
        const int x = grid.getX() + whiteKeyColumn * keyWidth + (keyWidth - sharpWidth) / 2;
        pads[pad]->setColour (juce::TextButton::buttonColourId, theme::panel());
        pads[pad]->setColour (juce::TextButton::textColourOffId, theme::textDim());
        pads[pad]->setBounds (x, grid.getY() + 2, sharpWidth, rowHeight - 4);
    }

    selectedPadLabel.setBounds (controls.removeFromTop (30));
    auto sampleButtons = controls.removeFromTop (30);
    loadButton.setBounds (sampleButtons.removeFromLeft (controls.getWidth() / 2 - 3));
    sampleButtons.removeFromLeft (6);
    clearSelectedButton.setBounds (sampleButtons);
    controls.removeFromTop (4);
    modeLabel.setBounds (controls.removeFromTop (16));
    modeBox.setBounds (controls.removeFromTop (28));
    controls.removeFromTop (5);
    effectsLabel.setBounds (controls.removeFromTop (16));
    effectBox.setBounds (controls.removeFromTop (28));
    auto effectButtons = controls.removeFromTop (28);
    editEffectButton.setBounds (effectButtons.removeFromLeft (52));
    effectButtons.removeFromLeft (4);
    removeEffectButton.setBounds (effectButtons.removeFromLeft (64));
    effectButtons.removeFromLeft (4);
    bypassEffectButton.setBounds (effectButtons.removeFromLeft (64));
    effectButtons.removeFromLeft (4);
    moveEffectUpButton.setBounds (effectButtons.removeFromLeft (54));
    effectButtons.removeFromLeft (4);
    moveEffectDownButton.setBounds (effectButtons.removeFromLeft (62));
    controls.removeFromTop (5);

    const auto setKnobCell = [] (juce::Rectangle<int> cell,
                                 juce::Label& label, ThemeKnobSlider& slider)
    {
        label.setBounds (cell.removeFromTop (18));
        slider.setBounds (cell.reduced (4, 0));
    };
    auto firstKnobRow = controls.removeFromTop (96);
    setKnobCell (firstKnobRow.removeFromLeft (firstKnobRow.getWidth() / 2 - 2),
                 gainLabel, gainSlider);
    firstKnobRow.removeFromLeft (4);
    setKnobCell (firstKnobRow, pitchLabel, pitchSlider);
    controls.removeFromTop (4);
    auto secondKnobRow = controls.removeFromTop (96);
    setKnobCell (secondKnobRow.removeFromLeft (secondKnobRow.getWidth() / 2 - 2),
                 startLabel, startSlider);
    secondKnobRow.removeFromLeft (4);
    setKnobCell (secondKnobRow, endLabel, endSlider);
    auto toggles = controls.removeFromTop (28);
    reverseButton.setBounds (toggles.removeFromLeft (112));
    loopButton.setBounds (toggles.removeFromLeft (100));
}

bool DrumRackEditor::isInterestedInDragSource (const SourceDetails& details)
{
    const auto file = audioFileFromDrag (details.description);
    return (file.existsAsFile() && isAudioSampleFile (file))
        || pluginIdentifierFromDrag (details.description).isNotEmpty();
}

void DrumRackEditor::itemDragEnter (const SourceDetails& details)
{
    updateDropTarget (details.localPosition);
}

void DrumRackEditor::itemDragMove (const SourceDetails& details)
{
    updateDropTarget (details.localPosition);
}

void DrumRackEditor::itemDragExit (const SourceDetails&)
{
    dropTargetPad = -1;
    refreshPadLabels();
}

void DrumRackEditor::itemDropped (const SourceDetails& details)
{
    const int pad = padAt (details.localPosition);
    const auto file = audioFileFromDrag (details.description);
    const auto pluginIdentifier = pluginIdentifierFromDrag (details.description);
    dropTargetPad = -1;
    refreshPadLabels();
    if (pad >= 0)
    {
        if (pluginIdentifier.isNotEmpty() && onPadEffectRequested)
            onPadEffectRequested (pad, pluginIdentifier);
        else if (file.existsAsFile())
            loadSampleIntoPad (pad, file);
    }
}

bool DrumRackEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return ! files.isEmpty() && isAudioSampleFile (juce::File (files[0]));
}

void DrumRackEditor::fileDragEnter (const juce::StringArray&, int x, int y)
{
    updateDropTarget ({ x, y });
}

void DrumRackEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    updateDropTarget ({ x, y });
}

void DrumRackEditor::fileDragExit (const juce::StringArray&)
{
    dropTargetPad = -1;
    refreshPadLabels();
}

void DrumRackEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int pad = padAt ({ x, y });
    dropTargetPad = -1;
    refreshPadLabels();
    if (pad >= 0 && ! files.isEmpty())
        loadSampleIntoPad (pad, juce::File (files[0]));
}

void DrumRackEditor::chooseSample()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load Sample for MIDI "
            + juce::String (DrumRackProcessor::firstMidiNote + selectedPad), juce::File(),
        "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    juce::Component::SafePointer<DrumRackEditor> safeThis (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
        [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;
            const auto file = chooser.getResult();
            if (! file.existsAsFile())
                return;
            juce::String error;
            if (! safeThis->processor.loadPadFromFile (safeThis->selectedPad, file, error))
            {
                AppModalDialog::show (
                    *safeThis.getComponent(), "Drum Master", error, AppModalDialog::Icon::Warning,
                    { { "OK", 1, AppModalDialog::ButtonStyle::Primary } });
                return;
            }
            safeThis->refreshPadLabels();
            safeThis->refreshPadControls();
            safeThis->refreshHistoryButtons();
        });
}

void DrumRackEditor::selectPad (int pad)
{
    if (pad < 0 || pad >= DrumRackProcessor::numPads)
        return;
    selectedPad = pad;
    selectedEffect = -1;
    refreshPadLabels();
    refreshPadControls();
}

bool DrumRackEditor::keyPressed (const juce::KeyPress& key)
{
    return handleKeyPress (key, this);
}

bool DrumRackEditor::keyPressed (const juce::KeyPress& key, juce::Component* source)
{
    return handleKeyPress (key, source);
}

bool DrumRackEditor::handleKeyPress (const juce::KeyPress& key, juce::Component* source)
{
    if (key.isKeyCode (juce::KeyPress::spaceKey) && onTransportKeyPress)
        return onTransportKeyPress (key);

    if (dynamic_cast<juce::TextEditor*> (source) != nullptr)
        return false;

    const bool command = key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown();
    const auto keyCode = juce::CharacterFunctions::toUpperCase (
        static_cast<juce::juce_wchar> (key.getKeyCode()));
    if (command && ! key.getModifiers().isAltDown()
        && (keyCode == 'Z' || keyCode == 'Y'))
    {
        const bool changed = keyCode == 'Y' || key.getModifiers().isShiftDown()
            ? processor.redo() : processor.undo();
        if (changed)
        {
            refreshPadLabels();
            refreshPadControls();
            refreshHistoryButtons();
        }
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::deleteKey)
        && ! key.getModifiers().isAnyModifierKeyDown())
    {
        clearSelectedPad();
        return true;
    }
    return false;
}

void DrumRackEditor::addKeyListenerRecursively (juce::Component& component)
{
    component.addKeyListener (this);
    for (int index = 0; index < component.getNumChildComponents(); ++index)
        if (auto* child = component.getChildComponent (index))
            addKeyListenerRecursively (*child);
}

void DrumRackEditor::timerCallback()
{
    processor.collectRetiredPadEffects();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto triggeredPads = processor.consumeTriggeredPads();
    bool changed = false;
    for (int pad = 0; pad < DrumRackProcessor::numPads; ++pad)
    {
        const auto mask = std::uint32_t { 1 } << pad;
        if ((triggeredPads & mask) != 0)
            padFlashUntil[static_cast<std::size_t> (pad)] = now + 140.0;
        const bool flashing = padFlashUntil[static_cast<std::size_t> (pad)] > now;
        if (padIsFlashing[static_cast<std::size_t> (pad)] != flashing)
        {
            padIsFlashing[static_cast<std::size_t> (pad)] = flashing;
            changed = true;
        }
    }
    if (changed)
        refreshPadLabels();
    refreshHistoryButtons();
}

void DrumRackEditor::clearSelectedPad()
{
    processor.clearPad (selectedPad);
    refreshPadLabels();
    refreshPadControls();
    refreshHistoryButtons();
}

int DrumRackEditor::padAt (juce::Point<int> position) const
{
    for (int index = 0; index < DrumRackProcessor::visiblePadCount; ++index)
        if (pads[static_cast<std::size_t> (index)]->getBounds().contains (position))
            return index;
    return -1;
}

void DrumRackEditor::updateDropTarget (juce::Point<int> position)
{
    const int pad = padAt (position);
    if (dropTargetPad == pad)
        return;
    dropTargetPad = pad;
    refreshPadLabels();
}

bool DrumRackEditor::loadSampleIntoPad (int pad, const juce::File& file)
{
    juce::String error;
    if (! processor.loadPadFromFile (pad, file, error))
    {
        AppModalDialog::show (
            *this, "Drum Master", error, AppModalDialog::Icon::Warning,
            { { "OK", 1, AppModalDialog::ButtonStyle::Primary } });
        refreshPadLabels();
        return false;
    }

    refreshPadLabels();
    refreshHistoryButtons();
    if (pad == selectedPad)
        refreshPadControls();
    return true;
}

juce::String DrumRackEditor::padKeyboardKey (int pad) const
{
    if (pad < 0 || pad >= DrumRackProcessor::numPads)
        return {};
    return juce::String::charToString (static_cast<juce::juce_wchar> (
        DrumRackProcessor::computerMidiKeyboardMap[static_cast<std::size_t> (pad)].first));
}

void DrumRackEditor::refreshPadLabels()
{
    for (int index = 0; index < DrumRackProcessor::visiblePadCount; ++index)
    {
        const int pad = index;
        auto* button = pads[static_cast<std::size_t> (pad)].get();
        const auto sampleName = processor.padSampleLength (pad) > 0
            ? processor.padName (pad) : juce::String ("Empty");
        button->setButtonText (padKeyboardKey (pad) + "  MIDI "
                               + juce::String (DrumRackProcessor::firstMidiNote + pad)
                               + "\n" + sampleName);
        const bool sharp = isSharpPad (pad);
        const bool flashing = padIsFlashing[static_cast<std::size_t> (pad)];
        button->setColour (juce::TextButton::buttonColourId,
                           pad == dropTargetPad ? theme::accentHover()
                           : flashing || pad == selectedPad ? theme::accent()
                           : sharp ? theme::panel() : theme::control());
        button->setColour (juce::TextButton::textColourOffId, theme::text());
    }
}

void DrumRackEditor::refreshPadControls()
{
    const auto settings = processor.padSettings (selectedPad);
    const int sampleLength = processor.padSampleLength (selectedPad);
    selectedPadLabel.setText ("Selected pad: " + padKeyboardKey (selectedPad)
                              + " - MIDI " + juce::String (DrumRackProcessor::firstMidiNote + selectedPad)
                              + " - " + (sampleLength > 0 ? processor.padName (selectedPad)
                                                           : juce::String ("Empty")),
                              juce::dontSendNotification);
    gainSlider.setValue (settings.gain, juce::dontSendNotification);
    pitchSlider.setValue (settings.pitchSemitones, juce::dontSendNotification);
    startSlider.setValue (sampleLength > 0
        ? 1000.0 * settings.startSample / sampleLength : 0.0, juce::dontSendNotification);
    endSlider.setValue (sampleLength > 0
        ? 1000.0 * (settings.endSample > 0 ? settings.endSample : sampleLength) / sampleLength
        : 1000.0, juce::dontSendNotification);
    reverseButton.setToggleState (settings.reverse, juce::dontSendNotification);
    loopButton.setToggleState (settings.loop, juce::dontSendNotification);
    loopButton.setEnabled (settings.mode == DrumRackProcessor::PlaybackMode::Classic);
    modeBox.setSelectedId (settings.mode == DrumRackProcessor::PlaybackMode::Classic ? 2 : 1,
                           juce::dontSendNotification);
    clearSelectedButton.setEnabled (sampleLength > 0);
    refreshPadEffects (true);
}

void DrumRackEditor::refreshPadEffects (bool selectLast)
{
    const auto effects = processor.padEffects (selectedPad);
    const int previousSelection = selectedEffect;
    updatingEffectSelection = true;
    effectBox.clear (juce::dontSendNotification);
    for (int index = 0; index < static_cast<int> (effects.size()); ++index)
        effectBox.addItem (juce::String (index + 1) + ": "
                               + effects[static_cast<std::size_t> (index)].name
                               + (effects[static_cast<std::size_t> (index)].bypassed
                                      ? " (bypass)" : ""),
                           index + 1);
    selectedEffect = effects.empty() ? -1
        : selectLast || previousSelection < 0 || previousSelection >= static_cast<int> (effects.size())
            ? static_cast<int> (effects.size()) - 1 : previousSelection;
    effectBox.setSelectedId (selectedEffect + 1, juce::dontSendNotification);
    updatingEffectSelection = false;
    refreshPadEffectButtons();
}

void DrumRackEditor::refreshPadEffectButtons()
{
    const auto effects = processor.padEffects (selectedPad);
    const bool validSelection = selectedEffect >= 0
        && selectedEffect < static_cast<int> (effects.size());
    editEffectButton.setEnabled (validSelection);
    removeEffectButton.setEnabled (validSelection);
    bypassEffectButton.setEnabled (validSelection);
    moveEffectUpButton.setEnabled (validSelection && selectedEffect > 0);
    moveEffectDownButton.setEnabled (validSelection
        && selectedEffect + 1 < static_cast<int> (effects.size()));
    if (validSelection)
    {
        const bool bypassed = effects[static_cast<std::size_t> (selectedEffect)].bypassed;
        bypassEffectButton.setToggleState (bypassed, juce::dontSendNotification);
        bypassEffectButton.setButtonText (bypassed ? "Enable" : "Bypass");
    }
}

void DrumRackEditor::refreshHistoryButtons()
{
    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());
}

void DrumRackEditor::applyPadSettings()
{
    auto settings = processor.padSettings (selectedPad);
    settings.gain = static_cast<float> (gainSlider.getValue());
    settings.pitchSemitones = static_cast<int> (std::lround (pitchSlider.getValue()));
    settings.reverse = reverseButton.getToggleState();
    settings.loop = loopButton.getToggleState();
    settings.mode = modeBox.getSelectedId() == 2 ? DrumRackProcessor::PlaybackMode::Classic
                                                  : DrumRackProcessor::PlaybackMode::OneShot;

    const int sampleLength = processor.padSampleLength (selectedPad);
    if (sampleLength > 0)
    {
        settings.startSample = std::clamp (static_cast<int> (std::lround (
            startSlider.getValue() * sampleLength / 1000.0)), 0, sampleLength - 1);
        settings.endSample = std::clamp (static_cast<int> (std::lround (
            endSlider.getValue() * sampleLength / 1000.0)), settings.startSample + 1, sampleLength);
    }
    processor.setPadSettings (selectedPad, settings);
    loopButton.setEnabled (settings.mode == DrumRackProcessor::PlaybackMode::Classic);
}
