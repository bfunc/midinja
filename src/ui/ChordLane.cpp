#include "ChordLane.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
juce::String u8 (const std::string& s) { return juce::String (juce::CharPointer_UTF8 (s.c_str())); }
constexpr int kBtn = 16;
} // namespace

float ChordLane::beatWidth() const
{
    const int total = sess != nullptr ? std::max (1, sess->doc.totalBeats()) : 4;
    return (float) laneArea().getWidth() / (float) total;
}

juce::Rectangle<int> ChordLane::blockRect (const midinja::Chord& c) const
{
    const auto la = laneArea();
    const float bw = beatWidth();
    const int x = la.getX() + (int) std::lround (c.start * bw) + 2;
    const int w = std::max (10, (int) std::lround (c.len * bw) - 4);
    return { x, la.getY() + 4, w, la.getHeight() - 8 };
}

ChordLane::HitAt ChordLane::hitAt (juce::Point<int> pt) const
{
    if (sess == nullptr)
        return {};
    const auto& d = sess->doc;
    for (int i : d.sortedChordIndices())
    {
        const auto& c = d.chords[(size_t) i];
        const auto r = blockRect (c);
        if (! r.expanded (3, 0).contains (pt))
            continue;
        // the small buttons: lock and inversion on the top row, regen and remove on the bottom
        const auto top = r.withHeight (kBtn + 4).reduced (3, 2), bottom = r.withTop (r.getBottom() - kBtn - 4).reduced (3, 2);
        if (top.withWidth (kBtn + 4).contains (pt)) return { Act::Lock, c.id };
        if (top.withLeft (top.getRight() - kBtn - 8).contains (pt)) return { Act::Inv, c.id };
        if (bottom.withWidth (kBtn + 4).contains (pt)) return { Act::Regen, c.id };
        if (bottom.withLeft (bottom.getRight() - kBtn - 4).contains (pt)) return { Act::Del, c.id };
        if (pt.x <= r.getX() + 8) return { Act::EdgeL, c.id };
        if (pt.x >= r.getRight() - 8) return { Act::EdgeR, c.id };
        return { Act::Move, c.id };
    }
    if (laneArea().contains (pt))
        return { Act::Empty, -1 };
    return {};
}

void ChordLane::paint (juce::Graphics& g)
{
    if (sess == nullptr)
        return;
    const auto& d = sess->doc;
    const auto la = laneArea();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (la.toFloat(), 6.0f);
    const int total = std::max (1, d.totalBeats());
    const float bw = beatWidth();
    for (int b = 0; b <= total; ++b)
    {
        const float x = (float) la.getX() + (float) b * bw;
        g.setColour (b % 4 == 0 ? theme::lineHi : juce::Colours::white.withAlpha (0.04f));
        g.fillRect (x - (b == total ? 1.0f : 0.0f), (float) la.getY(), b % 4 == 0 ? 2.0f : 1.0f, (float) la.getHeight());
    }
    if (playBeat >= 0.0)
    {
        g.setColour (theme::teal.withAlpha (0.6f));
        g.fillRect ((float) la.getX() + (float) playBeat * bw, (float) la.getY(), 2.0f, (float) la.getHeight());
    }
    const auto hit = hitAt (hover);
    for (int i : d.sortedChordIndices())
    {
        const auto& c = d.chords[(size_t) i];
        const auto r = blockRect (c);
        const bool sel = c.id == d.csel;
        g.setColour (juce::Colour (0xff2b303a));
        g.fillRoundedRectangle (r.toFloat(), 7.0f);
        g.setColour (sel ? theme::accent : c.lock ? theme::lock : juce::Colour (0xff4a5263));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, sel ? 2.0f : 1.0f);
        // edges
        g.setColour (sel ? juce::Colour (0xff8a5a2c) : theme::lineHi);
        g.fillRoundedRectangle ((float) r.getX() + 3.0f, (float) r.getY() + r.getHeight() * 0.3f, 2.0f, r.getHeight() * 0.4f, 1.0f);
        g.fillRoundedRectangle ((float) r.getRight() - 5.0f, (float) r.getY() + r.getHeight() * 0.3f, 2.0f, r.getHeight() * 0.4f, 1.0f);
        const auto top = r.withHeight (kBtn + 4).reduced (3, 2), bottom = r.withTop (r.getBottom() - kBtn - 4).reduced (3, 2);
        const auto btn = [&] (juce::Rectangle<int> br, const juce::String& txt, bool on, Act act) {
            const bool over = hit.act == act && hit.chordId == c.id;
            if (over)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (br.toFloat(), 4.0f);
            }
            g.setColour (on ? theme::lock : over ? theme::ink : theme::ink2);
            g.setFont (theme::font (10.0f, on));
            g.drawText (txt, br, juce::Justification::centred);
        };
        btn (top.withWidth (kBtn + 4), c.lock ? "L" : "l", c.lock, Act::Lock);
        btn (top.withLeft (top.getRight() - kBtn - 8), u8 ("\xE2\x86\x95") + juce::String (c.inv), false, Act::Inv);
        btn (bottom.withWidth (kBtn + 4), u8 ("\xE2\x86\xBB"), false, Act::Regen);
        btn (bottom.withLeft (bottom.getRight() - kBtn - 4), u8 ("\xC3\x97"), false, Act::Del);
        // name and numeral in the middle, the rhythm bottom centre
        const auto mid = r.reduced (6, 0);
        g.setColour (theme::accent);
        g.setFont (theme::font (15.0f, true));
        const auto type = c.type;
        g.drawText (u8 (d.numeral (c.deg, &type)), mid.withY (r.getY() + 14).withHeight (18), juce::Justification::centred, true);
        g.setColour (theme::ink2);
        g.setFont (theme::font (10.5f));
        g.drawText (u8 (d.chordName (c)), mid.withY (r.getY() + 31).withHeight (13), juce::Justification::centred, true);
        g.setColour (theme::ink3);
        g.setFont (theme::font (9.5f));
        g.drawText (midinja::rhythmName (c.rhythm), bottom.reduced (kBtn + 6, 0), juce::Justification::centred, true);
    }
    if (d.chords.empty())
    {
        g.setColour (theme::ink3);
        g.setFont (theme::font (11.0f));
        g.drawText ("Empty lane: roll Progression, pick a mood or a style, type one, or double-click to add a chord", la, juce::Justification::centred);
    }
    g.setColour (theme::line);
    g.drawRoundedRectangle (la.toFloat().reduced (0.5f), 6.0f, 1.0f);
}

