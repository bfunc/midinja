#include "GridComponent.h"
#include "Theme.h"
#include "core/Scales.h"

#include <algorithm>
#include <cmath>

namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
juce::String u8 (const std::string& s) { return juce::String (juce::CharPointer_UTF8 (s.c_str())); }
} // namespace

int GridComponent::theme_keyWidth() { return theme::keyWidth; }

void GridComponent::refresh()
{
    rows.clear();
    hits.clear();
    ghosts.clear();
    cuts.clear();
    if (sess == nullptr)
        return;
    const auto& d = doc();
    const auto& t = d.T();
    for (int n = t.highNote; n >= t.lowNote; --n)
        rows.push_back (n);
    const int b = d.shownBar();
    if (t.poly && b >= 0)
    {
        for (const auto& h : d.barHits (b))
        {
            const auto& c = d.chords[(size_t) h.chordIndex];
            PolyHit ph { h.step, h.len, h.chordIndex, d.voice (c), ((d.gen.root + d.at (c.deg)) % 12 + 12) % 12 };
            juce::StringArray up, down;
            for (int n : ph.notes)
            {
                if (n > t.highNote) up.add (theme::noteName (n));
                if (n < t.lowNote) down.add (theme::noteName (n));
            }
            if (! up.isEmpty()) cuts.push_back ({ h.step, h.len, u8 ("\xE2\x96\xB2 ") + up.joinIntoString (" "), c.id == d.csel, true });
            if (! down.isEmpty()) cuts.push_back ({ h.step, h.len, u8 ("\xE2\x96\xBC ") + down.joinIntoString (" "), c.id == d.csel, false });
            hits.push_back (std::move (ph));
        }
    }
    // the other tab's notes, faintly: the whole layer moved by octaves to fit the visible range best
    const auto& other = d.tabs[(size_t) (t.poly ? midinja::kRiffTab : midinja::kChordTab)];
    if (other.on && b >= 0)
    {
        std::vector<GhostHit> raw;
        if (t.poly)
        {
            const auto& p = other.parts[(size_t) d.arr[(size_t) b]];
            for (int s = 0; s < p.numSteps; ++s)
                if (p.steps[(size_t) s].on)
                    raw.push_back ({ s, p.steps[(size_t) s].length, { p.steps[(size_t) s].note } });
        }
        else
            for (const auto& h : d.barHits (b))
                raw.push_back ({ h.step, h.len, d.voice (d.chords[(size_t) h.chordIndex]) });
        int best = 0, bestN = -1;
        for (int k : { 0, -1, 1, -2, 2, -3, 3 })
        {
            int n = 0;
            for (const auto& h : raw)
                for (int x : h.notes)
                    n += (x + 12 * k >= t.lowNote && x + 12 * k <= t.highNote) ? 1 : 0;
            if (n > bestN) { bestN = n; best = k; }
        }
        for (auto h : raw)
        {
            std::vector<int> in;
            for (int x : h.notes)
                if (x + 12 * best >= t.lowNote && x + 12 * best <= t.highNote)
                    in.push_back (x + 12 * best);
            h.notes = in;
            if (! in.empty())
                ghosts.push_back (h);
        }
    }
    repaint();
}

int GridComponent::layoutHeight (int budget)
{
    if (rows.empty())
    {
        rowH = 30;
        return 120;
    }
    rowH = juce::jlimit (11, 30, (budget - 2) / (int) rows.size());
    return rowH * (int) rows.size() + 2;
}

float GridComponent::colWidth() const
{
    return (float) (getWidth() - 2 - theme::keyWidth) / (float) std::max (1, doc().numSteps());
}

int GridComponent::colAt (float x) const
{
    const int c = (int) std::floor ((x - 1.0f - (float) theme::keyWidth) / colWidth());
    return c >= 0 && c < doc().numSteps() ? c : -1;
}

int GridComponent::rowAt (float y) const
{
    const int r = (int) std::floor ((y - 1.0f) / (float) rowH);
    return r >= 0 && r < (int) rows.size() ? r : -1;
}

int GridComponent::rowOf (int note) const
{
    const auto it = std::find (rows.begin(), rows.end(), note);
    return it == rows.end() ? -1 : (int) (it - rows.begin());
}

