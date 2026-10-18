#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The small bar at an edge of the roll (web `.edge`): the edge row's note, a [+] that adds a row
// beyond the edge and a [−] that removes the edge row, and on the top bar the range as text.
class EdgeBar : public juce::Component
{
public:
    static constexpr int height = 18;

    juce::TextButton plus { "+" }, minus { juce::String (juce::CharPointer_UTF8 ("\xE2\x88\x92")) };
    juce::String label, right;

    EdgeBar()
    {
        addAndMakeVisible (plus);
        addAndMakeVisible (minus);
    }

    void resized() override
    {
        plus.setBounds (theme::keyWidth + 3, 1, 20, 16);
        minus.setBounds (plus.getRight() + 3, 1, 20, 16);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::ink3);
        g.setFont (theme::font (9.5f));
        g.drawText (label, 0, 0, theme::keyWidth - 5, height, juce::Justification::centredRight);
        g.drawText (right, minus.getRight() + 8, 0, getWidth() - minus.getRight() - 12, height, juce::Justification::centredRight);
    }
};
