#include "Document.h"
#include "core/MidiFileWriter.h"
#include "core/Scales.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <random>
#include <set>

namespace midinja
{

const char* partName (int part)
{
    static const char* names[] = { "A", "A\xE2\x80\xB2", "B", "C" }; // A followed by U+2032 (prime)
    return names[part < 0 ? 0 : part > 3 ? 3 : part];
}

const char* partHint (int part)
{
    static const char* hints[] = { "the motif", "variation of A", "contrast", "leads home" };
    return hints[part < 0 ? 0 : part > 3 ? 3 : part];
}

const char* degreeName (int pc)
{
    static const char* names[] = { "1", "\xE2\x99\xAD" "2", "2", "\xE2\x99\xAD" "3", "3", "4", "\xE2\x99\xAF" "4",
                                   "5", "\xE2\x99\xAD" "6", "6", "\xE2\x99\xAD" "7", "7" };
    return names[((pc % 12) + 12) % 12];
}

const char* chordTypeName (ChordType t)
{
    switch (t)
    {
        case ChordType::Seventh: return "7th";
        case ChordType::Sus2:    return "sus2";
        case ChordType::Sus4:    return "sus4";
        case ChordType::Triad:   break;
    }
    return "Triad";
}

std::string numeral (int degree, int scale)
{
    const auto& iv = scales()[(size_t) std::clamp (scale, 0, (int) scales().size() - 1)].intervals;
    const int n = (int) iv.size();
    const int d = ((degree % n) + n) % n;
    const int r = iv[(size_t) d];
    const int third = (iv[(size_t) ((d + 2) % n)] - r + 12) % 12;
    const int fifth = (iv[(size_t) ((d + 4) % n)] - r + 12) % 12;
    static const char* roman[] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII" };
    std::string s = d < 12 ? roman[d] : std::to_string (d + 1);
    if (n == 7)
    {
        static const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
        const int diff = r - major[d];
        if (diff == -1) s = "\xE2\x99\xAD" + s;
        else if (diff == 1) s = "\xE2\x99\xAF" + s;
    }
    const bool minor = third == 3, dim = minor && fifth == 6, aug = third == 4 && fifth == 8;
    if (minor)
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);
    if (dim) s += "\xC2\xB0";
    else if (aug) s += "\xE2\x81\xBA";
    return s;
}

// ---------------------------------------------------------------------------------------------

namespace
{
bool hasNotes (const Pattern& p)
{
    for (int i = 0; i < std::min (p.numSteps, kMaxSteps); ++i)
        if (p.steps[(size_t) i].on)
            return true;
    return false;
}

int lastOnIdx (const Pattern& p)
{
    for (int i = std::min (p.numSteps, kMaxSteps) - 1; i >= 0; --i)
        if (p.steps[(size_t) i].on)
            return i;
    return -1;
}

int foldNoteIn (int n, int lo, int hi)
{
    if (n >= lo && n <= hi)
        return n;
    int m = n;
    while (m < lo) m += 12;
    while (m > hi) m -= 12;
    return m >= lo ? m : n < lo ? lo : hi;
}

int nearestOfPcs (int note, const std::vector<int>& pcs, int lo, int hi)
{
    int best = note, dist = 1 << 20;
    for (int n = lo; n <= hi; ++n)
    {
        const int pc = ((n % 12) + 12) % 12;
        if (std::find (pcs.begin(), pcs.end(), pc) != pcs.end() && std::abs (n - note) < dist)
        {
            dist = std::abs (n - note);
            best = n;
        }
    }
    return best;
}

void run (Op op, Pattern& p, const GenSettings& g, float amount)
{
    switch (op)
    {
        case Op::Rhythm: generateRhythm (p, g); break;
        case Op::Notes:  generateNotes (p, g); break;
        case Op::Mutate: mutate (p, g, amount); break;
        case Op::Roll:   generate (p, g); break;
    }
}

// The chord rhythms, in sixteenths of a bar: (start, length).
const std::vector<std::vector<std::pair<int, int>>>& rhythms()
{
    static const std::vector<std::vector<std::pair<int, int>>> r = {
        { { 0, 16 } },                                              // whole
        { { 0, 8 }, { 8, 8 } },                                     // halves
        { { 0, 4 }, { 4, 4 }, { 8, 4 }, { 12, 4 } },                // quarters
        { { 0, 2 }, { 6, 2 }, { 8, 2 }, { 14, 2 } },                // stabs
        { { 0, 3 }, { 3, 3 }, { 6, 2 }, { 10, 2 }, { 12, 4 } },     // push
        { { 2, 2 }, { 6, 2 }, { 10, 2 }, { 14, 2 } },               // offbeat
    };
    return r;
}
} // namespace

