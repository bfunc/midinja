// Form A A' B C: four one-bar parts that belong together. A is the motif, A' a variation of it,
// B a contrasting bar on the IV degree, C a bar that cadences to the root so the loop closes.
// Also the key guess used when a MIDI file is imported.

#include "Engine.h"
#include "Scales.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace midinja
{

int fourthDegree (int scale)
{
    const auto& list = scales();
    if (scale < 0 || scale >= (int) list.size())
        scale = 0;
    const auto& iv = list[(size_t) scale].intervals;
    if (iv.size() < 2)
        return 0;
    int best = 1;
    for (size_t d = 1; d < iv.size(); ++d)
        if (std::abs (iv[d] - 5) < std::abs (iv[(size_t) best] - 5))
            best = (int) d;
    return best;
}

namespace
{

int pcOf (int note, int root) { return ((note - root) % 12 + 12) % 12; }

int lastOn (const Pattern& p)
{
    for (int i = std::min (p.numSteps, kMaxSteps) - 1; i >= 0; --i)
        if (p.steps[(size_t) i].on)
            return i;
    return -1;
}

int prevOn (const Pattern& p, int before)
{
    for (int i = before - 1; i >= 0; --i)
        if (p.steps[(size_t) i].on)
            return i;
    return -1;
}

std::vector<int> onSteps (const Pattern& p)
{
    std::vector<int> v;
    for (int i = 0; i < std::min (p.numSteps, kMaxSteps); ++i)
        if (p.steps[(size_t) i].on)
            v.push_back (i);
    return v;
}

// Nearest note of pitch class `pc` (from root) inside [low, high]; the note itself if none.
int nearestOfClass (int note, int pc, int root, int low, int high)
{
    int best = -1;
    for (int n = std::max (0, low); n <= std::min (127, high); ++n)
        if (pcOf (n, root) == pc && (best < 0 || std::abs (n - note) < std::abs (best - note)))
            best = n;
    return best < 0 ? note : best;
}

// A's notes copied into the unlocked steps of p. A's own locks do not travel.
void copyFrom (Pattern& p, const Pattern& a, int n)
{
    for (int i = 0; i < n; ++i)
    {
        auto& st = p.steps[(size_t) i];
        if (st.locked)
            continue;
        st = i < std::min (a.numSteps, kMaxSteps) ? a.steps[(size_t) i] : Step {};
        st.locked = false;
    }
}

bool sameRhythm (const Pattern& a, const Pattern& b, int n)
{
    for (int i = 0; i < n; ++i)
        if (a.steps[(size_t) i].on != b.steps[(size_t) i].on)
            return false;
    return true;
}

// A': one of three techniques, picked by how similar the result should be, then a little mutation.
void variation (Pattern& p, const Pattern& a, const GenSettings& g)
{
    Rng rng (g.seed ^ 0x41505249u);
    const int n = std::clamp (p.numSteps, 1, kMaxSteps);
    p.numSteps = n;
    const int low = std::min (g.lowNote, g.highNote), high = std::max (g.lowNote, g.highNote);
    copyFrom (p, a, n);
    const float s = std::clamp (g.similarity, 0.0f, 1.0f);

    enum { kNewTail, kSequence, kNewNotes };
    const float r = rng.unit();
    int tech;
    if (s >= 0.66f)      tech = r < 0.7f ? kNewTail : kSequence;
    else if (s >= 0.33f) tech = r < 0.5f ? kSequence : (r < 0.8f ? kNewTail : kNewNotes);
    else                 tech = r < 0.6f ? kNewNotes : kSequence;

    GenSettings g2 = g;
    g2.seed = rng.next();

    if (tech == kNewTail)
    {
        // The first three quarters stay A; the last beat gets new notes (sometimes a new rhythm).
        const int tail = std::max (1, n / 4);
        std::vector<char> wasLocked ((size_t) n, 0);
        for (int i = 0; i < n - tail; ++i)
        {
            wasLocked[(size_t) i] = p.steps[(size_t) i].locked;
            p.steps[(size_t) i].locked = true;
        }
        if (rng.unit() < 0.5f)
            generate (p, g2);
        else
            generateNotes (p, g2);
        for (int i = 0; i < n - tail; ++i)
            p.steps[(size_t) i].locked = wasLocked[(size_t) i] != 0;
    }
    else if (tech == kSequence)
    {
        // The whole phrase moves a scale step up or down.
        const int d = rng.unit() < 0.5f ? -1 : 1;
        for (int i = 0; i < n; ++i)
        {
            auto& st = p.steps[(size_t) i];
            if (! st.on || st.locked)
                continue;
            int moved = moveByScaleSteps (st.note, d, g.root, g.scale);
            if (moved < low || moved > high)
                moved = moveByScaleSteps (st.note, -d, g.root, g.scale);
            st.note = std::clamp (snapToScale (moved, g.root, g.scale), low, high);
        }
    }
    else
    {
        // Same rhythm, new notes, same contour: A's ups and downs from a new starting note.
        const auto pool = scaleNotes (g.root, g.scale, low, high);
        if (! pool.empty())
        {
            auto indexOf = [&] (int note) {
                size_t best = 0;
                for (size_t k = 0; k < pool.size(); ++k)
                    if (std::abs (pool[k] - note) < std::abs (pool[best] - note))
                        best = k;
                return (int) best;
            };
            const ModeInfo mi = modeInfo (g.scale);
            auto isChordTone = [&] (int note) {
                const int pc = pcOf (note, g.root);
                return pc == 0 || pc == mi.third || pc == mi.fifth;
            };
            const auto on = onSteps (p);
            const auto onA = onSteps (a);
            int prevIdx = -1, prevIdxA = -1;
            for (size_t j = 0; j < on.size(); ++j)
            {
                auto& st = p.steps[(size_t) on[j]];
                const int idxA = j < onA.size() ? indexOf (a.steps[(size_t) onA[j]].note) : indexOf (st.note);
                int idx;
                if (prevIdx < 0)
                    idx = std::clamp (indexOf (st.note) + rng.range (-2, 2), 0, (int) pool.size() - 1);
                else
                {
                    int delta = idxA - prevIdxA;
                    if (delta != 0 && rng.unit() < 0.35f)
                        delta += delta > 0 ? 1 : -1; // the shape, stretched a little
                    idx = std::clamp (prevIdx + delta, 0, (int) pool.size() - 1);
                }
                int note = pool[(size_t) idx];
                if (g.mode == kMusician && on[j] % 4 == 0)
                {
                    int best = -1;
                    for (int x : pool)
                        if (isChordTone (x) && (best < 0 || std::abs (x - note) < std::abs (best - note)))
                            best = x;
                    if (best >= 0)
                        note = best;
                }
                if (! st.locked)
                    st.note = note;
                prevIdx = indexOf (st.note);
                prevIdxA = idxA;
            }
        }
    }

    // The further from A, the more the rest moves too.
    GenSettings g3 = g;
    g3.seed = rng.next();
    mutate (p, g3, (1.0f - s) * 0.5f);
    fitLengths (p);
}

// B: a new bar whose chord tones sit on the IV degree, with a rhythm that is not A's.
void contrast (Pattern& p, const Pattern& a, const GenSettings& g)
{
    GenSettings g2 = g;
    g2.degree = fourthDegree (g.scale);
    Rng rng (g.seed ^ 0x42424242u);
    const int n = std::clamp (p.numSteps, 1, kMaxSteps);
    const int shift = rng.unit() < 0.5f ? -2 : 2;
    g2.notes = std::clamp (g.notes + shift, 1, n);
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        g2.seed = rng.next();
        generate (p, g2);
        if (! sameRhythm (p, a, n))
            break;
    }
}

// C: A's rhythm with new notes (so the loop feels whole) or a new bar, then the cadence.
void closing (Pattern& p, const Pattern& a, const GenSettings& g)
{
    Rng rng (g.seed ^ 0x43434343u);
    const int n = std::clamp (p.numSteps, 1, kMaxSteps);
    p.numSteps = n;
    GenSettings g2 = g;
    g2.seed = rng.next();
    if (rng.unit() < 0.6f && lastOn (a) >= 0)
    {
        copyFrom (p, a, n);
        generateNotes (p, g2);
    }
    else
        generate (p, g2);
    cadence (p, g);
    fitLengths (p);
}

} // namespace

