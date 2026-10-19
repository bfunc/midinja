#include "PartsBar.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>

namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
juce::String u8 (const std::string& s) { return juce::String (juce::CharPointer_UTF8 (s.c_str())); }
constexpr int kThumbW = 44, kThumbH = 16, kAdd = 26, kBtn = 18, kNum = 12;
const juce::String kDot = u8 (" \xC2\xB7 ");
juce::Font nameFont() { return theme::font (13.0f, true); }
juce::Font chordFont (bool bold) { return theme::font (11.0f, bold); }

bool hasNotes (const midinja::Pattern& p)
{
    for (int i = 0; i < std::min (p.numSteps, midinja::kMaxSteps); ++i)
        if (p.steps[(size_t) i].on)
            return true;
    return false;
}
} // namespace

// Chords tab: the numerals of the chords in the bar, "no chord" when it has none
juce::String PartsBar::chordsText (int bar) const
{
    const auto& d = sess->doc;
    juce::StringArray names;
    for (int i : d.sortedChordIndices())
    {
        const auto& c = d.chords[(size_t) i];
        if (d.overlapsBar (c, bar))
            names.add (u8 (d.numeral (c.deg, &c.type)));
    }
    return names.isEmpty() ? juce::String ("no chord") : names.joinIntoString (kDot);
}

// ⧉ needs another bar of the same part
bool PartsBar::canCopy (int bar) const
{
    const auto& arr = sess->doc.arr;
    return bar >= 0 && bar < (int) arr.size() && std::count (arr.begin(), arr.end(), arr[(size_t) bar]) > 1;
}

PartsBar::Layout PartsBar::layout() const
{
    Layout L;
    if (sess == nullptr)
        return L;
    const auto& d = sess->doc;
    const bool poly = d.T().poly;
    int x = 0;
    for (int i = 0; i < (int) d.arr.size(); ++i)
    {
        const int k = d.arr[(size_t) i];
        if (poly)
        {
            const int tw = juce::jlimit (44, 150, (int) std::ceil (theme::textWidth (chordFont (true), chordsText (i))) + 2);
            const int w = 6 + kNum + 4 + tw + 6 + kBtn + 2 + kBtn + 4;
            L.slots.push_back ({ x, 0, w, height });
            L.slotLocks.push_back ({});
            L.slotNames.push_back ({ x + 6 + kNum + 4, 0, tw, height });
            L.slotCopies.push_back ({ x + w - 4 - kBtn - 2 - kBtn, 6, kBtn, kBtn });
            L.slotXs.push_back ({ x + w - 4 - kBtn, 6, kBtn, kBtn });
        }
        else
        {
            const int tn = (int) std::ceil (theme::textWidth (nameFont(), u8 (midinja::partName (k)))) + 10; // the name and its ▾
            const int w = 6 + kNum + 4 + 16 + 6 + tn + 6 + kThumbW + 6 + kBtn + 4;
            L.slots.push_back ({ x, 0, w, height });
            L.slotLocks.push_back ({ x + 6 + kNum + 4, 7, 16, 16 });
            L.slotNames.push_back ({ x + 6 + kNum + 4 + 16 + 6, 0, tn, height });
            L.slotCopies.push_back ({});
            L.slotXs.push_back ({ x + w - 4 - kBtn, 6, kBtn, kBtn });
        }
        x += L.slots.back().getWidth() + 6;
    }
    L.add = { x, (height - kAdd) / 2, kAdd, kAdd };
    x += kAdd + 6;
    L.full = (int) d.arr.size() >= midinja::kMaxBars;
    const int infoRight = reserveW > 0 ? getWidth() - reserveW - 8 : getWidth();
    L.reserved = reserveW > 0 ? juce::Rectangle<int> (getWidth() - reserveW, 0, reserveW, height) : juce::Rectangle<int>();
    L.info = { x, 0, std::max (0, infoRight - x), height };
    return L;
}