// ---------------------------------------------------------------------------------------------
// Doc

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
    for (bool l : T().locks)
        if (l)
            return true;
    return false;
}

int Doc::scaleSize() const { return (int) scales()[(size_t) gen.scale].intervals.size(); }

int Doc::degreeOf (int k) const
{
    const int n = scaleSize();
    return ((chords[(size_t) k].degree % n) + n) % n;
}

int Doc::degreePc (int k) const { return scales()[(size_t) gen.scale].intervals[(size_t) degreeOf (k)]; }

int Doc::pcOf (int note) const { return ((note - gen.root) % 12 + 12) % 12; }

bool Doc::inScale (int note) const { return midinja::inScale (note, gen.root, gen.scale); }

std::vector<int> Doc::chordNotes (int k, const Tab& t) const
{
    const auto& iv = scales()[(size_t) gen.scale].intervals;
    const int n = (int) iv.size(), d = degreeOf (k);
    const auto& c = chords[(size_t) k];
    std::vector<int> degs = c.type == ChordType::Sus2 ? std::vector<int> { 0, 1, 4 }
                          : c.type == ChordType::Sus4 ? std::vector<int> { 0, 3, 4 }
                          : c.type == ChordType::Seventh ? std::vector<int> { 0, 2, 4, 6 }
                                                         : std::vector<int> { 0, 2, 4 };
    const int base = iv[(size_t) d];
    std::vector<int> offs;
    for (int x : degs)
        offs.push_back ((iv[(size_t) ((d + x) % n)] - base + 12) % 12);
    for (size_t i = 1; i < offs.size(); ++i)
        while (offs[i] <= offs[i - 1])
            offs[i] += 12;
    const int inv = ((c.inv % (int) offs.size()) + (int) offs.size()) % (int) offs.size();
    for (int i = 0; i < inv; ++i)
    {
        offs.push_back (offs.front() + 12);
        offs.erase (offs.begin());
    }
    const int m = offs.front();
    for (auto& o : offs)
        o -= m;
    const int bassPc = (gen.root + base + m) % 12;
    int bass = std::max (0, t.lowNote);
    while (((bass % 12) + 12) % 12 != bassPc)
        ++bass;
    std::vector<int> notes;
    for (int o : offs)
        notes.push_back (bass + o);
    while (notes.back() > t.highNote && notes.front() - 12 >= t.lowNote)
        for (auto& x : notes)
            x -= 12;
    return notes;
}

std::vector<std::pair<int, int>> Doc::pairsOf (const Pattern& p) const
{
    std::vector<std::pair<int, int>> out;
    const auto mi = modeInfo (gen.scale);
    if (mi.moves.empty())
        return out;
    std::vector<int> on;
    for (int i = 0; i < std::min (p.numSteps, kMaxSteps); ++i)
        if (p.steps[(size_t) i].on)
            on.push_back (i);
    for (size_t j = 0; j + 1 < on.size(); ++j)
    {
        const auto& a = p.steps[(size_t) on[j]];
        const auto& b = p.steps[(size_t) on[j + 1]];
        if (a.locked || b.locked || std::abs (a.note - b.note) > 4)
            continue;
        const int pa = pcOf (a.note), pb = pcOf (b.note);
        for (const auto& mv : mi.moves)
            if ((mv.first == pa && mv.second == pb) || (mv.first == pb && mv.second == pa))
            {
                out.push_back ({ on[j], on[j + 1] });
                ++j;
                break;
            }
    }
    return out;
}

GenSettings Doc::genFor (const Tab& t, int k) const
{
    GenSettings g = gen;
    g.lowNote = t.lowNote;
    g.highNote = t.highNote;
    g.degree = degreeOf (k);
    return g;
}

// ---------------------------------------------------------------------------------------------
// Session

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
    doc.tabs[0].channel = 1; doc.tabs[0].poly = false; doc.tabs[0].on = true;
    doc.tabs[0].rootNote = 40; doc.tabs[0].lowNote = 40; doc.tabs[0].highNote = 64;
    doc.tabs[1].channel = 2; doc.tabs[1].poly = true; doc.tabs[1].on = false;
    doc.tabs[1].rootNote = 52; doc.tabs[1].lowNote = 52; doc.tabs[1].highNote = 76;
    doc.tabs[2].channel = 3; doc.tabs[2].poly = false; doc.tabs[2].on = false;
    doc.tabs[2].rootNote = 52; doc.tabs[2].lowNote = 52; doc.tabs[2].highNote = 76;
    for (auto& t : doc.tabs)
        for (auto& p : t.parts)
            p.numSteps = doc.numSteps();
    for (int k = 0; k < kNumParts; ++k)
        doc.chords[(size_t) k] = { partDegree (k, doc.gen.scale), k == kPartC ? ChordType::Seventh : ChordType::Triad, k == kPartAPrime ? 1 : 0 };

    // A new riff: one bar A on tab 1, a whole-bar chord on tab 2.
    {
        auto& t = doc.tabs[0];
        generate (t.parts[0], doc.genFor (t, 0));
        rebase (t, 0);
    }
    doc.tabs[1].parts[0] = chordRhythm (0);
    doc.tabs[1].tpl[0] = 0;
    commitVersion();
}

