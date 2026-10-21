// The cases of the Run parity test: every riff the dice make for a grid of settings, hashed.
// tests/RunParityGolden.h holds the hashes the engine gave before Ending existed (2b38b72);
// with ending = Run the engine must give the same ones.
#pragma once

#include "Engine.h"

#include <array>
#include <cstdint>
#include <vector>

namespace runparity
{

inline uint64_t hashOf (const midinja::Pattern& p, uint64_t h = 1469598103934665603ull)
{
    auto mix = [&h] (int v) { h ^= (uint64_t) (uint32_t) v; h *= 1099511628211ull; };
    mix (p.numSteps);
    for (int i = 0; i < p.numSteps; ++i)
    {
        const auto& st = p.steps[(size_t) i];
        mix (st.on); mix (st.note); mix (st.length); mix (st.velocity); mix (st.repeats); mix (st.locked);
    }
    return h;
}

// Modes Random/Musician × start on root off/on × notes 4..14 × seeds; per case: roll, rhythm,
// notes, mutate, A' / B / C from A, and the whole form.
inline std::vector<uint64_t> hashes()
{
    using namespace midinja;
    std::vector<uint64_t> out;
    for (int mode = 0; mode < 2; ++mode)
        for (int root = 0; root < 2; ++root)
            for (int notes = 4; notes <= 14; ++notes)
                for (uint32_t seed = 1; seed <= 40; ++seed)
                {
                    GenSettings g;
                    g.mode = mode;
                    g.startOnRoot = root == 1;
                    g.notes = notes;
                    g.seed = seed * 7919u + (uint32_t) notes;
                    g.root = (int) (seed % 12);
                    g.scale = (int) (seed % 9);
                    g.repeatsOn = seed % 2 == 0;
                    g.lenMax = 1 + (int) (seed % 4);
                    Pattern a;
                    a.numSteps = 16;
                    generate (a, g);
                    out.push_back (hashOf (a));
                    for (int op = 0; op < 3; ++op)
                    {
                        Pattern p = a;
                        GenSettings g2 = g;
                        g2.seed = g.seed ^ 0xABCDu;
                        if (op == 0) generateRhythm (p, g2);
                        if (op == 1) generateNotes (p, g2);
                        if (op == 2) mutate (p, g2, 0.33f);
                        out.push_back (hashOf (p));
                    }
                    for (int part = 1; part < kNumParts; ++part)
                    {
                        Pattern p;
                        p.numSteps = 16;
                        generatePart (p, part, a, g);
                        out.push_back (hashOf (p));
                    }
                    std::array<Pattern, kNumParts> form;
                    for (auto& p : form)
                        p.numSteps = 16;
                    generateForm (form, 0u, g);
                    uint64_t h = 1469598103934665603ull;
                    for (const auto& p : form)
                        h = hashOf (p, h);
                    out.push_back (h);
                }
    return out;
}

} // namespace runparity
