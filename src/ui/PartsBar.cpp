#include "PartsBar.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>

namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
juce::String u8 (const std::string& s) { return juce::String (juce::CharPointer_UTF8 (s.c_str())); }
constexpr int kThumbW = 44, kThumbH = 16, kAdd = 26;
} // namespace

// bar >= 0: the slot of a bar (its chord degree under the letter); -1: a palette letter
int PartsBar::nameWidth (int k, int bar) const
{
    const auto& d = sess->doc;
    const float w1 = theme::textWidth (theme::font (13.0f, true), u8 (midinja::partName (k)));
    const float w2 = bar >= 0 ? theme::textWidth (theme::font (9.0f), u8 (d.numeral (d.barDegree (bar)))) : 0.0f;
    return std::max (16, (int) std::ceil (std::max (w1, w2)));
}

void PartsBar::drawName (juce::Graphics& g, int k, int bar, int x, juce::Colour ink) const
{
    const auto& d = sess->doc;
    const int tn = nameWidth (k, bar);
    g.setColour (ink);
    g.setFont (theme::font (13.0f, true));
    if (bar < 0)
    {
        g.drawText (u8 (midinja::partName (k)), x, 0, tn, height, juce::Justification::centred);
        return;
    }
    g.drawText (u8 (midinja::partName (k)), x, 1, tn, 16, juce::Justification::centred);
    g.setColour (ink == theme::accent ? theme::accent : theme::ink3);
    g.setFont (theme::font (9.0f));
    g.drawText (u8 (d.numeral (d.barDegree (bar))), x, 16, tn, 11, juce::Justification::centred);
}

PartsBar::Layout PartsBar::layout() const
{
    Layout L;
    if (sess == nullptr)
        return L;
    const auto& d = sess->doc;
    int x = 0;
    for (int i = 0; i < (int) d.arr.size(); ++i)
    {
        const int tn = nameWidth (d.arr[(size_t) i], i);
        const int w = 6 + 16 + 6 + tn + 6 + kThumbW + 6 + 18 + 4;
        L.slots.push_back ({ x, 0, w, height });
        L.slotLocks.push_back ({ x + 6, 7, 16, 16 });
        L.slotXs.push_back ({ x + w - 4 - 18, 6, 18, 18 });
        x += w + 6;
    }
    L.add = { x, (height - kAdd) / 2, kAdd, kAdd };
    x += kAdd + 6;
    L.full = (int) d.arr.size() >= midinja::kMaxBars;
    int px = getWidth();
    for (int k = midinja::kNumParts - 1; k >= 0; --k)
    {
        const int w = 9 + nameWidth (k, -1) + 9;
        px -= w;
        L.tabs.insert (L.tabs.begin(), { px, 0, w, height });
        px -= 6;
    }
    const int palLeft = px + 6 - 10 - 1;
    const int infoRight = reserveW > 0 ? palLeft - 8 - reserveW : palLeft;
    L.reserved = reserveW > 0 ? juce::Rectangle<int> (palLeft - 8 - reserveW, 0, reserveW, height) : juce::Rectangle<int>();
    L.info = { x, 0, std::max (0, infoRight - x), height };
    return L;
}