unsigned Session::lockMask() const
{
    unsigned m = 0;
    for (int k = 0; k < kNumParts; ++k)
        if (doc.T().locks[(size_t) k])
            m |= 1u << k;
    return m;
}

// ---- the scale switch -----------------------------------------------------------------------

void Session::rebase (Tab& t, int k)
{
    auto& b = t.base[(size_t) k];
    b.valid = true;
    b.scale = doc.gen.scale;
    for (int i = 0; i < kMaxSteps; ++i)
        b.notes[(size_t) i] = t.parts[(size_t) k].steps[(size_t) i].note;
}

// A riff into the current scale from its remembered notes: a note keeps its degree; a colour
// note goes to the new scale's colour note nearest in pitch; an augmented-second pair moves as
// a unit to the new scale's pair (the same one if it exists, else the nearest), else to plain
// degrees; an altered note keeps its degree plus its alteration. Locked notes stay.
void Session::remap (Tab& t, int k)
{
    auto& p = t.parts[(size_t) k];
    const auto& b = t.base[(size_t) k];
    if (! b.valid)
        return;
    if (b.scale == doc.gen.scale)
    {
        for (int i = 0; i < kMaxSteps; ++i)
            if (p.steps[(size_t) i].on && ! p.steps[(size_t) i].locked)
                p.steps[(size_t) i].note = b.notes[(size_t) i];
        return;
    }
    const auto& from = scales()[(size_t) b.scale].intervals;
    const auto& to = scales()[(size_t) doc.gen.scale].intervals;
    const auto fromMi = modeInfo (b.scale), toMi = modeInfo (doc.gen.scale);
    const auto pcOf = [this] (int note) { return doc.pcOf (note); };
    const auto has = [] (const std::vector<int>& v, int x) { return std::find (v.begin(), v.end(), x) != v.end(); };
    const auto nearestTo = [&] (int note, const std::vector<int>& pcs) {
        int best = note, dist = 1 << 20;
        for (int n = note - 11; n <= note + 11; ++n)
            if (has (pcs, pcOf (n)) && std::abs (n - note) < dist)
            {
                dist = std::abs (n - note);
                best = n;
            }
        return best;
    };
    const auto mapNote = [&] (int bn) {
        const int pc = pcOf (bn);
        if (has (fromMi.colors, pc) && ! toMi.colors.empty())
            return nearestTo (bn, toMi.colors);
        const auto it = std::find (from.begin(), from.end(), pc);
        if (it != from.end())
        {
            const size_t idx = (size_t) (it - from.begin());
            return idx < to.size() ? bn - pc + to[idx] : nearestTo (bn, to);
        }
        int alt = 1;
        while (! has (from, pcOf (bn - alt)) && alt < 12)
            ++alt;
        const int lower = bn - alt, lpc = pcOf (lower);
        const size_t li = (size_t) (std::find (from.begin(), from.end(), lpc) - from.begin());
        return (li < to.size() ? lower - lpc + to[li] : nearestTo (lower, to)) + alt;
    };

    // pairs in the remembered notes
    std::vector<int> on;
    for (int i = 0; i < std::min (p.numSteps, kMaxSteps); ++i)
        if (p.steps[(size_t) i].on)
            on.push_back (i);
    struct PairRef { std::pair<int, int> mv; int s1, s2; };
    std::vector<PairRef> pairs;
    std::array<int, kMaxSteps> pairAt {};
    pairAt.fill (-1);
    for (size_t j = 0; j + 1 < on.size(); ++j)
    {
        const int i1 = on[j], i2 = on[j + 1];
        if (p.steps[(size_t) i1].locked || p.steps[(size_t) i2].locked || std::abs (b.notes[(size_t) i1] - b.notes[(size_t) i2]) > 4)
            continue;
        const int a = pcOf (b.notes[(size_t) i1]), c = pcOf (b.notes[(size_t) i2]);
        for (const auto& mv : fromMi.moves)
            if ((mv.first == a && mv.second == c) || (mv.first == c && mv.second == a))
            {
                pairAt[(size_t) i1] = pairAt[(size_t) i2] = (int) pairs.size();
                pairs.push_back ({ mv, i1, i2 });
                ++j;
                break;
            }
    }
    std::set<int> done;
    for (int i = 0; i < std::min (p.numSteps, kMaxSteps); ++i)
    {
        auto& st = p.steps[(size_t) i];
        if (! st.on || st.locked || done.count (i))
            continue;
        if (pairAt[(size_t) i] >= 0 && ! toMi.moves.empty())
        {
            const auto& pr = pairs[(size_t) pairAt[(size_t) i]];
            std::pair<int, int> target = toMi.moves.front();
            bool same = false;
            for (const auto& mv : toMi.moves)
                if (mv == pr.mv) { target = mv; same = true; }
            if (! same)
                for (const auto& mv : toMi.moves)
                    if (std::abs (mv.first - pr.mv.first) < std::abs (target.first - pr.mv.first))
                        target = mv;
            for (int s : { pr.s1, pr.s2 })
            {
                const int bn = b.notes[(size_t) s], pc = pcOf (bn);
                const int tpc = pc == pr.mv.first ? target.first : target.second;
                p.steps[(size_t) s].note = bn - pc + tpc;
                done.insert (s);
            }
            continue;
        }
        st.note = mapNote (b.notes[(size_t) i]);
        done.insert (i);
    }
}

