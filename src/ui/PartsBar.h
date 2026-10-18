#pragma once

#include "Document.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The row above the chord lane (web `.tabs`): the arrangement's bars on the left (lock, name with
// the bar's chord degree under it, thumbnail, remove) and a square + that repeats the last bar, a
// note about what is shown in the middle, room kept for the editor's Unlock button, and the
// palette of the four parts on the right (letters; click adds a bar).
class PartsBar : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int height = 30;

    midinja::Session* sess = nullptr;
    int playingSlot = -1;
    int reserveW = 0; // width kept free before the palette (the Unlock bar button)
    std::function<void (int)> onSelectSlot, onToggleLock, onRemoveBar, onAddBar;
    std::function<void()> onRepeatBar;

    juce::Rectangle<int> reservedArea() const { return layout().reserved; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = { -1, -1 }; repaint(); }
    juce::String getTooltip() override;

private:
    struct Hit { enum Kind { None, Slot, SlotLock, SlotX, Add, Tab, Info } kind = None; int index = -1; };
    struct Layout
    {
        std::vector<juce::Rectangle<int>> slots, slotLocks, slotXs, tabs;
        juce::Rectangle<int> add, info, reserved;
        bool full = false;
    };
    Layout layout() const;
    Hit hitAt (juce::Point<int>) const;
    int nameWidth (int k, int bar) const;
    void drawName (juce::Graphics&, int k, int bar, int x, juce::Colour ink) const;
    static void drawPadlock (juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool open);

    juce::Point<int> hover { -1, -1 };
};
