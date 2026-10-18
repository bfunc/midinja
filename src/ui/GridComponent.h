#pragma once

#include "Document.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The piano roll of the bar shown (web `.roll`): one column per step, one row per semitone of the
// tab's range, all the same height; out-of-scale rows are dimmed. The Riff tab edits a part:
//   click an empty cell = add a note (an off-scale one is locked at once)   click a note = remove it
//   drag a note = pitch, snapping to scale rows (Alt: every row)            Shift+click = lock
//   right-click a note = longer (wraps to 1)
// The Chords tab shows the lane's hits in the bar with their voicing; a click selects the chord.
// Either tab shows the other's notes faintly underneath (the whole layer moved by octaves to fit).
class GridComponent : public juce::Component, public juce::TooltipClient
{
public:
    midinja::Session* sess = nullptr;
    std::function<void()> onEdit;
    std::function<void()> onPreview;
    std::function<void()> onSelectChord; // a chord hit was clicked (the selection changed)
    int playStep = -1;

    // A chord note the range cuts off: shown as a named ghost above or below the roll.
    struct Cut { int step, len; juce::String text; bool selected; bool above; };
    std::vector<Cut> cuts;

    void refresh();
    int layoutHeight (int budget);
    float columnX (int step) const { return 1.0f + (float) theme_keyWidth() + (float) step * colWidth(); }
    float colWidth() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); }
    juce::String getTooltip() override;

private:
    struct PolyHit { int step, len, chordIndex; std::vector<int> notes; int rootPc; };
    struct GhostHit { int step, len; std::vector<int> notes; };
    const midinja::Doc& doc() const { return sess->doc; }
    static int theme_keyWidth();
    int colAt (float x) const;
    int rowAt (float y) const;
    int rowOf (int note) const;
    int nearestRow (float y, bool chromatic) const;
    int noteAt (int col, int row) const;
    int polyHitAt (int col, int row) const; // index into hits, or -1
    void drawPadlock (juce::Graphics&, juce::Rectangle<float>, juce::Colour) const;

    std::vector<int> rows;
    std::vector<PolyHit> hits;
    std::vector<GhostHit> ghosts;
    int rowH = 30;
    int dragStep = -1, dragNote0 = 60, pressY = 0;
    bool moved = false;
    juce::Point<int> hover;
};