// ---- the range ------------------------------------------------------------------------------

// Notes outside the range fold in by octave (pitch class kept); a pair folds as a whole; locked
// notes never move.
void Session::foldPattern (Pattern& p, int lo, int hi) const
{
    std::set<int> done;
    for (const auto& pr : doc.pairsOf (p))
    {
        auto& a = p.steps[(size_t) pr.first];
        auto& b = p.steps[(size_t) pr.second];
        const auto inR = [&] (int x) { return x >= lo && x <= hi; };
        if (inR (a.note) && inR (b.note))
        {
            done.insert (pr.first);
            done.insert (pr.second);
            continue;
        }
        for (int k : { -1, 1, -2, 2, -3, 3 })
            if (inR (a.note + 12 * k) && inR (b.note + 12 * k))
            {
                a.note += 12 * k;
                b.note += 12 * k;
                done.insert (pr.first);
                done.insert (pr.second);
                break;
            }
    }
    for (int i = 0; i < kMaxSteps; ++i)
    {
        auto& s = p.steps[(size_t) i];
        if (s.on && ! s.locked && ! done.count (i))
            s.note = foldNoteIn (s.note, lo, hi);
    }
}

void Session::foldTab (Tab& t)
{
    if (t.poly)
        return;
    for (auto& p : t.parts)
        foldPattern (p, t.lowNote, t.highNote);
}

// The tab's root, range and parts move together by d semitones; the remembered notes too.
void Session::shiftTab (Tab& t, int d)
{
    t.rootNote += d;
    t.lowNote += d;
    t.highNote += d;
    for (int k = 0; k < kNumParts; ++k)
    {
        for (auto& s : t.parts[(size_t) k].steps)
            s.note += d;
        if (t.base[(size_t) k].valid)
            for (auto& n : t.base[(size_t) k].notes)
                n += d;
    }
}

bool Session::lockedOn (int note) const
{
    const auto& t = doc.T();
    if (t.poly)
        return false;
    for (const auto& p : t.parts)
        for (int i = 0; i < std::min (p.numSteps, kMaxSteps); ++i)
            if (p.steps[(size_t) i].on && p.steps[(size_t) i].locked && p.steps[(size_t) i].note == note)
                return true;
    return false;
}

int Session::scaleRows() const
{
    int n = 0;
    for (int x = doc.T().lowNote; x <= doc.T().highNote; ++x)
        if (doc.inScale (x))
            ++n;
    return n;
}

bool Session::canGrow (bool top) const
{
    const auto& t = doc.T();
    if (t.highNote - t.lowNote >= kMaxSpan)
        return false;
    return top ? t.highNote < 127 : t.lowNote > 0;
}

bool Session::canShrink (bool top, std::string* why) const
{
    const auto& t = doc.T();
    const int cur = top ? t.highNote : t.lowNote;
    const std::string name = std::string (noteNames()[(size_t) ((cur % 12) + 12) % 12]) + std::to_string (cur / 12 - 1);
    if (t.highNote - t.lowNote < 1)
    {
        if (why) *why = "The range is one note";
        return false;
    }
    if (lockedOn (cur))
    {
        if (why) *why = name + " holds a locked note: unlock it first";
        return false;
    }
    if (doc.inScale (cur) && scaleRows() <= 1)
    {
        if (why) *why = name + " is the last scale note in the range: the range keeps at least one";
        return false;
    }
    return true;
}

