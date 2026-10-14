#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The step grid: one column per step, one row per scale note in the pitch range.
//   click empty cell    = add a note           click a note        = remove it
//   drag a note         = change its pitch     shift+click a note  = lock / unlock
//   right-click a note  = longer (wraps to 1)
class GridComponent : public juce::Component
{
public:
    using Edit = std::function<void (MidinjaProcessor::State&)>;
    std::function<void (const Edit&)> onEdit;

    void setState (const MidinjaProcessor::State& s);
    void setPlayStep (int step);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    void edit (const Edit& fn);
    void rebuildRows();
    int stepAt (float x) const;
    int rowAt (float y) const;
    juce::Rectangle<float> gridArea() const;
    float colWidth() const;
    float rowHeight() const;

    MidinjaProcessor::State state;
    std::vector<int> rows; // MIDI notes, top row first
    int playStep = -1;
    int dragStep = -1;
    bool dragged = false;
};
