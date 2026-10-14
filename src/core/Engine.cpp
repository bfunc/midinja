#include "Engine.h"
#include "Scales.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace midinja
{

const char* rateName (Rate r)
{
    switch (r)
    {
        case Rate::Eighth:           return "1/8";
        case Rate::Sixteenth:        return "1/16";
        case Rate::ThirtySecond:     return "1/32";
        case Rate::SixteenthTriplet: return "1/16T";
    }
    return "1/16";
}

double stepsPerBeat (Rate r)
{
    switch (r)
    {
        case Rate::Eighth:           return 2.0;
        case Rate::Sixteenth:        return 4.0;
        case Rate::ThirtySecond:     return 8.0;
        case Rate::SixteenthTriplet: return 6.0;
    }
    return 4.0;
}

static const std::vector<int>& intervalsOf (int scale)
{
    const auto& list = scales();
    if (scale < 0 || scale >= (int) list.size())
        scale = 0;
    return list[(size_t) scale].intervals;
}

bool inScale (int note, int root, int scale)
{
    const int pc = ((note - root) % 12 + 12) % 12;
    const auto& iv = intervalsOf (scale);
    return std::find (iv.begin(), iv.end(), pc) != iv.end();
}

std::vector<int> scaleNotes (int root, int scale, int low, int high)
{
    std::vector<int> out;
    for (int n = std::max (0, low); n <= std::min (127, high); ++n)
        if (inScale (n, root, scale))
            out.push_back (n);
    return out;
}

int snapToScale (int note, int root, int scale)
{
    note = std::clamp (note, 0, 127);
    for (int d = 0; d < 12; ++d)
    {
        if (note - d >= 0 && inScale (note - d, root, scale)) return note - d;
        if (note + d <= 127 && inScale (note + d, root, scale)) return note + d;
    }
    return note;
}

int moveByScaleSteps (int note, int steps, int root, int scale)
{
    int n = snapToScale (note, root, scale);
    const int dir = steps > 0 ? 1 : -1;
    for (int s = 0; s != steps; s += dir)
    {
        int m = n + dir;
        while (m >= 0 && m <= 127 && ! inScale (m, root, scale))
            m += dir;
        if (m < 0 || m > 127)
            break;
        n = m;
    }
    return n;
}

void fitLengths (Pattern& p)
{
    const int n = std::clamp (p.numSteps, 1, kMaxSteps);
    for (int i = 0; i < n; ++i)
    {
        auto& st = p.steps[(size_t) i];
        if (! st.on)
            continue;
        int next = i + 1;
        while (next < n && ! p.steps[(size_t) next].on)
            ++next;
        st.length = std::clamp (st.length, 1, std::min (kMaxLength, next - i));
    }
}

void generate (Pattern& p, const GenSettings& g)
{
    Rng rng (g.seed);
    const int n = std::clamp (p.numSteps, 1, kMaxSteps);
    p.numSteps = n;

    int low = std::min (g.lowNote, g.highNote), high = std::max (g.lowNote, g.highNote);
    auto pool = scaleNotes (g.root, g.scale, low, high);
    if (pool.empty())
        pool.push_back (snapToScale (low, g.root, g.scale));

    std::vector<int> roots;
    for (int note : pool)
        if (((note - g.root) % 12 + 12) % 12 == 0)
            roots.push_back (note);

    // Clear the unlocked steps; count the locked notes.
    int lockedOn = 0;
    std::vector<int> free;
    for (int i = 0; i < n; ++i)
    {
        auto& st = p.steps[(size_t) i];
        if (st.locked)
        {
            if (st.on) ++lockedOn;
            continue;
        }
        st = Step {};
        free.push_back (i);
    }

    // Pick positions: weighted toward beats and 8ths, without replacement.
    int toPlace = std::clamp (g.notes, 0, n) - lockedOn;
    toPlace = std::clamp (toPlace, 0, (int) free.size());
    std::vector<int> chosen;
    while ((int) chosen.size() < toPlace)
    {
        double total = 0.0;
        for (int i : free)
            total += (i % 4 == 0) ? 3.0 : (i % 2 == 0 ? 1.8 : 1.0);
        double r = rng.unit() * total;
        size_t pick = 0;
        for (size_t k = 0; k < free.size(); ++k)
        {
            const int i = free[k];
            r -= (i % 4 == 0) ? 3.0 : (i % 2 == 0 ? 1.8 : 1.0);
            if (r <= 0.0) { pick = k; break; }
            pick = k;
        }
        chosen.push_back (free[pick]);
        free.erase (free.begin() + (long) pick);
    }
    std::sort (chosen.begin(), chosen.end());

    // Pitches: a walk over the scale with root anchors and the odd leap.
    auto indexOf = [&] (int note) {
        size_t best = 0;
        for (size_t k = 0; k < pool.size(); ++k)
            if (std::abs (pool[k] - note) < std::abs (pool[best] - note))
                best = k;
        return (int) best;
    };
    int cur = roots.empty() ? (int) pool.size() / 2 : indexOf (roots[(size_t) rng.range (0, (int) roots.size() - 1)]);

    bool first = true;
    for (int i : chosen)
    {
        auto& st = p.steps[(size_t) i];
        const float r = rng.unit();
        if ((first && r < 0.5f) || (! first && r < 0.22f && ! roots.empty()))
        {
            cur = indexOf (roots[(size_t) rng.range (0, (int) roots.size() - 1)]);
        }
        else if (r < 0.37f)
        {
            cur = rng.range (0, (int) pool.size() - 1); // leap
        }
        else
        {
            static const int moves[] = { -2, -1, -1, 0, 1, 1, 2 };
            cur = std::clamp (cur + moves[rng.range (0, 6)], 0, (int) pool.size() - 1);
        }
        first = false;

        st.on = true;
        st.note = pool[(size_t) cur];
        st.length = rng.range (std::max (1, std::min (g.lenMin, g.lenMax)), std::min (kMaxLength, std::max (g.lenMin, g.lenMax)));
        int vel = rng.range (std::min (g.velMin, g.velMax), std::max (g.velMin, g.velMax));
        if (i % 4 == 0)
            vel += 10; // accent on the beat
        st.velocity = std::clamp (vel, 1, 127);
        st.repeats = 1;
        if (g.repeatsOn && rng.unit() < g.repChance)
            st.repeats = rng.range (std::clamp (std::min (g.repMin, g.repMax), 1, kMaxRepeats),
                                    std::clamp (std::max (g.repMin, g.repMax), 1, kMaxRepeats));
    }

    fitLengths (p);
}

void transposeScaleSteps (Pattern& p, int steps, int root, int scale)
{
    for (int i = 0; i < p.numSteps; ++i)
        if (p.steps[(size_t) i].on)
            p.steps[(size_t) i].note = moveByScaleSteps (p.steps[(size_t) i].note, steps, root, scale);
}

void transposeSemitones (Pattern& p, int semitones)
{
    for (int i = 0; i < p.numSteps; ++i)
        if (p.steps[(size_t) i].on)
            p.steps[(size_t) i].note = std::clamp (p.steps[(size_t) i].note + semitones, 0, 127);
}

void conformToScale (Pattern& p, int oldRoot, int newRoot, int newScale)
{
    int d = ((newRoot - oldRoot) % 12 + 12) % 12;
    if (d > 6)
        d -= 12; // move the shorter way
    for (int i = 0; i < kMaxSteps; ++i)
    {
        auto& st = p.steps[(size_t) i];
        st.note = snapToScale (std::clamp (st.note + d, 0, 127), newRoot, newScale);
    }
}

double swingOffset (int stepIndex, float swing)
{
    return (stepIndex % 2 == 1) ? (double) std::clamp (swing, 0.0f, 1.0f) * 0.5 : 0.0;
}

void stepEvents (const Pattern& p, int stepIndex, float swing, std::vector<NoteEvent>& out)
{
    if (stepIndex < 0 || stepIndex >= p.numSteps)
        return;
    const auto& st = p.steps[(size_t) stepIndex];
    if (! st.on)
        return;

    const double start = swingOffset (stepIndex, swing);
    const double len = (double) std::clamp (st.length, 1, kMaxLength);
    const int reps = std::clamp (st.repeats, 1, kMaxRepeats);
    if (reps == 1)
    {
        out.push_back ({ start, len * 0.9, st.note, st.velocity });
        return;
    }
    const double sub = len / reps;
    for (int r = 0; r < reps; ++r)
        out.push_back ({ start + r * sub, sub * 0.6, st.note, std::max (1, st.velocity - (r > 0 ? 12 : 0)) });
}

std::vector<NoteEvent> renderCycle (const Pattern& p, float swing)
{
    std::vector<NoteEvent> all, one;
    for (int i = 0; i < p.numSteps; ++i)
    {
        one.clear();
        stepEvents (p, i, swing, one);
        for (auto e : one)
        {
            e.start += i;
            all.push_back (e);
        }
    }
    return all;
}

// ---------------------------------------------------------------------------------------------

void Sequencer::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    scratch.reserve (16);
}

