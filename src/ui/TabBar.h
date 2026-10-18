#pragma once

#include "Document.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

// The tab bar under the key row (web `.tabbar`): Riff and Chords, each with a dot that turns it
// on or off; click the name to show it. On the right the shown tab's settings: its channel and
// its range.
class TabBar : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int height = 36;

    midinja::Session* sess = nullptr;
    std::function<void (int)> onSelect;
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
    std::array<juce::Rectangle<int>, midinja::kNumTabs> boxes, dots;
    juce::ComboBox channel;
    juce::Rectangle<int> rRange, rLabel;
    juce::Point<int> hover;
};