void Session::growRange (bool top)
{
    if (! canGrow (top))
        return;
    auto& t = doc.T();
    if (top) ++t.highNote;
    else --t.lowNote;
    push();
}

void Session::shrinkRange (bool top)
{
    if (! canShrink (top))
        return;
    auto& t = doc.T();
    if (top) --t.highNote;
    else ++t.lowNote;
    foldTab (t);
    push();
}

// ---- the dice -------------------------------------------------------------------------------

Pattern Session::chordRhythm (int tpl) const
{
    const int n = doc.numSteps();
    Pattern q;
    q.numSteps = n;
    for (const auto& [s, l] : rhythms()[(size_t) std::clamp (tpl, 0, kNumRhythms - 1)])
    {
        const int i = (int) std::lround (s * n / 16.0);
        if (i >= n)
            continue;
        auto& st = q.steps[(size_t) i];
        st = Step {};
        st.on = true;
        st.note = 60;
        st.length = std::max (1, (int) std::lround (l * n / 16.0));
        st.velocity = 100;
    }
    fitLengths (q);
    return q;
}

void Session::rollChord (Tab& t, int k)
{
    Rng rng (newSeed());
    int tpl = rng.range (0, kNumRhythms - 1);
    if (tpl == t.tpl[(size_t) k])
        tpl = (tpl + 1 + rng.range (0, kNumRhythms - 2)) % kNumRhythms;
    t.parts[(size_t) k] = chordRhythm (tpl);
    t.tpl[(size_t) k] = tpl;
}

// A mono part: the Form's own moves (A' as a variation, B's contrast, C's cadence) when the part
// sits on its usual degree, else a plain roll on its degree.
void Session::rollMono (Tab& t, int k, Op op)
{
    GenSettings g = doc.genFor (t, k);
    g.seed = newSeed();
    auto& p = t.parts[(size_t) k];
    if (op == Op::Roll && k == kPartAPrime)
        generatePart (p, k, t.parts[0], g);
    else if (op == Op::Roll && k > kPartAPrime && g.degree == partDegree (k, doc.gen.scale))
        generatePart (p, k, t.parts[0], g);
    else
        run (op, p, g, mutAmt);
}

void Session::apply (Op op)
{
    auto& d = doc;
    auto& t = d.T();
    if (t.locks[(size_t) d.cur])
        return;
    d.gen.seed = newSeed();
    t.parts[(size_t) d.cur].numSteps = d.numSteps();
    if (t.poly) rollChord (t, d.cur);
    else rollMono (t, d.cur, op);
    t.src[(size_t) d.cur] = Src::Roll;
    rebase (t, d.cur);
    commitVersion();
}

void Session::applyAll (Op op, bool keepSeed)
{
    auto& d = doc;
    auto& t = d.T();
    if (! keepSeed)
        d.gen.seed = newSeed();
    for (auto& p : t.parts)
        p.numSteps = d.numSteps();
    bool usual = true;
    for (int k = 0; k < kNumParts; ++k)
        usual = usual && d.degreeOf (k) == partDegree (k, d.gen.scale);
    if (t.poly)
    {
        for (int k = 0; k < kNumParts; ++k)
            if (! t.locks[(size_t) k])
                rollChord (t, k);
    }
    else if (op == Op::Roll && usual)
    {
        generateForm (t.parts, lockMask(), d.genFor (t, 0));
    }
    else
    {
        for (int k = 0; k < kNumParts; ++k)
            if (! t.locks[(size_t) k])
                rollMono (t, k, op);
    }
    for (int k = 0; k < kNumParts; ++k)
        if (! t.locks[(size_t) k])
        {
            t.src[(size_t) k] = Src::Roll;
            rebase (t, k);
        }
    if (! d.anyLocked())
        message.clear();
    commitVersion();
}

void Session::cycleMutAmt()
{
    mutAmt = mutAmt < 0.2f ? 0.33f : mutAmt < 0.4f ? 0.5f : 0.15f;
}

bool Session::canProgression() const { return doc.scaleSize() == 7; }

