#pragma once

#include <array>
#include <vector>

namespace briffer
{

struct Scale
{
    const char* name;
    std::vector<int> intervals; // semitones from the root, ascending, first is 0
};

// Western modes first, then the rest, roughly as Riffer lists them.
inline const std::vector<Scale>& scales()
{
    static const std::vector<Scale> list {
        { "Major (Ionian)",      { 0, 2, 4, 5, 7, 9, 11 } },
        { "Natural Minor",       { 0, 2, 3, 5, 7, 8, 10 } },
        { "Dorian",              { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygian",            { 0, 1, 3, 5, 7, 8, 10 } },
        { "Lydian",              { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian",          { 0, 2, 4, 5, 7, 9, 10 } },
        { "Locrian",             { 0, 1, 3, 5, 6, 8, 10 } },
        { "Harmonic Minor",      { 0, 2, 3, 5, 7, 8, 11 } },
        { "Melodic Minor",       { 0, 2, 3, 5, 7, 9, 11 } },
        { "Minor Pentatonic",    { 0, 3, 5, 7, 10 } },
        { "Major Pentatonic",    { 0, 2, 4, 7, 9 } },
        { "Blues",               { 0, 3, 5, 6, 7, 10 } },
        { "Major Blues",         { 0, 2, 3, 4, 7, 9 } },
        { "Chromatic",           { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        { "Whole Tone",          { 0, 2, 4, 6, 8, 10 } },
        { "Diminished (H-W)",    { 0, 1, 3, 4, 6, 7, 9, 10 } },
        { "Diminished (W-H)",    { 0, 2, 3, 5, 6, 8, 9, 11 } },
        { "Augmented",           { 0, 3, 4, 7, 8, 11 } },
        { "Phrygian Dominant",   { 0, 1, 4, 5, 7, 8, 10 } },
        { "Lydian Dominant",     { 0, 2, 4, 6, 7, 9, 10 } },
        { "Super Locrian",       { 0, 1, 3, 4, 6, 8, 10 } },
        { "Dorian #4",           { 0, 2, 3, 6, 7, 9, 10 } },
        { "Lydian Augmented",    { 0, 2, 4, 6, 8, 9, 11 } },
        { "Harmonic Major",      { 0, 2, 4, 5, 7, 8, 11 } },
        { "Double Harmonic",     { 0, 1, 4, 5, 7, 8, 11 } },
        { "Hungarian Minor",     { 0, 2, 3, 6, 7, 8, 11 } },
        { "Hungarian Major",     { 0, 3, 4, 6, 7, 9, 10 } },
        { "Neapolitan Minor",    { 0, 1, 3, 5, 7, 8, 11 } },
        { "Neapolitan Major",    { 0, 1, 3, 5, 7, 9, 11 } },
        { "Enigmatic",           { 0, 1, 4, 6, 8, 10, 11 } },
        { "Persian",             { 0, 1, 4, 5, 6, 8, 11 } },
        { "Arabic",              { 0, 2, 4, 5, 6, 8, 10 } },
        { "Spanish 8-Tone",      { 0, 1, 3, 4, 5, 6, 8, 10 } },
        { "Flamenco",            { 0, 1, 4, 5, 7, 8, 11 } },
        { "Gypsy",               { 0, 2, 3, 6, 7, 8, 10 } },
        { "Romanian Minor",      { 0, 2, 3, 6, 7, 9, 10 } },
        { "Ukrainian Dorian",    { 0, 2, 3, 6, 7, 9, 10 } },
        { "Hirajoshi",           { 0, 2, 3, 7, 8 } },
        { "In Sen",              { 0, 1, 5, 7, 10 } },
        { "Iwato",               { 0, 1, 5, 6, 10 } },
        { "Kumoi",               { 0, 2, 3, 7, 9 } },
        { "Yo",                  { 0, 2, 5, 7, 9 } },
        { "Pelog",               { 0, 1, 3, 7, 8 } },
        { "Chinese",             { 0, 4, 6, 7, 11 } },
        { "Egyptian",            { 0, 2, 5, 7, 10 } },
        { "Bhairav",             { 0, 1, 4, 5, 7, 8, 11 } },
        { "Todi",                { 0, 1, 3, 6, 7, 8, 11 } },
        { "Marva",               { 0, 1, 4, 6, 7, 9, 11 } },
        { "Purvi",               { 0, 1, 4, 6, 7, 8, 11 } },
        { "Prometheus",          { 0, 2, 4, 6, 9, 10 } },
        { "Bebop Dominant",      { 0, 2, 4, 5, 7, 9, 10, 11 } },
        { "Bebop Major",         { 0, 2, 4, 5, 7, 8, 9, 11 } },
        { "Minor 6th Pent.",     { 0, 3, 5, 7, 9 } },
    };
    return list;
}

inline const std::array<const char*, 12>& noteNames()
{
    static const std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names;
}

} // namespace briffer
