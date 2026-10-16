// Engine tests: plain C++, no framework. Exit code 0 = all passed.
// With an argument, also writes a demo .mid there (CI uploads it).

#include "Engine.h"
#include "MidiFileWriter.h"
#include "Scales.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace midinja;

static int failures = 0;
#define CHECK(cond)                                                                 \
    do                                                                              \
    {                                                                               \
        if (! (cond))                                                               \
        {                                                                           \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

static int countOn (const Pattern& p)
{
    int n = 0;
    for (int i = 0; i < p.numSteps; ++i)
        n += p.steps[(size_t) i].on ? 1 : 0;
    return n;
}

static void testScales()
{
    CHECK (scales().size() >= 50);
    for (const auto& s : scales())
    {
        CHECK (! s.intervals.empty());
        CHECK (s.intervals.front() == 0);
        for (size_t i = 1; i < s.intervals.size(); ++i)
            CHECK (s.intervals[i] > s.intervals[i - 1] && s.intervals[i] < 12);
    }
    CHECK (snapToScale (61, 0, 0) == 60); // C# -> C in C major (ties go down)
    CHECK (moveByScaleSteps (60, 1, 0, 0) == 62);
    CHECK (moveByScaleSteps (60, -1, 0, 0) == 59);
}

static void testGenerateRespectsSettings()
{
    for (uint32_t seed = 1; seed <= 300; ++seed)
    {
        GenSettings g;
        g.seed = seed;
        g.root = (int) (seed % 12);
        g.scale = (int) (seed % scales().size());
        g.lowNote = 36 + (int) (seed % 12);
        g.highNote = g.lowNote + 12 + (int) (seed % 13);
        g.notes = 1 + (int) (seed % 16);
        g.lenMin = 1; g.lenMax = 3;
        g.velMin = 60; g.velMax = 100;
        g.repeatsOn = seed % 2 == 0;
        g.repChance = 0.5f;
        Pattern p;
        p.numSteps = 16;
        generate (p, g);

        CHECK (countOn (p) == g.notes);
        for (int i = 0; i < p.numSteps; ++i)
        {
            const auto& st = p.steps[(size_t) i];
            if (! st.on) continue;
            CHECK (inScale (st.note, g.root, g.scale));
            CHECK (st.note >= g.lowNote && st.note <= g.highNote);
            CHECK (st.length >= 1 && st.length <= 3);
            CHECK (st.velocity >= 60 && st.velocity <= 110); // +10 accent on beats
            CHECK (st.repeats >= 1 && st.repeats <= kMaxRepeats);
            if (! g.repeatsOn) CHECK (st.repeats == 1);
            // Monophonic: never overlaps the next note or the pattern end.
            int next = i + 1;
            while (next < p.numSteps && ! p.steps[(size_t) next].on) ++next;
            CHECK (i + st.length <= next);
        }
    }
}

static void testDeterministicSeed()
{
    GenSettings g;
    g.seed = 4821;
    Pattern a, b;
    generate (a, g);
    generate (b, g);
    for (int i = 0; i < kMaxSteps; ++i)
        CHECK (a.steps[(size_t) i] == b.steps[(size_t) i]);

    g.seed = 4822;
    Pattern c;
    generate (c, g);
    bool differs = false;
    for (int i = 0; i < kMaxSteps; ++i)
        differs |= ! (a.steps[(size_t) i] == c.steps[(size_t) i]);
    CHECK (differs);
}

static void testLocksSurviveRerolls()
{
    GenSettings g;
    Pattern p;
    generate (p, g);
    // Lock two notes and one empty step.
    p.steps[0] = { true, 52, 2, 111, 1, true };
    p.steps[5] = { true, 47, 1, 90, 1, true };
    p.steps[7] = Step {};
    p.steps[7].locked = true;
    const Pattern before = p;
    for (uint32_t s = 1; s <= 1000; ++s)
    {
        g.seed = s;
        generate (p, g);
        CHECK (p.steps[0].note == 52 && p.steps[0].on && p.steps[0].locked);
        CHECK (p.steps[5] == before.steps[5]);
        CHECK (! p.steps[7].on && p.steps[7].locked);
        CHECK (countOn (p) == g.notes); // locked notes count toward the total
    }
}

static void testStepEventsAndSwing()
{
    Pattern p;
    p.steps[1] = { true, 60, 2, 100, 1, false };
    p.steps[2] = { true, 62, 1, 100, 3, false };
    std::vector<NoteEvent> ev;
    stepEvents (p, 1, 1.0f, ev);
    CHECK (ev.size() == 1);
    CHECK (ev[0].start == 0.5); // full swing delays odd steps by half a step
    stepEvents (p, 2, 1.0f, ev);
    CHECK (ev.size() == 4); // 1 + 3 ratchets
    CHECK (ev[1].start == 0.0);
}

// Runs the real-time sequencer for 4 bars at 120 BPM and checks timing and note pairing.
static void testSequencerTiming()
{
    const double sr = 48000.0;
    Pattern p;
    p.numSteps = 16;
    for (int i = 0; i < 16; i += 4)
        p.steps[(size_t) i] = { true, 48 + i, 1, 100, 1, false };
    p.steps[6] = { true, 70, 1, 100, 2, false }; // a ratchet

    Sequencer seq;
    seq.prepare (sr);
    seq.start();
    std::vector<MidiEvent> out;
    std::vector<std::pair<long long, MidiEvent>> all;
    long long t = 0;
    const int block = 480;
    // 4 bars at 120 BPM = 8 s; stop half a step early so the 5th cycle never starts.
    const long long total = (long long) (sr * 8.0) - 3000;
    int cycles = 0;
    while (t < total)
    {
        out.clear();
        seq.process (block, 120.0, Rate::Sixteenth, 0.0f, p, out, [&cycles] (Pattern&) { ++cycles; });
        for (const auto& e : out)
            all.push_back ({ t + e.sampleOffset, e });
        t += block;
    }
    out.clear();
    seq.stop (out);
    for (const auto& e : out)
        all.push_back ({ t, e });

    CHECK (cycles == 3); // called at the start of cycles 2, 3, 4

    const double stepSamples = sr * 60.0 / 120.0 / 4.0; // 6000
    std::map<int, int> open;
    int ons = 0;
    for (const auto& [time, e] : all)
    {
        if (e.status == 0x90)
        {
            ++ons;
            const double steps = (double) time / stepSamples;
            const double frac = steps - (long long) steps;
            // Ons land on the grid, or halfway for the ratchet's second hit.
            CHECK (frac < 1e-3 || std::abs (frac - 0.5) < 1e-3 || frac > 1.0 - 1e-3);
            CHECK (open[e.data1] == 0);
            open[e.data1]++;
        }
        else if (e.status == 0x80)
        {
            CHECK (open[e.data1] == 1);
            open[e.data1]--;
        }
    }
    CHECK (ons == 4 * 6); // per bar: 4 notes + 2 ratchet hits
    for (const auto& [note, count] : open)
        CHECK (count == 0); // every on has an off
}


static std::vector<int> onSteps (const Pattern& p)
{
    std::vector<int> v;
    for (int i = 0; i < p.numSteps; ++i)
        if (p.steps[(size_t) i].on) v.push_back (i);
    return v;
}

static void checkBasics (const Pattern& p, const GenSettings& g)
{
    for (int i = 0; i < p.numSteps; ++i)
    {
        const auto& st = p.steps[(size_t) i];
        if (! st.on) continue;
        CHECK (inScale (st.note, g.root, g.scale));
        CHECK (st.note >= g.lowNote && st.note <= g.highNote);
        CHECK (st.velocity >= 1 && st.velocity <= 127);
        int next = i + 1;
        while (next < p.numSteps && ! p.steps[(size_t) next].on) ++next;
        CHECK (i + st.length <= next);
    }
}

static void testModeInfo()
{
    auto idx = [] (const char* name) {
        for (size_t i = 0; i < scales().size(); ++i)
            if (std::string (scales()[i].name) == name) return (int) i;
        return -1;
    };
    CHECK (modeInfo (idx ("Dorian")).color == 9);
    CHECK (modeInfo (idx ("Phrygian")).color == 1);
    CHECK (modeInfo (idx ("Lydian")).color == 6);
    CHECK (modeInfo (idx ("Mixolydian")).color == 10);
    CHECK (modeInfo (idx ("Natural Minor")).color == 8);
    CHECK (modeInfo (idx ("Major (Ionian)")).color == 11);
    CHECK (modeInfo (idx ("Harmonic Minor")).color == 11);
    CHECK (modeInfo (idx ("Locrian")).color == 1); // b5 is its fifth already
    CHECK (modeInfo (idx ("Minor Pentatonic")).color == -1);
    CHECK (modeInfo (idx ("Dorian")).third == 3 && modeInfo (idx ("Dorian")).fifth == 7);
    CHECK (musicianCellLength (16) == 4 && musicianCellLength (32) == 8 && musicianCellLength (8) == 4);
}

static void testMusician()
{
    int strongTotal = 0, strongChord = 0, endsHome = 0, motifRepeats = 0, withColor = 0, runs = 0;
    for (uint32_t seed = 1; seed <= 400; ++seed)
    {
        GenSettings g;
        g.mode = kMusician;
        g.seed = seed;
        g.root = (int) (seed % 12);
        g.scale = (int) (seed % 9); // the 7 modes + harmonic / melodic minor
        g.notes = 6 + (int) (seed % 9);
        g.surprise = 0.0f;
        g.color = 0.5f;
        Pattern p;
        p.numSteps = 16;
        generate (p, g);
        checkBasics (p, g);
        CHECK ((int) onSteps (p).size() == g.notes);

        const auto mi = modeInfo (g.scale);
        const auto on = onSteps (p);
        for (int s : on)
        {
            const int pc = ((p.steps[(size_t) s].note - g.root) % 12 + 12) % 12;
            if (s % 4 == 0)
            {
                ++strongTotal;
                strongChord += (pc == 0 || pc == mi.third || pc == mi.fifth) ? 1 : 0;
            }
            withColor += pc == mi.color ? 1 : 0;
        }
        const int lastPc = ((p.steps[(size_t) on.back()].note - g.root) % 12 + 12) % 12;
        endsHome += (lastPc == 0 || lastPc == mi.fifth) ? 1 : 0;
        // A and the third cell (A again) share their rhythm.
        bool same = true;
        for (int o = 0; o < 4; ++o)
            same &= p.steps[(size_t) o].on == p.steps[(size_t) (8 + o)].on;
        motifRepeats += same ? 1 : 0;
        ++runs;
    }
    CHECK (strongChord * 100 >= strongTotal * 85); // A' and the ending may leave the triad
    CHECK (endsHome == runs);
    CHECK (motifRepeats * 100 >= runs * 70);
    CHECK (withColor > 0);

    // Deterministic, and locks hold in Musician mode too.
    GenSettings g;
    g.mode = kMusician;
    Pattern a, b;
    generate (a, g);
    generate (b, g);
    for (int i = 0; i < kMaxSteps; ++i)
        CHECK (a.steps[(size_t) i] == b.steps[(size_t) i]);
    a.steps[3] = { true, 50, 1, 100, 1, true };
    for (uint32_t s = 1; s <= 300; ++s)
    {
        g.seed = s;
        generate (a, g);
        CHECK (a.steps[3].note == 50 && a.steps[3].on && a.steps[3].locked);
        CHECK ((int) onSteps (a).size() == g.notes);
    }
}

static void testRhythmNotesMutate()
{
    for (int mode = 0; mode < 2; ++mode)
        for (uint32_t seed = 1; seed <= 200; ++seed)
        {
            GenSettings g;
            g.mode = mode;
            g.seed = seed;
            Pattern p;
            generate (p, g);
            const auto onBefore = onSteps (p);

            // Notes only: same positions.
            Pattern q = p;
            g.seed = seed + 1000;
            generateNotes (q, g);
            CHECK (onSteps (q) == onBefore);
            checkBasics (q, g);

            // Rhythm only: same pitches in the same order, same count.
            Pattern r = p;
            generateRhythm (r, g);
            const auto onR = onSteps (r);
            CHECK (onR.size() == onBefore.size());
            for (size_t k = 0; k < onR.size() && k < onBefore.size(); ++k)
                CHECK (r.steps[(size_t) onR[k]].note == p.steps[(size_t) onBefore[k]].note);
            checkBasics (r, g);

            // Mutate: same count, stays in the scale and range; 0 changes nothing.
            Pattern m = p;
            mutate (m, g, 0.25f);
            CHECK (onSteps (m).size() == onBefore.size());
            checkBasics (m, g);
            Pattern z = p;
            mutate (z, g, 0.0f);
            for (int i = 0; i < kMaxSteps; ++i)
                CHECK (z.steps[(size_t) i] == p.steps[(size_t) i]);
        }
}


static int fourthPc (int scale)
{
    return scales()[(size_t) scale].intervals[(size_t) fourthDegree (scale)];
}

static void testForm()
{
    int runs = 0, aPrimeDiffers = 0, aPrimeClose = 0, bStrongTotal = 0, bStrongIV = 0, bRhythmDiffers = 0, cRoot = 0;
    for (uint32_t seed = 1; seed <= 300; ++seed)
    {
        GenSettings g;
        g.mode = (int) (seed % 2);
        g.seed = seed;
        g.root = (int) (seed % 12);
        g.scale = (int) (seed % 9);
        g.notes = 6 + (int) (seed % 8);
        g.similarity = 0.85f;
        g.surprise = 0.0f;
        std::array<Pattern, kNumParts> parts;
        for (auto& p : parts)
            p.numSteps = 16;
        generateForm (parts, 0, g);
        for (const auto& p : parts)
        {
            checkBasics (p, g);
            CHECK (countOn (p) > 0);
        }
        const Pattern& a = parts[0];
        const Pattern& a2 = parts[1];
        const Pattern& b = parts[2];
        const Pattern& c = parts[3];

        int same = 0, onA = 0;
        bool differs = false;
        for (int i = 0; i < 16; ++i)
        {
            if (a.steps[(size_t) i].on) { ++onA; same += (a2.steps[(size_t) i].on && a2.steps[(size_t) i].note == a.steps[(size_t) i].note) ? 1 : 0; }
            differs |= ! (a.steps[(size_t) i] == a2.steps[(size_t) i]);
        }
        aPrimeClose += same * 2 >= onA ? 1 : 0;
        aPrimeDiffers += differs ? 1 : 0;

        if (g.mode == kMusician)
        {
            const auto& iv = scales()[(size_t) g.scale].intervals;
            const int d = fourthDegree (g.scale), sz = (int) iv.size();
            const int pcs[3] = { iv[(size_t) d], iv[(size_t) ((d + 2) % sz)], iv[(size_t) ((d + 4) % sz)] };
            for (int s : onSteps (b))
                if (s % 4 == 0)
                {
                    ++bStrongTotal;
                    const int pc = ((b.steps[(size_t) s].note - g.root) % 12 + 12) % 12;
                    bStrongIV += (pc == pcs[0] || pc == pcs[1] || pc == pcs[2]) ? 1 : 0;
                }
        }
        bool sameRhythm = true;
        for (int i = 0; i < 16; ++i)
            sameRhythm &= a.steps[(size_t) i].on == b.steps[(size_t) i].on;
        bRhythmDiffers += sameRhythm ? 0 : 1;

        const auto onC = onSteps (c);
        cRoot += ((c.steps[(size_t) onC.back()].note - g.root) % 12 + 12) % 12 == 0 ? 1 : 0;
        ++runs;
    }
    CHECK (cRoot == runs);
    CHECK (aPrimeDiffers * 100 >= runs * 85);
    CHECK (aPrimeClose * 100 >= runs * 60);
    CHECK (bRhythmDiffers * 100 >= runs * 90);
    CHECK (bStrongIV * 100 >= bStrongTotal * 60);
    CHECK (fourthPc (0) == 5 && fourthPc (9) == 5 && fourthPc (13) == 5); // major, minor pentatonic, chromatic

    // Lower similarity moves A' further from A.
    auto closeness = [] (float similarity) {
        double sum = 0.0;
        for (uint32_t seed = 1; seed <= 200; ++seed)
        {
            GenSettings g;
            g.mode = kMusician;
            g.seed = seed;
            g.similarity = similarity;
            std::array<Pattern, kNumParts> parts;
            for (auto& p : parts) p.numSteps = 16;
            generateForm (parts, 0, g);
            int same = 0, onA = 0;
            for (int i = 0; i < 16; ++i)
                if (parts[0].steps[(size_t) i].on) { ++onA; same += (parts[1].steps[(size_t) i].on && parts[1].steps[(size_t) i].note == parts[0].steps[(size_t) i].note) ? 1 : 0; }
            sum += onA > 0 ? (double) same / onA : 1.0;
        }
        return sum / 200.0;
    };
    CHECK (closeness (0.9f) > closeness (0.1f) + 0.15);

    // Deterministic; locked parts and locked steps are kept.
    GenSettings g;
    g.seed = 77;
    std::array<Pattern, kNumParts> x, y;
    for (auto& p : x) p.numSteps = 16;
    for (auto& p : y) p.numSteps = 16;
    generateForm (x, 0, g);
    generateForm (y, 0, g);
    for (int k = 0; k < kNumParts; ++k)
        for (int i = 0; i < kMaxSteps; ++i)
            CHECK (x[(size_t) k].steps[(size_t) i] == y[(size_t) k].steps[(size_t) i]);
    const Pattern keepA = x[0];
    x[3].steps[2] = { true, 45, 1, 99, 1, true };
    for (uint32_t seed = 1; seed <= 100; ++seed)
    {
        g.seed = seed;
        generateForm (x, 1u << kPartA, g);
        for (int i = 0; i < kMaxSteps; ++i)
            CHECK (x[0].steps[(size_t) i] == keepA.steps[(size_t) i]);
        CHECK (x[3].steps[2].on && x[3].steps[2].note == 45 && x[3].steps[2].locked);
    }
    // A locked last note is not moved by the cadence.
    Pattern p;
    p.numSteps = 16;
    p.steps[15] = { true, 47, 1, 100, 1, true };
    cadence (p, g);
    CHECK (p.steps[15].note == 47);
}

static void testDetectKey()
{
    int rootOk = 0, setOk = 0, compatible = 0, runs = 0;
    for (uint32_t seed = 1; seed <= 200; ++seed)
    {
        GenSettings g;
        g.mode = kMusician;
        g.seed = seed;
        g.root = (int) (seed % 12);
        g.scale = (int) (seed % 7);
        g.notes = 12;
        g.surprise = 0.0f;
        Pattern p;
        p.numSteps = 16;
        generate (p, g);
        std::vector<WeightedNote> w;
        const auto on = onSteps (p);
        for (size_t j = 0; j < on.size(); ++j)
        {
            const auto& st = p.steps[(size_t) on[j]];
            float weight = (float) st.length * (on[j] % 4 == 0 ? 2.0f : 1.0f);
            if (j == 0) weight += 1.0f;
            if (j + 1 == on.size()) weight += 2.0f;
            w.push_back ({ st.note, weight });
        }
        const KeyGuess k = detectKey (w);
        rootOk += k.root == g.root ? 1 : 0;
        bool same = true;
        for (int pc = 0; pc < 12; ++pc)
            same &= inScale (pc, g.root, g.scale) == inScale (pc, k.root, k.scale);
        setOk += same ? 1 : 0;
        bool explains = true;
        for (int s : on)
            explains &= inScale (p.steps[(size_t) s].note, k.root, k.scale);
        compatible += explains ? 1 : 0;
        CHECK (k.confidence > 0.0f && k.confidence <= 1.0f);
        ++runs;
    }
    // Short riffs often leave out the note that tells two modes apart, so the pitch set is only a
    // guess; the root should mostly be right and the guess must explain the notes.
    CHECK (rootOk * 100 >= runs * 60);
    CHECK (setOk * 100 >= runs * 55);
    CHECK (compatible * 100 >= runs * 95);
    CHECK (detectKey ({}).confidence == 0.0f);
    // A plain C major scale with C emphasised.
    std::vector<WeightedNote> cmaj;
    for (int n : { 60, 62, 64, 65, 67, 69, 71, 72 })
        cmaj.push_back ({ n, n % 12 == 0 ? 3.0f : 1.0f });
    const KeyGuess k = detectKey (cmaj);
    CHECK (k.root == 0 && k.scale == 0);
}

static void testMidiFile (const char* path)
{
    GenSettings g;
    g.seed = 4821;
    Pattern p;
    generate (p, g);
    const auto notes = renderCycle (p, 0.2f);
    const auto bytes = writeMidiFile (notes, 4.0, 122.0, 1, "Midinja demo", 16.0);
    CHECK (bytes.size() > 30);
    CHECK (bytes[0] == 'M' && bytes[1] == 'T' && bytes[2] == 'h' && bytes[3] == 'd');
    CHECK (bytes[bytes.size() - 3] == 0xFF && bytes[bytes.size() - 2] == 0x2F);
    if (path != nullptr)
    {
        std::ofstream f (path, std::ios::binary);
        f.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
        std::printf ("wrote %s (%zu notes)\n", path, notes.size());
    }
}

int main (int argc, char** argv)
{
    testScales();
    testGenerateRespectsSettings();
    testDeterministicSeed();
    testLocksSurviveRerolls();
    testStepEventsAndSwing();
    testSequencerTiming();
    testModeInfo();
    testMusician();
    testRhythmNotesMutate();
    testForm();
    testDetectKey();
    testMidiFile (argc > 1 ? argv[1] : nullptr);

    if (failures == 0)
        std::printf ("all tests passed\n");
    else
        std::printf ("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
