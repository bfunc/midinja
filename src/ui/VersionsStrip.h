#pragma once

#include "Document.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// The versions strip (web `.hist`): three thumbnails of whole arrangements with older / newer
// arrows. Click switches to a version, double-click stars it (starred ones are kept).
class VersionsStrip : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int thumbW = 40, thumbH = 26, arrowW = 20, gap = 4;
    static constexpr int width = arrowW + 3 * thumbW + 2 * gap + 2 * 4 + arrowW;

    midinja::Session* sess = nullptr;
    std::function<void (int)> onGoto, onStar;
    std::function<void()> onPrev, onNext;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); }
    juce::String getTooltip() override;

private:
    juce::Rectangle<int> thumbRect (int k) const { return { arrowW + 4 + k * (thumbW + gap), (getHeight() - thumbH) / 2, thumbW, thumbH }; }
    int thumbAt (juce::Point<int>) const; // 0..2 or -1
    int versionAt (int k) const;          // the version index a thumb shows, or -1
    juce::Point<int> hover;
};
