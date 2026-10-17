#include "Document.h"
#include "core/MidiFileWriter.h"
#include "core/Scales.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <random>

namespace midinja
{

const char* partName (int part)
{
    static const char* names[] = { "A", "A\xE2\x80\xB2", "B", "C" }; // A followed by U+2032 (prime)
    return names[part < 0 ? 0 : part > 3 ? 3 : part];
}

const char* partHint (int part)
{
    static const char* hints[] = { "the motif on I", "variation of A", "contrast on IV", "on V, cadence to the root" };
    return hints[part < 0 ? 0 : part > 3 ? 3 : part];
}

const char* degreeName (int pc)
{
    static const char* names[] = { "1", "\xE2\x99\xAD" "2", "2", "\xE2\x99\xAD" "3", "3", "4", "\xE2\x99\xAF" "4",
                                   "5", "\xE2\x99\xAD" "6", "6", "\xE2\x99\xAD" "7", "7" };
    return names[((pc % 12) + 12) % 12];
}

const char* chordName (int pc)
{
    static const char* names[] = { "I", "\xE2\x99\xAD" "II", "II", "\xE2\x99\xAD" "III", "III", "IV", "\xE2\x99\xAD" "V",
                                   "V", "\xE2\x99\xAD" "VI", "VI", "\xE2\x99\xAD" "VII", "VII" };
    return names[((pc % 12) + 12) % 12];
}

std::vector<int> Doc::playSeq() const
{
    if (sel >= 0 && sel < (int) arr.size())
        return { arr[(size_t) sel] };
    if (! arr.empty())
        return arr;
    return { cur };
}

bool Doc::anyLocked() const
{
    for (bool l : locks)
        if (l)
            return true;
    return false;
}

// ---------------------------------------------------------------------------------------------

uint32_t Session::newSeed()
{
    static std::mt19937 rng ((uint32_t) std::chrono::steady_clock::now().time_since_epoch().count());
    const uint32_t s = rng();
    return s != 0 ? s : 1u;
}

Session::Session()
{
    doc.gen.mode = kMusician;
    doc.gen.seed = 4821;
    for (auto& p : doc.parts)
        p.numSteps = doc.numSteps();
    applyAll (Op::Roll);
}

unsigned Session::lockMask() const
{
    unsigned m = 0;
    for (int k = 0; k < kNumParts; ++k)
        if (doc.locks[(size_t) k])
            m |= 1u << k;
    return m;
}

static void run (Op op, Pattern& p, const GenSettings& g, float amount)
{
    switch (op)
    {
        case Op::Rhythm: generateRhythm (p, g); break;
        case Op::Notes:  generateNotes (p, g); break;
        case Op::Mutate: mutate (p, g, amount); break;
        case Op::Roll:   generate (p, g); break;
    }
}

void Session::apply (Op op)
{
    auto& d = doc;
    if (d.locks[(size_t) d.cur])
        return;
    d.gen.seed = newSeed();
    Pattern& p = d.shown();
    p.numSteps = d.numSteps();
    GenSettings g = d.gen;
    g.degree = partDegree (d.cur, d.gen.scale);
    if (op == Op::Roll && d.cur > 0)
        generatePart (p, d.cur, d.parts[0], g);
    else
        run (op, p, g, mutAmt);
    d.src[(size_t) d.cur] = Src::Roll;
    commitVersion();
}

void Session::applyAll (Op op, bool keepSeed)
{
    auto& d = doc;
    if (! keepSeed)
        d.gen.seed = newSeed();
    for (auto& p : d.parts)
        p.numSteps = d.numSteps();
    GenSettings g = d.gen;
    if (op == Op::Roll)
    {
        generateForm (d.parts, lockMask(), g);
        for (int k = 0; k < kNumParts; ++k)
            if (! d.locks[(size_t) k])
                d.src[(size_t) k] = Src::Roll;
        if (! d.anyLocked())
            message.clear();
    }
    else
    {
        for (int k = 0; k < kNumParts; ++k)
        {
            if (d.locks[(size_t) k])
                continue;
            g.seed = newSeed();
            g.degree = partDegree (k, d.gen.scale);
            run (op, d.parts[(size_t) k], g, mutAmt);
        }
    }
    commitVersion();
}

void Session::cycleMutAmt()
{
    mutAmt = mutAmt < 0.2f ? 0.33f : mutAmt < 0.4f ? 0.5f : 0.15f;
}

void Session::rekey (int root, int scale)
{
    auto& d = doc;
    const int oldRoot = d.gen.root;
    d.gen.root = root;
    d.gen.scale = scale;
    // Imported bars stay as they are; bars generated from an import are made again in the new key.
    bool anyImport = false;
    for (auto s : d.src)
        anyImport = anyImport || s == Src::Import;
    GenSettings g = d.gen;
    for (int k = 0; k < kNumParts; ++k)
    {
        auto& p = d.parts[(size_t) k];
        if (d.src[(size_t) k] == Src::Import)
            continue;
        if (anyImport && d.src[(size_t) k] == Src::Derived && ! d.locks[(size_t) k])
        {
            g.seed = newSeed();
            g.degree = partDegree (k, d.gen.scale);
            generatePart (p, k, d.parts[0], g);
            continue;
        }
        conformToScale (p, oldRoot, root, scale);
        fitLengths (p);
    }
    push();
}

void Session::setRate (int rate)
{
    doc.rate = rate == 2 || rate == 6 ? rate : 4;
    for (auto& p : doc.parts)
    {
        p.numSteps = doc.numSteps();
        fitLengths (p);
    }
    push();
}

void Session::transposeAll (int dir)
{
    for (auto& p : doc.parts)
        transposeScaleSteps (p, dir, doc.gen.root, doc.gen.scale);
    doc.transpose += dir;
    push();
}

// ---------------------------------------------------------------------------------------------

void Session::selectPart (int k)
{
    if (k >= 0 && k < kNumParts)
        doc.cur = k;
}

void Session::selectSlot (int i)
{
    doc.sel = doc.sel == i ? -1 : i;
    if (doc.sel >= 0 && doc.sel < (int) doc.arr.size())
        doc.cur = doc.arr[(size_t) doc.sel];
}

void Session::toggleLock (int k)
{
    doc.locks[(size_t) k] = ! doc.locks[(size_t) k];
    push();
}

void Session::removeBar (int i)
{
    if (i < 0 || i >= (int) doc.arr.size())
        return;
    doc.arr.erase (doc.arr.begin() + i);
    if (doc.sel == i)
        doc.sel = -1;
    else if (doc.sel > i)
        --doc.sel;
    push();
}

void Session::addBar (int k)
{
    if ((int) doc.arr.size() >= kMaxBars)
        return;
    doc.cur = k;
    doc.arr.push_back (k);
    doc.sel = -1;
    push();
}

// ---------------------------------------------------------------------------------------------
// History. A snapshot is the whole Doc, but switching versions brings back only what the web
// preview's snapshot holds: parts, locks, sources, arrangement, selection, key and rate. The knobs
// keep their current values.

void Session::restore (const Doc& d)
{
    doc.parts = d.parts;
    doc.locks = d.locks;
    doc.src = d.src;
    doc.arr = d.arr;
    doc.sel = d.sel;
    doc.cur = d.cur;
    doc.gen.root = d.gen.root;
    doc.gen.scale = d.gen.scale;
    doc.rate = d.rate;
}

void Session::pushUndo (const Doc& snap)
{
    if (ui + 1 < (int) undo.size())
        undo.resize ((size_t) (ui + 1));
    undo.push_back ({ snap, vi });
    if ((int) undo.size() > kMaxUndo)
        undo.erase (undo.begin());
    ui = (int) undo.size() - 1;
}

void Session::commitVersion()
{
    versions.push_back ({ doc, false });
    while ((int) versions.size() > kMaxHist)
    {
        int k = 0;
        for (int i = 0; i < (int) versions.size(); ++i)
            if (! versions[(size_t) i].star) { k = i; break; }
        versions.erase (versions.begin() + k);
        for (auto& u : undo)
        {
            if (u.vi == k) u.vi = -1;
            else if (u.vi > k) --u.vi;
        }
    }
    vi = (int) versions.size() - 1;
    histEnd = (int) versions.size();
    pushUndo (doc);
}

void Session::push()
{
    if (vi >= 0 && vi < (int) versions.size())
        versions[(size_t) vi].snap = doc;
    pushUndo (doc);
}

void Session::gotoVersion (int i)
{
    if (i < 0 || i >= (int) versions.size())
        return;
    vi = i;
    restore (versions[(size_t) i].snap);
    pushUndo (doc);
}

void Session::undoTo (int j)
{
    if (j < 0 || j >= (int) undo.size())
        return;
    ui = j;
    const auto u = undo[(size_t) j];
    restore (u.snap);
    vi = u.vi;
    if (vi >= 0 && vi < (int) versions.size())
        versions[(size_t) vi].snap = doc;
}

void Session::clampHist()
{
    const int n = (int) versions.size();
    histEnd = std::min (n, std::max (std::min (kPerPage, n), histEnd));
}

// ---------------------------------------------------------------------------------------------

bool Session::importMidi (const std::vector<RawNote>& raw, double ppq, const std::string& name)
{
    constexpr int N = 16, MAX = kMaxBars * N;
    const double stepTicks = (ppq > 0 ? ppq : 480.0) / 4.0;
    struct Q { int s, e, note, vel; };
    std::vector<Q> q;
    for (const auto& x : raw)
    {
        const int s = (int) std::lround (x.tick / stepTicks);
        int e = (int) std::lround ((x.tick + x.dur) / stepTicks);
        if (e <= s)
            e = s + 1;
        if (s >= 0 && s < MAX)
            q.push_back ({ s, e, x.note, x.vel });
    }
    std::stable_sort (q.begin(), q.end(), [] (const Q& a, const Q& b) { return a.s != b.s ? a.s < b.s : a.note < b.note; });

    std::vector<Q> placed;
    for (auto x : q)
    {
        if (! placed.empty())
        {
            Q& last = placed.back();
            if (x.s == last.s)
                continue; // same start: the lowest came first
            if (x.s < last.e)
            {
                if (x.note < last.note) last.e = x.s; // overlap: the lowest note wins
                else continue;
            }
        }
        x.e = std::min (x.e, (x.s / N + 1) * N); // cut at the bar line
        placed.push_back (x);
    }
    if (placed.empty())
    {
        message = "No notes found in " + name;
        return false;
    }
    int maxE = 0;
    for (const auto& x : placed)
        maxE = std::max (maxE, x.e);
    const int nBars = std::min (kMaxBars, (maxE - 1) / N + 1);

    std::vector<WeightedNote> w;
    for (size_t j = 0; j < placed.size(); ++j)
    {
        const auto& x = placed[j];
        w.push_back ({ x.note, (float) ((x.e - x.s) * (x.s % 4 == 0 ? 2 : 1) + (j == 0 ? 1 : 0) + (j + 1 == placed.size() ? 2 : 0)) });
    }
    const KeyGuess k = detectKey (w);

    auto& d = doc;
    d.gen.root = k.root;
    d.gen.scale = k.scale;
    d.rate = 4;
    d.transpose = 0;
    for (auto& p : d.parts)
    {
        p = Pattern {};
        p.numSteps = N;
    }
    for (const auto& x : placed)
    {
        const int b = x.s / N;
        auto& st = d.parts[(size_t) b].steps[(size_t) (x.s - b * N)];
        st = Step {};
        st.on = true;
        st.note = x.note;
        st.length = std::min (kMaxLength, x.e - x.s);
        st.velocity = std::max (1, std::min (127, x.vel));
    }
    d.locks.fill (false);
    for (int b = 0; b < kNumParts; ++b)
        d.src[(size_t) b] = b < nBars ? Src::Import : Src::Derived;
    d.arr = { 0, 1, 2, 3 };
    d.sel = -1;
    for (auto& p : d.parts)
        fitLengths (p);
    d.cur = 0;
    d.gen.seed = newSeed();
    GenSettings g = d.gen;
    for (int b = nBars; b < kNumParts; ++b)
    {
        g.seed = newSeed();
        g.degree = partDegree (b, d.gen.scale);
        generatePart (d.parts[(size_t) b], b, d.parts[0], g);
    }

    const int dropped = (int) raw.size() - (int) placed.size();
    std::string m = "Imported " + std::to_string (nBars) + (nBars > 1 ? " bars from " : " bar from ") + name + " \xE2\x86\x92 ";
    for (int b = 0; b < nBars; ++b)
        m += std::string (b ? " " : "") + partName (b);
    m += " \xC2\xB7 key guess " + std::string (noteNames()[(size_t) k.root]) + " " + scales()[(size_t) k.scale].name
       + " (" + std::to_string ((int) std::lround (k.confidence * 100)) + "% sure; change Key to correct it)";
    if (nBars < kNumParts)
    {
        m += " \xC2\xB7 ";
        for (int b = nBars; b < kNumParts; ++b)
            m += std::string (b > nBars ? ", " : "") + partName (b);
        m += " generated from it";
    }
    if (dropped > 0)
        m += " \xC2\xB7 " + std::to_string (dropped) + (dropped > 1 ? " overlapping or late notes dropped" : " overlapping or late note dropped");
    message = m;
    commitVersion();
    return true;
}

std::vector<uint8_t> Session::midiFile (double bpm, int channel) const
{
    std::vector<NoteEvent> all;
    double offset = 0.0;
    const auto seq = doc.arr.empty() ? std::vector<int> { doc.cur } : doc.arr;
    for (int k : seq)
    {
        const auto& p = doc.parts[(size_t) k];
        for (auto e : renderCycle (p, doc.swing))
        {
            e.start += offset;
            all.push_back (e);
        }
        offset += p.numSteps;
    }
    return writeMidiFile (all, (double) doc.rate, bpm, channel, "Midinja", offset);
}

std::string Session::exportName() const
{
    std::string root = noteNames()[(size_t) doc.gen.root];
    for (auto& c : root)
        if (c == '#') c = 's';
    std::string scale;
    for (const char* c = scales()[(size_t) doc.gen.scale].name; *c; ++c)
        if (std::isalnum ((unsigned char) *c))
            scale += *c;
    return "midinja-" + root + "-" + scale;
}

} // namespace midinja