void Session::rollProgression()
{
    if (! canProgression())
        return;
    struct Idiom { const char* name; std::vector<std::array<int, 3>> list; };
    static const std::vector<Idiom> idioms = {
        { "Major (Ionian)",    { { 0, 3, 4 }, { 0, 5, 4 }, { 0, 1, 4 }, { 0, 4, 3 } } },
        { "Natural Minor",     { { 0, 5, 6 }, { 0, 3, 4 }, { 0, 5, 4 }, { 0, 2, 6 } } },
        { "Dorian",            { { 0, 3, 6 }, { 0, 3, 4 }, { 0, 6, 3 } } },
        { "Phrygian",          { { 0, 3, 1 }, { 0, 6, 1 }, { 0, 2, 1 } } },
        { "Lydian",            { { 0, 1, 4 }, { 0, 4, 1 }, { 0, 5, 1 } } },
        { "Mixolydian",        { { 0, 6, 3 }, { 0, 3, 6 }, { 0, 1, 6 } } },
        { "Locrian",           { { 0, 1, 5 }, { 0, 3, 1 } } },
        { "Harmonic Minor",    { { 0, 5, 4 }, { 0, 3, 4 }, { 0, 1, 4 } } },
        { "Melodic Minor",     { { 0, 3, 4 }, { 0, 1, 4 }, { 0, 5, 4 } } },
        { "Phrygian Dominant", { { 0, 5, 1 }, { 0, 6, 1 }, { 0, 3, 1 } } },
    };
    static const std::vector<std::array<int, 3>> generic = { { 0, 3, 4 }, { 0, 5, 4 }, { 0, 1, 4 } };
    const std::string name = scales()[(size_t) doc.gen.scale].name;
    const std::vector<std::array<int, 3>>* list = &generic;
    for (const auto& i : idioms)
        if (name == i.name)
            list = &i.list;
    const std::array<int, 3> cur { doc.degreeOf (0), doc.degreeOf (2), doc.degreeOf (3) };
    std::vector<std::array<int, 3>> opts;
    for (const auto& p : *list)
        if (p != cur)
            opts.push_back (p);
    if (opts.empty())
        opts.push_back (list->front());
    Rng rng (newSeed());
    const auto pick = opts[(size_t) rng.range (0, (int) opts.size() - 1)];
    const std::array<int, kNumParts> want { pick[0], pick[0], pick[1], pick[2] };
    for (int k = 0; k < kNumParts; ++k)
    {
        if (doc.T().locks[(size_t) k])
            continue; // a locked part keeps its degree
        for (auto& t : doc.tabs)
            retune (t, k, doc.degreeOf (k), want[(size_t) k]);
        doc.chords[(size_t) k].degree = want[(size_t) k];
    }
    doc.arr = { 0, 1, 2, 3 };
    doc.sel = -1;
    for (auto& t : doc.tabs)
        for (int k = 1; k < kNumParts; ++k)
            extendPart (t, k);
    commitVersion();
}

int Session::scaleStep (int note, int steps) const
{
    int alt = 0;
    while (! doc.inScale (note - alt) && alt < 12)
        ++alt;
    int n = note - alt;
    const int dir = steps > 0 ? 1 : -1;
    for (int i = 0; i < std::abs (steps); ++i)
        do n += dir; while (! doc.inScale (n) && n > 0 && n < 127);
    return n + alt;
}

// The whole bar shifts an octave before single notes fold into the range.
void Session::fitBar (Pattern& p, const Tab& t) const
{
    std::vector<int> on;
    for (int i = 0; i < std::min (p.numSteps, kMaxSteps); ++i)
        if (p.steps[(size_t) i].on && ! p.steps[(size_t) i].locked)
            on.push_back (p.steps[(size_t) i].note);
    for (int k : { 0, -1, 1, -2, 2 })
    {
        bool fits = true;
        for (int x : on)
            fits = fits && x + 12 * k >= t.lowNote && x + 12 * k <= t.highNote;
        if (fits)
        {
            if (k != 0)
                for (auto& s : p.steps)
                    if (s.on && ! s.locked)
                        s.note += 12 * k;
            return;
        }
    }
    foldPattern (p, t.lowNote, t.highNote);
}

// A riff moves from one degree to another by scale steps.
void Session::retune (Tab& t, int k, int from, int to)
{
    auto& p = t.parts[(size_t) k];
    if (t.poly || ! hasNotes (p) || from == to)
        return;
    const int n = doc.scaleSize();
    int delta = ((to - from) % n + n) % n;
    if (delta > n / 2)
        delta -= n;
    for (auto& s : p.steps)
        if (s.on && ! s.locked)
            s.note = scaleStep (s.note, delta);
    fitBar (p, t);
    rebase (t, k);
}

