#include "Document.h"
#include "core/MidiFileWriter.h"
#include "core/Scales.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <sstream>

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
        case ChordType::Ninth:   return "9th";
        case ChordType::Sus2:    return "sus2";
        case ChordType::Sus4:    return "sus4";
        case ChordType::Add9:    return "add9";
        case ChordType::Triad:   break;
    }
    return "Triad";
}

const char* rhythmName (Rhythm r)
{
    static const char* names[] = { "Whole", "Halves", "Quarters", "Stabs", "Push", "Offbeat", "Charleston", "Funk 16ths" };
    return names[std::clamp ((int) r, 0, kNumRhythms - 1)];
}

const char* moodName (int i)
{
    static const char* names[] = { "Bright", "Colour", "Fable", "Drama", "Dark" };
    return names[std::clamp (i, 0, kNumMoods - 1)];
}

const char* moodTip (int i)
{
    static const char* tips[] = { "calm, light, friendly", "unusual, curious, a hint of fantasy", "fairy tale, wide landscapes, folk",
                                  "sad, heroic, cinematic", "tense, gloomy, danger" };
    return tips[std::clamp (i, 0, kNumMoods - 1)];
}

const char* styleName (int i)
{
    static const char* names[] = { "Jazz", "R&B", "Funk", "House", "Drum & Bass" };
    return names[std::clamp (i, 0, kNumStyles - 1)];
}

const char* styleTip (int i)
{
    static const char* tips[] = { "ii\xE2\x80\x93V\xE2\x80\x93I and turnarounds in 7th chords, Charleston comping",
                                  "neo-soul colours: maj9, m9, chords that push into the next bar",
                                  "one- and two-chord vamps, 9th chords, syncopated 16th stabs; best in Dorian or Mixolydian",
                                  "deep-house loops of minor 7th and 9th chords, organ stabs on the offbeat",
                                  "liquid drum & bass: long pads of minor 9th and major 7th chords" };
    return tips[std::clamp (i, 0, kNumStyles - 1)];
}

std::vector<std::pair<int, int>> rhythmHits (Rhythm r, int L)
{
    std::vector<std::pair<int, int>> o;
    const int S16 = L * 4;
    switch (r)
    {
        case Rhythm::Whole:    o.push_back ({ 0, S16 }); break;
        case Rhythm::Halves:   for (int b = 0; b < L; b += 2) o.push_back ({ b * 4, 8 }); break;
        case Rhythm::Quarters: for (int b = 0; b < L; ++b) o.push_back ({ b * 4, 3 }); break;
        case Rhythm::Stabs:    for (int s = 0; s < S16; s += 16) for (auto [a, l] : { std::pair { 0, 2 }, std::pair { 6, 2 }, std::pair { 10, 2 } }) o.push_back ({ s + a, l }); break;
        case Rhythm::Push:     if (S16 > 2) { o.push_back ({ 0, S16 - 2 }); o.push_back ({ S16 - 2, 2 }); } else o.push_back ({ 0, S16 }); break;
        case Rhythm::Offbeat:  for (int b = 0; b < L; ++b) o.push_back ({ b * 4 + 2, 2 }); break;
        case Rhythm::Charleston: for (int s = 0; s < S16; s += 16) for (auto [a, l] : { std::pair { 0, 2 }, std::pair { 6, 2 }, std::pair { 8, 2 }, std::pair { 14, 2 } }) o.push_back ({ s + a, l }); break;
        case Rhythm::Funk:     for (int s = 0; s < S16; s += 16) for (auto [a, l] : { std::pair { 0, 1 }, std::pair { 3, 1 }, std::pair { 6, 2 }, std::pair { 10, 1 }, std::pair { 12, 1 }, std::pair { 14, 1 } }) o.push_back ({ s + a, l }); break;
    }
    std::vector<std::pair<int, int>> out;
    for (auto [a, l] : o)
        if (a < S16)
            out.push_back ({ a, std::min (l, S16 - a) });
    return out;
}

// ---------------------------------------------------------------------------------------------

