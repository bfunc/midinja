#pragma once

// Midinja core: the pattern model, the riff generator and the step timing.
// Plain C++17, no JUCE, so it is shared by the app and the tests.

#include <array>
#include <cstdint>
#include <vector>

namespace midinja
{

constexpr int kMaxSteps = 32;
constexpr int kMaxLength = 8;  // steps
constexpr int kMaxRepeats = 4; // ratchets per note

struct Step
{
    bool on = false;
    int note = 60;      // MIDI note
    int length = 1;     // in steps, 1..kMaxLength
    int velocity = 100; // 1..127
    int repeats = 1;    // 1..kMaxRepeats
    bool locked = false;

    bool operator== (const Step& o) const
    {
        return on == o.on && note == o.note && length == o.length && velocity == o.velocity
            && repeats == o.repeats && locked == o.locked;
    }
};

struct Pattern
{
    std::array<Step, kMaxSteps> steps {};
    int numSteps = 16;
};

// What the dice uses. Only the dice reads these; editing a note never does.
struct GenSettings
{
    int root = 4;      // 0 = C .. 11 = B
    int scale = 2;     // index into scales()
    int lowNote = 40;  // E2
    int highNote = 64; // E4
    int notes = 10;    // how many steps get a note
    int lenMin = 1, lenMax = 2;
    int velMin = 70, velMax = 120;
    bool repeatsOn = false; // off by default
    int repMin = 2, repMax = 3;
    float repChance = 0.25f;
    uint32_t seed = 1;

    int mode = 0;           // 0 = Random, 1 = Musician
    float color = 0.4f;     // Musician: how often the mode's colour note sounds, 0..1
    float surprise = 0.15f; // Musician: how often a rule is broken, 0..1
};

enum Mode { kRandom = 0, kMusician = 1 };

enum class Rate { Eighth = 0, Sixteenth, ThirtySecond, SixteenthTriplet };
const char* rateName (Rate r);
double stepsPerBeat (Rate r);

// Small deterministic RNG (same results on every platform for a given seed).
class Rng
{
public:
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 0x9E3779B9u) { if (s == 0) s = 0x1234567u; next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int range (int lo, int hi) { if (hi <= lo) return lo; return lo + (int) (next() % (uint32_t) (hi - lo + 1)); }
    float unit() { return (float) (next() >> 8) / 16777216.0f; }
private:
    uint32_t s;
};

bool inScale (int note, int root, int scale);
// Notes of the scale inside [low, high], ascending.
std::vector<int> scaleNotes (int root, int scale, int low, int high);
// Nearest note of the scale (ties go down).
int snapToScale (int note, int root, int scale);
// Move a note by whole scale steps.
int moveByScaleSteps (int note, int steps, int root, int scale);

// Re-rolls every unlocked step from the settings and settings.seed. Locked steps are kept.
void generate (Pattern& p, const GenSettings& g);
// New positions for the unlocked notes; their pitches keep their order.
void generateRhythm (Pattern& p, const GenSettings& g);
// New pitches for the unlocked notes; positions and lengths stay.
void generateNotes (Pattern& p, const GenSettings& g);
// Changes about `amount` (0..1) of the unlocked notes a little: a scale step or two, or a nudge.
void mutate (Pattern& p, const GenSettings& g, float amount);

// What Musician mode builds on, as pitch classes from the root (-1 = the scale has none).
struct ModeInfo
{
    int third = -1, fifth = -1; // the tonic triad
    int color = -1;             // the note that tells this mode from plain major / minor
};
ModeInfo modeInfo (int scale);
// Musician phrases are built from equal cells (A A' A B); this is a cell's length in steps.
int musicianCellLength (int numSteps);
// Notes never overlap the next note (the riff is monophonic) or run past the pattern end.
void fitLengths (Pattern& p);
void transposeScaleSteps (Pattern& p, int steps, int root, int scale);
void transposeSemitones (Pattern& p, int semitones);
// After a root/scale change: every note moves into the new scale.
void conformToScale (Pattern& p, int oldRoot, int newRoot, int newScale);

struct NoteEvent
{
    double start;  // in steps from the cycle start
    double length; // in steps
    int note;
    int velocity;
};

// Swing 0..1 maps to 50%..75%: odd steps are delayed by swing * 0.5 steps.
double swingOffset (int stepIndex, float swing);
// The notes one step plays, start relative to the step's grid position (swing included).
void stepEvents (const Pattern& p, int stepIndex, float swing, std::vector<NoteEvent>& out);
// One whole cycle of the pattern.
std::vector<NoteEvent> renderCycle (const Pattern& p, float swing);

// ---------------------------------------------------------------------------------------------
// Real-time step sequencer: turns the pattern into MIDI events at sample positions.

struct MidiEvent
{
    int sampleOffset;
    uint8_t status; // 0x90 note on, 0x80 note off, 0xB0 CC (channel is added by the caller)
    uint8_t data1;
    uint8_t data2;
};

class Sequencer
{
public:
    void prepare (double sampleRate);
    void start();
    // Note-offs for everything sounding + All Notes Off, at sample 0 of the next block.
    void stop (std::vector<MidiEvent>& out);
    bool isPlaying() const { return playing; }

