#pragma once

#include "PluginProcessor.h"
#include "ui/ChordLane.h"
#include "ui/EdgeBar.h"
#include "ui/GridComponent.h"
#include "ui/LaneComponent.h"
#include "ui/PartsBar.h"
#include "ui/TabBar.h"
#include "ui/Theme.h"
#include "ui/VersionsStrip.h"
#include "ui/Widgets.h"

#include <array>
#include <vector>

// The plugin window, laid out like web/index.html: the dice row, the key and knobs row, the two
// tabs, the Advanced side panel, the arrangement, the chord lane with its generators and the
// selected chord, the piano roll with its range edges, the footer with Play (standalone only),
// the synth, MIDI out, Import / Export and a handle to drag the arrangement into a DAW.
class MidinjaEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit MidinjaEditor (MidinjaProcessor&);
    ~MidinjaEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void edited();
    void changed();
    void preview();
    void render();
    void importFile (const juce::File&);
    void exportMidi();
    juce::File writeTempMidi();
    void refreshMidiPorts();
    juce::String colorLabel() const;
    juce::String colorWords() const;
    void setupKnob (ui::Knob& k, std::function<double()> get, std::function<void (double)> set,
                    std::function<juce::String (double)> fmt, double def, std::function<void()> onChange);
    void applyTyped();

    MidinjaProcessor& proc;
    midinja::Session& sess;
    theme::Look look;
    int shownVersion = -1;
    int portTick = 0;
    bool dragOver = false;
    bool dragging = false;

    // Row A
    ui::DiceButton dice;
    juce::TextButton rhythmB, notesB, mutateB;
    ui::IconButton rollAll { ui::IconButton::Dice, "All", false };
    VersionsStrip versions;
    ui::IconButton undoB { ui::IconButton::Undo }, redoB { ui::IconButton::Redo };

    // Row B
    juce::ComboBox rootBox, scaleBox, rateBox;
    juce::TextButton ocD { juce::String (juce::CharPointer_UTF8 ("\xE2\x88\x92")) }, ocU { "+" };
    int rootOctShown = -99;
    ui::Knob density { "Density", 1, 16, 1 }, swingK { "Swing", 0, 1, 0.01 }, similarity { "Similarity", 0, 1, 0.05 };
    ui::Knob color { "Color", 0, 1, 0.05, true }, surprise { "Surprise", 0, 1, 0.05, true };
    ui::Switch advSw { "Advanced" };

    TabBar tabBar;

    // Side panel (Advanced)
    ui::RangeSlider lenR { 1, midinja::kMaxLength }, velR { 1, 127 }, repR { 1, midinja::kMaxRepeats };
    ui::Pill repPill, rootPill;
    ui::NumBox seedBox { 1.0, 4294967295.0, 1.0 };

    // Grid area
    PartsBar parts;
    juce::TextButton unlockBarB { "Unlock bar" };
    ChordLane lane;
    juce::TextButton progB, applyB { "Apply" };
    std::array<juce::TextButton, midinja::kNumMoods> moodBs;
    std::array<juce::TextButton, midinja::kNumStyles> styleBs;
    juce::TextEditor typedEd;
    juce::ComboBox degBox, typeBox, rhythmBox;
    std::array<juce::TextButton, 4> invBs;
    juce::TextButton rhythmAllB { "Rhythm to all" }, lockB { "Lock" };
    EdgeBar edgeTop, edgeBot;
    GridComponent grid;
    LaneComponent velLane { LaneComponent::Kind::Velocity }, lenLane { LaneComponent::Kind::Length },
                  repLane { LaneComponent::Kind::Repeats };

    // Footer
    ui::PlayButton play;
    ui::NumBox bpmBox { 40, 240, 1 };
    ui::Switch synthSw { "Synth" };
    juce::TextButton waveBtn { "Saw" };
    ui::Knob cutoffK { "Cutoff", 60, 12000, 1, false, 24 }, resoK { "Reso", 0, 100, 1, false, 24 },
             decayK { "Decay", 30, 1500, 1, false, 24 }, volK { "Volume", 0, 100, 1, false, 24 };
    juce::ComboBox midiOutBox;
    juce::StringArray midiPortIds;
    ui::IconButton importB { ui::IconButton::Import, "Import MIDI", false }, exportB { ui::IconButton::Export, "Export MIDI", false },
                   dragB { ui::IconButton::Drag, "Drag to DAW", false };
    std::unique_ptr<juce::FileChooser> chooser;

    // Painted parts of the layout
    struct Text { juce::Rectangle<int> r; juce::String s; };
    std::vector<Text> labels, sideTitles, smallLabels;
    std::vector<juce::Rectangle<int>> vseps;
    juce::Rectangle<int> rLogo, rPreset, rSide, rMsg, rPlayRow, rSteps, rLegend, rRowA, rRowB, rFoot, rGen, rInsp, rInspName, rGhostTop, rGhostBot, rHostNote;

    juce::TooltipWindow tips { this, 400 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidinjaEditor)
};
