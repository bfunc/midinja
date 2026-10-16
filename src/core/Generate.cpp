// The dice: Random and Musician generation, rhythm-only / notes-only re-rolls and Mutate.

#include "Engine.h"
#include "Scales.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace midinja
{

ModeInfo modeInfo (int scale)
{
    const auto& list = scales();
    if (scale < 0 || scale >= (int) list.size())
        scale = 0;
    const auto& iv = list[(size_t) scale].intervals;
    auto has = [&] (int pc) { return std::find (iv.begin(), iv.end(), pc) != iv.end(); };

    ModeInfo m;
    m.third = has (4) ? 4 : has (3) ? 3 : has (2) ? 2 : has (5) ? 5 : -1;
    m.fifth = has (7) ? 7 : has (6) ? 6 : has (8) ? 8 : -1;

    // The colour note is what the scale has and plain major (or minor, by the third) has not.
    static const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minor[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* ref = m.third == 4 ? major : minor;
    auto inRef = [&] (int pc) { return std::find (ref, ref + 7, pc) != ref + 7; };

    static const int preference[] = { 6, 1, 9, 11, 10, 8, 2, 3, 4, 5, 7 };
    for (int pc : preference)
        if (has (pc) && ! inRef (pc) && pc != m.fifth && pc != m.third)
        {
            m.color = pc;
            return m;
        }
    // Plain major and minor: the major 7th and the minor 6th give them their colour.
    const int fallback = m.third == 4 ? 11 : 8;
    if (has (fallback))
        m.color = fallback;
    return m;
}

int musicianCellLength (int numSteps)
{
    const int cells = numSteps >= 12 ? 4 : (numSteps >= 6 ? 2 : 1);
    return std::max (1, numSteps / cells);
}

namespace
{

double posWeight (int i) { return (i % 4 == 0) ? 3.0 : (i % 2 == 0 ? 1.8 : 1.0); }

int pcOf (int note, int root) { return ((note - root) % 12 + 12) % 12; }

// Weighted pick of `count` of `free` without replacement, sorted.
std::vector<int> pickPositions (Rng& rng, std::vector<int> free, int count)
{
    std::vector<int> chosen;
    while ((int) chosen.size() < count && ! free.empty())
    {
        double total = 0.0;
        for (int i : free)
            total += posWeight (i);
        double r = rng.unit() * total;
        size_t pick = 0;
        for (size_t k = 0; k < free.size(); ++k)
        {
            r -= posWeight (free[k]);
            pick = k;
            if (r <= 0.0)
                break;
        }
        chosen.push_back (free[pick]);
        free.erase (free.begin() + (long) pick);
    }
    std::sort (chosen.begin(), chosen.end());
    return chosen;
}

struct Ctx
{
    const GenSettings& g;
    int n;
    std::vector<int> pool, roots, fifths, chord, colors;
    ModeInfo mi;

    Ctx (const GenSettings& gs, int numSteps) : g (gs), n (numSteps)
    {
        const int low = std::min (g.lowNote, g.highNote), high = std::max (g.lowNote, g.highNote);
        pool = scaleNotes (g.root, g.scale, low, high);
        if (pool.empty())
            pool.push_back (snapToScale (low, g.root, g.scale));
        mi = modeInfo (g.scale);
        // The chord tones: the tonic triad, or the triad on another degree (B leans on IV).
        int rootPc = 0, thirdPc = mi.third, fifthPc = mi.fifth;
        if (g.degree > 0)
        {
            const auto& iv = scales()[(size_t) std::clamp (g.scale, 0, (int) scales().size() - 1)].intervals;
            const int size = (int) iv.size(), d = g.degree % size;
            rootPc = iv[(size_t) d];
            thirdPc = iv[(size_t) ((d + 2) % size)];
            fifthPc = iv[(size_t) ((d + 4) % size)];
        }
        for (int note : pool)
        {
            const int pc = pcOf (note, g.root);
            if (pc == rootPc) roots.push_back (note);
            if (pc == fifthPc) fifths.push_back (note);
            if (pc == rootPc || pc == thirdPc || pc == fifthPc) chord.push_back (note);
            if (pc == mi.color) colors.push_back (note);
        }
        if (chord.empty())
            chord = pool;
    }

    int indexOf (int note) const
    {
        size_t best = 0;
        for (size_t k = 0; k < pool.size(); ++k)
            if (std::abs (pool[k] - note) < std::abs (pool[best] - note))
                best = k;
        return (int) best;
    }

    int stepBy (int note, int k) const
    {
        return pool[(size_t) std::clamp (indexOf (note) + k, 0, (int) pool.size() - 1)];
    }

    static int nearestIn (const std::vector<int>& set, int note)
    {
        if (set.empty())
            return note;
        int best = set.front();
        for (int x : set)
            if (std::abs (x - note) < std::abs (best - note))
                best = x;
        return best;
    }
};

// Clears the unlocked steps; returns them and counts the locked notes.
std::vector<int> clearUnlocked (Pattern& p, int n, int& lockedOn)
{
    lockedOn = 0;
    std::vector<int> free;
    for (int i = 0; i < n; ++i)
    {
        auto& st = p.steps[(size_t) i];
        if (st.locked)
        {
            lockedOn += st.on ? 1 : 0;
            continue;
        }
        st = Step {};
        free.push_back (i);
    }
    return free;
}

void fillShape (Step& st, int i, const GenSettings& g, Rng& rng)
{
    st.length = rng.range (std::max (1, std::min (g.lenMin, g.lenMax)), std::min (kMaxLength, std::max (g.lenMin, g.lenMax)));
    const int vLo = std::min (g.velMin, g.velMax), vHi = std::max (g.velMin, g.velMax);
    int vel = rng.range (vLo, vHi);
    if (g.mode == kMusician)
    {
        if (i % 4 == 0)
            vel = std::max (vel, vLo + (vHi - vLo) * 3 / 4); // accent on the beat
        else if (i % 2 == 1)
            vel = vLo + (vel - vLo) / 3; // ghost notes on the off 16ths
    }
    else if (i % 4 == 0)
    {
        vel += 10; // accent on the beat
    }
    st.velocity = std::clamp (vel, 1, 127);
    st.repeats = 1;
    if (g.repeatsOn && rng.unit() < g.repChance)
        st.repeats = rng.range (std::clamp (std::min (g.repMin, g.repMax), 1, kMaxRepeats),
                                std::clamp (std::max (g.repMin, g.repMax), 1, kMaxRepeats));
}

// Random: a walk over the scale with root anchors and the odd leap.
void randomPitches (Pattern& p, const std::vector<int>& positions, const Ctx& c, Rng& rng)
{
    const auto& pool = c.pool;
    int cur = c.roots.empty() ? (int) pool.size() / 2
                              : c.indexOf (c.roots[(size_t) rng.range (0, (int) c.roots.size() - 1)]);
    bool first = true;
    for (int i : positions)
    {
        const float r = rng.unit();
        if ((first && r < 0.5f) || (! first && r < 0.22f && ! c.roots.empty()))
            cur = c.indexOf (c.roots[(size_t) rng.range (0, (int) c.roots.size() - 1)]);
        else if (r < 0.37f)
            cur = rng.range (0, (int) pool.size() - 1); // leap
        else
        {
            static const int moves[] = { -2, -1, -1, 0, 1, 1, 2 };
            cur = std::clamp (cur + moves[rng.range (0, 6)], 0, (int) pool.size() - 1);
        }
        first = false;
        p.steps[(size_t) i].note = pool[(size_t) cur];
    }
}

// Musician rhythm: a motif cell repeated as A A' A B (B changes one hit), fitted to the count.
std::vector<int> musicianPositions (Rng& rng, const Ctx& c, const std::vector<int>& freeSteps, int target)
{
    if (target <= 0 || freeSteps.empty())
        return {};
    const int n = c.n, cell = musicianCellLength (n), cells = std::max (1, n / cell);
    std::vector<char> isFree ((size_t) n, 0);
    for (int f : freeSteps)
        isFree[(size_t) f] = 1;
    auto cellOf = [&] (int s) { return std::min (s / cell, cells - 1); };

    const int perCell = std::clamp ((int) std::lround ((double) target / cells), 1, cell);
    std::vector<int> offs;
    {
        std::vector<int> all;
        for (int o = 0; o < cell; ++o)
            all.push_back (o);
        // The cell's first step is the most likely hit.
        while ((int) offs.size() < perCell)
        {
            double total = 0.0;
            for (int o : all)
                total += o == 0 ? 4.0 : posWeight (o);
            double r = rng.unit() * total;
            size_t pick = 0;
            for (size_t k = 0; k < all.size(); ++k)
            {
                r -= all[k] == 0 ? 4.0 : posWeight (all[k]);
                pick = k;
                if (r <= 0.0)
                    break;
            }
            offs.push_back (all[pick]);
            all.erase (all.begin() + (long) pick);
        }
    }

    std::vector<char> chosen ((size_t) n, 0);
    for (int k = 0; k < cells; ++k)
    {
        auto o2 = offs;
        if (k == cells - 1 && cells > 1 && cell > 1 && (int) o2.size() < cell)
        {
            // B: one hit moves somewhere new.
            std::vector<int> unused;
            for (int o = 1; o < cell; ++o)
                if (std::find (o2.begin(), o2.end(), o) == o2.end())
                    unused.push_back (o);
            std::vector<int> movable;
            for (size_t j = 0; j < o2.size(); ++j)
                if (o2[j] != 0)
                    movable.push_back ((int) j);
            if (! unused.empty() && ! movable.empty())
                o2[(size_t) movable[(size_t) rng.range (0, (int) movable.size() - 1)]] = unused[(size_t) rng.range (0, (int) unused.size() - 1)];
        }
        for (int o : o2)
        {
            int s = k * cell + o;
            if (o != 0 && rng.unit() < c.g.surprise * 0.5f)
                s += rng.unit() < 0.5f ? -1 : 1; // a surprise push or drag
            if (s >= 0 && s < n && isFree[(size_t) s])
                chosen[(size_t) s] = 1;
        }
    }

    // Fit to the count, changing A' and B first so the motif stays.
    std::vector<int> order;
    order.push_back (cells - 1);
    for (int k = 1; k < cells - 1; ++k)
        order.push_back (k);
    if (cells > 1)
        order.push_back (0);
    if (cells == 1)
        order = { 0 };

    auto count = [&] { int t = 0; for (char x : chosen) t += x; return t; };
    for (int k : order)
    {
        while (count() > target)
        {
            std::vector<int> cand;
            for (int s = 0; s < n; ++s)
                if (chosen[(size_t) s] && cellOf (s) == k && s % cell != 0)
                    cand.push_back (s);
            if (cand.empty())
                break;
            chosen[(size_t) cand[(size_t) rng.range (0, (int) cand.size() - 1)]] = 0;
        }
    }
    while (count() > target) // only cell starts are left
        for (int s = n - 1; s >= 0 && count() > target; --s)
            if (chosen[(size_t) s])
                chosen[(size_t) s] = 0;

    for (int k : order)
    {
        if (count() >= target)
            break;
        std::vector<int> cand;
        for (int s = 0; s < n; ++s)
            if (isFree[(size_t) s] && ! chosen[(size_t) s] && cellOf (s) == k)
                cand.push_back (s);
        const int need = target - count();
        for (int s : pickPositions (rng, cand, need))
            chosen[(size_t) s] = 1;
    }

    std::vector<int> out;
    for (int s = 0; s < n; ++s)
        if (chosen[(size_t) s])
            out.push_back (s);
    return out;
}

// Musician pitches: a motif from chord tones and steps, repeated over the cells (A' a scale step
// away), the colour note on weak steps, and the last note resolving to the root or the fifth.
void musicianPitches (Pattern& p, const std::vector<int>& positions, const Ctx& c, Rng& rng)
{
    if (positions.empty())
        return;
    const int n = c.n, cell = musicianCellLength (n), cells = std::max (1, n / cell);
    const auto& g = c.g;
    auto strong = [] (int s) { return s % 4 == 0; };

    std::vector<std::vector<int>> byCell ((size_t) cells);
    for (int s : positions)
        byCell[(size_t) std::min (s / cell, cells - 1)].push_back (s);
    size_t m = 0, longest = 0;
    for (size_t k = 0; k < byCell.size(); ++k)
        if (byCell[k].size() > m)
        {
            m = byCell[k].size();
            longest = k;
        }
    const auto& ref = byCell[0].size() == m ? byCell[0] : byCell[longest];

    const int centre = Ctx::nearestIn (c.chord, c.pool[c.pool.size() / 2]);
    int prev = (rng.unit() < 0.5f && ! c.roots.empty()) ? Ctx::nearestIn (c.roots, centre) : centre;
    int prevMove = 0;
    std::vector<int> motif;
    for (size_t j = 0; j < m; ++j)
    {
        const int off = ref[j] % cell;
        int note = prev;
        if (j > 0)
        {
            int mv;
            if (std::abs (prevMove) >= 2)
                mv = prevMove > 0 ? -1 : 1; // after a leap, step back the other way
            else
            {
                static const int moves[] = { -1, 1, -1, 1, -2, 2, 0, 3, -3 };
                mv = moves[rng.range (0, 8)];
            }
            note = c.stepBy (prev, mv);
            prevMove = mv;
        }
        if (strong (off))
            note = Ctx::nearestIn (c.chord, note);
        else if (! c.colors.empty() && rng.unit() < g.color)
            note = Ctx::nearestIn (c.colors, note);
        motif.push_back (note);
        prev = note;
    }

    const int shift = rng.unit() < 0.5f ? -1 : 1;
    for (int k = 0; k < cells; ++k)
    {
        const bool aPrime = cells >= 4 && k == 1;
        for (size_t j = 0; j < byCell[(size_t) k].size(); ++j)
        {
            const int s = byCell[(size_t) k][j];
            int note = motif[j % m];
            if (aPrime)
            {
                note = c.stepBy (note, shift);
                if (strong (s))
                    note = Ctx::nearestIn (c.chord, note);
            }
            if (rng.unit() < g.surprise)
                note = c.pool[(size_t) rng.range (0, (int) c.pool.size() - 1)];
            p.steps[(size_t) s].note = note;
        }
    }

    // The cycle ends at home, unless a locked note comes after.
    const int last = positions.back();
    bool lockedAfter = false;
    for (int s = last + 1; s < n; ++s)
        lockedAfter |= p.steps[(size_t) s].on;
    if (! lockedAfter)
    {
        const int before = positions.size() > 1 ? p.steps[(size_t) positions[positions.size() - 2]].note : centre;
        const auto& home = (rng.unit() < 0.3f && ! c.fifths.empty()) ? c.fifths : (c.roots.empty() ? c.chord : c.roots);
        p.steps[(size_t) last].note = Ctx::nearestIn (home, before);
    }
}

void pitches (Pattern& p, const std::vector<int>& positions, const Ctx& c, Rng& rng)
{
    if (c.g.mode == kMusician)
        musicianPitches (p, positions, c, rng);
    else
        randomPitches (p, positions, c, rng);
}

std::vector<int> positionsFor (Rng& rng, const Ctx& c, const std::vector<int>& free, int count)
{
    return c.g.mode == kMusician ? musicianPositions (rng, c, free, count) : pickPositions (rng, free, count);
}

int clampSteps (Pattern& p)
{
    p.numSteps = std::clamp (p.numSteps, 1, kMaxSteps);
    return p.numSteps;
}

} // namespace

void generate (Pattern& p, const GenSettings& g)
{
    Rng rng (g.seed);
    const int n = clampSteps (p);
    const Ctx c (g, n);
    int lockedOn = 0;
    const auto free = clearUnlocked (p, n, lockedOn);
    const int target = std::clamp (std::clamp (g.notes, 0, n) - lockedOn, 0, (int) free.size());

    const auto positions = positionsFor (rng, c, free, target);
    for (int s : positions)
        p.steps[(size_t) s].on = true;
    pitches (p, positions, c, rng);
    for (int s : positions)
        fillShape (p.steps[(size_t) s], s, g, rng);
    fitLengths (p);
}

void generateRhythm (Pattern& p, const GenSettings& g)
{
    Rng rng (g.seed ^ 0x52485954u);
    const int n = clampSteps (p);
    const Ctx c (g, n);
    std::vector<int> notes;
    for (int i = 0; i < n; ++i)
        if (p.steps[(size_t) i].on && ! p.steps[(size_t) i].locked)
            notes.push_back (p.steps[(size_t) i].note);
    if (notes.empty())
        return;

    int lockedOn = 0;
    const auto free = clearUnlocked (p, n, lockedOn);
    const auto positions = positionsFor (rng, c, free, (int) notes.size());
    for (size_t k = 0; k < positions.size(); ++k)
    {
        auto& st = p.steps[(size_t) positions[k]];
        st.on = true;
        st.note = notes[k % notes.size()];
        fillShape (st, positions[k], g, rng);
    }
    fitLengths (p);
}

void generateNotes (Pattern& p, const GenSettings& g)
{
    Rng rng (g.seed ^ 0x4E4F5445u);
    const int n = clampSteps (p);
    const Ctx c (g, n);
    std::vector<int> positions;
    for (int i = 0; i < n; ++i)
        if (p.steps[(size_t) i].on && ! p.steps[(size_t) i].locked)
            positions.push_back (i);
    pitches (p, positions, c, rng);
}

void mutate (Pattern& p, const GenSettings& g, float amount)
{
    Rng rng (g.seed ^ 0x4D555441u);
    const int n = clampSteps (p);
    const Ctx c (g, n);
    amount = std::clamp (amount, 0.0f, 1.0f);
    for (int i = 0; i < n; ++i)
    {
        auto& st = p.steps[(size_t) i];
        if (! st.on || st.locked || rng.unit() >= amount)
            continue;
        if (rng.unit() < 0.7f)
        {
            static const int moves[] = { -2, -1, 1, 2 };
            st.note = c.stepBy (st.note, moves[rng.range (0, 3)]);
            if (g.mode == kMusician && i % 4 == 0)
                st.note = Ctx::nearestIn (c.chord, st.note);
        }
        else
        {
            // Nudge a step earlier or later, if that step is free.
            const int to = i + (rng.unit() < 0.5f ? -1 : 1);
            if (to >= 0 && to < n && ! p.steps[(size_t) to].on && ! p.steps[(size_t) to].locked)
            {
                p.steps[(size_t) to] = st;
                st = Step {};
                if (to > i)
                    ++i; // don't move it twice
            }
        }
    }
    fitLengths (p);
}

} // namespace midinja