    // bpm, rate and swing may change every block. onCycle is called on the audio thread at each
    // cycle start after the first (Infinity mode re-rolls there) and may change the pattern.
    template <typename OnCycle>
    void process (int numSamples, double bpm, Rate rate, float swing, Pattern& pattern,
                  std::vector<MidiEvent>& out, OnCycle&& onCycle);

    // Step being played (0-based in the pattern), -1 when stopped.
    int currentStep() const { return playing ? currentStepIdx : -1; }

private:
    struct Pending { double phase; bool on; uint8_t note; uint8_t vel; uint32_t id; };
    static constexpr int kMaxPending = 96;

    void schedule (const Pattern& p, int stepIdx, float swing);
    void addPending (const Pending& e);

    double sampleRate = 44100.0;
    bool playing = false;
    double phase = 0.0;    // in steps since start
    long long nextStep = 0; // next step boundary to schedule
    int currentStepIdx = -1;
    std::array<Pending, kMaxPending> pending {};
    int numPending = 0;
    std::array<uint32_t, 128> soundingId {}; // 0 = silent
    uint32_t nextId = 1;
    std::vector<NoteEvent> scratch;
};

template <typename OnCycle>
void Sequencer::process (int numSamples, double bpm, Rate rate, float swing, Pattern& pattern,
                         std::vector<MidiEvent>& out, OnCycle&& onCycle)
{
    if (! playing || numSamples <= 0)
        return;

    const double inc = stepsPerBeat (rate) * bpm / 60.0 / sampleRate;
    const int n = pattern.numSteps < 1 ? 1 : pattern.numSteps;

    for (int i = 0; i < numSamples; ++i)
    {
        while (phase >= (double) nextStep)
        {
            const int idx = (int) (nextStep % n);
            if (idx == 0 && nextStep > 0)
                onCycle (pattern);
            currentStepIdx = idx;
            schedule (pattern, idx, swing);
            ++nextStep;
        }

        // Fire everything due; offs first so a repeated pitch is not cut by its own off.
        for (int pass = 0; pass < 2; ++pass)
        {
            const bool wantOn = pass == 1;
            for (int k = 0; k < numPending;)
            {
                const Pending e = pending[(size_t) k];
                if (e.on != wantOn || e.phase > phase)
                {
                    ++k;
                    continue;
                }
                if (e.on)
                {
                    if (soundingId[e.note] != 0) // an overlapping earlier note: end it first
                        out.push_back ({ i, 0x80, e.note, 0 });
                    out.push_back ({ i, 0x90, e.note, e.vel });
                    soundingId[e.note] = e.id;
                }
                else if (soundingId[e.note] == e.id) // its note was not retriggered meanwhile
                {
                    out.push_back ({ i, 0x80, e.note, 0 });
                    soundingId[e.note] = 0;
                }
                pending[(size_t) k] = pending[(size_t) (numPending - 1)];
                --numPending;
            }
        }

        phase += inc;
    }
}

} // namespace midinja
