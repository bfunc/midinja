#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "Document.h"
#include "core/Engine.h"
#include "core/Synth.h"

#include <atomic>

class MidinjaProcessor : public juce::AudioProcessor
{
public:
    MidinjaProcessor();
    ~MidinjaProcessor() override;

    // ---- UI side (message thread) ----
    // The document: edit it, then call publish() so the audio thread plays the new bars.
    midinja::Session& session() { return sess; }
    const midinja::Session& session() const { return sess; }
    void publish();
    // Bumps on every publish and on a state restore; the editor redraws when it changes.
    int publishedVersion() const { return version.load(); }
    // The next block starts again from the first bar (after selecting a bar).
    void restartPlayback() { restart = true; }

    // Where the playhead is: the bar within the play sequence and the step; -1 when stopped.
    int currentBar() const { return playBar.load(); }
    int currentStep() const { return playStep.load(); }
    bool isPlayingNow() const { return playStep.load() >= 0; }

    bool isHosted() const { return wrapperType != wrapperType_Standalone; }

    juce::Array<juce::MidiDeviceInfo> midiOutputs() const { return juce::MidiOutput::getAvailableDevices(); }
    bool openMidiOut (const juce::String& identifier); // "" closes
    juce::String midiOutId() const { return midiOutIdentifier; }

    // The arrangement (or the part shown) as a .mid file: a track per tab that is on.
    juce::MemoryBlock exportMidi();

    // Playback and synth settings: plain atomics, read by the audio thread every block.
    std::atomic<bool> playing { false }, synthOn { true };
    std::atomic<float> bpm { 122.0f };
    std::atomic<int> synthWave { 0 };
    std::atomic<float> cutoff { 1800.0f }, resonance { 0.3f }, decay { 250.0f }, volume { 0.6f };
    // From the host, for the UI: tempo (0 = none) and transport.
    std::atomic<float> hostBpm { 0.0f };
    std::atomic<bool> hostPlaying { false };

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
    bool producesMidi() const override { return true; }
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
    // What the audio thread plays: per tab its parts (a chord tab's steps carry the part index
    // as the note, expanded to the chord's notes on the way out) and the sequence of bars.
    struct PlayTab
    {
        bool on = false, poly = false;
        int channel = 1;
        std::array<midinja::Pattern, midinja::kNumParts> parts;
        std::array<std::array<int, midinja::kMaxChordNotes>, midinja::kNumParts> chord {};
        std::array<int, midinja::kNumParts> chordN {};
    };
    struct PlayModel
    {
        std::array<PlayTab, midinja::kNumTabs> tabs;
        std::array<int, midinja::kMaxBars> seq {};
        int seqLen = 1;
        int rate = 4;
        float swing = 0.0f;
    };

    void sendAllNotesOffNow();
    static midinja::Rate rateOf (int stepsPerBeat);

    midinja::Session sess; // message thread only

    mutable juce::SpinLock stateLock;
    PlayModel shared;
    std::atomic<int> version { 1 };
    std::atomic<bool> restart { false };

    // Audio-thread copies.
    PlayModel audio;
    int seenVersion = 0;
    std::array<midinja::Pattern, midinja::kNumTabs> playPat;
    int barIdx = 0;
    bool lastHostPlaying = false;

    std::array<midinja::Sequencer, midinja::kNumTabs> sequencers;
    std::array<midinja::MonoSynth, midinja::kNumTabs> mono;
    std::array<midinja::PadSynth, midinja::kNumTabs> pad;
    // The chord notes a chord tab's part is holding, so a note-off releases what was played.
    std::array<std::array<std::array<int, midinja::kMaxChordNotes>, midinja::kNumParts>, midinja::kNumTabs> sounding {};
    std::array<std::array<int, midinja::kNumParts>, midinja::kNumTabs> soundingN {};
    std::vector<midinja::MidiEvent> events;
    juce::MidiBuffer outBuffer;
    std::atomic<int> playStep { -1 }, playBar { -1 };
    double currentSampleRate = 44100.0;
    std::vector<float> monoScratch;

    juce::SpinLock midiOutLock;
    std::unique_ptr<juce::MidiOutput> midiOut;
    juce::String midiOutIdentifier;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidinjaProcessor)
};
