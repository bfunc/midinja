#pragma once

#include "Document.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

// The tab bar under the key row (web `.tabbar`): three tabs like Riffer, each with its number, a
// Riff / Chords select, a channel select and an on/off switch. Click a tab to show it.
class TabBar : public juce::Component
{
public:
    static constexpr int height = 34;

    midinja::Session* sess = nullptr;
    std::function<void (int)> onSelect;
    std::function<void (int, bool)> onPoly, onOn;
    std::function<void (int, int)> onChannel;

    TabBar();
    void refresh(); // the selects and switches follow the session
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Item
    {
        juce::ComboBox mode, channel;
        ui::Switch sw { "" };
        juce::Rectangle<int> box;
    };
    std::array<Item, midinja::kNumTabs> items;
};
