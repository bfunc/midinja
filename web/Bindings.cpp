// WebAssembly bindings for the web preview: the same generator as the app, called from JS.
// Patterns and settings travel as plain JS objects.

#include "Engine.h"
#include "Scales.h"

#include <emscripten/bind.h>
#include <emscripten/val.h>

using emscripten::val;
using namespace midinja;

namespace
{

template <typename T>
T get (const val& o, const char* key, T fallback)
{
    const val v = o[key];
    return v.isUndefined() || v.isNull() ? fallback : v.as<T>();
}

GenSettings toSettings (const val& o)
{
    GenSettings g;
    g.root = get (o, "root", g.root);
    g.scale = get (o, "scale", g.scale);
    g.lowNote = get (o, "lowNote", g.lowNote);
    g.highNote = get (o, "highNote", g.highNote);
    g.notes = get (o, "notes", g.notes);
    g.lenMin = get (o, "lenMin", g.lenMin);
    g.lenMax = get (o, "lenMax", g.lenMax);
    g.velMin = get (o, "velMin", g.velMin);
    g.velMax = get (o, "velMax", g.velMax);
    g.repeatsOn = get (o, "repeatsOn", g.repeatsOn);
    g.repMin = get (o, "repMin", g.repMin);
    g.repMax = get (o, "repMax", g.repMax);
    g.repChance = get (o, "repChance", g.repChance);
    g.seed = get (o, "seed", g.seed);
    g.mode = get (o, "mode", g.mode);
    g.color = get (o, "color", g.color);
    g.surprise = get (o, "surprise", g.surprise);
    return g;
}

// JS pattern: { numSteps, steps: [{ on, note, length, velocity, repeats, locked }, ...] }
Pattern toPattern (const val& o)
{
    Pattern p;
    p.numSteps = get (o, "numSteps", 16);
    const val steps = o["steps"];
    if (steps.isUndefined())
        return p;
    const int len = std::min (kMaxSteps, steps["length"].as<int>());
    for (int i = 0; i < len; ++i)
    {
        const val s = steps[i];
        auto& st = p.steps[(size_t) i];
        st.on = get (s, "on", false);
        st.note = get (s, "note", 60);
        st.length = get (s, "length", 1);
        st.velocity = get (s, "velocity", 100);
        st.repeats = get (s, "repeats", 1);
        st.locked = get (s, "locked", false);
    }
    return p;
}

val fromPattern (const Pattern& p)
{
    val o = val::object();
    o.set ("numSteps", p.numSteps);
    val steps = val::array();
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto& st = p.steps[(size_t) i];
        val s = val::object();
        s.set ("on", st.on);
        s.set ("note", st.note);
        s.set ("length", st.length);
        s.set ("velocity", st.velocity);
        s.set ("repeats", st.repeats);
        s.set ("locked", st.locked);
        steps.call<void> ("push", s);
    }
    o.set ("steps", steps);
    return o;
}

// op: "roll" | "rhythm" | "notes" | "mutate"
val run (std::string op, val pattern, val settings, float amount)
{
    Pattern p = toPattern (pattern);
    const GenSettings g = toSettings (settings);
    if (op == "rhythm")      generateRhythm (p, g);
    else if (op == "notes")  generateNotes (p, g);
    else if (op == "mutate") mutate (p, g, amount);
    else                     generate (p, g);
    return fromPattern (p);
}

val transpose (val pattern, int scaleSteps, int root, int scale)
{
    Pattern p = toPattern (pattern);
    transposeScaleSteps (p, scaleSteps, root, scale);
    return fromPattern (p);
}

val conform (val pattern, int oldRoot, int newRoot, int newScale)
{
    Pattern p = toPattern (pattern);
    conformToScale (p, oldRoot, newRoot, newScale);
    fitLengths (p);
    return fromPattern (p);
}

val scaleList()
{
    val out = val::array();
    for (size_t i = 0; i < scales().size(); ++i)
    {
        const auto& s = scales()[i];
        const auto mi = modeInfo ((int) i);
        val o = val::object();
        o.set ("name", std::string (s.name));
        val iv = val::array();
        for (int x : s.intervals)
            iv.call<void> ("push", x);
        o.set ("intervals", iv);
        o.set ("third", mi.third);
        o.set ("fifth", mi.fifth);
        o.set ("color", mi.color);
        out.call<void> ("push", o);
    }
    return out;
}

} // namespace

EMSCRIPTEN_BINDINGS (midinja)
{
    emscripten::function ("run", &run);
    emscripten::function ("transpose", &transpose);
    emscripten::function ("conform", &conform);
    emscripten::function ("scales", &scaleList);
    emscripten::function ("cellLength", &musicianCellLength);
}