void cadence (Pattern& p, const GenSettings& g)
{
    const int last = lastOn (p);
    if (last < 0 || p.steps[(size_t) last].locked)
        return;
    const int low = std::min (g.lowNote, g.highNote), high = std::max (g.lowNote, g.highNote);
    auto& l = p.steps[(size_t) last];
    l.note = nearestOfClass (l.note, 0, g.root, low, high);

    const int prev = prevOn (p, last);
    if (prev < 0 || p.steps[(size_t) prev].locked)
        return;
    auto& q = p.steps[(size_t) prev];
    if (pcOf (q.note, g.root) != 0)
        return; // already an approach note of some kind
    const int up = moveByScaleSteps (l.note, 1, g.root, g.scale);
    const int down = moveByScaleSteps (l.note, -1, g.root, g.scale);
    const bool upOk = up >= low && up <= high, downOk = down >= low && down <= high;
    if (upOk && downOk)
        q.note = std::abs (up - q.note) < std::abs (down - q.note) ? up : down;
    else if (upOk)
        q.note = up;
    else if (downOk)
        q.note = down;
}

void generatePart (Pattern& p, int part, const Pattern& a, const GenSettings& g)
{
    switch (part)
    {
        case kPartAPrime: variation (p, a, g); break;
        case kPartB:      contrast (p, a, g); break;
        case kPartC:      closing (p, a, g); break;
        default:          generate (p, g); break;
    }
}