namespace
{
// ---- the Beats tab ------------------------------------------------------------------------
struct HatDef { const char* name; bool (*f) (int s, int q, int n); };
const HatDef kHats[kNumHats] = {
    { "offbeat 8ths", [] (int s, int q, int) { return s % q == q / 2; } },
    { "8ths", [] (int s, int q, int) { return s % std::max (1, q / 2) == 0; } },
    { "16ths", [] (int, int, int) { return true; } },
    { "offbeats + pickup", [] (int s, int q, int n) { return s % q == q / 2 || s == n - 1; } },
    { "broken 16ths", [] (int s, int q, int) { return s % q != 1; } },
};
// eight slots over the last two beats, as rows; A' and B use the last four, C all eight
struct FillDef { const char* name; std::vector<int> slots[8]; };
const FillDef kFills[kNumFills] = {
    { "toms down", { { kRowSnare }, {}, { kRowSnare }, { kRowHiTom }, { kRowSnare }, { kRowHiTom }, { kRowMidTom }, { kRowLoTom } } },
    { "snare roll", { { kRowSnare }, {}, { kRowSnare }, { kRowSnare }, { kRowSnare }, { kRowSnare }, { kRowSnare }, { kRowSnare } } },
    { "snare and toms", { { kRowSnare }, { kRowHiTom }, { kRowSnare }, { kRowMidTom }, { kRowSnare }, { kRowSnare }, { kRowHiTom }, { kRowLoTom } } },
    { "tom pairs", { { kRowSnare }, {}, { kRowHiTom }, { kRowHiTom }, { kRowHiTom }, { kRowHiTom }, { kRowMidTom }, { kRowLoTom } } },
    { "clap build", { { kRowClap }, {}, { kRowClap }, { kRowSnare }, { kRowClap }, { kRowSnare }, { kRowClap, kRowSnare }, { kRowLoTom } } },
};
int fillLenOf (const DrumPart& p, int k, int n) { return p.fillOn && k > 0 ? (n / 4) * (k == 3 ? 2 : 1) : 0; }
void rebuildDrumPart (DrumPart& p, int k, int n, int velMin, int velMax)
{
    const int q = std::max (1, n / 4), L = fillLenOf (p, k, n), f0 = n - L;
    std::array<std::array<float, kMaxSteps>, kNumDrums> want {};
    const int openAt = p.open * q + q / 2;
    for (int s = 0; s < n; ++s)
    {
        if (s % q == 0 && (s < f0 || (s == f0 && p.kickFill)))
            want[kRowKick][(size_t) s] = s == 0 ? 1.0f : 0.9f;
        if (s >= f0)
            continue;
        if (s % q == 0 && (s / q) % 2 == 1)
        {
            want[kRowSnare][(size_t) s] = 0.95f;
            if (p.clap)
                want[kRowClap][(size_t) s] = 0.8f;
        }
        if (s == openAt)
            want[kRowOpenHat][(size_t) s] = 0.8f;
        else if (kHats[p.hat].f (s, q, n))
            want[kRowClosedHat][(size_t) s] = s % q == 0 ? 0.75f : s % q == q / 2 ? 0.6f : 0.45f;
    }
    for (int i = 0; i < L; ++i)
    {
        const int slot = std::min (7, (int) std::lround ((2 * q - L + i) * 8.0 / (2 * q)));
        for (int row : kFills[p.fill].slots[slot])
            want[(size_t) row][(size_t) (f0 + i)] = 0.6f + 0.4f * (L > 1 ? (float) i / (float) (L - 1) : 1.0f);
    }
    for (int r = 0; r < kNumDrums; ++r)
        for (int s = 0; s < kMaxSteps; ++s)
        {
            auto& h = p.hits[(size_t) r][(size_t) s];
            if (h.locked)
                continue;
            const float w = s < n ? want[(size_t) r][(size_t) s] : 0.0f;
            h.on = w > 0.0f;
            if (h.on)
                h.velocity = drumVel (w, velMin, velMax);
        }
}

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

std::mt19937& rng()
{
    static std::mt19937 r ((uint32_t) std::chrono::steady_clock::now().time_since_epoch().count());
    return r;
}

template <typename T>
const T& pick (const std::vector<T>& v)
{
    return v[(size_t) (rng()() % (uint32_t) v.size())];
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// typical progressions per scale, as scale degrees (0 = I); "*" for the others
const std::map<std::string, std::vector<std::vector<int>>>& progs()
{
    static const std::map<std::string, std::vector<std::vector<int>>> p = {
        { "Major", { { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 0, 3, 4, 0 }, { 0, 3, 5, 4 }, { 5, 3, 0, 4 }, { 0, 1, 3, 4 } } },
        { "Natural Minor", { { 0, 5, 2, 6 }, { 0, 3, 4, 0 }, { 0, 6, 5, 6 }, { 0, 5, 3, 4 }, { 0, 3, 6, 2 }, { 0, 2, 6, 3 } } },
        { "Dorian", { { 0, 3, 0, 3 }, { 0, 3, 6, 0 }, { 0, 1, 3, 0 }, { 0, 6, 3, 0 }, { 0, 2, 3, 0 } } },
        { "Phrygian", { { 0, 1, 0, 1 }, { 0, 1, 6, 0 }, { 0, 1, 2, 1 }, { 0, 6, 1, 0 } } },
        { "Lydian", { { 0, 1, 0, 1 }, { 0, 1, 6, 0 }, { 0, 1, 4, 0 }, { 0, 4, 1, 0 } } },
        { "Mixolydian", { { 0, 6, 3, 0 }, { 0, 3, 6, 0 }, { 0, 6, 0, 3 }, { 0, 4, 6, 3 } } },
        { "Locrian", { { 0, 1, 0, 1 }, { 0, 1, 5, 0 } } },
        { "Harmonic Minor", { { 0, 3, 4, 0 }, { 0, 5, 4, 0 }, { 0, 5, 3, 4 }, { 0, 3, 5, 4 } } },
        { "Melodic Minor", { { 0, 3, 4, 0 }, { 0, 1, 4, 0 }, { 0, 5, 3, 4 } } },
        { "Phrygian Dominant", { { 0, 1, 0, 1 }, { 3, 6, 1, 0 }, { 0, 6, 1, 0 }, { 0, 1, 6, 1 } } },
        { "*", { { 0, 3, 4, 0 }, { 0, 5, 3, 4 }, { 0, 1, 4, 0 } } },
    };
    return p;
}

std::string scaleKey (int scale)
{
    const std::string name = scales()[(size_t) scale].name;
    return name.rfind ("Major", 0) == 0 ? "Major" : name;
}

// styles: [degree, type] per bar, a rhythm, lists per scale, else by the scale's third
struct StyleDef
{
    Rhythm rhythm;
    std::map<std::string, std::vector<std::vector<std::pair<int, ChordType>>>> lists; // "maj", "min" or a scale name
};

std::vector<std::pair<int, ChordType>> P (const std::string& s)
{
    std::vector<std::pair<int, ChordType>> out;
    std::istringstream in (s);
    std::string tok;
    while (in >> tok)
    {
        const auto colon = tok.find (':');
        const int d = std::stoi (tok.substr (0, colon));
        const char t = colon == std::string::npos ? '7' : tok[colon + 1];
        out.push_back ({ d, t == '9' ? ChordType::Ninth : t == 'T' ? ChordType::Triad : ChordType::Seventh });
    }
    return out;
}

const std::vector<StyleDef>& styles()
{
    static const std::vector<StyleDef> s = {
        { Rhythm::Charleston, { { "Harmonic Minor", { P ("1:7 4:7 0:7 0:7"), P ("0:7 5:7 1:7 4:7") } },
                                { "maj", { P ("1:7 4:7 0:7 0:7"), P ("0:7 5:7 1:7 4:7"), P ("2:7 5:7 1:7 4:7"), P ("0:7 3:7 2:7 5:7") } },
                                { "min", { P ("1:7 4:7 0:7 0:7"), P ("0:7 3:7 6:7 2:7"), P ("0:7 5:7 1:7 4:7") } } } },
        { Rhythm::Push, { { "maj", { P ("3:9 2:7 5:9 4:7"), P ("1:9 4:9 0:9 5:9"), P ("0:9 5:9 1:9 4:9"), P ("3:9 4:9 2:7 5:9") } },
                          { "min", { P ("0:9 3:9 6:7 2:7"), P ("5:7 4:7 0:9 0:9"), P ("0:9 5:9 3:9 4:7") } } } },
        { Rhythm::Funk, { { "Dorian", { P ("0:9 3:7 0:9 3:7"), P ("0:9 1:7 0:9 1:7"), P ("0:9 0:9 3:9 3:9"), P ("0:9 6:7 0:9 6:7"), P ("0:9 0:9 0:9 3:7") } },
                          { "Mixolydian", { P ("0:9 6:7 0:9 6:7"), P ("0:9 0:9 0:9 0:9"), P ("0:9 3:7 0:9 3:7"), P ("0:9 6:7 3:7 0:9") } },
                          { "Phrygian", { P ("0:7 1:7 0:7 1:7"), P ("0:7 0:7 6:7 6:7") } },
                          { "maj", { P ("1:9 4:9 1:9 4:9"), P ("0:9 3:7 0:9 3:7"), P ("1:7 4:7 0:9 0:9") } },
                          { "min", { P ("0:9 3:7 0:9 3:7"), P ("0:9 6:7 5:7 6:7"), P ("0:9 0:9 3:9 3:9") } } } },
        { Rhythm::Offbeat, { { "Dorian", { P ("0:9 3:9 0:9 3:9"), P ("0:9 6:9 3:9 0:9") } },
                             { "maj", { P ("5:9 3:9 0:9 4:9"), P ("1:9 4:9 5:9 3:9"), P ("0:9 5:9 3:9 4:9") } },
                             { "min", { P ("0:9 3:9 0:9 3:9"), P ("0:7 5:7 6:7 3:7"), P ("0:9 2:7 3:9 2:7"), P ("0:9 5:9 6:9 5:9") } } } },
        { Rhythm::Whole, { { "maj", { P ("5:9 3:7 0:7 4:7"), P ("0:7 2:7 5:9 3:7") } },
                           { "min", { P ("0:9 5:7 2:7 6:7"), P ("0:9 0:9 5:7 5:7"), P ("0:9 3:9 5:7 4:7"), P ("0:9 5:7 3:9 6:7") } } } },
    };
    return s;
}

// mood moves "tonic -> chord": semitones from the tonic, major (true) / minor chord, strong
struct Move { int semis; bool major; bool strong; };
struct MoodDef { std::vector<Move> maj, min; };
const std::vector<MoodDef>& moods()
{
    static const std::vector<MoodDef> m = {
        { { { 2, true, true }, { 5, true, false }, { 7, true, false }, { 10, true, false }, { 2, false, false }, { 9, false, true } },
          { { 7, true, false }, { 10, true, false }, { 0, true, false } } },
        { { { 1, true, false }, { 6, true, true }, { 8, true, true }, { 11, true, false }, { 5, false, true }, { 11, false, false } },
          { { 1, true, false }, { 6, true, false }, { 9, true, false }, { 2, false, false } } },
        { { { 3, true, false }, { 4, true, false }, { 9, true, false }, { 7, false, true }, { 10, false, false } },
          { { 3, true, false }, { 4, true, false }, { 5, true, true }, { 7, false, true } } },
        { { { 1, false, false }, { 4, false, true }, { 0, false, false } },
          { { 2, true, true }, { 8, true, true }, { 11, true, true }, { 5, false, false }, { 10, false, false } } },
        { { { 3, false, false }, { 6, false, false }, { 8, false, false } },
          { { 1, false, false }, { 3, false, true }, { 4, false, false }, { 6, false, true }, { 8, false, true }, { 9, false, false }, { 11, false, false } } },
    };
    return m;
}
} // namespace

// ---------------------------------------------------------------------------------------------
// Doc

int drumNote (int row)
{
    static const int notes[kNumDrums] = { 49, 50, 47, 45, 46, 42, 39, 38, 36 };
    return notes[std::clamp (row, 0, kNumDrums - 1)];
}
const char* drumName (int row)
{
    static const char* names[kNumDrums] = { "Crash", "High tom", "Mid tom", "Low tom", "Open hat", "Closed hat", "Clap", "Snare", "Kick" };
    return names[std::clamp (row, 0, kNumDrums - 1)];
}
const char* hatName (int i) { return kHats[std::clamp (i, 0, kNumHats - 1)].name; }
const char* fillName (int i) { return kFills[std::clamp (i, 0, kNumFills - 1)].name; }
int drumVel (float weight, int velMin, int velMax)
{
    return std::clamp ((int) std::lround ((float) velMin + (float) (velMax - velMin) * weight), 1, 127);
}

int Doc::fillLen (int k) const
{
    return k >= 0 && k < kNumParts ? fillLenOf (beatTab().beats[(size_t) k], k, numSteps()) : 0;
}

bool Doc::autoCrash (int bar) const
{
    if (bar < 0 || bar >= (int) arr.size())
        return false;
    const int prev = arr[(size_t) ((bar - 1 + (int) arr.size()) % (int) arr.size())];
    return fillLen (prev) > 0 && ! beatTab().beats[(size_t) arr[(size_t) bar]].hits[kRowCrash][0].on;
}

std::vector<DrumEvent> Doc::drumHits (int k, int bar) const
{
    std::vector<DrumEvent> out;
    if (k < 0 || k >= kNumParts)
        return out;
    const auto& p = beatTab().beats[(size_t) k];
    const int n = numSteps();
    for (int s = 0; s < n; ++s)
        for (int r = 0; r < kNumDrums; ++r)
        {
            const auto& h = p.hits[(size_t) r][(size_t) s];
            if (h.on)
                out.push_back ({ s, r, h.velocity });
        }
    if (autoCrash (bar))
        out.insert (out.begin(), { 0, kRowCrash, drumVelocity (1.0f) });
    return out;
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
    for (bool l : riff().locks)
        if (l)
            return true;
    return false;
}

int Doc::shownBar() const
{
    if (sel >= 0 && sel < (int) arr.size())
        return sel;
    const auto it = std::find (arr.begin(), arr.end(), cur);
    return it == arr.end() ? -1 : (int) (it - arr.begin());
}

int Doc::scaleSize() const { return (int) scales()[(size_t) gen.scale].intervals.size(); }

int Doc::at (int k) const
{
    const auto& iv = scales()[(size_t) gen.scale].intervals;
    const int n = (int) iv.size();
    const int idx = ((k % n) + n) % n;
    return iv[(size_t) idx] + 12 * (int) std::floor ((double) k / n);
}

Quality Doc::qual (int d) const
{
    const int t = rel (d, d + 2), f = rel (d, d + 4);
    if (t == 4 && f == 7) return Quality::Maj;
    if (t == 3 && f == 7) return Quality::Min;
    if (t == 3 && f == 6) return Quality::Dim;
    if (t == 4 && f == 8) return Quality::Aug;
    return Quality::Maj;
}

int Doc::pcOf (int note) const { return ((note - gen.root) % 12 + 12) % 12; }
bool Doc::inScale (int note) const { return midinja::inScale (note, gen.root, gen.scale); }
bool Doc::tonicMinor() const { const auto& iv = scales()[(size_t) gen.scale].intervals; return iv.size() > 2 && iv[2] == 3; }

std::vector<int> Doc::degsOf (const Chord& c) const
{
    const int d = c.deg;
    switch (c.type)
    {
        case ChordType::Seventh: return { d, d + 2, d + 4, d + 6 };
        case ChordType::Ninth:   return { d, d + 2, d + 4, d + 6, d + 8 };
        case ChordType::Sus2:    return { d, d + 1, d + 4 };
        case ChordType::Sus4:    return { d, d + 3, d + 4 };
        case ChordType::Add9:    return { d, d + 2, d + 4, d + 8 };
        case ChordType::Triad:   break;
    }
    return { d, d + 2, d + 4 };
}

int Doc::maxInv (const Chord& c) const { return std::min (3, (int) degsOf (c).size() - 1); }

std::string Doc::chordName (const Chord& c) const
{
    const std::string r = noteNames()[(size_t) (((gen.root + at (c.deg)) % 12 + 12) % 12)];
    const Quality q = qual (c.deg);
    static const char* flat = "\xE2\x99\xAD";
    static const char* sharp = "\xE2\x99\xAF";
    if (c.type == ChordType::Sus2 || c.type == ChordType::Sus4)
        return r + chordTypeName (c.type);
    const std::string tri = q == Quality::Maj ? "" : q == Quality::Min ? "m" : q == Quality::Dim ? "dim" : "+";
    if (c.type == ChordType::Add9)
        return r + tri + "add9";
    if (c.type == ChordType::Seventh)
    {
        const int s = rel (c.deg, c.deg + 6);
        std::string m;
        if (q == Quality::Maj) m = s == 11 ? "maj7" : s == 10 ? "7" : "";
        else if (q == Quality::Min) m = s == 10 ? "m7" : s == 11 ? "m(maj7)" : "";
        else if (q == Quality::Dim) m = s == 10 ? std::string ("m7") + flat + "5" : s == 9 ? "dim7" : "";
        else m = s == 11 ? std::string ("maj7") + sharp + "5" : s == 10 ? std::string ("7") + sharp + "5" : "";
        return r + (m.empty() ? tri + "7" : m);
    }
    if (c.type == ChordType::Ninth)
    {
        const int s = rel (c.deg, c.deg + 6), nine = rel (c.deg, c.deg + 8) - 12;
        const std::string b9 = nine == 1 ? std::string (flat) + "9" : nine == 3 ? std::string (sharp) + "9" : "9";
        std::string base = tri;
        if (q == Quality::Maj) base = s == 11 ? "maj" : s == 10 ? "" : tri;
        else if (q == Quality::Min) base = s == 10 ? "m" : s == 11 ? "m(maj)" : tri;
        else if (q == Quality::Dim) base = s == 10 ? "m" : s == 9 ? "dim" : tri;
        else base = s == 11 ? "maj" : s == 10 ? "" : tri;
        const std::string fifth = q == Quality::Dim && s == 10 ? std::string (flat) + "5" : "";
        return b9 == "9" ? r + base + "9" + fifth : r + base + "7" + fifth + b9;
    }
    return r + tri;
}

std::string Doc::numeral (int deg, const ChordType* type) const
{
    const auto& iv = scales()[(size_t) gen.scale].intervals;
    const int n = (int) iv.size();
    const int d = ((deg % n) + n) % n;
    static const char* roman[] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII" };
    std::string s = d < 12 ? roman[d] : std::to_string (d + 1);
    std::string acc;
    if (n == 7)
    {
        static const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
        const int diff = iv[(size_t) d] - major[d];
        if (diff == -1) acc = "\xE2\x99\xAD";
        else if (diff == 1) acc = "\xE2\x99\xAF";
    }
    const Quality q = qual (d);
    if (type != nullptr && (*type == ChordType::Sus2 || *type == ChordType::Sus4))
        return acc + s + chordTypeName (*type);
    if (q == Quality::Min || q == Quality::Dim)
        s = lower (s);
    std::string suf = q == Quality::Dim ? "\xC2\xB0" : q == Quality::Aug ? "+" : "";
    if (type != nullptr)
    {
        if (*type == ChordType::Seventh)
        {
            const int x = rel (d, d + 6);
            suf = q == Quality::Dim ? (x == 10 ? "\xC3\xB8" "7" : "\xC2\xB0" "7") : (q == Quality::Maj && x == 11 ? "maj7" : "7");
        }
        else if (*type == ChordType::Ninth)
        {
            const int x = rel (d, d + 6);
            suf = std::string (q == Quality::Dim ? "\xC3\xB8" : "") + (q == Quality::Maj && x == 11 ? "maj9" : "9");
        }
        else if (*type == ChordType::Add9)
            suf += "add9";
    }
    return acc + s + suf;
}

std::vector<int> Doc::voice (const Chord& c) const
{
    const int base = chordTab().rootNote;
    std::vector<int> notes;
    for (int k : degsOf (c))
        notes.push_back (base + at (k));
    std::sort (notes.begin(), notes.end());
    for (int i = 0; i < c.inv && i < (int) notes.size(); ++i)
    {
        notes.front() += 12;
        std::sort (notes.begin(), notes.end());
    }
    const double anchor = base + 9.5;
    std::vector<int> best;
    double bestDist = 1.0e9;
    for (int k = -4; k <= 4; ++k)
    {
        std::vector<int> v;
        for (int n : notes)
            v.push_back (n + 12 * k);
        const double dist = std::abs ((v.front() + v.back()) / 2.0 - anchor);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = v;
        }
    }
    return best;
}

