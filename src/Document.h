#pragma once

// The document the UI edits, after web/index.html (the approved reference): two tabs, Riff and
// Chords, over a shared key, rate, arrangement (A A' B C, the melody form) and chord lane
// (blocks with a start and a length in beats); two records of changes: versions (one per roll or
// import, edits change the current one in place) and the undo stack (every action).
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
constexpr int kRiffTab = 0, kChordTab = 1, kBeatTab = 2;
constexpr int kNumDrums = 9, kNumHats = 5, kNumFills = 5;
// rows of the drum grid, top to bottom
constexpr int kRowCrash = 0, kRowHiTom = 1, kRowMidTom = 2, kRowLoTom = 3, kRowOpenHat = 4, kRowClosedHat = 5, kRowClap = 6, kRowSnare = 7, kRowKick = 8;
constexpr int kMaxSpan = 36;       // the range is at most three octaves: more rows get too thin
constexpr int kMaxChordNotes = 6;
constexpr int kNumRhythms = 8;
constexpr int kNumMoods = 5, kNumStyles = 5;

enum class Src { Roll, Import, Derived };
enum class Op { Roll, Rhythm, Notes, Mutate };
enum class ChordType { Triad, Seventh, Ninth, Sus2, Sus4, Add9 };
enum class Rhythm { Whole, Halves, Quarters, Stabs, Push, Offbeat, Charleston, Funk };
enum class Quality { Maj, Min, Dim, Aug };

const char* partName (int part);          // "A", "A'", "B", "C"
const char* partHint (int part);          // "the motif", ...
const char* degreeName (int pc);          // "1", "b2", ... for a pitch class from the root
const char* chordTypeName (ChordType t);  // "Triad", "7th", "9th", "sus2", "sus4", "add9"
const char* rhythmName (Rhythm r);        // "Whole", ...
const char* moodName (int i);             // "Bright", "Colour", "Fable", "Drama", "Dark"
const char* moodTip (int i);
const char* styleName (int i);            // "Jazz", "R&B", "Funk", "House", "Drum & Bass"
const char* styleTip (int i);
// The chord rhythm in sixteenths from the chord start for a chord len beats long: (start, length).
std::vector<std::pair<int, int>> rhythmHits (Rhythm r, int len);
// The Beats tab: GM drums per row (Crash 49 ... Kick 36), the hats patterns and the fills.
int drumNote (int row);
const char* drumName (int row);
const char* hatName (int i);
const char* fillName (int i);
int drumVel (float weight, int velMin, int velMax);

struct DrumHit { bool on = false; int velocity = 100; bool locked = false; };
// A drum part: the hits per row, and the choices the dice make; the hits are rebuilt from the
// choices (locked hits stay). The groove: kick on every beat, snare (and a clap) on 2 and 4, a hats
// pattern, one open hat on an offbeat. A' B C end with a fill on the last beat (C on the last
// two); the kick keeps the fill's downbeat or drops out.
struct DrumPart
{
    std::array<std::array<DrumHit, kMaxSteps>, kNumDrums> hits {};
    int hat = 0, open = 1, clap = 0, fill = 0;
    bool fillOn = false, kickFill = false;
};
struct DrumEvent { int step, row, velocity; };

// What a riff remembers for the scale switch: its notes in the scale it was made or edited in.
struct Base
{
    bool valid = false;
    int scale = 0;
    std::array<int, kMaxSteps> notes {};
};

struct Tab
{
    int channel = 1;
    bool poly = false; // the Chords tab: plays the lane
    bool on = true;    // off: no play, no export
    int rootNote = 40, lowNote = 40, highNote = 64; // the root's octave and the range, in semitones
    std::array<Pattern, kNumParts> parts;           // the Riff tab's parts
    std::array<bool, kNumParts> locks {};
    std::array<Src, kNumParts> src { Src::Roll, Src::Roll, Src::Roll, Src::Roll };
    std::array<Base, kNumParts> base;
    bool drums = false; // the Beats tab: plays its drum parts on GM notes
    std::array<DrumPart, kNumParts> beats;
};

// A block of the chord lane. start and len are beats of the arrangement (4 per bar).
struct Chord
{
    int id = 0;
    int start = 0, len = 4;
    int deg = 0;
    ChordType type = ChordType::Seventh;
    int inv = 0;
    bool lock = false;
    Rhythm rhythm = Rhythm::Whole;
};

