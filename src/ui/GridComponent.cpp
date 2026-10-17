#include "GridComponent.h"
#include "Theme.h"
#include "core/Scales.h"

#include <algorithm>
#include <cmath>
#include <set>

int GridComponent::pcOf (int note) const { return ((note - doc().gen.root) % 12 + 12) % 12; }

bool GridComponent::inScale (int note) const
{
    const auto& iv = midinja::scales()[(size_t) doc().gen.scale].intervals;
    return std::find (iv.begin(), iv.end(), pcOf (note)) != iv.end();
}

void GridComponent::refresh()
{
    rows.clear();
    if (sess == nullptr)
        return;
    const auto& d = doc();
    const auto& p = d.shown();
    std::vector<int> used;
    for (int i = 0; i < p.numSteps; ++i)
        if (p.steps[(size_t) i].on)
            used.push_back (p.steps[(size_t) i].note);
    int lo = d.gen.lowNote, hi = d.gen.highNote;
    for (int n : used)
    {
        lo = std::min (lo, n);
        hi = std::max (hi, n);
    }
    if (sess->view == midinja::View::Riff)
    {
        std::set<int> s (used.begin(), used.end());
        rows.assign (s.begin(), s.end());
    }
    else
    {
        for (int n = lo; n <= hi; ++n)
            if (sess->view == midinja::View::All || inScale (n))
                rows.push_back (n);
    }
    std::sort (rows.begin(), rows.end(), std::greater<int>());
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
    return (float) (getWidth() - 2 - theme::keyWidth) / (float) std::max (1, doc().shown().numSteps);
}

int GridComponent::colAt (float x) const
{
    const int c = (int) std::floor ((x - 1.0f - (float) theme::keyWidth) / colWidth());
    return c >= 0 && c < doc().shown().numSteps ? c : -1;
}

int GridComponent::rowAt (float y) const
{
    const int r = (int) std::floor ((y - 1.0f) / (float) rowH);
    return r >= 0 && r < (int) rows.size() ? r : -1;
}

