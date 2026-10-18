#pragma once

// The document the UI edits, after web/index.html (the approved reference): three tabs like
// Riffer (riff / chords, each with its own parts, locks, octave, range and channel) over a shared
// key, rate, arrangement and chord per part type; two records of changes: versions (one per roll
// or import, edits change the current one in place) and the undo stack (every action).
// Plain C++, no JUCE; message thread only.

#include "core/Engine.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace midinja
{

constexpr int kMaxBars = 4;
constexpr int kNumTabs = 3;
constexpr int kMaxSpan = 36;       // the range is at most three octaves: more rows get too thin
constexpr int kMaxChordNotes = 4;
constexpr int kNumRhythms = 6;     // the chord rhythms the dice pick from

enum class Src { Roll, Import, Derived };
enum class Op { Roll, Rhythm, Notes, Mutate };
enum class ChordType { Triad, Seventh, Sus2, Sus4 };

const char* partName (int part);          // "A", "A'", "B", "C"
const char* partHint (int part);          // "the motif", ...
const char* degreeName (int pc);          // "1", "b2", ... for a pitch class from the root
const char* chordTypeName (ChordType t);  // "Triad", "7th", "sus2", "sus4"
// The chord on a scale degree as a roman numeral with its quality from the scale: "i", "IV",
// "bVII", "vii°". UTF-8.
std::string numeral (int degree, int scale);

// What a riff remembers for the scale switch: its notes in the scale it was made or edited in.
// A switch is always computed from here, so any sequence of switches is reversible.
struct Base
{
    bool valid = false;
    int scale = 0;
    std::array<int, kMaxSteps> notes {};
};

struct Tab
{
    int channel = 1;
    bool poly = false; // chords: one held chord per bar, the dice vary only its rhythm
    bool on = true;    // off: no play, no export
    int rootNote = 40, lowNote = 40, highNote = 64; // the root's octave and the range, in semitones
    std::array<Pattern, kNumParts> parts;
    std::array<bool, kNumParts> locks {};
    std::array<Src, kNumParts> src { Src::Roll, Src::Roll, Src::Roll, Src::Roll };
    std::array<Base, kNumParts> base;
    std::array<int, kNumParts> tpl { -1, -1, -1, -1 }; // the chord rhythm a part was given
};

// The chord of a part type: a scale degree (index into the scale), a type and an inversion.
struct ChordSpec
{
    int degree = 0;
    ChordType type = ChordType::Triad;
    int inv = 0;
};

struct Doc
{
    std::array<Tab, kNumTabs> tabs;
    int tab = 0;                          // the tab shown
    std::array<ChordSpec, kNumParts> chords;
    std::vector<int> arr { 0 };           // the arrangement: part index per bar
    int sel = -1;                         // selected bar (index into arr), or -1
    int cur = 0;                          // the part shown
    GenSettings gen;                      // root, scale and the knobs; the range comes from the tab
    int rate = 4;                         // steps per beat: 2, 4 or 6 -> 8, 16 or 12 steps per bar
    float swing = 0.18f;

    Tab& T() { return tabs[(size_t) tab]; }
    const Tab& T() const { return tabs[(size_t) tab]; }
    Pattern& shown() { return T().parts[(size_t) cur]; }
    const Pattern& shown() const { return T().parts[(size_t) cur]; }
    int numSteps() const { return rate == 2 ? 8 : rate == 6 ? 12 : 16; }
    // What Play goes through: the selected bar, else the arrangement, else the part shown.
    std::vector<int> playSeq() const;
    bool anyLocked() const;

    int scaleSize() const;
    int degreeOf (int k) const;   // the chord degree of part k, within the scale
    int degreePc (int k) const;   // its pitch class from the root
    int pcOf (int note) const;
    bool inScale (int note) const;
    // The chord of part k voiced inside a tab's range, bottom up.
    std::vector<int> chordNotes (int k, const Tab& t) const;
    // The augmented-second pairs of a riff: two neighbouring notes that form a signature move of
    // the scale, as (step, step); one locked note splits the pair.
    std::vector<std::pair<int, int>> pairsOf (const Pattern& p) const;
    // The generator settings for a part of a tab: the knobs, the tab's range, the part's degree.
    GenSettings genFor (const Tab& t, int k) const;
};

struct Version { Doc snap; bool star = false; };
struct UndoEntry { Doc snap; int vi = -1; };

struct RawNote { double tick; double dur; int note; int vel; }; // straight from a MIDI file

class Session
{
public:
    Session();

    Doc doc;
    // UI preferences, saved with the document.
    bool adv = false;
    Op what = Op::Roll;
    float mutAmt = 0.15f;

    std::vector<Version> versions;
    int vi = -1;      // current version
    int histEnd = 0;  // the strip shows versions[histEnd - kPerPage .. histEnd)
    std::vector<UndoEntry> undo;
    int ui = -1;
    static constexpr int kPerPage = 3, kMaxHist = 60, kMaxUndo = 200;
    std::string message; // the line above the grid (import result, lock notice)

    // The dice on the part shown, and on every unlocked part of the tab shown.
    void apply (Op op);
    void applyAll (Op op, bool keepSeed = false);
    void cycleMutAmt();
    // Progression: A A' B C with chord degrees from the mode's idiom (A = A', C leads home); the
    // riffs of every tab move to the new degrees, locked parts keep theirs. 7-note scales only.
    bool canProgression() const;
    void rollProgression();

    // The key. A new scale maps every riff from its remembered notes; a new root moves every tab;
    // an octave moves the tab shown.
    void setScale (int scale);
    void setRootPc (int pc);
    void setOctave (int dir);
    void setRate (int rate);

    // The range of the tab shown: the rows of the roll, one semitone per step at each edge.
    bool canGrow (bool top) const;
    bool canShrink (bool top, std::string* why = nullptr) const;
    void growRange (bool top);
    void shrinkRange (bool top);

    // Tabs.
    void selectTab (int i);
    void setTabPoly (int i, bool poly);
    void setTabChannel (int i, int channel);
    void setTabOn (int i, bool on);

    // The chord of a part type (the degree is also where its riffs start).
    void setChord (int k, int degree, ChordType type, int inv);

    // Arrangement.
    void selectPart (int k);
    void selectSlot (int i); // click again deselects
    void toggleLock (int k);
    void removeBar (int i);
    void addBar (int k);     // an empty part joins as a copy of A moved to its degree

    // History.
    void commitVersion();             // a roll or an import: a new version in the strip
    void push (int editedPart = -1);  // a manual edit: the current version changes in place; the
                                      // edited part remembers its notes for the scale switch
    void gotoVersion (int i);
    void undoTo (int j);
    bool canUndo() const { return ui > 0; }
    bool canRedo() const { return ui < (int) undo.size() - 1; }
    void histPrev() { histEnd -= kPerPage; clampHist(); }
    void histNext() { histEnd += kPerPage; clampHist(); }
    void clampHist();

    // Import into the tab shown: up to 4 bars from the file start into A A' B C (1/16 grid, lowest
    // note where notes overlap, a note cut at the bar line, velocities kept); the key is guessed
    // and set for every tab. Returns false (and sets message) when nothing could be used.
    bool importMidi (const std::vector<RawNote>& notes, double ppq, const std::string& name);

    // Export: the arrangement (or the part shown) as one Standard MIDI File, a track per tab that
    // is on, each on its channel. Empty (and a message) when no tab is on.
    std::vector<uint8_t> midiFile (double bpm);
    std::string exportName() const; // "midinja-E-Dorian"

    static uint32_t newSeed();
    // A note moved by scale steps; an altered (off-scale) note moves with the scale note below it.
    int scaleStep (int note, int steps) const;
    bool lockedOn (int note) const; // a locked note of the tab shown sits on this row
    int scaleRows() const;          // scale notes inside the range of the tab shown

private:
    unsigned lockMask() const;
    void pushUndo (const Doc& snap);
    void restore (const Doc& d);
    void rebase (Tab& t, int k);
    void remap (Tab& t, int k);
    void foldPattern (Pattern& p, int lo, int hi) const;
    void foldTab (Tab& t);
    void shiftTab (Tab& t, int d);
    void fitBar (Pattern& p, const Tab& t) const;
    bool extendPart (Tab& t, int k);
    void retune (Tab& t, int k, int from, int to);
    Pattern chordRhythm (int tpl) const;
    void rollChord (Tab& t, int k);
    void rollMono (Tab& t, int k, Op op);
};

} // namespace midinja
