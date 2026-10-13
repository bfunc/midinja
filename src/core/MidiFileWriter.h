#pragma once

#include "Engine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace briffer
{

// A Standard MIDI File (type 0, 480 PPQ) with tempo, 4/4 and the notes. The track ends at
// cycleSteps (the pattern length), so a DAW loops it right. Bytes only; the caller writes the
// file (JUCE in the app, std::ofstream in the tests).
std::vector<uint8_t> writeMidiFile (const std::vector<NoteEvent>& notes, double stepsPerBeat,
                                    double bpm, int channel, const std::string& trackName,
                                    double cycleSteps);

} // namespace briffer
