#pragma once

#include "Document.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// A value lane under the grid (web `.lane`): velocity, length or ratchets; drag to set values.
class LaneComponent : public juce::Component
{
public:
    enum class Kind { Velocity, Length, Repeats };
    explicit LaneComponent (Kind k) : kind (k) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }

    midinja::Session* sess = nullptr;
    std::function<void()> onEdit;
    std::function<void()> onPreview;

    int preferredHeight() const { return active() ? 40 : 24; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { changed = false; setFrom (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setFrom (e); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool active() const { return kind != Kind::Repeats || (sess != nullptr && sess->doc.gen.repeatsOn); }
    void setFrom (const juce::MouseEvent& e);
    int maxValue() const { return kind == Kind::Velocity ? 127 : kind == Kind::Length ? midinja::kMaxLength : midinja::kMaxRepeats; }

    Kind kind;
    bool changed = false;
};
