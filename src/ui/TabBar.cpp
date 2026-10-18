#include "TabBar.h"
#include "Theme.h"

namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
const char* tabName (int i) { return i == midinja::kChordTab ? "Chords" : "Riff"; }
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
    const int rx = getWidth() - 14;
    rRange = { rx - 70, 0, 70, height };
    channel.setBounds (rRange.getX() - 8 - 66 - 50, (height - 24) / 2, 66, 24);
    rLabel = { channel.getX() - 60, 0, 56, height };
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
    const auto& t = d.T();
    g.setColour (theme::ink3);
    g.setFont (theme::font (10.0f));
    g.drawText (juce::String (tabName (d.tab)).toUpperCase(), rLabel, juce::Justification::centredRight);
    g.drawText ("RANGE", rRange.withWidth (40), juce::Justification::centredLeft);
    g.setColour (theme::ink2);
    g.setFont (theme::font (11.0f));
    g.drawText (theme::noteName (t.lowNote) + u8 ("\xE2\x80\x93") + theme::noteName (t.highNote), rRange.withTrimmedLeft (0).withX (rRange.getX() + 2).withWidth (68), juce::Justification::centredRight);
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
}

juce::String TabBar::getTooltip()
{
    if (sess == nullptr)
        return {};
    for (int i = 0; i < midinja::kNumTabs; ++i)
    {
        const auto& t = sess->doc.tabs[(size_t) i];
        if (dots[(size_t) i].expanded (3).contains (hover))
            return t.on ? "On: plays and exports. Click to turn off" : "Off: no play, no export. Click to turn on";
        if (boxes[(size_t) i].contains (hover))
            return juce::String (tabName (i)) + " on channel " + juce::String (t.channel) + (t.on ? ", on" : ", off") + u8 (" \xC2\xB7 click the name to show it, the dot to turn it on or off");
    }
    if (rRange.contains (hover))
        return sess->doc.T().poly ? "The rows of the Chords roll: + and - at the edges crop the view only, chords never move; cut notes show as ghosts"
                                  : "The rows of the roll: grow or shrink it with + and - at the top and bottom edges; - and + by the key move it an octave";
    return "The two tabs share the key, the arrangement and the chord lane; each has its own octave, range and channel (the Riff tab its parts). Export = one .mid, a track per tab that is on";
}
