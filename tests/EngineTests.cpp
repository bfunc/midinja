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

using namespace briffer;

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

static void testMidiFile (const char* path)
{
    GenSettings g;
    g.seed = 4821;
    Pattern p;
    generate (p, g);
    const auto notes = renderCycle (p, 0.2f);
    const auto bytes = writeMidiFile (notes, 4.0, 122.0, 1, "bRiffer demo", 16.0);
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
    testMidiFile (argc > 1 ? argv[1] : nullptr);

    if (failures == 0)
        std::printf ("all tests passed\n");
    else
        std::printf ("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