// Extension: an empty part that joins the arrangement is a copy of the tab's A moved by scale
// steps to its degree (A' as it is); C's last note goes to the root or the fifth; locks are not
// copied; a part with notes is never overwritten.
bool Session::extendPart (Tab& t, int k)
{
    const auto& a = t.parts[0];
    auto& p = t.parts[(size_t) k];
    if (k == 0 || hasNotes (p) || ! hasNotes (a))
        return false;
    Pattern q = a;
    for (auto& s : q.steps)
        s.locked = false;
    if (! t.poly && k != kPartAPrime)
    {
        const int n = doc.scaleSize();
        const int delta = ((doc.degreeOf (k) - doc.degreeOf (0)) % n + n) % n;
        for (auto& s : q.steps)
            if (s.on)
                s.note = scaleStep (s.note, delta);
        fitBar (q, t);
        if (k == kPartC)
        {
            const int last = lastOnIdx (q);
            const auto mi = modeInfo (doc.gen.scale);
            if (last >= 0)
            {
                std::vector<int> pcs { doc.gen.root };
                if (mi.fifth >= 0)
                    pcs.push_back ((doc.gen.root + mi.fifth) % 12);
                q.steps[(size_t) last].note = nearestOfPcs (q.steps[(size_t) last].note, pcs, t.lowNote, t.highNote);
            }
        }
    }
    p = q;
    t.tpl[(size_t) k] = t.tpl[0];
    rebase (t, k);
    return true;
}

// ---- the key --------------------------------------------------------------------------------

void Session::setScale (int scale)
{
    doc.gen.scale = std::clamp (scale, 0, (int) scales().size() - 1);
    for (auto& t : doc.tabs)
    {
        if (t.poly)
            continue;
        for (int k = 0; k < kNumParts; ++k)
            remap (t, k);
        foldTab (t);
    }
    push();
}

void Session::setRootPc (int pc)
{
    pc = ((pc % 12) + 12) % 12;
    const int d = (((pc - doc.gen.root) % 12) + 12 + 6) % 12 - 6;
    if (d == 0)
        return;
    for (const auto& t : doc.tabs)
        if (t.lowNote + d < 0 || t.highNote + d > 127)
            return;
    doc.gen.root = pc;
    for (auto& t : doc.tabs)
        shiftTab (t, d);
    push();
}

void Session::setOctave (int dir)
{
    auto& t = doc.T();
    const int d = 12 * (dir > 0 ? 1 : -1);
    if (t.rootNote + d < 24 || t.rootNote + d > 96 || t.lowNote + d < 0 || t.highNote + d > 127)
        return;
    shiftTab (t, d);
    push();
}

void Session::setRate (int rate)
{
    doc.rate = rate == 2 || rate == 6 ? rate : 4;
    for (auto& t : doc.tabs)
        for (auto& p : t.parts)
        {
            p.numSteps = doc.numSteps();
            fitLengths (p);
        }
    push();
}

// ---- tabs and chords ------------------------------------------------------------------------

void Session::selectTab (int i)
{
    if (i >= 0 && i < kNumTabs)
        doc.tab = i;
}

void Session::setTabPoly (int i, bool poly)
{
    if (i < 0 || i >= kNumTabs)
        return;
    doc.tabs[(size_t) i].poly = poly;
    doc.tab = i;
    push();
}

void Session::setTabChannel (int i, int channel)
{
    if (i < 0 || i >= kNumTabs)
        return;
    doc.tabs[(size_t) i].channel = std::clamp (channel, 1, 16);
    push();
}

void Session::setTabOn (int i, bool on)
{
    if (i < 0 || i >= kNumTabs)
        return;
    doc.tabs[(size_t) i].on = on;
    push();
}

void Session::setChord (int k, int degree, ChordType type, int inv)
{
    if (k < 0 || k >= kNumParts)
        return;
    auto& c = doc.chords[(size_t) k];
    const int n = doc.scaleSize();
    c.degree = ((degree % n) + n) % n;
    c.type = type;
    c.inv = std::clamp (inv, 0, type == ChordType::Seventh ? 3 : 2);
    push();
}

// ---- arrangement ----------------------------------------------------------------------------

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
    doc.T().locks[(size_t) k] = ! doc.T().locks[(size_t) k];
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
    if ((int) doc.arr.size() >= kMaxBars || k < 0 || k >= kNumParts)
        return;
    doc.cur = k;
    doc.arr.push_back (k);
    doc.sel = -1;
    for (auto& t : doc.tabs)
        extendPart (t, k);
    push();
}

// ---------------------------------------------------------------------------------------------
// History. A snapshot is the whole Doc, but switching versions brings back only what the web
// preview's snapshot holds: tabs, chords, arrangement, selection, key and rate. The knobs keep
// their current values.