std::vector<int> Doc::chordPcs (const Chord& c) const
{
    std::vector<int> out;
    for (int k : degsOf (c))
        out.push_back (((gen.root + at (k)) % 12 + 12) % 12);
    return out;
}

std::vector<int> Doc::sortedChordIndices() const
{
    std::vector<int> idx;
    for (int i = 0; i < (int) chords.size(); ++i)
        idx.push_back (i);
    std::stable_sort (idx.begin(), idx.end(), [this] (int a, int b) { return chords[(size_t) a].start < chords[(size_t) b].start; });
    return idx;
}

const Chord* Doc::chordById (int id) const
{
    for (const auto& c : chords)
        if (c.id == id)
            return &c;
    return nullptr;
}

Chord* Doc::chordById (int id)
{
    for (auto& c : chords)
        if (c.id == id)
            return &c;
    return nullptr;
}

int Doc::chordAtBeat (int beat) const
{
    for (int i = 0; i < (int) chords.size(); ++i)
        if (chords[(size_t) i].start <= beat && beat < chords[(size_t) i].start + chords[(size_t) i].len)
            return i;
    return -1;
}

int Doc::firstChordInBar (int bar) const
{
    for (int i : sortedChordIndices())
        if (chords[(size_t) i].start >= bar * 4 && chords[(size_t) i].start < bar * 4 + 4)
            return i;
    return -1;
}

