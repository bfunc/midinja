#include "TabBar.h"
#include "Theme.h"

namespace
{
constexpr int kNumW = 14, kModeW = 76, kChW = 68, kGap = 6, kPad = 8;
constexpr int kItemW = kPad + kNumW + kGap + kModeW + kGap + kChW + kGap + 39 + kPad;
} // namespace

TabBar::TabBar()
{
    for (int i = 0; i < midinja::kNumTabs; ++i)
    {
        auto& it = items[(size_t) i];
        it.mode.addItem ("Riff", 1);
        it.mode.addItem ("Chords", 2);
        it.mode.setTooltip ("Riff: one note at a time. Chords: one held chord per bar, the dice vary its rhythm");
        it.mode.onChange = [this, i] { if (onPoly) onPoly (i, items[(size_t) i].mode.getSelectedId() == 2); };
        for (int c = 1; c <= 16; ++c)
            it.channel.addItem ("Ch " + juce::String (c), c);
        it.channel.setTooltip ("MIDI channel of this tab");
        it.channel.onChange = [this, i] { if (onChannel) onChannel (i, items[(size_t) i].channel.getSelectedId()); };
        it.sw.onClick = [this, i] { if (onOn) onOn (i, ! items[(size_t) i].sw.on); };
        addAndMakeVisible (it.mode);
        addAndMakeVisible (it.channel);
        addAndMakeVisible (it.sw);
    }
}

void TabBar::refresh()
{
    if (sess == nullptr)
        return;
    for (int i = 0; i < midinja::kNumTabs; ++i)
    {
        const auto& t = sess->doc.tabs[(size_t) i];
        auto& it = items[(size_t) i];
        it.mode.setSelectedId (t.poly ? 2 : 1, juce::dontSendNotification);
        it.channel.setSelectedId (t.channel, juce::dontSendNotification);
        it.sw.on = t.on;
        it.sw.setTooltip (t.on ? "On: plays and exports. Click to turn off" : "Off: no play, no export. Click to turn on");
        it.sw.repaint();
        const float alpha = t.on ? 1.0f : 0.55f;
        it.mode.setAlpha (alpha);
        it.channel.setAlpha (alpha);
    }
    repaint();
}

void TabBar::resized()
{
    int x = 14;
    const int y = (height - 24) / 2;
    for (auto& it : items)
    {
        it.box = { x, 3, kItemW, height - 6 };
        int cx = x + kPad + kNumW + kGap;
        it.mode.setBounds (cx, y, kModeW, 24);
        cx += kModeW + kGap;
        it.channel.setBounds (cx, y, kChW, 24);
        cx += kChW + kGap;
        it.sw.setBounds (cx, y + 2, 39, 20);
        x += kItemW + 8;
    }
}

void TabBar::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds());
    g.setColour (theme::line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
    if (sess == nullptr)
        return;
    for (int i = 0; i < midinja::kNumTabs; ++i)
    {
        const auto& it = items[(size_t) i];
        const auto& t = sess->doc.tabs[(size_t) i];
        const bool cur = i == sess->doc.tab;
        const auto r = it.box.toFloat().reduced (0.5f);
        g.setColour (theme::panel2);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (cur ? theme::accent : theme::line);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour ((cur ? theme::accent : theme::ink2).withAlpha (t.on ? 1.0f : 0.5f));
        g.setFont (theme::font (13.0f, true));
        g.drawText (juce::String (i + 1), it.box.getX() + kPad, it.box.getY(), kNumW, it.box.getHeight(), juce::Justification::centred);
    }
    const int hx = items.back().box.getRight() + 14;
    g.setColour (theme::ink3);
    g.setFont (theme::font (10.5f));
    g.drawFittedText (juce::String (juce::CharPointer_UTF8 ("tabs share the key and the arrangement; each has its own parts, octave, range and channel \xC2\xB7 export = one .mid, a track per tab that is on")),
                      hx, 0, getWidth() - hx - 14, height, juce::Justification::centredRight, 2, 1.0f);
}

void TabBar::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < midinja::kNumTabs; ++i)
        if (items[(size_t) i].box.contains (e.getPosition()) && onSelect)
            onSelect (i);
}