PartsBar::Hit PartsBar::hitAt (juce::Point<int> pt) const
{
    const auto L = layout();
    for (int i = 0; i < (int) L.slots.size(); ++i)
    {
        if (L.slotLocks[(size_t) i].contains (pt)) return { Hit::SlotLock, i };
        if (L.slotXs[(size_t) i].contains (pt)) return { Hit::SlotX, i };
        if (L.slots[(size_t) i].contains (pt)) return { Hit::Slot, i };
    }
    if (L.add.contains (pt))
        return { Hit::Add, 0 };
    for (int k = 0; k < (int) L.tabs.size(); ++k)
        if (L.tabs[(size_t) k].contains (pt))
            return { Hit::Tab, k };
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
    const auto& t = d.T().drums ? d.T() : d.riff(); // the locks of the tab shown
    const auto L = layout();
    const auto hit = hitAt (hover);

    for (int i = 0; i < (int) L.slots.size(); ++i)
    {
        const int k = d.arr[(size_t) i];
        const auto r = L.slots[(size_t) i].toFloat().reduced (0.5f);
        const bool on = i == d.sel, same = k == d.cur, locked = t.locks[(size_t) k];
        const bool over = (hit.kind == Hit::Slot || hit.kind == Hit::SlotLock || hit.kind == Hit::SlotX) && hit.index == i;
        g.setColour (on ? theme::accent.withAlpha (0.13f) : theme::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (on ? theme::accent : locked ? theme::lock : over ? theme::lineHi : theme::line);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        const auto ink = on ? theme::accent : same ? theme::ink : theme::ink2;
        const bool lockOver = hit.kind == Hit::SlotLock && hit.index == i;
        drawPadlock (g, L.slotLocks[(size_t) i].toFloat().reduced (3.0f, 2.5f),
                     (locked ? theme::lock : ink).withAlpha (locked ? 1.0f : lockOver ? 0.9f : 0.3f), ! locked);
        int x = L.slots[(size_t) i].getX() + 6 + 16 + 6;
        const int tn = nameWidth (k, i);
        drawName (g, k, i, x, ink);
        x += tn + 6;
        const auto tt = juce::Rectangle<float> ((float) x, 7.0f, (float) kThumbW, (float) kThumbH);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (tt, 4.0f);
        ui::paintThumb (g, tt, { &t.parts[(size_t) k] }, on ? theme::accent : theme::ink2);
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
    // the square +: repeats the last bar
    {
        const bool can = ! L.full && ! d.arr.empty();
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
        g.drawText ("No bars yet: click a part on the right to add it", L.info.reduced (6, 0), juce::Justification::centredLeft, true);
    }
    else
    {
        juce::String info;
        const bool lockMsg = ! d.T().poly && t.locks[(size_t) d.cur];
        const int b = d.shownBar();
        if (lockMsg)
            info = u8 (midinja::partName (d.cur)) + " locked: dice skip it";
        else
        {
            info = juce::String (d.T().poly ? "Chords" : "Riff") + u8 (" \xC2\xB7 ")
                 + (d.T().poly ? (b >= 0 ? "bar " + juce::String (b + 1) : juce::String ("no bar")) : u8 (midinja::partName (d.cur))) + u8 (" \xC2\xB7 ");
            if (d.sel >= 0) info += "Play: bar " + juce::String (d.sel + 1);
            else if (! d.arr.empty()) info += "Play: " + juce::String ((int) d.arr.size()) + (d.arr.size() > 1 ? " bars" : " bar");
            else info += "Play: this part";
        }
        g.setColour (lockMsg ? theme::lock : theme::ink3);
        g.setFont (theme::font (10.5f));
        g.drawText (info, L.info.reduced (8, 0), juce::Justification::centred, true);
    }
    if (! L.tabs.empty())
    {
        g.setColour (theme::line);
        g.fillRect (L.tabs.front().getX() - 10 - 1, 0, 1, height);
    }
    for (int k = 0; k < (int) L.tabs.size(); ++k)
    {
        const auto r = L.tabs[(size_t) k].toFloat().reduced (0.5f);
        const bool on = k == d.cur;
        const bool over = hit.kind == Hit::Tab && hit.index == k && ! L.full;
        const float alpha = L.full ? 0.4f : 1.0f;
        g.setColour ((on ? theme::accent.withAlpha (0.13f) : theme::panel).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour ((on ? theme::accent : over ? theme::teal : theme::line).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        drawName (g, k, -1, L.tabs[(size_t) k].getX() + 9, (on ? theme::accent : theme::ink2).withMultipliedAlpha (alpha));
    }
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
        case Hit::SlotX:    if (onRemoveBar) onRemoveBar (hit.index); break;
        case Hit::Slot:
            if (e.mods.isShiftDown()) { if (onToggleLock) onToggleLock (d.arr[(size_t) hit.index]); }
            else if (onSelectSlot) onSelectSlot (hit.index);
            break;
        case Hit::Add:
            if ((int) d.arr.size() < midinja::kMaxBars && ! d.arr.empty() && onRepeatBar)
                onRepeatBar();
            break;
        case Hit::Tab:
            if ((int) d.arr.size() < midinja::kMaxBars && onAddBar)
                onAddBar (hit.index);
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
    const auto& t = d.T().drums ? d.T() : d.riff();
    const auto hit = hitAt (hover);
    const auto nameOf = [] (int k) { return u8 (midinja::partName (k)); };
    const auto hasNotes = [] (const midinja::Pattern& p) {
        for (int i = 0; i < p.numSteps; ++i)
            if (p.steps[(size_t) i].on)
                return true;
        return false;
    };
    switch (hit.kind)
    {
        case Hit::Slot:
        {
            const int k = d.arr[(size_t) hit.index];
            return "Bar " + juce::String (hit.index + 1) + ": " + nameOf (k) + " on " + u8 (d.numeral (d.barDegree (hit.index))) + u8 (" \xC2\xB7 ")
                 + (hit.index == d.sel ? juce::String ("selected: Play loops this bar; click again to deselect") : "click to select it and edit " + nameOf (k))
                 + " (every " + nameOf (k) + " bar changes with it)";
        }
        case Hit::SlotLock:
        {
            const int k = d.arr[(size_t) hit.index];
            return t.locks[(size_t) k] ? "Unlock (Shift+click): " + nameOf (k) + " is locked, the dice and All skip it"
                                       : "Lock (Shift+click): the dice and All keep " + nameOf (k) + " as it is";
        }
        case Hit::SlotX: return "Remove this bar from the arrangement (its chords go with it)";
        case Hit::Add:
            if ((int) d.arr.size() >= midinja::kMaxBars) return "4 bars at most";
            if (d.arr.empty()) return "Add a part from the palette first";
            return "Repeat the last bar after it (" + nameOf (d.arr.back()) + " again)";
        case Hit::Tab:
        {
            const int k = hit.index;
            if ((int) d.arr.size() >= midinja::kMaxBars)
                return u8 ("Arrangement is full (4 bars) \xE2\x80\x94 remove a bar with \xC3\x97");
            juce::String s = nameOf (k) + u8 (" \xC2\xB7 ") + midinja::partHint (k);
            if (t.src[(size_t) k] == midinja::Src::Import) s += u8 (" \xC2\xB7 imported");
            if (! hasNotes (t.parts[(size_t) k])) s += u8 (" \xC2\xB7 empty: added as a copy of A on its bar's chord");
            return s + u8 (" \xC2\xB7 click to add a bar to the arrangement");
        }
        case Hit::Info:
            return d.T().poly ? juce::String ("Chords: the lane above, bar by bar")
                              : "Riff: editing " + nameOf (d.cur) + " (" + midinja::partHint (d.cur) + ", on " + u8 (d.numeral (d.degreeOf (d.cur))) + "); 1 bar = " + juce::String (d.numSteps()) + " steps";
        case Hit::None: return {};
    }
    return {};
}