int GridComponent::nearestRow (float y, bool chromatic) const
{
    int best = -1;
    float bestD = 1.0e9f;
    for (size_t r = 0; r < rows.size(); ++r)
    {
        if (! chromatic && ! doc().inScale (rows[r]))
            continue;
        const float top = 1.0f + (float) r * (float) rowH, bottom = top + (float) rowH;
        const float dist = y < top ? top - y : y > bottom ? y - bottom : 0.0f;
        if (dist < bestD)
        {
            bestD = dist;
            best = (int) r;
        }
    }
    return best;
}

int GridComponent::noteAt (int col, int row) const
{
    if (col < 0 || row < 0)
        return -1;
    const auto& p = doc().shown();
    const int note = rows[(size_t) row];
    for (int s = col; s >= 0; --s)
    {
        const auto& st = p.steps[(size_t) s];
        if (! st.on)
            continue;
        return st.note == note && s + st.length > col ? s : -1;
    }
    return -1;
}

int GridComponent::polyHitAt (int col, int row) const
{
    if (col < 0 || row < 0)
        return -1;
    const int note = rows[(size_t) row];
    for (int i = 0; i < (int) hits.size(); ++i)
    {
        const auto& h = hits[(size_t) i];
        if (col >= h.step && col < h.step + h.len && std::find (h.notes.begin(), h.notes.end(), note) != h.notes.end())
            return i;
    }
    return -1;
}

