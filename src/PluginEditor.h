#pragma once

#include "PluginProcessor.h"
#include "ui/GridComponent.h"
#include "ui/LaneComponent.h"
#include "ui/Theme.h"

class MidinjaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MidinjaEditor (MidinjaProcessor&);
    ~MidinjaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using Edit = std::function<void (MidinjaProcessor::State&)>;

    void timerCallback() override;
    void applyEdit (const Edit& fn);
    void refreshFromProcessor();
    void syncControls();
    void refreshMidiPorts();
    void exportMidi();
    void setStatus (const juce::String& text) { status.setText (text, juce::dontSendNotification); }

    // Small helpers to build the controls.
    void setupCombo (juce::ComboBox& box, juce::Label& label, const juce::String& title);
    void setupKnob (juce::Slider& s, juce::Label& label, const juce::String& title, double lo, double hi, double step);
    void setupRange (juce::Slider& s, juce::Label& label, const juce::String& title, double lo, double hi);

    MidinjaProcessor& proc;
    theme::Look look;
    MidinjaProcessor::State view;
    int shownVersion = -1;

    // Top bar
    juce::TextButton dice { "DICE" }, infinity { "Infinity" }, play { "Play" }, exportButton { "Export MIDI" };
    juce::TextButton transposeDown { "-" }, transposeUp { "+" };
    juce::ComboBox rootBox, scaleBox, stepsBox, rateBox;
    juce::Label rootLabel, scaleLabel, stepsLabel, rateLabel, transposeLabel;
    juce::Slider swingKnob, notesKnob, bpmSlider;
    juce::Label swingLabel, notesLabel, bpmLabel;

    // Left panel: what the dice uses
    juce::Slider pitchRange, lengthRange, velRange, repRange, repChance;
    juce::Label pitchLabel, lengthLabel, velLabel, repRangeLabel, repChanceLabel;
    juce::Label pitchValue, lengthValue, velValue, repValue, seedLabel;
    juce::ToggleButton repeatsToggle { "Repeats" };

    // Centre
    GridComponent grid;
    LaneComponent velLane { LaneComponent::Kind::Velocity };
    LaneComponent lenLane { LaneComponent::Kind::Length };
    LaneComponent repLane { LaneComponent::Kind::Repeats };

    // Footer: built-in synth and MIDI out
    juce::ToggleButton synthToggle { "Synth" };
    juce::ComboBox waveBox, midiOutBox, channelBox;
    juce::Label waveLabel, midiOutLabel, channelLabel;
    juce::Slider cutoffKnob, resoKnob, decayKnob, volumeKnob;
    juce::Label cutoffLabel, resoLabel, decayLabel, volumeLabel;
    juce::TextButton refreshPorts { "Rescan" };
    juce::StringArray midiPortIds;
    juce::Label status;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidinjaEditor)
};