PartsBar::Hit PartsBar::hitAt (juce::Point<int> pt) const
{
    if (sess == nullptr)
        return {};
    const auto L = layout();
    const bool poly = sess->doc.T().poly;
    for (int i = 0; i < (int) L.slots.size(); ++i)
    {
        if (L.slotLocks[(size_t) i].contains (pt)) return { Hit::SlotLock, i };
        if (L.slotCopies[(size_t) i].contains (pt)) return { Hit::SlotCopy, i };
        if (L.slotXs[(size_t) i].contains (pt)) return { Hit::SlotX, i };
        if (! poly && L.slotNames[(size_t) i].contains (pt)) return { Hit::SlotName, i };
        if (L.slots[(size_t) i].contains (pt)) return { Hit::Slot, i };
    }
    if (L.add.contains (pt))
        return { Hit::Add, 0 };
    if (L.info.contains (pt))
        return { Hit::Info, 0 };
    return {};
}

void PartsBar::drawPadlock (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool open)
{
    g.setColour (c);
    const auto body = juce::Rectangle<float> (r.getX(), r.getY() + r.getHeight() * 0.45f, r.getWidth(), r.getHeight() * 0.55f);
    g.fillRoundedRectangle (body, 2.0f);
    juce::Path shackle;
    const float cx = open ? r.getCentreX() + r.getWidth() * 0.3f : r.getCentreX();
    shackle.addCentredArc (cx, body.getY(), r.getWidth() * 0.3f, r.getHeight() * 0.42f, 0.0f,
                           -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    if (open)
        shackle.lineTo (cx + r.getWidth() * 0.3f, body.getY() - r.getHeight() * 0.15f);
    g.strokePath (shackle, juce::PathStrokeType (1.6f));
}

void PartsBar::paint (juce::Graphics& g)
{
    if (sess == nullptr)
        return;
    const auto& d = sess->doc;
    const bool poly = d.T().poly;
    const auto& t = d.T().drums ? d.T() : d.riff(); // the locks and parts of the tab shown
    const auto L = layout();
    const auto hit = hitAt (hover);
    const int b0 = d.shownBar();

    for (int i = 0; i < (int) L.slots.size(); ++i)
    {
        const int k = d.arr[(size_t) i];
        const auto sr = L.slots[(size_t) i];
        const auto r = sr.toFloat().reduced (0.5f);
        // the levels of the mockup: primary = the bar the roll edits (Riff, Beats); context = the
        // bar shown on Chords, or another bar of the part shown; state = a locked part
        const bool prim = ! poly && i == b0;
        const bool ctx = poly ? i == b0 : (k == d.cur && ! prim);
        const bool locked = ! poly && t.locks[(size_t) k];
        const bool over = hit.index == i && (hit.kind == Hit::Slot || hit.kind == Hit::SlotLock || hit.kind == Hit::SlotName || hit.kind == Hit::SlotCopy || hit.kind == Hit::SlotX);
        g.setColour (prim ? theme::accent.withAlpha (0.13f) : theme::panel2);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (prim ? theme::accent : ctx ? theme::ctx : locked ? theme::state : over ? theme::lineHi : theme::line);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        if (prim)
            g.drawRoundedRectangle (r.reduced (1.0f), 7.0f, 1.0f);
        const auto ink = prim ? theme::accent : ctx ? theme::ink : theme::ink2;
        g.setColour (prim ? theme::accent : theme::ink3);
        g.setFont (theme::font (10.0f));
        g.drawText (juce::String (i + 1), sr.getX() + 6, 0, kNum, height, juce::Justification::centredLeft);
        if (poly)
        {
            // the chords of the bar: numerals in bold, the dots between them dimmer
            juce::AttributedString as;
            bool any = false;
            for (int ci : d.sortedChordIndices())
            {
                const auto& c = d.chords[(size_t) ci];
                if (! d.overlapsBar (c, i))
                    continue;
                if (any)
                    as.append (kDot, chordFont (false), theme::ink2);
                as.append (u8 (d.numeral (c.deg, &c.type)), chordFont (true), ctx ? theme::ink : ink);
                any = true;
            }
            if (! any)
                as.append ("no chord", chordFont (false), theme::ink3);
            as.setJustification (juce::Justification::centredLeft);
            as.setWordWrap (juce::AttributedString::none);
            g.saveState();
            g.reduceClipRegion (L.slotNames[(size_t) i]);
            as.draw (g, L.slotNames[(size_t) i].toFloat());
            g.restoreState();
            // ⧉: copy the bar's chords to the other bars of its part
            const auto cr = L.slotCopies[(size_t) i];
            const bool can = canCopy (i);
            const bool cOver = can && hit.kind == Hit::SlotCopy && hit.index == i;
            if (cOver)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (cr.toFloat(), 4.0f);
            }
            const auto cc = (cOver ? theme::ink : theme::ink3).withMultipliedAlpha (can ? 1.0f : 0.4f);
            const float cx = (float) cr.getCentreX(), cy = (float) cr.getCentreY();
            g.setColour (cc);
            g.drawRect (juce::Rectangle<float> (cx - 5.0f, cy - 5.0f, 7.0f, 7.0f), 1.2f);
            g.setColour (theme::panel2);
            g.fillRect (juce::Rectangle<float> (cx - 2.0f, cy - 2.0f, 7.0f, 7.0f));
            g.setColour (cc);
            g.drawRect (juce::Rectangle<float> (cx - 2.0f, cy - 2.0f, 7.0f, 7.0f), 1.2f);
        }
        else
        {
            const bool lockOver = hit.kind == Hit::SlotLock && hit.index == i;
            drawPadlock (g, L.slotLocks[(size_t) i].toFloat().reduced (3.0f, 2.5f),
                         (locked ? theme::lock : ink).withAlpha (locked ? 1.0f : lockOver ? 0.9f : 0.3f), ! locked);
            const auto nr = L.slotNames[(size_t) i];
            g.setColour (ink);
            g.setFont (nameFont());
            g.drawText (u8 (midinja::partName (k)), nr.withTrimmedRight (10), juce::Justification::centredLeft);
            // the ▾ of the part picker
            const bool nameOver = hit.kind == Hit::SlotName && hit.index == i;
            juce::Path tri;
            const float tx = (float) nr.getRight() - 6.0f, ty = (float) height * 0.5f;
            tri.addTriangle (tx - 3.0f, ty - 1.5f, tx + 3.0f, ty - 1.5f, tx, ty + 2.0f);
            g.setColour (nameOver ? theme::ink : theme::ink3);
            g.fillPath (tri);
            const auto tt = juce::Rectangle<float> ((float) nr.getRight() + 6.0f, 7.0f, (float) kThumbW, (float) kThumbH);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (tt, 4.0f);
            ui::paintThumb (g, tt, { &t.parts[(size_t) k] }, prim ? theme::accent : theme::ink2);
        }
        const auto xr = L.slotXs[(size_t) i];
        const bool xOver = hit.kind == Hit::SlotX && hit.index == i;
        if (xOver)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (xr.toFloat(), 4.0f);
        }
        g.setColour (xOver ? theme::ink : theme::ink3);
        g.drawLine ((float) xr.getCentreX() - 3.5f, (float) xr.getCentreY() - 3.5f, (float) xr.getCentreX() + 3.5f, (float) xr.getCentreY() + 3.5f, 1.4f);
        g.drawLine ((float) xr.getCentreX() - 3.5f, (float) xr.getCentreY() + 3.5f, (float) xr.getCentreX() + 3.5f, (float) xr.getCentreY() - 3.5f, 1.4f);
        if (i == playingSlot)
        {
            g.setColour (theme::teal.withAlpha (0.35f));
            g.fillRoundedRectangle (r.getX() + 7.0f, r.getBottom() - 3.0f, r.getWidth() - 14.0f, 4.0f, 2.0f);
            g.setColour (theme::teal);
            g.fillRect (r.getX() + 8.0f, r.getBottom() - 2.0f, r.getWidth() - 16.0f, 2.0f);
        }
    }
    // the square +: repeats the last bar (bar 1 with A when the form is empty)
    {
        const bool can = ! L.full;
        const auto r = L.add.toFloat().reduced (0.5f);
        g.setColour (theme::panel2.withAlpha (can ? 1.0f : 0.4f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (hit.kind == Hit::Add && can ? theme::lineHi : theme::line);
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        g.setColour (theme::accent.withAlpha (can ? 1.0f : 0.35f));
        g.fillRect (r.getCentreX() - 1.0f, r.getCentreY() - 5.0f, 2.0f, 10.0f);
        g.fillRect (r.getCentreX() - 5.0f, r.getCentreY() - 1.0f, 10.0f, 2.0f);
    }
    if (L.slots.empty())
    {
        g.setColour (theme::ink3);
        g.setFont (theme::font (11.0f));
        g.drawText ("No bars yet: + adds bar 1 with part A", L.info.reduced (6, 0), juce::Justification::centredLeft, true);
    }
    else
    {
        juce::String info;
        const bool lockMsg = ! poly && t.locks[(size_t) d.cur];
        if (lockMsg)
            info = u8 (midinja::partName (d.cur)) + " locked: dice skip it";
        else
        {
            info = juce::String (poly ? "Chords" : d.T().drums ? "Beats" : "Riff") + kDot
                 + (poly ? (b0 >= 0 ? "bar " + juce::String (b0 + 1) : juce::String ("no bar")) : u8 (midinja::partName (d.cur))) + kDot;
            if (d.sel >= 0) info += "Play: bar " + juce::String (d.sel + 1);
            else if (! d.arr.empty()) info += "Play: " + juce::String ((int) d.arr.size()) + (d.arr.size() > 1 ? " bars" : " bar");
            else info += "Play: this part";
        }
        g.setColour (lockMsg ? theme::lock : theme::ink3);
        g.setFont (theme::font (10.5f));
        g.drawText (info, L.info.reduced (8, 0), juce::Justification::centred, true);
    }
}

// the ▾ on a bar: which part plays there
void PartsBar::showPartPicker (int bar)
{
    const auto& d = sess->doc;
    if (bar < 0 || bar >= (int) d.arr.size())
        return;
    const auto& t = d.T().drums ? d.T() : d.riff();
    juce::PopupMenu m;
    m.addSectionHeader ("Part in bar " + juce::String (bar + 1));
    for (int k = 0; k < midinja::kNumParts; ++k)
    {
        juce::String s = u8 (midinja::partName (k)) + kDot + midinja::partHint (k);
        if (t.src[(size_t) k] == midinja::Src::Import)
            s += kDot + "imported";
        else if (! d.T().drums && ! hasNotes (t.parts[(size_t) k]))
            s += kDot + "empty: comes in as a copy of A on this bar's chord";
        m.addItem (k + 1, s, true, k == d.arr[(size_t) bar]);
    }
    const auto L = layout();
    juce::Component::SafePointer<PartsBar> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (L.slotNames[(size_t) bar])),
                     [self, bar] (int r) {
                         if (self == nullptr || self->sess == nullptr || r <= 0)
                             return;
                         const auto& arr = self->sess->doc.arr;
                         if (bar < (int) arr.size() && arr[(size_t) bar] != r - 1 && self->onSetBarPart)
                             self->onSetBarPart (bar, r - 1);
                     });
}

