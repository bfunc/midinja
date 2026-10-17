#pragma once

// The document the UI edits: four parts (A A' B C), the arrangement of up to four bars, the
// generator settings, and two records of changes: versions (one per roll or import, edits change
// the current one in place) and the undo stack (every action). Mirrors web/index.html, which is
// the approved reference. Plain C++, no JUCE; message thread only.

#include "core/Engine.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace midinja
{

constexpr int kMaxBars = 4;

enum class Src { Roll, Import, Derived }; // Derived = generated from an import
enum class Op { Roll, Rhythm, Notes, Mutate };
enum class View { Riff, Scale, All };

const char* partName (int part);     // "A", "A'", "B", "C"
const char* partHint (int part);     // "the motif on I", ...
const char* degreeName (int pc);     // "1", "b2", ... for a pitch class from the root
const char* chordName (int pc);      // "I", "bII", ... for a chord root's pitch class

struct Doc
{
    std::array<Pattern, kNumParts> parts;
    std::array<bool, kNumParts> locks {};
    std::array<Src, kNumParts> src { Src::Roll, Src::Roll, Src::Roll, Src::Roll };
    std::vector<int> arr { 0, 1, 2, 3 }; // part index per bar
    int sel = -1;                        // selected bar (index into arr), or -1
    int cur = 0;                         // the part shown
    GenSettings gen;
    int rate = 4; // steps per beat: 2, 4 or 6 -> 8, 16 or 12 steps per bar
    float swing = 0.18f;
    int transpose = 0;

    int numSteps() const { return rate == 2 ? 8 : rate == 6 ? 12 : 16; }
    Pattern& shown() { return parts[(size_t) cur]; }
    const Pattern& shown() const { return parts[(size_t) cur]; }
    // What Play goes through: the selected bar, else the arrangement, else the part shown.
    std::vector<int> playSeq() const;
    bool anyLocked() const;
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
    View view = View::Scale;
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

    // The dice on the part shown, and on every unlocked part.
    void apply (Op op);
    void applyAll (Op op, bool keepSeed = false);
    void cycleMutAmt();
    void rekey (int root, int scale);
    void setRate (int rate);
    void transposeAll (int dir);

    // Arrangement.
    void selectPart (int k);
    void selectSlot (int i); // click again deselects
    void toggleLock (int k);
    void removeBar (int i);
    void addBar (int k);

    // History.
    void commitVersion(); // a roll or an import: a new version in the strip
    void push();          // a manual edit: the current version changes in place
    void gotoVersion (int i);
    void undoTo (int j);
    bool canUndo() const { return ui > 0; }
    bool canRedo() const { return ui < (int) undo.size() - 1; }
    void histPrev() { histEnd -= kPerPage; clampHist(); }
    void histNext() { histEnd += kPerPage; clampHist(); }
    void clampHist();

    // Import: up to 4 bars from the file start into A A' B C (1/16 grid, lowest note where notes
    // overlap, a note cut at the bar line, velocities kept); missing bars generated, key guessed.
    // Returns false (and sets message) when nothing could be used.
    bool importMidi (const std::vector<RawNote>& notes, double ppq, const std::string& name);

    // Export: the arrangement (or the part shown) as one Standard MIDI File.
    std::vector<uint8_t> midiFile (double bpm, int channel) const;
    std::string exportName() const; // "midinja-E-Dorian"

    static uint32_t newSeed();

private:
    unsigned lockMask() const;
    void pushUndo (const Doc& snap);
    void restore (const Doc& d);
};

} // namespace midinja
