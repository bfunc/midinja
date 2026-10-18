#pragma once

#include "Engine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace midinja
{

// A Standard MIDI File (type 0, 480 PPQ) with tempo, 4/4 and the notes. The track ends at
// cycleSteps (the pattern length), so a DAW loops it right. Bytes only; the caller writes the
// file (JUCE in the app, std::ofstream in the tests).
std::vector<uint8_t> writeMidiFile (const std::vector<NoteEvent>& notes, double stepsPerBeat,
                                    double bpm, int channel, const std::string& trackName,
                                    double cycleSteps);

// One track of a multi-track file: its name, channel (1..16) and notes.
struct MidiTrack
{
    std::string name;
    int channel = 1;
    std::vector<NoteEvent> notes;
};

// A type 1 file: a tempo track (tempo, 4/4), then one track per entry, each ending at cycleSteps.
std::vector<uint8_t> writeMidiFileTracks (const std::vector<MidiTrack>& tracks, double stepsPerBeat,
                                          double bpm, double cycleSteps);

} // namespace midinja