int Doc::barDegree (int bar) const
{
    int i = chordAtBeat (bar * 4);
    if (i < 0)
        i = firstChordInBar (bar);
    const int n = scaleSize();
    if (i >= 0)
        return ((chords[(size_t) i].deg % n) + n) % n;
    const int part = bar >= 0 && bar < (int) arr.size() ? arr[(size_t) bar] : 0;
    return partDegree (part, gen.scale) % n;
}

int Doc::degreeOf (int part) const
{
    const auto it = std::find (arr.begin(), arr.end(), part);
    if (it != arr.end())
        return barDegree ((int) (it - arr.begin()));
    return partDegree (part, gen.scale) % scaleSize();
}

std::vector<Hit> Doc::barHits (int bar) const
{
    std::vector<Hit> out;
    const int n = numSteps(), spb = stepsPerBeat();
    const auto toSteps = [n] (int x16) { return (int) std::lround (x16 * n / 16.0); };
    for (int i : sortedChordIndices())
    {
        const auto& c = chords[(size_t) i];
        const int cs = c.start * spb, ce = (c.start + c.len) * spb;
        for (auto [s16, l16] : rhythmHits (c.rhythm, c.len))
        {
            const int a = cs + toSteps (s16), l = std::max (1, toSteps (l16));
            const int local = a - bar * n;
            if (local < 0 || local >= n)
                continue;
            out.push_back ({ local, std::max (1, std::min ({ l, ce - a, (bar + 1) * n - a })), i });
        }
    }
    return out;
}

Pattern Doc::chordPat (int bar) const
{
    Pattern q;
    q.numSteps = numSteps();
    if (bar < 0)
        return q;
    for (const auto& h : barHits (bar))
    {
        auto& st = q.steps[(size_t) h.step];
        st.on = true;
        st.length = std::min (kMaxLength, h.len);
        st.note = 60;
    }
    return q;
}

int Doc::laneEnd() const
{
    int end = 0;
    for (const auto& c : chords)
        end = std::max (end, c.start + c.len);
    return end;
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
    const uint32_t s = rng()();
    return s != 0 ? s : 1u;
}

Chord Session::mk (int start, int len, int deg, ChordType type, int inv, Rhythm rhythm)
{
    Chord c;
    c.id = doc.nextChordId++;
    c.start = start;
    c.len = len;
    c.deg = deg;
    c.type = type;
    c.inv = inv;
    c.rhythm = rhythm;
    return c;
}

Session::Session()
{
    doc.gen.mode = kMusician;
    doc.gen.seed = 4821;
    auto& r = doc.tabs[kRiffTab];
    r.channel = 1; r.poly = false; r.on = true; r.rootNote = 40; r.lowNote = 40; r.highNote = 64;
    auto& c = doc.tabs[kChordTab];
    c.channel = 2; c.poly = true; c.on = false; c.rootNote = 52; c.lowNote = 52; c.highNote = 71;
    auto& b = doc.tabs[kBeatTab];
    b.channel = 10; b.drums = true; b.on = true; b.rootNote = 36; b.lowNote = 36; b.highNote = 36;
    for (int k = 0; k < kNumParts; ++k)
    {
        auto& p = b.beats[(size_t) k];
        p.hat = (int) (rng()() % kNumHats);
        p.open = rng()() % 2 ? 3 : 1;
        p.clap = (int) (rng()() % 2);
        p.fill = (int) (rng()() % kNumFills);
        p.fillOn = k > 0;
        p.kickFill = rng()() % 2 == 1;
        rebuildDrumPart (p, k, doc.numSteps(), doc.gen.velMin, doc.gen.velMax);
    }
    for (auto& t : doc.tabs)
        for (auto& p : t.parts)
            p.numSteps = doc.numSteps();
    doc.chords.push_back (mk (0, 4, 0, ChordType::Triad, 0, Rhythm::Whole));
    doc.csel = doc.chords.front().id;
    generate (r.parts[0], doc.genFor (r, 0));
    rebase (r, 0);
    commitVersion();
}

bool Session::currentOp (Op& out) const
{
    if (mutateMode) { out = Op::Mutate; return true; }
    if (doRhythm && doNotes) { out = Op::Roll; return true; }
    if (doRhythm) { out = Op::Rhythm; return true; }
    if (doNotes) { out = Op::Notes; return true; }
    return false;
}

unsigned Session::lockMask() const
{
    unsigned m = 0;
    for (int k = 0; k < kNumParts; ++k)
        if (doc.riff().locks[(size_t) k])
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
        const int lowerN = bn - alt, lpc = pcOf (lowerN);
        const size_t li = (size_t) (std::find (from.begin(), from.end(), lpc) - from.begin());
        return (li < to.size() ? lowerN - lpc + to[li] : nearestTo (lowerN, to)) + alt;
    };
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

