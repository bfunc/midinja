#pragma once

#include "Document.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The piano roll of the part shown (web `.roll`): one column per step, one row per semitone of
// the tab's range (scale rows full height, the others half and dimmed). A chord tab shows its
// held chord on every hit.
//   click an empty cell = add a note (an off-scale one is locked at once)   click a note = remove it
//   drag a note = pitch, snapping to scale rows (Alt: every row)            Shift+click = lock
//   right-click a note = longer (wraps to 1)
class GridComponent : public juce::Component, public juce::TooltipClient
{
public:
    midinja::Session* sess = nullptr;
    std::function<void()> onEdit;    // an edit is finished: the editor records it and redraws
    std::function<void()> onPreview; // during a drag: play and redraw, no undo entry
    int playStep = -1;               // the playhead column, -1 when not on this part

    // Rows follow the document; call before asking for the height.
    void refresh();
    // The rows fit the budget: 11..30 px each (web: 300 px for the grid), half for out-of-scale rows.
    int layoutHeight (int budget);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); }
    juce::String getTooltip() override;

private:
    const midinja::Doc& doc() const { return sess->doc; }
    float colWidth() const;
    int colAt (float x) const;
    int rowAt (float y) const;
    int rowOf (int note) const;                 // row index of a note, or -1
    int nearestRow (float y, bool chromatic) const;
    // The step whose hit covers this cell (a long note spans columns), or -1.
    int noteAt (int col, int row) const;
    void layoutRows();
    void drawPadlock (juce::Graphics&, juce::Rectangle<float>, juce::Colour) const;

    std::vector<int> rows;        // MIDI notes, top row first
    std::vector<int> rowTop, rowHt;
    std::vector<int> chord;       // the chord of the part shown (chord tabs)
    int baseH = 30, lastBudget = 300;
    int dragStep = -1, dragNote0 = 60, pressY = 0;
    bool moved = false;
    juce::Point<int> hover;
};
