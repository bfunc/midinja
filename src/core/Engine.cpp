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

void Sequencer::seek (double steps)
{
    phase = steps < 0.0 ? 0.0 : steps;
    nextStep = (long long) std::ceil (phase - 1.0e-9);
    currentStepIdx = (int) std::floor (phase);
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
