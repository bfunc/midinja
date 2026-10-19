#include "TabBar.h"
#include "Theme.h"

#include <cmath>

namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
const char* tabName (int i) { return i == midinja::kChordTab ? "Chords" : i == midinja::kBeatTab ? "Beats" : "Riff"; }
juce::Font palFont() { return theme::font (12.0f, true); }
juce::Font barsFont() { return theme::font (9.0f); }

// the bars part k plays in, as "1 3"
juce::String barsOf (const midinja::Doc& d, int k)
{
    juce::StringArray bars;
    for (int i = 0; i < (int) d.arr.size(); ++i)
        if (d.arr[(size_t) i] == k)
            bars.add (juce::String (i + 1));
    return bars.joinIntoString (" ");
}
} // namespace

TabBar::TabBar()
{
    for (int c = 1; c <= 16; ++c)
        channel.addItem ("Ch " + juce::String (c), c);
    channel.setTooltip ("MIDI channel of this tab");
    channel.onChange = [this] { if (sess != nullptr && onChannel) onChannel (sess->doc.tab, channel.getSelectedId()); };
    addAndMakeVisible (channel);
}

void TabBar::refresh()
{
    if (sess == nullptr)
        return;
    channel.setSelectedId (sess->doc.T().channel, juce::dontSendNotification);
    repaint();
}

void TabBar::resized()
{
    int x = 14;
    const auto f = theme::font (13.0f, true);
    for (int i = 0; i < midinja::kNumTabs; ++i)
    {
        const int w = 9 + 11 + 8 + (int) theme::textWidth (f, tabName (i)) + 12;
        boxes[(size_t) i] = { x, 4, w, height - 8 };
        dots[(size_t) i] = { x + 9, (height - 11) / 2, 11, 11 };
        x += w + 8;
    }
    tabsRight = x - 8;
    const int rx = getWidth() - 14;
    rRange = { rx - 70, 0, 70, height };
    channel.setBounds (rRange.getX() - 8 - 66 - 50, (height - 24) / 2, 66, 24);
    rLabel = { channel.getX() - 60, 0, 56, height };
}

// the part selector (web `.pal`): after the tabs, behind a thin line; not on Chords
TabBar::Pal TabBar::palLayout() const
{
    Pal P;
    if (sess == nullptr || sess->doc.T().poly)
        return P;
    const auto& d = sess->doc;
    int x = tabsRight + 4;
    P.sep = x;
    x += 1 + 10;
    for (int k = 0; k < midinja::kNumParts; ++k)
    {
        const auto bars = barsOf (d, k);
        const int w = 9 + (int) std::ceil (theme::textWidth (palFont(), u8 (midinja::partName (k))))
                    + (bars.isEmpty() ? 0 : 3 + (int) std::ceil (theme::textWidth (barsFont(), bars))) + 9;
        P.boxes[(size_t) k] = { x, (height - 24) / 2, w, 24 };
        x += w + 4;
    }
    return P;
}

void TabBar::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds());
    g.setColour (theme::line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
    if (sess == nullptr)
        return;
    const auto& d = sess->doc;
    for (int i = 0; i < midinja::kNumTabs; ++i)
    {
        const auto& t = d.tabs[(size_t) i];
        const bool cur = i == d.tab;
        const auto r = boxes[(size_t) i].toFloat().reduced (0.5f);
        g.setColour (theme::panel2);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (cur ? theme::accent : theme::line);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        const auto dot = dots[(size_t) i].toFloat();
        if (t.on)
        {
            g.setColour (theme::teal.withAlpha (0.35f));
            g.fillEllipse (dot.expanded (3.0f));
            g.setColour (theme::teal);
            g.fillEllipse (dot);
        }
        else
        {
            g.setColour (theme::ink3);
            g.drawEllipse (dot.reduced (1.0f), 2.0f);
        }
        g.setColour ((cur ? theme::accent : theme::ink2).withAlpha (t.on ? 1.0f : 0.6f));
        g.setFont (theme::font (13.0f, true));
        g.drawText (tabName (i), boxes[(size_t) i].withTrimmedLeft (9 + 11 + 8), juce::Justification::centredLeft);
    }
    const auto P = palLayout();
    if (P.sep >= 0)
    {
        g.setColour (theme::line);
        g.fillRect (P.sep, 6, 1, height - 12);
        const auto& t = d.T().drums ? d.T() : d.riff(); // the locks of the tab shown
        for (int k = 0; k < midinja::kNumParts; ++k)
        {
            const auto b = P.boxes[(size_t) k];
            const auto r = b.toFloat().reduced (0.5f);
            const bool on = k == d.cur, locked = t.locks[(size_t) k];
            g.setColour (on ? theme::teal.withAlpha (0.12f) : theme::panel2);
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (on ? theme::teal : locked ? theme::state : theme::line);
            g.drawRoundedRectangle (r, 7.0f, 1.0f);
            if (locked) // the inset ring of a lock
                g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 1.0f);
            const auto name = u8 (midinja::partName (k));
            const int nw = (int) std::ceil (theme::textWidth (palFont(), name));
            g.setColour (on ? theme::teal : theme::ink2);
            g.setFont (palFont());
            g.drawText (name, b.getX() + 9, 0, nw, height, juce::Justification::centredLeft);
            const auto bars = barsOf (d, k);
            if (bars.isNotEmpty())
            {
                g.setColour (on ? theme::teal : theme::ink3);
                g.setFont (barsFont());
                const int bx = b.getX() + 9 + nw + 3;
                g.drawText (bars, bx, 0, b.getRight() - 9 - bx, height, juce::Justification::centredLeft);
            }
        }
    }
    const auto& t = d.T();
    g.setColour (theme::ink3);
    g.setFont (theme::font (10.0f));
    g.drawText (juce::String (tabName (d.tab)).toUpperCase(), rLabel, juce::Justification::centredRight);
    g.drawText ("RANGE", rRange.withWidth (40), juce::Justification::centredLeft);
    g.setColour (theme::ink2);
    g.setFont (theme::font (11.0f));
    g.drawText (t.drums ? juce::String ("GM drums") : theme::noteName (t.lowNote) + u8 ("\xE2\x80\x93") + theme::noteName (t.highNote), rRange.withTrimmedLeft (0).withX (rRange.getX() + 2).withWidth (68), juce::Justification::centredRight);
}

