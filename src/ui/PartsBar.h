#pragma once

#include "Document.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The row above the grid (web `.tabs`): the arrangement's bars on the left (lock, name with the
// chord degree under it, thumbnail, remove), a note about the part being edited in the middle,
// room for the chord selectors, and the palette of the four parts on the right (letters with
// their degree; click adds a bar).
class PartsBar : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int height = 30;

    midinja::Session* sess = nullptr;
    int playingSlot = -1;
    int chordSelW = 0; // width kept free for the chord selectors (chord tabs)
    std::function<void (int)> onSelectSlot, onToggleLock, onRemoveBar, onAddBar;

    juce::Rectangle<int> chordArea() const { return layout().chord; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = { -1, -1 }; repaint(); }
    juce::String getTooltip() override;

private:
    struct Hit { enum Kind { None, Slot, SlotLock, SlotX, Tab, Info } kind = None; int index = -1; };
    struct Layout
    {
        std::vector<juce::Rectangle<int>> slots, slotLocks, slotXs, tabs;
        juce::Rectangle<int> info, chord;
        bool full = false;
    };
    Layout layout() const;
    Hit hitAt (juce::Point<int>) const;
    int nameWidth (int k) const;
    void drawName (juce::Graphics&, int k, int x, juce::Colour ink) const;
    static void drawPadlock (juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool open);

    juce::Point<int> hover { -1, -1 };
};