void PartsBar::mouseDown (const juce::MouseEvent& e)
{
    if (sess == nullptr)
        return;
    const auto hit = hitAt (e.getPosition());
    const auto& d = sess->doc;
    switch (hit.kind)
    {
        case Hit::SlotLock: if (onToggleLock) onToggleLock (d.arr[(size_t) hit.index]); break;
        case Hit::SlotCopy: if (canCopy (hit.index) && onCopyBar) onCopyBar (hit.index); break;
        case Hit::SlotX:    if (onRemoveBar) onRemoveBar (hit.index); break;
        case Hit::SlotName:
            if (e.mods.isShiftDown()) { if (onToggleLock) onToggleLock (d.arr[(size_t) hit.index]); }
            else showPartPicker (hit.index);
            break;
        case Hit::Slot:
            if (e.mods.isShiftDown() && ! d.T().poly) { if (onToggleLock) onToggleLock (d.arr[(size_t) hit.index]); }
            else if (onSelectSlot) onSelectSlot (hit.index);
            break;
        case Hit::Add:
            if ((int) d.arr.size() < midinja::kMaxBars && onRepeatBar)
                onRepeatBar();
            break;
        case Hit::Info:
        case Hit::None: break;
    }
}

juce::String PartsBar::getTooltip()
{
    if (sess == nullptr)
        return {};
    const auto& d = sess->doc;
    const bool poly = d.T().poly;
    const auto& t = d.T().drums ? d.T() : d.riff();
    const auto hit = hitAt (hover);
    const auto nameOf = [] (int k) { return u8 (midinja::partName (k)); };
    switch (hit.kind)
    {
        case Hit::Slot:
        {
            const int k = d.arr[(size_t) hit.index];
            if (poly)
            {
                juce::StringArray names;
                for (int i : d.sortedChordIndices())
                    if (d.overlapsBar (d.chords[(size_t) i], hit.index))
                        names.add (u8 (d.chordName (d.chords[(size_t) i])));
                return "Bar " + juce::String (hit.index + 1) + ": " + (names.isEmpty() ? juce::String ("no chord") : names.joinIntoString (", ")) + kDot
                     + (hit.index == d.sel ? "selected: Play loops this bar, the inspector edits its chords; click again to deselect" : "click to select it and edit its chords");
            }
            return "Bar " + juce::String (hit.index + 1) + ": part " + nameOf (k) + kDot
                 + (hit.index == d.sel ? juce::String ("selected: Play loops this bar; click again to deselect") : "click to select it and edit " + nameOf (k))
                 + " (every " + nameOf (k) + " bar changes with it)";
        }
        case Hit::SlotLock:
        {
            const int k = d.arr[(size_t) hit.index];
            return t.locks[(size_t) k] ? "Unlock (Shift+click): " + nameOf (k) + " is locked, the dice and All skip it"
                                       : "Lock (Shift+click): the dice and All keep " + nameOf (k) + " as it is";
        }
        case Hit::SlotName: return "Which part plays in bar " + juce::String (hit.index + 1) + ": click to choose";
        case Hit::SlotCopy:
        {
            const int k = d.arr[(size_t) hit.index];
            const int sib = (int) std::count (d.arr.begin(), d.arr.end(), k) - 1;
            return sib > 0 ? "Copy the chords of bar " + juce::String (hit.index + 1) + " to the other " + nameOf (k) + (sib > 1 ? " bars" : " bar") + " (locked chords there stay); one undo step"
                           : "No other bar plays " + nameOf (k) + ": nothing to copy to";
        }
        case Hit::SlotX: return "Remove this bar from the form (its chords go with it)";
        case Hit::Add:
            if ((int) d.arr.size() >= midinja::kMaxBars) return "4 bars at most";
            if (d.arr.empty()) return "Add bar 1 with part A";
            return "Repeat the last bar after it (" + nameOf (d.arr.back()) + " again)"
                 + (poly ? juce::String (" with a copy of its chords; its part changes on Riff or Beats") : u8 ("; the \xE2\x96\xBE on a bar changes its part"));
        case Hit::Info:
            return poly ? juce::String ("Chords: the lane below, bar by bar; click a bar to edit its chords")
                 : d.T().drums ? "Beats: editing " + nameOf (d.cur) + " (" + midinja::partHint (d.cur) + ")"
                               : "Riff: editing " + nameOf (d.cur) + " (" + midinja::partHint (d.cur) + ", on " + u8 (d.numeral (d.degreeOf (d.cur))) + "); 1 bar = " + juce::String (d.numSteps()) + " steps";
        case Hit::None: return {};
    }
    return {};
}