// A chord hit inside one bar: local step, length in steps, and which chord.
struct Hit
{
    int step, len;
    int chordIndex; // index into Doc::chords
};

struct Doc
{
    std::array<Tab, kNumTabs> tabs;
    int tab = 0;                          // the tab shown
    std::vector<Chord> chords;            // the chord lane, any order
    int nextChordId = 1;
    int csel = -1;                        // the selected chord's id, or -1
    std::vector<int> arr { 0 };           // the arrangement: part index per bar
    int sel = -1;                         // selected bar (index into arr), or -1
    int cur = 0;                          // the part shown
    GenSettings gen;                      // root, scale and the knobs; the range comes from the tab
    int rate = 4;                         // steps per beat: 2, 4 or 6 -> 8, 16 or 12 steps per bar
    float swing = 0.18f;

    Tab& T() { return tabs[(size_t) tab]; }
    const Tab& T() const { return tabs[(size_t) tab]; }
    Tab& riff() { return tabs[kRiffTab]; }
    const Tab& riff() const { return tabs[kRiffTab]; }
    const Tab& chordTab() const { return tabs[kChordTab]; }
    const Tab& beatTab() const { return tabs[kBeatTab]; }
    Tab& beatTab() { return tabs[kBeatTab]; }
    // The Beats tab. fillLen: steps of the fill at the end of part k (0 = none). A crash lands on
    // step 1 of a bar after a bar with a fill (the form loops: bar 1 follows the last). drumHits:
    // the events of part k when it plays in bar (an index into arr, or -1), crash included.
    int fillLen (int k) const;
    bool autoCrash (int bar) const;
    std::vector<DrumEvent> drumHits (int k, int bar) const;
    int drumVelocity (float weight) const { return drumVel (weight, gen.velMin, gen.velMax); }
    Pattern& shown() { return riff().parts[(size_t) cur]; }
    const Pattern& shown() const { return riff().parts[(size_t) cur]; }
    int numSteps() const { return rate == 2 ? 8 : rate == 6 ? 12 : 16; }
    int stepsPerBeat() const { return numSteps() / 4; }
    int totalBeats() const { return (int) arr.size() * 4; }
    // What Play goes through: the selected bar, else the arrangement, else the part shown.
    std::vector<int> playSeq() const;
    bool anyLocked() const;
    // The bar shown: the selected bar, else the first bar of the part shown, else -1.
    int shownBar() const;

    // Scale maths. at(k) is the semitone of scale step k from the tonic (any octave).
    int scaleSize() const;
    int at (int k) const;
    int rel (int d, int k) const { return at (k) - at (d); }
    Quality qual (int d) const;
    int pcOf (int note) const;
    bool inScale (int note) const;
    bool tonicMinor() const;
    std::vector<int> degsOf (const Chord& c) const;  // scale steps of the chord's notes
    int maxInv (const Chord& c) const;
    std::string chordName (const Chord& c) const;    // "Em7"
    std::string numeral (int deg, const ChordType* type = nullptr) const; // "i", "IV", "bVII7"
    // The chord's notes, placed by octaves around a fixed point of the key (the Chords tab's root
    // plus a sixth), not the range: the range edges only crop.
    std::vector<int> voice (const Chord& c) const;
    std::vector<int> chordPcs (const Chord& c) const;

    // The lane.
    std::vector<int> sortedChordIndices() const;
    const Chord* chordById (int id) const;
    Chord* chordById (int id);
    int chordAtBeat (int beat) const;      // index, or -1
    int firstChordInBar (int bar) const;   // index, or -1
    int barDegree (int bar) const;         // the degree a bar starts in (its usual degree when none)
    int degreeOf (int part) const;         // the degree of the part's first bar
    std::vector<Hit> barHits (int bar) const;
    Pattern chordPat (int bar) const;      // the hits as a pattern (note 60)
    int laneEnd() const;                   // the last chord's end, in beats
    GenSettings genFor (const Tab& t, int k) const;
    bool overlapsBar (const Chord& c, int bar) const { return c.start < bar * 4 + 4 && c.start + c.len > bar * 4; }
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
    bool doRhythm = true, doNotes = true, mutateMode = false;
    float mutAmt = 0.15f;
    // What the dice do, from the toggles: Roll (both), Rhythm, Notes, Mutate; false when nothing is on.
    bool currentOp (Op& out) const;

