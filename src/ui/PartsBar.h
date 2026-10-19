#pragma once

#include "Document.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The row above the chord lane (web `.ruler`): the form's bars on the left and a square + that
// repeats the last bar (bar 1 with A when the form is empty; on Chords the new bar gets a copy of
// the previous bar's chords). On Riff and Beats a bar shows its number, lock, part (the ▾ picks
// another) and thumbnail; on Chords its number and its chords, with ⧉ copying them to the other
// bars of the same part (chords are per bar). A note about what is shown sits in the middle, and
// room is kept on the right for the editor's Unlock bar button.
class PartsBar : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int height = 30;

    midinja::Session* sess = nullptr;
    int playingSlot = -1;
    int reserveW = 0; // width kept free on the right (the Unlock bar button)
    std::function<void (int)> onSelectSlot, onToggleLock, onRemoveBar, onCopyBar;
    std::function<void (int, int)> onSetBarPart; // (bar, part)
    std::function<void()> onRepeatBar;

    juce::Rectangle<int> reservedArea() const { return layout().reserved; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = { -1, -1 }; repaint(); }
    juce::String getTooltip() override;

private:
    struct Hit { enum Kind { None, Slot, SlotLock, SlotName, SlotCopy, SlotX, Add, Info } kind = None; int index = -1; };
    struct Layout
    {
        std::vector<juce::Rectangle<int>> slots, slotLocks, slotNames, slotCopies, slotXs;
        juce::Rectangle<int> add, info, reserved;
        bool full = false;
    };
    Layout layout() const;
    Hit hitAt (juce::Point<int>) const;
    juce::String chordsText (int bar) const;
    bool canCopy (int bar) const;
    void showPartPicker (int bar);
    static void drawPadlock (juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool open);

    juce::Point<int> hover { -1, -1 };
};
