#pragma once

#include "Document.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// The chord lane (web `.lanebox`): the arrangement's beats as a grid, the chords as blocks. Drag a
// block to move it, drag its left or right edge to resize it (one-beat snap, blocks never overlap,
// a gap = no chord). On each block: lock, inversion cycle, another chord, remove. Double-click on
// the empty lane adds a chord there; bars come from the + of the bar row above (on Chords with a
// copy of the previous bar's chords), so the lane has no + of its own.
class ChordLane : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int height = 74;

    midinja::Session* sess = nullptr;
    int playingBeatX = -1; // beats * steps, -1 when stopped
    double playBeat = -1.0;
    std::function<void()> onChanged;  // a recorded change: the editor publishes and redraws
    std::function<void()> onPreview;  // during a drag
    std::function<void()> onSelect;   // the selection changed only

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = { -1, -1 }; repaint(); }
    juce::String getTooltip() override;

private:
    enum class Act { None, Lock, Inv, Regen, Del, EdgeL, EdgeR, Move, Empty };
    struct HitAt { Act act = Act::None; int chordId = -1; };
    juce::Rectangle<int> laneArea() const { return getLocalBounds().reduced (0, 1); }
    juce::Rectangle<int> blockRect (const midinja::Chord& c) const;
    HitAt hitAt (juce::Point<int>) const;
    float beatWidth() const;

    struct Drag { int id; Act mode; int x0; int start0, len0; bool started = false, changed = false; };
    std::unique_ptr<Drag> drag;
    juce::Point<int> hover { -1, -1 };
};