void GridComponent::drawPadlock (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    g.setColour (c);
    const auto body = juce::Rectangle<float> (r.getX(), r.getY() + r.getHeight() * 0.45f, r.getWidth(), r.getHeight() * 0.55f);
    g.fillRoundedRectangle (body, 1.5f);
    juce::Path shackle;
    shackle.addCentredArc (r.getCentreX(), body.getY(), r.getWidth() * 0.3f, r.getHeight() * 0.42f, 0.0f,
                           -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.strokePath (shackle, juce::PathStrokeType (1.2f));
}

void GridComponent::paint (juce::Graphics& g)
{
    if (sess == nullptr)
        return;
    const auto& d = doc();
    const auto& t = d.T();
    const auto& p = d.shown();
    const auto mi = midinja::modeInfo (d.gen.scale);
    const int n = d.numSteps();
    const float cw = colWidth();
    const float kw = (float) theme::keyWidth;
    const int b = d.shownBar();

    juce::Path clip;
    clip.addRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.reduceClipRegion (clip);

    if (rows.empty())
    {
        g.setColour (theme::ink3);
        g.setFont (theme::font (12.0f));
        g.drawText ("No rows", getLocalBounds(), juce::Justification::centred);
        g.setColour (theme::line);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        return;
    }

    const int chordPc = d.at (d.degreeOf (d.cur)) % 12;
    const juce::String chordNm = u8 (d.numeral (d.degreeOf (d.cur)));
    const auto isColour = [&] (int pc) { return std::find (mi.colors.begin(), mi.colors.end(), pc) != mi.colors.end(); };

    for (size_t r = 0; r < rows.size(); ++r)
    {
        const int note = rows[r];
        const int pc = d.pcOf (note);
        const bool inS = d.inScale (note);
        const float y = 1.0f + (float) r * (float) rowH;
        g.setColour (theme::panel);
        g.fillRect (1.0f, y, kw, (float) rowH);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (1.0f, y + (float) rowH - 1.0f, kw, 1.0f);
        const bool isRoot = pc == 0;
        const float fs = t.poly ? 12.0f : 9.5f;
        const float nameW = theme::textWidth (theme::font (fs, isRoot), theme::noteName (note));
        const float x = kw + 1.0f - 5.0f - nameW;
        g.setColour (isRoot ? theme::accent : inS ? theme::ink3 : theme::outInk);
        g.setFont (theme::font (fs, isRoot));
        g.drawText (theme::noteName (note), juce::Rectangle<float> (x, y, nameW + 1.0f, (float) rowH), juce::Justification::centredLeft);
        if (! t.poly && inS && rowH >= 11)
        {
            juce::String badge;
            bool col = false;
            if (pc == 0) badge = "1";
            else if (pc == mi.third) badge = u8 (midinja::degreeName (pc));
            else if (pc == mi.fifth) badge = u8 (midinja::degreeName (pc));
            else if (isColour (pc)) { badge = u8 (midinja::degreeName (pc)); col = true; }
            if (badge.isNotEmpty())
            {
                const auto f = theme::font (8.5f, col);
                const float bw = theme::textWidth (f, badge) + 6.0f;
                const auto br = juce::Rectangle<float> (x - 4.0f - bw, y + ((float) rowH - 11.0f) * 0.5f, bw, 11.0f);
                g.setColour (col ? theme::teal : juce::Colours::white.withAlpha (0.05f));
                g.fillRoundedRectangle (br, 3.0f);
                g.setColour (col ? theme::bg : theme::ink2);
                g.setFont (f);
                g.drawText (badge, br, juce::Justification::centred);
            }
        }
        for (int s = 0; s < n; ++s)
        {
            const auto cell = juce::Rectangle<float> (1.0f + kw + (float) s * cw, y, cw, (float) rowH);
            if (! inS) g.setColour (juce::Colours::black.withAlpha (0.25f));
            else if (pc == 0) g.setColour (theme::accent.withAlpha (0.05f));
            else if (r % 2) g.setColour (juce::Colours::white.withAlpha (0.02f));
            else g.setColour (juce::Colours::transparentBlack);
            g.fillRect (cell);
            if (s == playStep)
            {
                g.setColour (theme::teal.withAlpha (0.08f));
                g.fillRect (cell);
            }
            g.setColour (juce::Colours::white.withAlpha (s % 4 == 0 ? 0.11f : 0.03f));
            g.fillRect (cell.getX(), cell.getY(), 1.0f, cell.getHeight());
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRect (cell.getX(), cell.getBottom() - 1.0f, cell.getWidth(), 1.0f);
        }
    }

    // the other tab, faintly underneath
    for (const auto& gh : ghosts)
        for (int note : gh.notes)
        {
            const int r = rowOf (note);
            if (r < 0)
                continue;
            const auto rect = juce::Rectangle<float> (1.0f + kw + (float) gh.step * cw + 2.0f, 1.0f + (float) r * (float) rowH + 2.0f,
                                                      (float) gh.len * cw - 4.0f, (float) rowH - 4.0f);
            g.setColour (theme::ink2.withAlpha (0.22f));
            g.fillRoundedRectangle (rect, 4.0f);
        }

    std::vector<juce::Rectangle<float>> noteRects ((size_t) n);
    if (t.poly)
    {
        for (const auto& h : hits)
        {
            const bool sel = d.chords[(size_t) h.chordIndex].id == d.csel;
            for (int note : h.notes)
            {
                const int r = rowOf (note);
                if (r < 0)
                    continue;
                const auto rect = juce::Rectangle<float> (1.0f + kw + (float) h.step * cw + 2.0f, 1.0f + (float) r * (float) rowH + 2.0f,
                                                          (float) h.len * cw - 4.0f, (float) rowH - 4.0f);
                g.setColour (sel ? theme::accent : juce::Colour (0xff8a5a2c));
                g.fillRoundedRectangle (rect, 4.0f);
                if (((note % 12) + 12) % 12 == h.rootPc)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.5f));
                    g.fillRect (rect.getX(), rect.getY(), 3.0f, rect.getHeight());
                }
            }
        }
        if (b < 0 || hits.empty())
        {
            g.setColour (theme::ink3);
            g.setFont (theme::font (12.0f));
            g.drawText (b < 0 ? juce::String ("No bar to show: add a bar on the right") : "No chord in bar " + juce::String (b + 1) + ": add one in the lane above",
                        getLocalBounds().withTrimmedLeft (theme::keyWidth), juce::Justification::centred);
        }
    }
    else
    {
        for (int s = 0; s < n; ++s)
        {
            const auto& st = p.steps[(size_t) s];
            if (! st.on)
                continue;
            const int r = rowOf (st.note);
            if (r < 0)
                continue;
            const int pc = d.pcOf (st.note);
            const bool inS = d.inScale (st.note);
            const auto rect = juce::Rectangle<float> (1.0f + kw + (float) s * cw + 2.0f, 1.0f + (float) r * (float) rowH + 2.0f,
                                                      (float) st.length * cw - 4.0f, (float) rowH - 4.0f);
            noteRects[(size_t) s] = rect;
            const auto colour = ! inS ? theme::outNote : isColour (pc) ? theme::teal : theme::accent;
            g.setColour (colour.withAlpha (0.55f + 0.45f * (float) st.velocity / 127.0f));
            g.fillRoundedRectangle (rect, 4.0f);
            {
                juce::Graphics::ScopedSaveState ss (g);
                juce::Path np;
                np.addRoundedRectangle (rect, 4.0f);
                g.reduceClipRegion (np);
                g.setColour (juce::Colours::black.withAlpha (0.2f));
                g.fillRect (rect.getX(), rect.getBottom() - 2.0f, rect.getWidth(), 2.0f);
                if (st.repeats > 1)
                {
                    const auto inner = rect.reduced (2.0f);
                    const float w = (inner.getWidth() - 2.0f * (float) (st.repeats - 1)) / (float) st.repeats;
                    for (int k = 0; k < st.repeats; ++k)
                        g.fillRoundedRectangle (inner.getX() + (float) k * (w + 2.0f), inner.getY(), w, inner.getHeight(), 2.0f);
                }
            }
            if (st.locked)
            {
                g.setColour (theme::lock);
                g.drawRoundedRectangle (rect.reduced (1.0f), 4.0f, 2.0f);
                const float sz = std::min (8.0f, rect.getHeight() - 4.0f);
                if (sz >= 4.0f)
                    drawPadlock (g, juce::Rectangle<float> (rect.getRight() - sz - 3.0f, rect.getCentreY() - sz * 0.5f, sz, sz), theme::lock);
            }
            if (s == 0 && d.gen.startOnRoot && pc == chordPc && rect.getHeight() >= 11.0f)
            {
                const auto f = theme::font (9.0f, true);
                const float bw = theme::textWidth (f, chordNm) + 6.0f;
                const auto br = juce::Rectangle<float> (rect.getX() + 2.0f, rect.getCentreY() - 5.5f, bw, 11.0f);
                g.setColour (juce::Colours::white);
                g.fillRoundedRectangle (br, 3.0f);
                g.setColour (theme::bg);
                g.setFont (f);
                g.drawText (chordNm, br, juce::Justification::centred);
            }
        }
        // augmented-second pairs: a thin arc between the two notes
        const auto mv = mi.moves;
        if (! mv.empty())
        {
            std::vector<int> on;
            for (int s = 0; s < n; ++s)
                if (p.steps[(size_t) s].on)
                    on.push_back (s);
            for (size_t j = 0; j + 1 < on.size(); ++j)
            {
                const auto& a = p.steps[(size_t) on[j]];
                const auto& c = p.steps[(size_t) on[j + 1]];
                if (a.locked || c.locked || std::abs (a.note - c.note) > 4)
                    continue;
                const int pa = d.pcOf (a.note), pb = d.pcOf (c.note);
                bool pair = false;
                for (const auto& m : mv)
                    pair = pair || (m.first == pa && m.second == pb) || (m.first == pb && m.second == pa);
                if (! pair)
                    continue;
                const auto ra = noteRects[(size_t) on[j]], rb = noteRects[(size_t) on[j + 1]];
                if (ra.isEmpty() || rb.isEmpty())
                    continue;
                juce::Path arc;
                arc.startNewSubPath (ra.getCentreX(), ra.getCentreY());
                arc.quadraticTo ((ra.getCentreX() + rb.getCentreX()) * 0.5f, std::min (ra.getCentreY(), rb.getCentreY()) - 10.0f, rb.getCentreX(), rb.getCentreY());
                g.setColour (theme::teal.withAlpha (0.9f));
                g.strokePath (arc, juce::PathStrokeType (1.5f));
                ++j;
            }
        }
        bool any = false;
        for (int s = 0; s < n; ++s)
            any = any || p.steps[(size_t) s].on;
        if (! any)
        {
            g.setColour (theme::ink3);
            g.setFont (theme::font (12.0f));
            g.drawText (u8 (midinja::partName (d.cur)) + " is empty: roll the dice or click the grid", getLocalBounds().withTrimmedLeft (theme::keyWidth), juce::Justification::centred);
        }
    }

    g.setColour (theme::line);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}