void Session::foldPattern (Pattern& p, int lo, int hi) const
{
    const auto mi = modeInfo (doc.gen.scale);
    std::set<int> done;
    if (! mi.moves.empty())
    {
        std::vector<int> on;
        for (int i = 0; i < std::min (p.numSteps, kMaxSteps); ++i)
            if (p.steps[(size_t) i].on)
                on.push_back (i);
        for (size_t j = 0; j + 1 < on.size(); ++j)
        {
            auto& a = p.steps[(size_t) on[j]];
            auto& b = p.steps[(size_t) on[j + 1]];
            if (a.locked || b.locked || std::abs (a.note - b.note) > 4)
                continue;
            const int pa = doc.pcOf (a.note), pb = doc.pcOf (b.note);
            bool isPair = false;
            for (const auto& mv : mi.moves)
                isPair = isPair || (mv.first == pa && mv.second == pb) || (mv.first == pb && mv.second == pa);
            if (! isPair)
                continue;
            const auto inR = [&] (int x) { return x >= lo && x <= hi; };
            if (inR (a.note) && inR (b.note))
            {
                done.insert (on[j]); done.insert (on[j + 1]); ++j;
                continue;
            }
            for (int k : { -1, 1, -2, 2, -3, 3 })
                if (inR (a.note + 12 * k) && inR (b.note + 12 * k))
                {
                    a.note += 12 * k;
                    b.note += 12 * k;
                    done.insert (on[j]); done.insert (on[j + 1]);
                    break;
                }
            ++j;
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

void Session::shiftTab (Tab& t, int d)
{
    if (t.drums)
        return; // GM drum notes are fixed
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
    if (t.poly)
    {
        if (t.highNote - t.lowNote <= 12)
        {
            if (why) *why = "One octave at least: a four-note chord needs it";
            return false;
        }
        return true;
    }
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

// ---- the riff follows the lane --------------------------------------------------------------

void Session::fixChordTones (int k)
{
    auto& tab = doc.riff();
    auto& p = tab.parts[(size_t) k];
    const auto it = std::find (doc.arr.begin(), doc.arr.end(), k);
    if (it == doc.arr.end() || tab.locks[(size_t) k] || ! hasNotes (p))
        return;
    const int b = (int) (it - doc.arr.begin()), n = p.numSteps, spb = doc.stepsPerBeat();
    for (int i : doc.sortedChordIndices())
    {
        const auto& c = doc.chords[(size_t) i];
        const int s = c.start * spb - b * n;
        if (s < 0 || s >= n)
            continue;
        auto& st = p.steps[(size_t) s];
        if (st.locked)
            continue;
        const auto pcs = doc.chordPcs (c);
        if (! st.on)
        {
            int prev = s - 1;
            while (prev >= 0 && ! p.steps[(size_t) prev].on)
                --prev;
            const int ref = prev >= 0 ? p.steps[(size_t) prev].note : (tab.lowNote + tab.highNote) / 2;
            st = Step {};
            st.on = true;
            st.note = ref;
            st.velocity = prev >= 0 ? p.steps[(size_t) prev].velocity : 100;
            if (prev >= 0)
                p.steps[(size_t) prev].length = std::min (p.steps[(size_t) prev].length, s - prev);
        }
        const std::vector<int> want = s == 0 && doc.gen.startOnRoot ? std::vector<int> { pcs.front() } : pcs;
        st.note = nearestOfPcs (st.note, want, tab.lowNote, tab.highNote);
    }
    fitLengths (p);
}

void Session::fixAllChordTones()
{
    for (int k = 0; k < kNumParts; ++k)
        fixChordTones (k);
}

std::array<int, kNumParts> Session::degreesNow() const
{
    std::array<int, kNumParts> d {};
    for (int k = 0; k < kNumParts; ++k)
        d[(size_t) k] = doc.degreeOf (k);
    return d;
}

void Session::laneChanged (const std::array<int, kNumParts>* from)
{
    if (from != nullptr)
        for (int k = 0; k < kNumParts; ++k)
        {
            const int to = doc.degreeOf (k);
            if ((*from)[(size_t) k] != to)
                retune (doc.riff(), k, (*from)[(size_t) k], to);
        }
    fixAllChordTones();
}

// ---- the dice -------------------------------------------------------------------------------

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
    if (d.T().drums)
    {
        if (d.T().locks[(size_t) d.cur])
            return;
        d.gen.seed = newSeed();
        rollDrum (d.cur);
        commitVersion();
        return;
    }
    if (d.T().poly)
    {
        auto* c = d.chordById (d.csel);
        if (c == nullptr)
            return;
        if (c->lock)
        {
            say ("This chord is locked: unlock it to change its rhythm", true);
            return;
        }
        d.gen.seed = newSeed();
        std::vector<Rhythm> others;
        for (int i = 0; i < kNumRhythms; ++i)
            if ((Rhythm) i != c->rhythm)
                others.push_back ((Rhythm) i);
        c->rhythm = pick (others);
        commitVersion();
        return;
    }
    auto& t = d.riff();
    if (t.locks[(size_t) d.cur])
        return;
    d.gen.seed = newSeed();
    t.parts[(size_t) d.cur].numSteps = d.numSteps();
    rollMono (t, d.cur, op);
    t.src[(size_t) d.cur] = Src::Roll;
    fixChordTones (d.cur);
    rebase (t, d.cur);
    commitVersion();
}

void Session::applyAll (Op op, bool keepSeed)
{
    auto& d = doc;
    if (! keepSeed)
        d.gen.seed = newSeed();
    if (d.T().drums)
    {
        for (int k = 0; k < kNumParts; ++k)
            if (! d.T().locks[(size_t) k])
                rollDrum (k);
        commitVersion();
        return;
    }
    if (d.T().poly)
    {
        for (auto& c : d.chords)
        {
            if (c.lock)
                continue;
            std::vector<Rhythm> others;
            for (int i = 0; i < kNumRhythms; ++i)
                if ((Rhythm) i != c.rhythm)
                    others.push_back ((Rhythm) i);
            c.rhythm = pick (others);
        }
        commitVersion();
        return;
    }
    auto& t = d.riff();
    for (auto& p : t.parts)
        p.numSteps = d.numSteps();
    bool usual = true;
    for (int k = 0; k < kNumParts; ++k)
        usual = usual && d.degreeOf (k) == partDegree (k, d.gen.scale);
    if (op == Op::Roll && usual)
        generateForm (t.parts, lockMask(), d.genFor (t, 0));
    else
        for (int k = 0; k < kNumParts; ++k)
            if (! t.locks[(size_t) k])
                rollMono (t, k, op);
    fixAllChordTones();
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

bool Session::extendPart (Tab& t, int k)
{
    if (t.poly || t.drums)
        return false;
    const auto& a = t.parts[0];
    auto& p = t.parts[(size_t) k];
    if (k == 0 || hasNotes (p) || ! hasNotes (a))
        return false;
    Pattern q = a;
    for (auto& s : q.steps)
        s.locked = false;
    if (k != kPartAPrime)
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
    rebase (t, k);
    return true;
}

// ---- the key --------------------------------------------------------------------------------

void Session::setScale (int scale)
{
    doc.gen.scale = std::clamp (scale, 0, (int) scales().size() - 1);
    const int n = doc.scaleSize();
    for (auto& c : doc.chords)
    {
        c.deg = ((c.deg % n) + n) % n;
        c.inv = std::min (c.inv, doc.maxInv (c));
    }
    for (auto& t : doc.tabs)
    {
        if (t.poly || t.drums)
            continue;
        for (int k = 0; k < kNumParts; ++k)
            remap (t, k);
        foldTab (t);
    }
    fixAllChordTones();
    say (std::string ("Degrees kept; chord quality now comes from ") + scales()[(size_t) doc.gen.scale].name);
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
    if (t.drums)
    {
        say ("The Beats tab has no octave: GM drum notes are fixed", true);
        return;
    }
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
    for (int k = 0; k < kNumParts; ++k)
        rebuildDrum (k);
    push();
}

// ---- tabs -----------------------------------------------------------------------------------

void Session::selectTab (int i)
{
    if (i >= 0 && i < kNumTabs)
        doc.tab = i;
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

// ---- the chord lane -------------------------------------------------------------------------

void Session::selectChord (int id)
{
    if (doc.chordById (id) != nullptr)
        doc.csel = id;
}

void Session::setChordSpan (int id, int start, int len, bool record)
{
    auto* c = doc.chordById (id);
    if (c == nullptr)
        return;
    const auto from = degreesNow();
    c->start = std::max (0, start);
    c->len = std::max (1, len);
    if (record)
    {
        laneChanged (&from);
        push();
    }
}

void Session::setChordDeg (int id, int deg)
{
    auto* c = doc.chordById (id);
    if (c == nullptr)
        return;
    const auto from = degreesNow();
    const int n = doc.scaleSize();
    c->deg = ((deg % n) + n) % n;
    if (c->inv > doc.maxInv (*c))
        c->inv = 0;
    laneChanged (&from);
    push();
}

void Session::setChordType (int id, ChordType type)
{
    auto* c = doc.chordById (id);
    if (c == nullptr)
        return;
    const auto from = degreesNow();
    c->type = type;
    c->inv = std::min (c->inv, doc.maxInv (*c));
    laneChanged (&from);
    push();
}

void Session::setChordInv (int id, int inv)
{
    auto* c = doc.chordById (id);
    if (c == nullptr)
        return;
    c->inv = std::clamp (inv, 0, doc.maxInv (*c));
    push();
}

void Session::cycleInversion (int id)
{
    auto* c = doc.chordById (id);
    if (c == nullptr)
        return;
    c->inv = c->inv >= doc.maxInv (*c) ? 0 : c->inv + 1;
    push();
}

void Session::setChordRhythm (int id, Rhythm r)
{
    auto* c = doc.chordById (id);
    if (c == nullptr)
        return;
    c->rhythm = r;
    push();
}

void Session::rhythmToAll (int id)
{
    auto* c = doc.chordById (id);
    if (c == nullptr)
        return;
    for (auto& x : doc.chords)
        x.rhythm = c->rhythm;
    say (std::string ("Rhythm \"") + rhythmName (c->rhythm) + "\" on every chord");
    push();
}

void Session::toggleChordLock (int id)
{
    auto* c = doc.chordById (id);
    if (c == nullptr)
        return;
    c->lock = ! c->lock;
    push();
}

void Session::regenChord (int id)
{
    auto* c = doc.chordById (id);
    if (c == nullptr)
        return;
    if (c->lock)
    {
        say ("This chord is locked: unlock it to change it", true);
        return;
    }
    const auto order = doc.sortedChordIndices();
    const int me = (int) (c - doc.chords.data());
    const auto pos = std::find (order.begin(), order.end(), me);
    std::set<int> avoid { c->deg };
    if (pos != order.begin()) avoid.insert (doc.chords[(size_t) *(pos - 1)].deg);
    if (pos + 1 != order.end()) avoid.insert (doc.chords[(size_t) *(pos + 1)].deg);
    std::vector<int> ok;
    for (int d = 0; d < doc.scaleSize(); ++d)
        if (doc.qual (d) != Quality::Dim && ! avoid.count (d))
            ok.push_back (d);
    if (ok.empty())
        return;
    const auto from = degreesNow();
    c->deg = pick (ok);
    if (c->inv > doc.maxInv (*c))
        c->inv = 0;
    laneChanged (&from);
    commitVersion();
}

void Session::addChordAt (int beat)
{
    std::vector<std::pair<int, int>> gaps;
    int p = 0;
    for (int i : doc.sortedChordIndices())
    {
        const auto& c = doc.chords[(size_t) i];
        if (c.start > p)
            gaps.push_back ({ p, c.start });
        p = c.start + c.len;
    }
    if (p < doc.totalBeats())
        gaps.push_back ({ p, doc.totalBeats() });
    const std::pair<int, int>* g = nullptr;
    for (const auto& x : gaps)
        if (beat >= x.first && beat < x.second)
            g = &x;
    if (g == nullptr)
    {
        say ("No free space in the lane: shorten a chord first", true);
        return;
    }
    int start = std::max (g->first, beat / 4 * 4);
    if (start >= g->second)
        start = beat;
    Chord c = mk (start, std::min (4, g->second - start), 0, ChordType::Seventh, 0, Rhythm::Whole);
    doc.chords.push_back (c);
    doc.csel = c.id;
    laneChanged (nullptr);
    message.clear();
    push();
}

void Session::removeChord (int id)
{
    const auto from = degreesNow();
    doc.chords.erase (std::remove_if (doc.chords.begin(), doc.chords.end(), [id] (const Chord& c) { return c.id == id; }), doc.chords.end());
    if (doc.csel == id)
    {
        const auto order = doc.sortedChordIndices();
        doc.csel = order.empty() ? -1 : doc.chords[(size_t) order.front()].id;
    }
    laneChanged (&from);
    push();
}

// ---- generators -----------------------------------------------------------------------------

void Session::addPartBar (int k)
{
    doc.arr.push_back (k);
    doc.sel = -1;
    for (auto& t : doc.tabs)
        extendPart (t, k);
}

void Session::fourBars()
{
    while ((int) doc.arr.size() < kMaxBars)
    {
        int k = (int) doc.arr.size() % kNumParts;
        for (int x = 0; x < kNumParts; ++x)
            if (std::find (doc.arr.begin(), doc.arr.end(), x) == doc.arr.end()) { k = x; break; }
        addPartBar (k);
    }
    doc.sel = -1;
}

void Session::fillBars (const std::vector<int>& degs, const std::string& label, const std::vector<ChordType>* types, const Rhythm* rhythm)
{
    const auto from = degreesNow();
    fourBars();
    const int n = std::min ((int) degs.size(), (int) doc.arr.size());
    const Chord* first = nullptr;
    for (int i : doc.sortedChordIndices())
        if (! doc.chords[(size_t) i].lock) { first = &doc.chords[(size_t) i]; break; }
    const Rhythm keepRh = rhythm != nullptr ? *rhythm : first != nullptr ? first->rhythm : Rhythm::Whole;
    const ChordType keepTy = first != nullptr ? first->type : ChordType::Seventh;
    for (int b = 0; b < n; ++b)
    {
        bool lockedHere = false;
        for (const auto& c : doc.chords)
            lockedHere = lockedHere || (c.lock && doc.overlapsBar (c, b));
        if (lockedHere)
            continue;
        doc.chords.erase (std::remove_if (doc.chords.begin(), doc.chords.end(), [&] (const Chord& c) { return ! c.lock && doc.overlapsBar (c, b); }), doc.chords.end());
        doc.chords.push_back (mk (b * 4, 4, degs[(size_t) b], types != nullptr ? (*types)[(size_t) b] : keepTy, 0, keepRh));
    }
    if (doc.chordById (doc.csel) == nullptr)
    {
        const auto order = doc.sortedChordIndices();
        doc.csel = order.empty() ? -1 : doc.chords[(size_t) order.front()].id;
    }
    laneChanged (&from);
    say (label);
    commitVersion();
}

void Session::progression()
{
    const auto& all = progs();
    const auto it = all.find (scaleKey (doc.gen.scale));
    const auto& lists = it != all.end() ? it->second : all.at ("*");
    std::vector<int> cur;
    for (int i : doc.sortedChordIndices())
        cur.push_back (doc.chords[(size_t) i].deg);
    std::vector<std::vector<int>> opts;
    for (const auto& l : lists)
        if (l != cur)
            opts.push_back (l);
    const auto l = pick (opts.empty() ? lists : opts);
    std::string label = "Progression: ";
    for (size_t i = 0; i < l.size(); ++i)
        label += (i ? " \xE2\x80\x93 " : "") + doc.numeral (l[i]);
    fillBars (l, label, nullptr, nullptr);
}

std::vector<int> Session::moodDegrees (int mood) const
{
    std::vector<int> out;
    const auto& m = moods()[(size_t) std::clamp (mood, 0, kNumMoods - 1)];
    const auto& list = doc.tonicMinor() ? m.min : m.maj;
    for (int d = 1; d < doc.scaleSize(); ++d)
    {
        const int s = doc.at (d);
        const Quality q = doc.qual (d);
        for (const auto& mv : list)
            if (mv.semis == s && ((mv.major && q == Quality::Maj) || (! mv.major && q == Quality::Min)))
            {
                out.push_back (d);
                if (mv.strong)
                    out.push_back (d);
            }
    }
    return out;
}

bool Session::moodAvailable (int mood) const { return ! moodDegrees (mood).empty(); }

void Session::mood (int mood)
{
    const auto mv = moodDegrees (mood);
    if (mv.empty())
        return;
    const int a = pick (mv);
    std::vector<int> others;
    for (int d : mv)
        if (d != a)
            others.push_back (d);
    const int b = others.empty() ? a : pick (others);
    static const std::pair<int, Quality> home[] = { { 7, Quality::Maj }, { 10, Quality::Maj }, { 5, Quality::Maj }, { 5, Quality::Min }, { 7, Quality::Min } };
    int c = -1;
    for (const auto& [s, q] : home)
    {
        for (int d = 1; d < doc.scaleSize() && c < 0; ++d)
            if (doc.at (d) == s && doc.qual (d) == q && d != b)
                c = d;
        if (c >= 0)
            break;
    }
    if (c < 0)
        c = a;
    const std::vector<int> degs { 0, a, b, c };
    std::string label = std::string (moodName (mood)) + ": ";
    for (size_t i = 0; i < degs.size(); ++i)
        label += (i ? " \xE2\x80\x93 " : "") + doc.numeral (degs[i]);
    fillBars (degs, label, nullptr, nullptr);
}

bool Session::styleHasOwnList (int style) const
{
    const auto& st = styles()[(size_t) std::clamp (style, 0, kNumStyles - 1)];
    return st.lists.count (scaleKey (doc.gen.scale)) > 0;
}

void Session::style (int style)
{
    const auto& st = styles()[(size_t) std::clamp (style, 0, kNumStyles - 1)];
    const auto it = st.lists.find (scaleKey (doc.gen.scale));
    const auto& lists = it != st.lists.end() ? it->second : st.lists.at (doc.tonicMinor() ? "min" : "maj");
    std::vector<int> cur;
    for (int i : doc.sortedChordIndices())
        cur.push_back (doc.chords[(size_t) i].deg);
    std::vector<std::vector<std::pair<int, ChordType>>> opts;
    for (const auto& l : lists)
    {
        std::vector<int> degs;
        for (const auto& x : l)
            degs.push_back (x.first);
        if (degs != cur)
            opts.push_back (l);
    }
    const auto l = pick (opts.empty() ? lists : opts);
    std::vector<int> degs;
    std::vector<ChordType> types;
    std::string label = std::string (styleName (style)) + ": ";
    for (size_t i = 0; i < l.size(); ++i)
    {
        degs.push_back (l[i].first);
        types.push_back (l[i].second);
        label += (i ? " \xE2\x80\x93 " : "") + doc.numeral (l[i].first, &l[i].second);
    }
    label += std::string (" \xC2\xB7 rhythm ") + rhythmName (st.rhythm);
    const Rhythm r = st.rhythm;
    fillBars (degs, label, &types, &r);
}

bool Session::applyTyped (const std::string& text)
{
    std::vector<std::pair<int, ChordType>> list;
    std::string tok;
    const auto flush = [&] () -> bool {
        if (tok.empty())
            return true;
        std::string t = tok;
        tok.clear();
        size_t p = 0;
        if (t[0] == 'b' || t[0] == '#') ++p;
        else if (t.size() >= 3 && (t.compare (0, 3, "\xE2\x99\xAD") == 0 || t.compare (0, 3, "\xE2\x99\xAF") == 0)) p += 3;
        static const char* numerals[] = { "VII", "VI", "IV", "V", "III", "II", "I" };
        static const int values[] = { 6, 5, 3, 4, 2, 1, 0 };
        int deg = -1;
        size_t len = 0;
        for (int i = 0; i < 7; ++i)
        {
            const std::string nm = numerals[i];
            if (t.size() >= p + nm.size() && lower (t.substr (p, nm.size())) == lower (nm)) { deg = values[i]; len = nm.size(); break; }
        }
        if (deg < 0)
        {
            say ("Could not read \"" + t + "\". Use numerals like i, IV, bVII, V7, iisus4.", true);
            return false;
        }
        const std::string suf = lower (t.substr (p + len));
        ChordType type = ChordType::Triad;
        if (suf.find ("sus2") != std::string::npos) type = ChordType::Sus2;
        else if (suf.find ("sus") != std::string::npos) type = ChordType::Sus4;
        else if (suf.find ("add9") != std::string::npos) type = ChordType::Add9;
        else if (suf.find ('9') != std::string::npos) type = ChordType::Ninth;
        else if (suf.find ('7') != std::string::npos) type = ChordType::Seventh;
        list.push_back ({ deg % doc.scaleSize(), type });
        return true;
    };
    for (char ch : text)
    {
        if (ch == ' ' || ch == ',' || ch == '-' || ch == '|' || ch == '\t')
        {
            if (! flush())
                return false;
        }
        else
            tok += ch;
    }
    if (! flush())
        return false;
    if (list.empty())
    {
        say ("Type some numerals first, e.g. i IV i VII", true);
        return false;
    }
    const auto from = degreesNow();
    if ((int) list.size() > (int) doc.arr.size())
        fourBars();
    const int n = (int) list.size(), T = doc.totalBeats();
    const int len = n <= (int) doc.arr.size() ? 4 : std::max (1, T / n);
    doc.chords.clear();
    for (int i = 0; i < std::min (n, T / len); ++i)
        doc.chords.push_back (mk (i * len, len, list[(size_t) i].first, list[(size_t) i].second, 0, Rhythm::Whole));
    doc.csel = doc.chords.empty() ? -1 : doc.chords.front().id;
    laneChanged (&from);
    say ("Applied " + std::to_string (doc.chords.size()) + (doc.chords.size() > 1 ? " chords" : " chord") + " \xC2\xB7 quality from the scale");
    commitVersion();
    return true;
}

// ---- arrangement ----------------------------------------------------------------------------

void Session::selectPart (int k)
{
    if (k >= 0 && k < kNumParts)
        doc.cur = k;
}

void Session::choosePart (int k)
{
    if (k < 0 || k >= kNumParts)
        return;
    doc.cur = k;
    doc.sel = -1;
}

void Session::selectSlot (int i)
{
    doc.sel = doc.sel == i ? -1 : i;
    if (doc.sel < 0 || doc.sel >= (int) doc.arr.size())
        return;
    doc.cur = doc.arr[(size_t) doc.sel];
    if (doc.T().poly)
    {
        int c = doc.firstChordInBar (doc.sel);
        if (c < 0)
            c = doc.chordAtBeat (doc.sel * 4);
        if (c >= 0)
            doc.csel = doc.chords[(size_t) c].id;
    }
}

void Session::toggleLock (int k)
{
    auto& t = doc.T().drums ? doc.T() : doc.riff();
    t.locks[(size_t) k] = ! t.locks[(size_t) k];
    push();
}

void Session::removeBar (int i)
{
    if (i < 0 || i >= (int) doc.arr.size())
        return;
    const auto from = degreesNow();
    doc.arr.erase (doc.arr.begin() + i);
    const int a = i * 4, z = a + 4;
    doc.chords.erase (std::remove_if (doc.chords.begin(), doc.chords.end(), [&] (const Chord& c) { return c.start >= a && c.start + c.len <= z; }), doc.chords.end());
    for (auto& c : doc.chords)
    {
        if (c.start < a && c.start + c.len > a)
            c.len = std::min (c.len, a - c.start) + std::max (0, c.start + c.len - z);
        else if (c.start < z && c.start + c.len > z)
        {
            c.len = c.start + c.len - z;
            c.start = a;
        }
        else if (c.start >= z)
            c.start -= 4;
    }
    doc.chords.erase (std::remove_if (doc.chords.begin(), doc.chords.end(), [] (const Chord& c) { return c.len <= 0; }), doc.chords.end());
    if (doc.sel == i)
        doc.sel = -1;
    else if (doc.sel > i)
        --doc.sel;
    if (doc.chordById (doc.csel) == nullptr)
    {
        const auto order = doc.sortedChordIndices();
        doc.csel = order.empty() ? -1 : doc.chords[(size_t) order.front()].id;
    }
    laneChanged (&from);
    push();
}

void Session::setBarPart (int i, int k)
{
    if (i < 0 || i >= (int) doc.arr.size() || k < 0 || k >= kNumParts || doc.arr[(size_t) i] == k)
        return;
    doc.arr[(size_t) i] = k;
    doc.cur = k;
    if (doc.sel >= 0)
        doc.sel = i;
    for (auto& t : doc.tabs)
        extendPart (t, k);
    laneChanged (nullptr);
    push();
}

void Session::repeatLastBar()
{
    if ((int) doc.arr.size() >= kMaxBars)
        return;
    const auto from = degreesNow();
    const int last = (int) doc.arr.size() - 1;
    doc.arr.push_back (last >= 0 ? doc.arr.back() : 0);
    doc.sel = -1;
    // on Chords the new bar starts with the chords of the bar before it (chords are per bar)
    if (doc.T().poly && last >= 0)
        for (int i : doc.sortedChordIndices())
        {
            const Chord c = doc.chords[(size_t) i];
            if (! doc.overlapsBar (c, last))
                continue;
            const int s = std::max (c.start, last * 4), e = std::min (c.start + c.len, last * 4 + 4);
            doc.chords.push_back (mk (s + 4, e - s, c.deg, c.type, c.inv, c.rhythm));
        }
    laneChanged (&from);
    push();
}

void Session::copyBarChords (int b)
{
    if (b < 0 || b >= (int) doc.arr.size())
        return;
    const int k = doc.arr[(size_t) b];
    std::vector<int> targets;
    for (int j = 0; j < (int) doc.arr.size(); ++j)
        if (doc.arr[(size_t) j] == k && j != b)
            targets.push_back (j);
    if (targets.empty())
        return;
    const auto from = degreesNow();
    struct Span { int start, end; Chord c; };
    std::vector<Span> src; // the source bar's chords, clipped to the bar
    for (int i : doc.sortedChordIndices())
    {
        const auto& c = doc.chords[(size_t) i];
        if (doc.overlapsBar (c, b))
            src.push_back ({ std::max (c.start, b * 4), std::min (c.start + c.len, b * 4 + 4), c });
    }
    int kept = 0, placed = 0;
    for (int b2 : targets)
    {
        const int a = b2 * 4, z = a + 4;
        std::vector<Chord> locked;
        for (const auto& c : doc.chords)
            if (c.lock && doc.overlapsBar (c, b2))
                locked.push_back (c);
        kept += (int) locked.size();
        // the unlocked chords leave the bar: inside ones go, crossing ones are cut at the bar line
        doc.chords.erase (std::remove_if (doc.chords.begin(), doc.chords.end(), [&] (const Chord& c) { return ! c.lock && c.start >= a && c.start + c.len <= z; }), doc.chords.end());
        for (auto& c : doc.chords)
        {
            if (c.lock || ! doc.overlapsBar (c, b2))
                continue;
            if (c.start < a)
                c.len = a - c.start;
            else
            {
                c.len = c.start + c.len - z;
                c.start = z;
            }
        }
        doc.chords.erase (std::remove_if (doc.chords.begin(), doc.chords.end(), [] (const Chord& c) { return c.len <= 0; }), doc.chords.end());
        // the copies, trimmed around the locked chords
        for (const auto& s : src)
        {
            int st = s.start + (b2 - b) * 4, en = s.end + (b2 - b) * 4;
            for (const auto& L : locked)
            {
                if (L.start <= st && st < L.start + L.len) st = L.start + L.len;
                if (st < L.start && L.start < en) en = L.start;
            }
            if (en > st)
            {
                doc.chords.push_back (mk (st, en - st, s.c.deg, s.c.type, s.c.inv, s.c.rhythm));
                ++placed;
            }
        }
    }
    if (doc.chordById (doc.csel) == nullptr)
    {
        const auto order = doc.sortedChordIndices();
        doc.csel = order.empty() ? -1 : doc.chords[(size_t) order.front()].id;
    }
    std::string msg = "Bar " + std::to_string (b + 1) + " \xE2\x86\x92 ";
    for (size_t j = 0; j < targets.size(); ++j)
        msg += (j > 0 ? ", bar " : "bar ") + std::to_string (targets[j] + 1);
    msg += ": " + std::to_string (placed) + (placed == 1 ? " chord copied" : " chords copied");
    if (kept > 0)
        msg += ", " + std::to_string (kept) + " locked kept";
    say (msg);
    laneChanged (&from);
    push();
}

int Session::barLocks() const
{
    const int b = doc.shownBar();
    if (doc.T().poly)
    {
        int n = 0;
        if (b >= 0)
            for (const auto& c : doc.chords)
                if (c.lock && doc.overlapsBar (c, b))
                    ++n;
        return n;
    }
    if (doc.T().drums)
        return (doc.T().locks[(size_t) doc.cur] ? 1 : 0) + lockedHits (doc.cur);
    const auto& p = doc.shown();
    int n = doc.riff().locks[(size_t) doc.cur] ? 1 : 0;
    for (int i = 0; i < std::min (p.numSteps, kMaxSteps); ++i)
        if (p.steps[(size_t) i].locked)
            ++n;
    return n;
}

void Session::unlockBar()
{
    const int b = doc.shownBar();
    if (doc.T().poly)
    {
        for (auto& c : doc.chords)
            if (doc.overlapsBar (c, b))
                c.lock = false;
    }
    else if (doc.T().drums)
    {
        doc.T().locks[(size_t) doc.cur] = false;
        for (auto& row : doc.T().beats[(size_t) doc.cur].hits)
            for (auto& h : row)
                h.locked = false;
    }
    else
    {
        doc.riff().locks[(size_t) doc.cur] = false;
        for (auto& s : doc.shown().steps)
            s.locked = false;
    }
    push();
}

// ---------------------------------------------------------------------------------------------
// The Beats tab

void Session::rebuildDrum (int k)
{
    if (k < 0 || k >= kNumParts)
        return;
    rebuildDrumPart (doc.beatTab().beats[(size_t) k], k, doc.numSteps(), doc.gen.velMin, doc.gen.velMax);
}

void Session::rollDrum (int k)
{
    if (k < 0 || k >= kNumParts)
        return;
    auto& p = doc.beatTab().beats[(size_t) k];
    std::vector<int> hats, fills;
    for (int i = 0; i < kNumHats; ++i)
        if (i != p.hat)
            hats.push_back (i);
    for (int i = 0; i < kNumFills; ++i)
        if (i != p.fill)
            fills.push_back (i);
    p.hat = pick (hats);
    p.open = rng()() % 2 ? 3 : 1;
    p.clap = (int) (rng()() % 2);
    if (k > 0)
        p.fill = pick (fills);
    p.kickFill = rng()() % 2 == 1;
    rebuildDrum (k);
}

void Session::setFill (int k, bool on)
{
    if (k <= 0 || k >= kNumParts)
        return; // A is the plain groove
    doc.beatTab().beats[(size_t) k].fillOn = on;
    rebuildDrum (k);
    push();
}

void Session::toggleDrumHit (int row, int step)
{
    if (row < 0 || row >= kNumDrums || step < 0 || step >= doc.numSteps())
        return;
    auto& h = doc.beatTab().beats[(size_t) doc.cur].hits[(size_t) row][(size_t) step];
    if (h.on)
    {
        h.on = false;
        h.locked = false;
    }
    else
    {
        if (h.locked)
            return;
        h.on = true;
        h.velocity = doc.drumVelocity (step % doc.stepsPerBeat() == 0 ? 0.95f : 0.7f);
    }
    push();
}

void Session::toggleDrumLock (int row, int step)
{
    if (row < 0 || row >= kNumDrums || step < 0 || step >= doc.numSteps())
        return;
    auto& h = doc.beatTab().beats[(size_t) doc.cur].hits[(size_t) row][(size_t) step];
    h.locked = ! h.locked;
    push();
}

void Session::cycleDrumVel (int row, int step)
{
    if (row < 0 || row >= kNumDrums || step < 0 || step >= doc.numSteps())
        return;
    auto& h = doc.beatTab().beats[(size_t) doc.cur].hits[(size_t) row][(size_t) step];
    if (! h.on)
        return;
    const int soft = doc.drumVelocity (0.5f), normal = doc.drumVelocity (0.8f), accent = doc.drumVelocity (1.0f);
    h.velocity = h.velocity >= doc.drumVelocity (0.95f) ? soft : h.velocity >= doc.drumVelocity (0.7f) ? accent : normal;
    push();
}

int Session::lockedHits (int k) const
{
    if (k < 0 || k >= kNumParts)
        return 0;
    int n = 0;
    const auto& p = doc.beatTab().beats[(size_t) k];
    for (int r = 0; r < kNumDrums; ++r)
        for (int s = 0; s < doc.numSteps(); ++s)
            n += p.hits[(size_t) r][(size_t) s].locked ? 1 : 0;
    return n;
}

void Session::unlockHits (int k)
{
    if (k < 0 || k >= kNumParts)
        return;
    for (auto& row : doc.beatTab().beats[(size_t) k].hits)
        for (auto& h : row)
            h.locked = false;
    push();
}

// ---------------------------------------------------------------------------------------------
// History

void Session::restore (const Doc& d)
{
    doc.tabs = d.tabs;
    doc.chords = d.chords;
    doc.nextChordId = std::max (doc.nextChordId, d.nextChordId);
    doc.csel = d.csel;
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
    foldTab (doc.riff());
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
        rebase (doc.riff(), editedPart);
    foldTab (doc.riff());
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
                continue;
            if (x.s < last.e)
            {
                if (x.note < last.note) last.e = x.s;
                else continue;
            }
        }
        x.e = std::min (x.e, (x.s / N + 1) * N);
        placed.push_back (x);
    }
    if (placed.empty())
    {
        say ("No notes found in " + name, true);
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
    d.tab = kRiffTab;
    auto& t = d.riff();
    const int dr = (((k.root - d.gen.root) % 12) + 12 + 6) % 12 - 6;
    if (dr != 0)
        shiftTab (d.tabs[kChordTab], dr);
    d.gen.root = k.root;
    d.gen.scale = k.scale;
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
    for (int k = 0; k < kNumParts; ++k)
        rebuildDrumPart (d.tabs[kBeatTab].beats[(size_t) k], k, N, d.gen.velMin, d.gen.velMax);
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
    for (int b = 0; b < kNumParts; ++b)
    {
        t.src[(size_t) b] = b < nBars ? Src::Import : Src::Roll;
        fitLengths (t.parts[(size_t) b]);
        rebase (t, b);
    }
    d.arr.clear();
    d.chords.clear();
    for (int b = 0; b < nBars; ++b)
    {
        d.arr.push_back (b);
        d.chords.push_back (mk (b * 4, 4, partDegree (b, d.gen.scale), ChordType::Triad, 0, Rhythm::Whole));
    }
    d.csel = d.chords.empty() ? -1 : d.chords.front().id;
    d.sel = -1;
    d.cur = 0;
    d.gen.seed = newSeed();

    const int dropped = (int) raw.size() - (int) placed.size();
    std::string m = "Imported " + std::to_string (nBars) + (nBars > 1 ? " bars from " : " bar from ") + name + " \xE2\x86\x92 ";
    for (int b = 0; b < nBars; ++b)
        m += std::string (b ? " " : "") + partName (b);
    m += " \xC2\xB7 key guess " + std::string (noteNames()[(size_t) k.root]) + " " + scales()[(size_t) k.scale].name
       + " (" + std::to_string ((int) std::lround (k.confidence * 100)) + "% sure; change Key to correct it)";
    if (dropped > 0)
        m += " \xC2\xB7 " + std::to_string (dropped) + (dropped > 1 ? " overlapping or late notes dropped" : " overlapping or late note dropped");
    say (m);
    commitVersion();
    return true;
}

std::vector<uint8_t> Session::midiFile (double bpm)
{
    std::vector<MidiTrack> tracks;
    const auto seq = doc.playSeq();
    const int n = doc.numSteps();
    for (int i = 0; i < kNumTabs; ++i)
    {
        const auto& t = doc.tabs[(size_t) i];
        if (! t.on)
            continue;
        MidiTrack tr;
        tr.name = std::string ("Midinja ") + (t.drums ? "Beats" : t.poly ? "Chords" : "Riff");
        tr.channel = t.channel;
        if (t.poly)
        {
            for (int b = 0; b < (int) doc.arr.size(); ++b)
                for (const auto& h : doc.barHits (b))
                    for (int note : doc.voice (doc.chords[(size_t) h.chordIndex]))
                        tr.notes.push_back ({ (double) (b * n + h.step) + swingOffset (h.step, doc.swing), (double) h.len, note, 100 });
        }
        else if (t.drums)
        {
            double offset = 0.0;
            for (size_t b = 0; b < seq.size(); ++b)
            {
                const int bar = doc.sel >= 0 && doc.sel < (int) doc.arr.size() ? doc.sel : doc.arr.empty() ? -1 : (int) b;
                for (const auto& e : doc.drumHits (seq[b], bar))
                    tr.notes.push_back ({ offset + (double) e.step + swingOffset (e.step, doc.swing), 1.0, drumNote (e.row), e.velocity });
                offset += n;
            }
        }
        else
        {
            double offset = 0.0;
            for (int k : seq)
            {
                const auto& p = t.parts[(size_t) k];
                for (auto e : renderCycle (p, doc.swing))
                {
                    e.start += offset;
                    tr.notes.push_back (e);
                }
                offset += p.numSteps;
            }
        }
        tracks.push_back (std::move (tr));
    }
    if (tracks.empty())
    {
        say ("No tab is on: turn one on in the tab bar to export", true);
        return {};
    }
    return writeMidiFileTracks (tracks, (double) doc.rate, bpm, (double) seq.size() * n);
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