void TabBar::mouseDown (const juce::MouseEvent& e)
{
    if (sess == nullptr)
        return;
    for (int i = 0; i < midinja::kNumTabs; ++i)
    {
        if (dots[(size_t) i].expanded (3).contains (e.getPosition()))
        {
            if (onOn) onOn (i, ! sess->doc.tabs[(size_t) i].on);
            return;
        }
        if (boxes[(size_t) i].contains (e.getPosition()))
        {
            if (onSelect) onSelect (i);
            return;
        }
    }
    const auto P = palLayout();
    if (P.sep < 0)
        return;
    for (int k = 0; k < midinja::kNumParts; ++k)
        if (P.boxes[(size_t) k].contains (e.getPosition()))
        {
            const auto& d = sess->doc;
            if (k == d.cur && d.sel < 0)
                return;
            if (onSelectPart) onSelectPart (k);
            return;
        }
}

juce::String TabBar::getTooltip()
{
    if (sess == nullptr)
        return {};
    const auto& d = sess->doc;
    for (int i = 0; i < midinja::kNumTabs; ++i)
    {
        const auto& t = d.tabs[(size_t) i];
        if (dots[(size_t) i].expanded (3).contains (hover))
            return t.on ? "On: plays and exports. Click to turn off" : "Off: no play, no export. Click to turn on";
        if (boxes[(size_t) i].contains (hover))
            return juce::String (tabName (i)) + " on channel " + juce::String (t.channel) + (t.on ? ", on" : ", off") + u8 (" \xC2\xB7 click the name to show it, the dot to turn it on or off");
    }
    const auto P = palLayout();
    if (P.sep >= 0)
    {
        const auto& t = d.T().drums ? d.T() : d.riff();
        for (int k = 0; k < midinja::kNumParts; ++k)
            if (P.boxes[(size_t) k].contains (hover))
            {
                juce::StringArray list;
                list.addTokens (barsOf (d, k), " ", juce::String());
                return u8 (midinja::partName (k)) + u8 (" \xC2\xB7 ") + midinja::partHint (k)
                     + (t.locks[(size_t) k] ? u8 (" \xC2\xB7 locked: the dice and All skip it") : juce::String())
                     + u8 (" \xC2\xB7 ") + (list.isEmpty() ? u8 ("not in the form (\xE2\x96\xBE on a bar puts it there)") : "in bar" + juce::String (list.size() > 1 ? "s " : " ") + list.joinIntoString (", "))
                     + u8 (" \xC2\xB7 click to edit it in the roll");
            }
    }
    if (rRange.contains (hover))
        return d.T().drums ? "Nine GM drum notes, Kick 36 to Crash 49; the key and the octave do not apply"
             : d.T().poly ? "The rows of the Chords roll: + and - at the edges crop the view only, chords never move; cut notes show as ghosts"
                          : "The rows of the roll: grow or shrink it with + and - at the top and bottom edges; - and + by the key move it an octave";
    return "The tabs share the key, the form and the chord lane; each has its own channel (Riff and Chords an octave and a range, Riff and Beats their parts). Export = one .mid, a track per tab that is on";
}
