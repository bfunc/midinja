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
    void setChannel (int newChannel); // 1..16; All Notes Off on the old one first

    // The arrangement (or the part shown) as a .mid file, on the current channel.
    juce::MemoryBlock exportMidi() const;

    // Playback and synth settings: plain atomics, read by the audio thread every block.
    std::atomic<bool> playing { false }, synthOn { true };
    std::atomic<float> bpm { 122.0f };
    std::atomic<int> channel { 1 }, synthWave { 0 };
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
    // What the audio thread plays: the four parts and the sequence of bars.
    struct PlayModel
    {
        std::array<midinja::Pattern, midinja::kNumParts> parts;
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
    midinja::Pattern playPat;
    int barIdx = 0;
    bool lastHostPlaying = false;

    midinja::Sequencer sequencer;
    midinja::MonoSynth synth;
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