    std::vector<Version> versions;
    int vi = -1;
    int histEnd = 0;
    std::vector<UndoEntry> undo;
    int ui = -1;
    static constexpr int kPerPage = 3, kMaxHist = 60, kMaxUndo = 200;
    std::string message; // the line above the grid (import result, lock notice)
    bool messageIsError = false;

    // The dice on the part shown (Riff) or a new rhythm for the selected chord (Chords); and on
    // every unlocked part / chord.
    void apply (Op op);
    void applyAll (Op op, bool keepSeed = false);
    void cycleMutAmt();

    // The key. A new scale maps every riff from its remembered notes; a new root moves both tabs;
    // an octave moves the tab shown.
    void setScale (int scale);
    void setRootPc (int pc);
    void setOctave (int dir);
    void setRate (int rate);

    // The range of the tab shown.
    bool canGrow (bool top) const;
    bool canShrink (bool top, std::string* why = nullptr) const;
    void growRange (bool top);
    void shrinkRange (bool top);

    // Tabs.
    void selectTab (int i);
    void setTabChannel (int i, int channel);
    void setTabOn (int i, bool on);

    // The chord lane.
    void selectChord (int id);
    void setChordSpan (int id, int start, int len, bool record); // a drag: record on release
    void setChordDeg (int id, int deg);
    void setChordType (int id, ChordType type);
    void setChordInv (int id, int inv);
    void cycleInversion (int id);
    void setChordRhythm (int id, Rhythm r);
    void rhythmToAll (int id);
    void toggleChordLock (int id);
    void regenChord (int id);              // another chord here (not the neighbours', no diminished)
    void appendChord();                    // the square +: repeats the last chord (adds a bar below 4)
    void addChordAt (int beat);            // double-click on the empty lane
    void removeChord (int id);
    bool canAppendChord() const;
    // Generators. Locked chords are kept.
    void progression();
    bool moodAvailable (int mood) const;
    std::vector<int> moodDegrees (int mood) const; // the diatonic moves, for a tooltip
    void mood (int mood);
    bool styleHasOwnList (int style) const;
    void style (int style);
    bool applyTyped (const std::string& text); // false: the message says what was wrong

    // Arrangement.
    void selectPart (int k);
    void selectSlot (int i);
    void toggleLock (int k);
    void removeBar (int i);                // its chords go, later ones move left
    void addBar (int k);                   // an empty part joins as a copy of A moved to its degree
    void repeatLastBar();                  // the square +: A -> A A
    // Unlock bar: every lock of the bar shown (the part and its notes on Riff, the chords on Chords).
    int barLocks() const;
    void unlockBar();

    // The Beats tab: the dice pick other hats and another fill; Fill on/off per part (never A);
    // hits toggle, lock, and cycle soft / normal / accent. These record their own undo step.
    void rebuildDrum (int k);
    void rollDrum (int k);
    void setFill (int k, bool on);
    void toggleDrumHit (int row, int step);
    void toggleDrumLock (int row, int step);
    void cycleDrumVel (int row, int step);
    int lockedHits (int k) const;
    void unlockHits (int k);

    // History.
    void commitVersion();
    void push (int editedPart = -1);
    void gotoVersion (int i);
    void undoTo (int j);
    bool canUndo() const { return ui > 0; }
    bool canRedo() const { return ui < (int) undo.size() - 1; }
    void histPrev() { histEnd -= kPerPage; clampHist(); }
    void histNext() { histEnd += kPerPage; clampHist(); }
    void clampHist();

    bool importMidi (const std::vector<RawNote>& notes, double ppq, const std::string& name);
    std::vector<uint8_t> midiFile (double bpm);
    std::string exportName() const;

    static uint32_t newSeed();
    int scaleStep (int note, int steps) const;
    bool lockedOn (int note) const;
    int scaleRows() const;
    void say (const std::string& text, bool error = false) { message = text; messageIsError = error; }

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
    void rollMono (Tab& t, int k, Op op);
    // The riff follows the lane: on each chord start in a bar the riff plays a chord tone.
    void fixChordTones (int k);
    void fixAllChordTones();
    std::array<int, kNumParts> degreesNow() const;
    void laneChanged (const std::array<int, kNumParts>* from);
    void fourBars();
    void fillBars (const std::vector<int>& degs, const std::string& label, const std::vector<ChordType>* types, const Rhythm* rhythm);
    Chord mk (int start, int len, int deg, ChordType type, int inv, Rhythm rhythm);
    void addPartBar (int k);
};

} // namespace midinja