void generateForm (std::array<Pattern, kNumParts>& parts, unsigned lockedParts, const GenSettings& g)
{
    for (int k = 0; k < kNumParts; ++k)
    {
        if ((lockedParts >> k) & 1u)
            continue;
        GenSettings gk = g;
        if (k > 0)
            gk.seed = g.seed ^ (0x9E3779B9u * (uint32_t) k);
        generatePart (parts[(size_t) k], k, parts[0], gk);
    }
}

KeyGuess detectKey (const std::vector<WeightedNote>& notes)
{
    KeyGuess best;
    float pcw[12] = {};
    float total = 0.0f;
    for (const auto& w : notes)
    {
        if (w.weight <= 0.0f)
            continue;
        pcw[(w.note % 12 + 12) % 12] += w.weight;
        total += w.weight;
    }
    if (total <= 0.0f)
        return best;

    const auto& list = scales();
    float bestScore = -1e9f, bestIn = 0.0f;
    for (size_t s = 0; s < list.size(); ++s)
    {
        const auto& iv = list[s].intervals;
        const ModeInfo mi = modeInfo ((int) s);
        for (int r = 0; r < 12; ++r)
        {
            float in = 0.0f, out = 0.0f;
            for (int pc = 0; pc < 12; ++pc)
            {
                const int rel = (pc - r + 12) % 12;
                const bool has = std::find (iv.begin(), iv.end(), rel) != iv.end();
                (has ? in : out) += pcw[pc];
            }
            float score = in - 2.0f * out;
            score += 1.5f * pcw[r]; // the root is what a riff leans on; the third would favour the relative key
            if (mi.fifth >= 0) score += 0.4f * pcw[(size_t) ((r + mi.fifth) % 12)];
            score -= 0.03f * total * (float) std::max<size_t> (0, iv.size() - 7); // chromatic explains anything
            if (s >= 9)      score -= 0.12f * total; // the seven modes first, then harmonic / melodic minor
            else if (s >= 7) score -= 0.04f * total;
            if (score > bestScore)
            {
                bestScore = score;
                bestIn = in;
                best.root = r;
                best.scale = (int) s;
            }
        }
    }
    best.confidence = std::clamp (bestIn / total * (0.5f + 0.5f * std::min (1.0f, pcw[best.root] * 4.0f / total)), 0.0f, 1.0f);
    return best;
}

} // namespace midinja
