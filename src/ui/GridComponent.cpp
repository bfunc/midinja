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

void GridComponent::refresh()
{
    rows.clear();
    chord.clear();
    if (sess == nullptr)
        return;
    const auto& d = doc();
    const auto& t = d.T();
    for (int n = t.highNote; n >= t.lowNote; --n)
        rows.push_back (n);
    if (t.poly)
        chord = d.chordNotes (d.cur, t);
    layoutRows();
    repaint();
}

void GridComponent::layoutRows()
{
    rowTop.clear();
    rowHt.clear();
    if (sess == nullptr || rows.empty())
        return;
    const auto& d = doc();
    double wsum = 0.0;
    for (int n : rows)
        wsum += d.inScale (n) ? 1.0 : 0.5;
    baseH = juce::jlimit (11, 30, (int) std::floor ((double) (lastBudget - 2) / std::max (0.5, wsum)));
    int y = 1;
    for (int n : rows)
    {
        const int h = d.inScale (n) ? baseH : std::max (5, baseH / 2);
        rowTop.push_back (y);
        rowHt.push_back (h);
        y += h;
    }
}

int GridComponent::layoutHeight (int budget)
{
    lastBudget = budget;
    if (rows.empty())
    {
        baseH = 30;
        return 120;
    }
    layoutRows();
    return rowTop.back() + rowHt.back() + 1;
}

float GridComponent::colWidth() const
{
    return (float) (getWidth() - 2 - theme::keyWidth) / (float) std::max (1, doc().shown().numSteps);
}

int GridComponent::colAt (float x) const
{
    const int c = (int) std::floor ((x - 1.0f - (float) theme::keyWidth) / colWidth());
    return c >= 0 && c < doc().shown().numSteps ? c : -1;
}