int GridComponent::noteAt (int col, int row) const
{
    if (col < 0 || row < 0)
        return -1;
    const auto& p = doc().shown();
    for (int s = col; s >= 0; --s)
    {
        const auto& st = p.steps[(size_t) s];
        if (st.on && st.note == rows[(size_t) row] && s + st.length > col)
            return s;
        if (st.on)
            return -1; // an earlier note on another row ends any longer note before col
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
    const auto& p = d.shown();
    const auto& sc = midinja::scales()[(size_t) d.gen.scale];
    const auto mi = midinja::modeInfo (d.gen.scale);
    const int n = p.numSteps;
    const float cw = colWidth();
    const float kw = (float) theme::keyWidth;

    juce::Path clip;
    clip.addRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.reduceClipRegion (clip);

    if (rows.empty())
    {
        g.setColour (theme::ink3);
        g.setFont (theme::font (12.0f));
        g.drawText (juce::String (midinja::partName (d.cur)) + " has no notes yet: roll the dice or switch View to Scale and click the grid",
                    getLocalBounds(), juce::Justification::centred);
        g.setColour (theme::line);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        return;
    }

    const int chordDeg = midinja::partDegree (d.cur, d.gen.scale);
    const int chordPc = sc.intervals[(size_t) std::min ((int) sc.intervals.size() - 1, chordDeg)];
    const auto isColour = [&] (int pc) { return std::find (mi.colors.begin(), mi.colors.end(), pc) != mi.colors.end(); };

    for (size_t r = 0; r < rows.size(); ++r)
    {
        const int note = rows[r];
        const int pc = pcOf (note);
        const bool inS = inScale (note);
        const float y = 1.0f + (float) r * (float) rowH;

        // key column
        g.setColour (theme::panel);
        g.fillRect (1.0f, y, kw, (float) rowH);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (1.0f, y + (float) rowH - 1.0f, kw, 1.0f);
        const bool isRoot = pc == 0;
        const float nameW = theme::textWidth (theme::font (9.5f, isRoot), theme::noteName (note));
        float x = kw + 1.0f - 5.0f - nameW;
        g.setColour (isRoot ? theme::accent : inS ? theme::ink3 : theme::outInk);
        g.setFont (theme::font (9.5f, isRoot));
        g.drawText (theme::noteName (note), juce::Rectangle<float> (x, y, nameW + 1.0f, (float) rowH), juce::Justification::centredLeft);
        if (inS && rowH >= 11)
        {
            juce::String badge;
            bool col = false;
            if (pc == 0) badge = "1";
            else if (pc == mi.third) badge = midinja::degreeName (pc);
            else if (pc == mi.fifth) badge = midinja::degreeName (pc);
            else if (isColour (pc)) { badge = midinja::degreeName (pc); col = true; }
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

        // cells
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

    // notes, on top of the cells
    for (int s = 0; s < n; ++s)
    {
        const auto& st = p.steps[(size_t) s];
        if (! st.on)
            continue;
        const auto it = std::find (rows.begin(), rows.end(), st.note);
        if (it == rows.end())
            continue;
        const float y = 1.0f + (float) (it - rows.begin()) * (float) rowH;
        const int pc = pcOf (st.note);
        const bool inS = inScale (st.note);
        const auto rect = juce::Rectangle<float> (1.0f + kw + (float) s * cw + 2.0f, y + 2.0f,
                                                  (float) st.length * cw - 4.0f, (float) rowH - 4.0f);
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
            drawPadlock (g, juce::Rectangle<float> (rect.getRight() - sz - 3.0f, rect.getCentreY() - sz * 0.5f, sz, sz), theme::lock);
        }
        if (s == 0 && d.gen.startOnRoot && pc == chordPc && rowH >= 13)
        {
            const juce::String name (juce::CharPointer_UTF8 (midinja::chordName (chordPc)));
            const auto f = theme::font (9.0f, true);
            const float bw = theme::textWidth (f, name) + 6.0f;
            const auto br = juce::Rectangle<float> (rect.getX() + 2.0f, rect.getCentreY() - 5.5f, bw, 11.0f);
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (br, 3.0f);
            g.setColour (theme::bg);
            g.setFont (f);
            g.drawText (name, br, juce::Justification::centred);
        }
    }

    g.setColour (theme::line);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}

juce::String GridComponent::getTooltip()
{
    if (sess == nullptr || rows.empty())
        return {};
    const int col = colAt ((float) hover.x), row = rowAt ((float) hover.y);
    const int s = noteAt (col, row);
    if (s != 0 || ! doc().gen.startOnRoot)
        return {};
    const auto& sc = midinja::scales()[(size_t) doc().gen.scale];
    const int chordDeg = midinja::partDegree (doc().cur, doc().gen.scale);
    const int chordPc = sc.intervals[(size_t) std::min ((int) sc.intervals.size() - 1, chordDeg)];
    if (pcOf (doc().shown().steps[0].note) != chordPc)
        return {};
    return juce::String ("Start on chord root: step 1 of ") + juce::String (juce::CharPointer_UTF8 (midinja::partName (doc().cur)))
         + " is held on its chord root (" + juce::String (juce::CharPointer_UTF8 (midinja::chordName (chordPc)))
         + "); the dice, Rhythm, Notes and Mutate keep it. Turn off under Advanced";
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
    auto& p = sess->doc.shown();
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
    midinja::fitLengths (p);
    if (onEdit) onEdit();
}

void GridComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0 || sess == nullptr)
        return;
    const int dRows = (int) std::lround ((double) -e.getDistanceFromDragStartY() / (double) rowH);
    if (dRows == 0 && ! moved)
        return;
    moved = true;
    int note = dragNote0;
    if (sess->view == midinja::View::All)
        note = dragNote0 + dRows;
    else
    {
        const int dir = dRows > 0 ? 1 : -1;
        for (int i = 0; i < std::abs (dRows); ++i)
            do note += dir; while (! inScale (note) && note > 0 && note < 127);
    }
    note = juce::jlimit (0, 127, note);
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
    if (! moved)
    {
        st.on = false;
        st.locked = false;
    }
    dragStep = -1;
    moved = false;
    if (onEdit) onEdit();
}