juce::String GridComponent::getTooltip()
{
    if (sess == nullptr || rows.empty())
        return {};
    const auto& d = doc();
    const int col = colAt ((float) hover.x), row = rowAt ((float) hover.y);
    if (d.T().poly)
    {
        const int i = polyHitAt (col, row);
        if (i < 0)
            return {};
        const auto& h = hits[(size_t) i];
        juce::StringArray names;
        for (int note : h.notes)
            names.add (theme::noteName (note));
        return u8 (d.chordName (d.chords[(size_t) h.chordIndex])) + ": " + names.joinIntoString (" ") + u8 (" \xC2\xB7 click selects the chord");
    }
    const int s = noteAt (col, row);
    if (s != 0 || ! d.gen.startOnRoot || d.pcOf (d.shown().steps[0].note) != d.at (d.degreeOf (d.cur)) % 12)
        return {};
    return "Start on chord root: step 1 of " + u8 (midinja::partName (d.cur)) + " is held on its bar's chord root ("
         + u8 (d.numeral (d.degreeOf (d.cur))) + "); the dice keep it. Turn off under Advanced";
}

void GridComponent::mouseDown (const juce::MouseEvent& e)
{
    dragStep = -1;
    moved = false;
    if (sess == nullptr || rows.empty())
        return;
    const int col = colAt ((float) e.x), row = rowAt ((float) e.y);
    if (col < 0 || row < 0)
        return;
    auto& d = sess->doc;
    if (d.T().poly)
    {
        const int i = polyHitAt (col, row);
        if (i >= 0)
        {
            sess->selectChord (d.chords[(size_t) hits[(size_t) i].chordIndex].id);
            if (onSelectChord) onSelectChord();
        }
        return;
    }
    auto& p = d.shown();
    const int s = noteAt (col, row);
    if (s >= 0)
    {
        auto& st = p.steps[(size_t) s];
        if (e.mods.isPopupMenu())
        {
            int next = s + 1;
            while (next < p.numSteps && ! p.steps[(size_t) next].on)
                ++next;
            const int room = std::min (midinja::kMaxLength, next - s);
            st.length = st.length + 1 > room ? 1 : st.length + 1;
            if (onEdit) onEdit();
            return;
        }
        if (e.mods.isShiftDown())
        {
            st.locked = ! st.locked;
            if (onEdit) onEdit();
            return;
        }
        if (! e.mods.isLeftButtonDown())
            return;
        dragStep = s;
        dragNote0 = st.note;
        pressY = e.y;
        return;
    }
    if (! e.mods.isLeftButtonDown() || e.mods.isPopupMenu())
        return;
    auto& st = p.steps[(size_t) col];
    if (st.locked && ! st.on)
        return;
    st = midinja::Step {};
    st.on = true;
    st.note = rows[(size_t) row];
    st.locked = ! d.inScale (st.note);
    midinja::fitLengths (p);
    if (onEdit) onEdit();
}

void GridComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0 || sess == nullptr)
        return;
    if (! moved && std::abs (e.y - pressY) < 6)
        return;
    const int r = nearestRow ((float) e.y, e.mods.isAltDown());
    if (r < 0)
        return;
    moved = true;
    const int note = rows[(size_t) r];
    auto& st = sess->doc.shown().steps[(size_t) dragStep];
    if (st.note != note)
    {
        st.note = note;
        refresh();
        if (onPreview) onPreview();
    }
}

void GridComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragStep < 0 || sess == nullptr)
        return;
    auto& st = sess->doc.shown().steps[(size_t) dragStep];
    if (moved)
    {
        if (! sess->doc.inScale (st.note))
            st.locked = true;
    }
    else
    {
        st.on = false;
        st.locked = false;
    }
    dragStep = -1;
    moved = false;
    if (onEdit) onEdit();
}