void Sequencer::start()
{
    playing = true;
    phase = 0.0;
    nextStep = 0;
    currentStepIdx = 0;
    numPending = 0;
}

void Sequencer::stop (std::vector<MidiEvent>& out)
{
    for (int n = 0; n < 128; ++n)
        if (soundingId[(size_t) n] != 0)
        {
            out.push_back ({ 0, 0x80, (uint8_t) n, 0 });
            soundingId[(size_t) n] = 0;
        }
    out.push_back ({ 0, 0xB0, 123, 0 }); // All Notes Off
    numPending = 0;
    playing = false;
    currentStepIdx = -1;
}

void Sequencer::addPending (const Pending& e)
{
    if (numPending < kMaxPending)
        pending[(size_t) numPending++] = e;
}

void Sequencer::schedule (const Pattern& p, int stepIdx, float swing)
{
    scratch.clear();
    stepEvents (p, stepIdx, swing, scratch);
    const double base = (double) nextStep;
    for (const auto& e : scratch)
    {
        const uint32_t id = nextId++;
        if (nextId == 0)
            nextId = 1;
        addPending ({ base + e.start, true, (uint8_t) e.note, (uint8_t) std::clamp (e.velocity, 1, 127), id });
        addPending ({ base + e.start + e.length, false, (uint8_t) e.note, 0, id });
    }
}

} // namespace midinja
