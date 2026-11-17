#pragma once

#include "DrumRackProcessor.h"

class DrumRackEditor final : public juce::AudioProcessorEditor,
                             public juce::DragAndDropTarget,
                             public juce::FileDragAndDropTarget,
                             private juce::KeyListener,
                             private juce::Timer
{
public:
    explicit DrumRackEditor (DrumRackProcessor&);
    ~DrumRackEditor() override = default;

    std::function<bool (const juce::KeyPress&)> onTransportKeyPress;
    std::function<void (int, const juce::String&)> onPadEffectRequested;
    std::function<void (int, int)> onPadEffectEditorRequested;
    std::function<void (int)> onPadEffectsChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshPadEffects (bool selectLast = false);
    bool keyPressed (const juce::KeyPress&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    void chooseSample();
    void selectPad (int pad);
    int padAt (juce::Point<int> position) const;
    void updateDropTarget (juce::Point<int> position);
    bool loadSampleIntoPad (int pad, const juce::File& file);
    juce::String padKeyboardKey (int pad) const;
    bool handleKeyPress (const juce::KeyPress&, juce::Component* source);
    bool keyPressed (const juce::KeyPress&, juce::Component* source) override;
    void addKeyListenerRecursively (juce::Component&);
    void timerCallback() override;
    void clearSelectedPad();
    void refreshPadControls();
    void refreshPadLabels();
    void refreshHistoryButtons();
    void refreshPadEffectButtons();
    void applyPadSettings();

    DrumRackProcessor& processor;
    juce::Label title;
    juce::Label selectedPadLabel;
    juce::TextButton clearButton { "Limpar rack..." };
    juce::TextButton undoButton { "Desfazer" }, redoButton { "Refazer" };
    juce::TextButton loadButton { "Carregar sample" }, clearSelectedButton { "Remover sample" };
    juce::TextButton editEffectButton { "Abrir" }, removeEffectButton { "Remover" },
                     bypassEffectButton { "Bypass" }, moveEffectUpButton { "Subir" },
                     moveEffectDownButton { "Descer" };
    juce::Label modeLabel { {}, "Modo de reproducao" };
    juce::Label effectsLabel { {}, "Efeitos do pad" };
    juce::Label gainLabel { {}, "Volume" }, pitchLabel { {}, "Transpose (st)" };
    juce::Label startLabel { {}, "Inicio (%)" }, endLabel { {}, "Fim (%)" };
    juce::Slider gainSlider, pitchSlider, startSlider, endSlider;
    juce::ToggleButton reverseButton { "Reverse" }, loopButton { "Loop" };
    juce::ComboBox modeBox;
    juce::ComboBox effectBox;
    std::array<std::unique_ptr<juce::TextButton>, DrumRackProcessor::visiblePadCount> pads;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::array<double, DrumRackProcessor::numPads> padFlashUntil {};
    std::array<bool, DrumRackProcessor::numPads> padIsFlashing {};
    int selectedPad = 0;
    int selectedEffect = -1;
    int dropTargetPad = -1;
    bool updatingEffectSelection = false;
};