void Session::restore (const Doc& d)
{
    doc.tabs = d.tabs;
    doc.chords = d.chords;
    doc.arr = d.arr;
    doc.sel = d.sel;
    doc.cur = d.cur;
    doc.tab = d.tab;
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
    foldTab (doc.T());
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

void Session::push (int editedPart)
{
    if (editedPart >= 0 && editedPart < kNumParts)
        rebase (doc.T(), editedPart);
    foldTab (doc.T());
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
    auto& t = d.T();
    // the other tabs follow the new root; then every riff follows the new scale
    const int dr = (((k.root - d.gen.root) % 12) + 12 + 6) % 12 - 6;
    for (auto& o : d.tabs)
        if (&o != &t && dr != 0)
            shiftTab (o, dr);
    d.gen.root = k.root;
    d.gen.scale = k.scale;
    for (auto& o : d.tabs)
        if (&o != &t && ! o.poly)
        {
            for (int part = 0; part < kNumParts; ++part)
                remap (o, part);
            foldTab (o);
        }
    int lo = 127, hi = 0;
    for (const auto& x : placed)
    {
        lo = std::min (lo, x.note);
        hi = std::max (hi, x.note);
    }
    t.lowNote = lo;
    t.highNote = std::min (hi, lo + kMaxSpan);
    t.rootNote = lo + ((d.gen.root - lo) % 12 + 12) % 12;
    d.rate = 4;
    for (auto& o : d.tabs)
        for (auto& p : o.parts)
        {
            p.numSteps = N;
            fitLengths (p);
        }
    for (auto& p : t.parts)
    {
        p = Pattern {};
        p.numSteps = N;
    }
    for (const auto& x : placed)
    {
        const int b = x.s / N;
        auto& st = t.parts[(size_t) b].steps[(size_t) (x.s - b * N)];
        st = Step {};
        st.on = true;
        st.note = x.note;
        st.length = std::min (kMaxLength, x.e - x.s);
        st.velocity = std::max (1, std::min (127, x.vel));
    }
    t.locks.fill (false);
    t.poly = false;
    for (int b = 0; b < kNumParts; ++b)
    {
        t.src[(size_t) b] = b < nBars ? Src::Import : Src::Roll;
        fitLengths (t.parts[(size_t) b]);
        rebase (t, b);
    }
    d.arr.clear();
    for (int b = 0; b < nBars; ++b)
        d.arr.push_back (b);
    d.sel = -1;
    d.cur = 0;
    d.gen.seed = newSeed();

    const int dropped = (int) raw.size() - (int) placed.size();
    std::string m = "Imported " + std::to_string (nBars) + (nBars > 1 ? " bars from " : " bar from ") + name + " \xE2\x86\x92 ";
    for (int b = 0; b < nBars; ++b)
        m += std::string (b ? " " : "") + partName (b);
    m += " on tab " + std::to_string (d.tab + 1);
    m += " \xC2\xB7 key guess " + std::string (noteNames()[(size_t) k.root]) + " " + scales()[(size_t) k.scale].name
       + " (" + std::to_string ((int) std::lround (k.confidence * 100)) + "% sure; change Key to correct it)";
    if (dropped > 0)
        m += " \xC2\xB7 " + std::to_string (dropped) + (dropped > 1 ? " overlapping or late notes dropped" : " overlapping or late note dropped");
    message = m;
    commitVersion();
    return true;
}

std::vector<uint8_t> Session::midiFile (double bpm)
{
    std::vector<MidiTrack> tracks;
    const auto seq = doc.playSeq();
    for (int i = 0; i < kNumTabs; ++i)
    {
        const auto& t = doc.tabs[(size_t) i];
        if (! t.on)
            continue;
        MidiTrack tr;
        tr.name = "Midinja tab " + std::to_string (i + 1) + (t.poly ? " chords" : " riff");
        tr.channel = t.channel;
        double offset = 0.0;
        for (int k : seq)
        {
            const auto& p = t.parts[(size_t) k];
            const auto chord = t.poly ? doc.chordNotes (k, t) : std::vector<int> {};
            for (auto e : renderCycle (p, doc.swing))
            {
                e.start += offset;
                if (t.poly)
                {
                    for (int n : chord)
                    {
                        auto c = e;
                        c.note = n;
                        tr.notes.push_back (c);
                    }
                }
                else
                    tr.notes.push_back (e);
            }
            offset += p.numSteps;
        }
        tracks.push_back (std::move (tr));
    }
    if (tracks.empty())
    {
        message = "No tab is on: turn one on in the tab bar to export";
        return {};
    }
    double length = 0.0;
    for (int k : seq)
        length += doc.T().parts[(size_t) k].numSteps;
    return writeMidiFileTracks (tracks, (double) doc.rate, bpm, length);
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
