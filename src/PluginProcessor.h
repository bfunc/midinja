#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "core/Engine.h"
#include "core/Synth.h"

#include <atomic>
#include <functional>

class BrifferProcessor : public juce::AudioProcessor
{
public:
    // Everything the dice and the grid work on. Shared between the UI and the audio thread
    // under a spin lock; the audio thread only ever try-locks.
    struct State
    {
        briffer::Pattern pattern;
        briffer::GenSettings gen;
    };

    BrifferProcessor();
    ~BrifferProcessor() override;

    // ---- UI side (message thread) ----
    State getState() const;
    void editState (const std::function<void (State&)>& fn);
    int stateVersion() const { return version.load(); }
    // New seed + generate.
    void roll();
    int currentStep() const { return playStep.load(); }

    juce::Array<juce::MidiDeviceInfo> midiOutputs() const { return juce::MidiOutput::getAvailableDevices(); }
    bool openMidiOut (const juce::String& identifier); // "" closes
    juce::String midiOutId() const { return midiOutIdentifier; }
    // Changes the MIDI channel; sends All Notes Off on the old one first.
    void setChannel (int newChannel);

    // The current pattern as a .mid file (one cycle).
    juce::MemoryBlock exportMidi() const;

    // Playback and synth settings: plain atomics, read by the audio thread every block.
    std::atomic<bool> playing { false }, infinity { false }, synthOn { true };
    std::atomic<float> bpm { 120.0f }, swing { 0.0f };
    std::atomic<int> rate { (int) briffer::Rate::Sixteenth }, channel { 1 };
    std::atomic<int> synthWave { 0 };
    std::atomic<float> cutoff { 1800.0f }, resonance { 0.3f }, decay { 250.0f }, volume { 0.6f };

    // ---- juce::AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    void sendAllNotesOffNow();

    mutable juce::SpinLock stateLock;
    State shared;
    std::atomic<int> version { 1 };

    // Audio-thread copies.
    State audio;
    int seenVersion = 0;
    bool pendingWriteBack = false;

    briffer::Sequencer sequencer;
    briffer::MonoSynth synth;
    std::vector<briffer::MidiEvent> events;
    juce::MidiBuffer outBuffer;
    std::atomic<int> playStep { -1 };
    double currentSampleRate = 44100.0;
    std::vector<float> monoScratch;

    juce::SpinLock midiOutLock;
    std::unique_ptr<juce::MidiOutput> midiOut;
    juce::String midiOutIdentifier;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrifferProcessor)
};
