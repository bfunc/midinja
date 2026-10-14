#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// A value lane under the grid (velocity, length or repeats); drag the bars to set values.
class LaneComponent : public juce::Component
{
public:
    enum class Kind { Velocity, Length, Repeats };
    using Edit = std::function<void (MidinjaProcessor::State&)>;

    explicit LaneComponent (Kind k) : kind (k) {}

    std::function<void (const Edit&)> onEdit;

    void setState (const MidinjaProcessor::State& s) { state = s; repaint(); }
    void setPlayStep (int step) { if (step != playStep) { playStep = step; repaint(); } }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override { setFrom (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setFrom (e); }

private:
    void setFrom (const juce::MouseEvent& e);
    bool enabled() const { return kind != Kind::Repeats || state.gen.repeatsOn; }
    int maxValue() const;
    int valueOf (const midinja::Step& st) const;

    Kind kind;
    MidinjaProcessor::State state;
    int playStep = -1;
};