int GridComponent::rowAt (float y) const
{
    for (size_t r = 0; r < rows.size(); ++r)
        if (y >= (float) rowTop[r] && y < (float) (rowTop[r] + rowHt[r]))
            return (int) r;
    return -1;
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
        const float top = (float) rowTop[r], bottom = top + (float) rowHt[r];
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
    const bool poly = doc().T().poly;
    const int note = rows[(size_t) row];
    for (int s = col; s >= 0; --s)
    {
        const auto& st = p.steps[(size_t) s];
        if (! st.on)
            continue;
        const bool hits = poly ? std::find (chord.begin(), chord.end(), note) != chord.end() : st.note == note;
        return hits && s + st.length > col ? s : -1; // an earlier hit ends any longer one before col
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
    const int n = p.numSteps;
    const float cw = colWidth();
    const float kw = (float) theme::keyWidth;

    juce::Path clip;
    clip.addRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.reduceClipRegion (clip);

    if (rows.empty() || rowTop.size() != rows.size())
    {
        g.setColour (theme::ink3);
        g.setFont (theme::font (12.0f));
        g.drawText (u8 (midinja::partName (d.cur)) + " has no notes yet: roll the dice or click the grid", getLocalBounds(), juce::Justification::centred);
        g.setColour (theme::line);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        return;
    }

    const int chordPc = d.degreePc (d.cur);
    const juce::String chordName = u8 (midinja::numeral (d.degreeOf (d.cur), d.gen.scale));
    const auto isColour = [&] (int pc) { return std::find (mi.colors.begin(), mi.colors.end(), pc) != mi.colors.end(); };

    for (size_t r = 0; r < rows.size(); ++r)
    {
        const int note = rows[r];
        const int pc = d.pcOf (note);
        const bool inS = d.inScale (note);
        const float y = (float) rowTop[r], rh = (float) rowHt[r];

        // key column
        g.setColour (theme::panel);
        g.fillRect (1.0f, y, kw, rh);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (1.0f, y + rh - 1.0f, kw, 1.0f);
        const bool isRoot = pc == 0;
        if (rh >= 10.0f)
        {
            const float nameW = theme::textWidth (theme::font (9.5f, isRoot), theme::noteName (note));
            const float x = kw + 1.0f - 5.0f - nameW;
            g.setColour (isRoot ? theme::accent : inS ? theme::ink3 : theme::outInk);
            g.setFont (theme::font (9.5f, isRoot));
            g.drawText (theme::noteName (note), juce::Rectangle<float> (x, y, nameW + 1.0f, rh), juce::Justification::centredLeft);
            if (inS && rh >= 11.0f)
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
                    const auto br = juce::Rectangle<float> (x - 4.0f - bw, y + (rh - 11.0f) * 0.5f, bw, 11.0f);
                    g.setColour (col ? theme::teal : juce::Colours::white.withAlpha (0.05f));
                    g.fillRoundedRectangle (br, 3.0f);
                    g.setColour (col ? theme::bg : theme::ink2);
                    g.setFont (f);
                    g.drawText (badge, br, juce::Justification::centred);
                }
            }
        }

        // cells
        for (int s = 0; s < n; ++s)
        {
            const auto cell = juce::Rectangle<float> (1.0f + kw + (float) s * cw, y, cw, rh);
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

    // notes (or chord hits), on top of the cells
    const auto drawHit = [&] (int s, const midinja::Step& st, int note, bool chordHit) -> juce::Rectangle<float> {
        const int r = rowOf (note);
        if (r < 0)
            return {};
        const float y = (float) rowTop[(size_t) r], rh = (float) rowHt[(size_t) r];
        const float inset = rh < (float) baseH ? 0.0f : 2.0f;
        const int pc = d.pcOf (note);
        const bool inS = d.inScale (note);
        const auto rect = juce::Rectangle<float> (1.0f + kw + (float) s * cw + 2.0f, y + inset,
                                                  (float) st.length * cw - 4.0f, rh - 2.0f * inset);
        const auto colour = chordHit ? juce::Colour (0xffffb264) : ! inS ? theme::outNote : isColour (pc) ? theme::teal : theme::accent;
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
        return rect;
    };
    std::vector<juce::Rectangle<float>> noteRects ((size_t) n);
    for (int s = 0; s < n; ++s)
    {
        const auto& st = p.steps[(size_t) s];
        if (! st.on)
            continue;
        if (t.poly)
        {
            for (int note : chord)
                drawHit (s, st, note, true);
            continue;
        }
        const auto rect = drawHit (s, st, st.note, false);
        noteRects[(size_t) s] = rect;
        if (! rect.isEmpty() && s == 0 && d.gen.startOnRoot && d.pcOf (st.note) == chordPc && rect.getHeight() >= 11.0f)
        {
            const auto f = theme::font (9.0f, true);
            const float bw = theme::textWidth (f, chordName) + 6.0f;
            const auto br = juce::Rectangle<float> (rect.getX() + 2.0f, rect.getCentreY() - 5.5f, bw, 11.0f);
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (br, 3.0f);
            g.setColour (theme::bg);
            g.setFont (f);
            g.drawText (chordName, br, juce::Justification::centred);
        }
    }

    // augmented-second pairs: a thin arc between the two notes
    if (! t.poly)
        for (const auto& pr : d.pairsOf (p))
        {
            const auto a = noteRects[(size_t) pr.first], b = noteRects[(size_t) pr.second];
            if (a.isEmpty() || b.isEmpty())
                continue;
            juce::Path arc;
            arc.startNewSubPath (a.getCentreX(), a.getCentreY());
            arc.quadraticTo ((a.getCentreX() + b.getCentreX()) * 0.5f, std::min (a.getCentreY(), b.getCentreY()) - 10.0f, b.getCentreX(), b.getCentreY());
            g.setColour (theme::teal.withAlpha (0.9f));
            g.strokePath (arc, juce::PathStrokeType (1.5f));
        }

    bool any = false;
    for (int s = 0; s < n; ++s)
        any = any || p.steps[(size_t) s].on;
    if (! any)
    {
        g.setColour (theme::ink3);
        g.setFont (theme::font (12.0f));
        g.drawText (u8 (midinja::partName (d.cur)) + " is empty on tab " + juce::String (d.tab + 1) + ": roll the dice or click the grid",
                    getLocalBounds().withTrimmedLeft (theme::keyWidth), juce::Justification::centred);
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
    const int s = noteAt (col, row);
    if (s < 0)
        return {};
    if (d.T().poly)
    {
        juce::StringArray names;
        for (int note : chord)
            names.add (theme::noteName (note));
        return u8 (midinja::numeral (d.degreeOf (d.cur), d.gen.scale)) + " " + midinja::chordTypeName (d.chords[(size_t) d.cur].type) + ": "
             + names.joinIntoString (" ") + u8 (" \xC2\xB7 click = remove the hit \xC2\xB7 Shift+click = lock \xC2\xB7 right-click = longer");
    }
    if (s != 0 || ! d.gen.startOnRoot || d.pcOf (d.shown().steps[0].note) != d.degreePc (d.cur))
        return {};
    return "Start on chord root: step 1 of " + u8 (midinja::partName (d.cur)) + " is held on its chord root ("
         + u8 (midinja::numeral (d.degreeOf (d.cur), d.gen.scale)) + "); the dice, Rhythm, Notes and Mutate keep it. Turn off under Advanced";
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
    const bool poly = d.T().poly;
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
        if (poly)
        {
            st.on = false;
            st.locked = false;
            if (onEdit) onEdit();
            return;
        }
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
    if (poly && st.on)
        return; // the hit is there already, on other rows
    st = midinja::Step {};
    st.on = true;
    st.note = poly ? 60 : rows[(size_t) row];
    st.locked = ! poly && ! d.inScale (st.note); // an off-scale note by hand is locked at once
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
