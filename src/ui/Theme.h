#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Colours from the approved mockup (dark, orange notes, teal accents, yellow locks).
namespace theme
{
inline const juce::Colour bg      { 0xff15171c };
inline const juce::Colour panel   { 0xff1d2027 };
inline const juce::Colour panel2  { 0xff242832 };
inline const juce::Colour line    { 0xff2e3340 };
inline const juce::Colour ink     { 0xffe6e8ee };
inline const juce::Colour ink2    { 0xff9aa1b2 };
inline const juce::Colour ink3    { 0xff626a7c };
inline const juce::Colour accent  { 0xffff9f43 };
inline const juce::Colour teal    { 0xff5ad1c7 };
inline const juce::Colour lock    { 0xfff5d76e };
inline const juce::Colour blue    { 0xff7aa2f7 };
inline const juce::Colour purple  { 0xffc792ea };
inline const juce::Colour off     { 0xff333845 };

constexpr int keyWidth = 40; // the note-name column of the grid; the lanes line up with it

class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColourScheme ({ bg, panel, panel2, line, ink, accent, ink, panel2, ink });
        setColour (juce::ComboBox::backgroundColourId, panel);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::ComboBox::arrowColourId, ink3);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, panel2);
        setColour (juce::TextButton::buttonColourId, panel2);
        setColour (juce::TextButton::buttonOnColourId, teal.withAlpha (0.25f));
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, teal);
        setColour (juce::Slider::rotarySliderFillColourId, accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, off);
        setColour (juce::Slider::thumbColourId, ink);
        setColour (juce::Slider::trackColourId, teal);
        setColour (juce::Slider::backgroundColourId, off);
        setColour (juce::Slider::textBoxTextColourId, ink2);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, ink2);
        setColour (juce::ToggleButton::textColourId, ink);
        setColour (juce::ToggleButton::tickColourId, teal);
        setColour (juce::ToggleButton::tickDisabledColourId, ink3);
    }
};

inline juce::String noteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
}
} // namespace theme
