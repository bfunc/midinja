#pragma once

#include "Document.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

// The tab bar under the key row (web `.tabbar`): Riff, Chords and Beats, each with a dot that
// turns it on or off; click the name to show it. On Riff and Beats the part selector A A′ B C
// follows (the part the roll edits, with the bars it plays in); chords are per bar, so Chords has
// none. On the right the shown tab's settings: its channel and its range.
class TabBar : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int height = 36;

    midinja::Session* sess = nullptr;
    std::function<void (int)> onSelect, onSelectPart;
    std::function<void (int, bool)> onOn;
    std::function<void (int, int)> onChannel;

    TabBar();
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); }
    juce::String getTooltip() override;

private:
    struct Pal { std::array<juce::Rectangle<int>, midinja::kNumParts> boxes; int sep = -1; }; // sep < 0: no selector on this tab
    Pal palLayout() const;

    std::array<juce::Rectangle<int>, midinja::kNumTabs> boxes, dots;
    int tabsRight = 0;
    juce::ComboBox channel;
    juce::Rectangle<int> rRange, rLabel;
    juce::Point<int> hover;
};