void ChordLane::mouseDown (const juce::MouseEvent& e)
{
    drag.reset();
    if (sess == nullptr)
        return;
    const auto h = hitAt (e.getPosition());
    switch (h.act)
    {
        case Act::Lock:  sess->selectChord (h.chordId); sess->toggleChordLock (h.chordId); if (onChanged) onChanged(); return;
        case Act::Inv:   sess->selectChord (h.chordId); sess->cycleInversion (h.chordId); if (onChanged) onChanged(); return;
        case Act::Regen: sess->selectChord (h.chordId); sess->regenChord (h.chordId); if (onChanged) onChanged(); return;
        case Act::Del:   sess->removeChord (h.chordId); if (onChanged) onChanged(); return;
        case Act::EdgeL:
        case Act::EdgeR:
        case Act::Move:
        {
            if (! e.mods.isLeftButtonDown())
                return;
            if (sess->doc.csel != h.chordId)
            {
                sess->selectChord (h.chordId);
                if (onSelect) onSelect();
            }
            const auto* c = sess->doc.chordById (h.chordId);
            if (c == nullptr)
                return;
            drag = std::make_unique<Drag> (Drag { h.chordId, h.act, e.x, c->start, c->len });
            return;
        }
        case Act::Empty:
        case Act::None:
            return;
    }
}

void ChordLane::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == nullptr || sess == nullptr)
        return;
    const int dx = e.x - drag->x0;
    if (! drag->started)
    {
        if (std::abs (dx) < 4)
            return;
        drag->started = true;
    }
    const auto& d = sess->doc;
    const auto* c = d.chordById (drag->id);
    if (c == nullptr)
        return;
    const int delta = (int) std::lround ((double) dx / (double) beatWidth());
    // the neighbours of the block's original place
    int prevEnd = 0, nextStart = d.totalBeats();
    for (const auto& o : d.chords)
    {
        if (o.id == c->id)
            continue;
        if (o.start + o.len <= drag->start0)
            prevEnd = std::max (prevEnd, o.start + o.len);
        if (o.start >= drag->start0 + drag->len0)
            nextStart = std::min (nextStart, o.start);
    }
    int st = drag->start0, ln = drag->len0;
    if (drag->mode == Act::Move)
        st = std::max (prevEnd, std::min (nextStart - drag->len0, drag->start0 + delta));
    else if (drag->mode == Act::EdgeR)
        ln = std::max (1, std::min (nextStart - drag->start0, drag->len0 + delta));
    else
    {
        st = std::max (prevEnd, std::min (drag->start0 + drag->len0 - 1, drag->start0 + delta));
        ln = drag->start0 + drag->len0 - st;
    }
    if (st != c->start || ln != c->len)
    {
        sess->setChordSpan (drag->id, st, ln, false);
        drag->changed = true;
        repaint();
        if (onPreview) onPreview();
    }
}

void ChordLane::mouseUp (const juce::MouseEvent&)
{
    if (drag == nullptr || sess == nullptr)
        return;
    const auto dr = *drag;
    drag.reset();
    if (dr.changed)
    {
        const auto* c = sess->doc.chordById (dr.id);
        if (c != nullptr)
            sess->setChordSpan (dr.id, c->start, c->len, true);
        if (onChanged) onChanged();
    }
    else
        repaint();
}

void ChordLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (sess == nullptr)
        return;
    if (hitAt (e.getPosition()).act != Act::Empty)
        return;
    const int beat = (int) std::floor ((float) (e.x - laneArea().getX()) / beatWidth());
    sess->addChordAt (juce::jlimit (0, std::max (0, sess->doc.totalBeats() - 1), beat));
    if (onChanged) onChanged();
}

juce::String ChordLane::getTooltip()
{
    if (sess == nullptr)
        return {};
    const auto h = hitAt (hover);
    const auto* c = h.chordId >= 0 ? sess->doc.chordById (h.chordId) : nullptr;
    switch (h.act)
    {
        case Act::Lock:  return c != nullptr && c->lock ? "Unlock" : "Lock: rolls keep this chord";
        case Act::Inv:   return "Inversion: click for the next";
        case Act::Regen: return "Another chord here";
        case Act::Del:   return "Remove";
        case Act::EdgeL:
        case Act::EdgeR: return "Drag to change the length (one-beat steps)";
        case Act::Move:  return c != nullptr ? u8 (sess->doc.chordName (*c)) + u8 (" \xC2\xB7 beats ") + juce::String (c->start + 1) + u8 ("\xE2\x80\x93") + juce::String (c->start + c->len)
                                               + u8 (" \xC2\xB7 drag to move, drag an edge for the length") : juce::String();
        case Act::Empty: return "Double-click to add a chord here";
        case Act::None:  return {};
    }
    return {};
}
