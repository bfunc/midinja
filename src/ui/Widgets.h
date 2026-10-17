#pragma once

// The small controls of the web preview, drawn the same way: knobs, range sliders, segmented
// buttons, switches, pills, icon buttons, the dice, the play button, number boxes and thumbnails.

#include "Theme.h"
#include "core/Engine.h"

#include <functional>
#include <vector>

namespace ui
{

// Pattern thumbnails: one dot per note, parts side by side (web `thumb`).
void paintThumb (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<const midinja::Pattern*>& parts,
                 juce::Colour dot);

// A rotary knob with its name and value to the right. Drag up/down or scroll; Ctrl+click resets.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (const juce::String& name, double min, double max, double step, bool teal = false, int diameter = 32);

    std::function<double()> get;
    std::function<void (double)> set;
    std::function<juce::String (double)> format;
    std::function<void()> onChange; // after every change (drag, wheel, reset)
    double resetValue = 0.0;
    bool showText = true; // the footer's mini knobs show the ring only

    int preferredWidth() const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void apply (double v);
    juce::String name;
    double lo, hi, step;
    bool teal;
    int diameter;
    double dragStart = 0.0;
};

// Two handles on a track, with the two values written under it.
class RangeSlider : public juce::Component
{
public:
    RangeSlider (int min, int max);
    std::function<std::pair<int, int>()> get;
    std::function<void (int, int)> set;
    std::function<juce::String (int)> format;
    std::function<void()> onChange;
    bool dimmed = false; // ratchets off

    static constexpr int height = 34;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    float xOf (int v) const;
    int valueAt (float x) const;
    int lo, hi;
    int handle = -1;
};

// Segmented buttons (.seg.small). An item may carry a dim suffix that is clickable on its own
// (the Mutate percentage).
class Seg : public juce::Component, public juce::TooltipClient
{
public:
    struct Item { juce::String text, suffix, tip; };
    std::vector<Item> items;
    int on = 0;
    std::function<void (int)> onSelect;
    std::function<void (int)> onSuffixClick;
    juce::String tooltip; // for the whole control, when an item has none

    int preferredWidth() const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); }
    juce::String getTooltip() override;

private:
    int itemAt (int x, int* suffixStart = nullptr) const;
    juce::Point<int> hover;
};

// A toggle switch with a label (.tog).
class Switch : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Switch (const juce::String& label) : text (label) {}
    bool on = false;
    std::function<void()> onClick;
    int preferredWidth() const { return 30 + 7 + (int) theme::textWidth (theme::font (12.0f), text) + 2; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
private:
    juce::String text;
};

// ON / OFF pill (.pill).
class Pill : public juce::Component, public juce::SettableTooltipClient
{
public:
    bool on = false;
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
};

// Icon buttons drawn as paths (no emoji fonts needed).
class IconButton : public juce::Button
{
public:
    enum Icon { Prev, Next, Undo, Redo, Close, Import, Export, Drag, Dice };
    IconButton (Icon i, const juce::String& txt = {}, bool ghost = true);
    int preferredWidth() const;
    void paintButton (juce::Graphics&, bool, bool) override;
    // Set on the drag handle: called once when the mouse moves a little while pressed.
    std::function<void()> onDragStart;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent& e) override { dragStarted = false; Button::mouseUp (e); }
private:
    Icon icon;
    juce::String label;
    bool ghost;
    bool dragStarted = false;
};

// The orange dice (.dice): 38px, five dots.
class DiceButton : public juce::Button
{
public:
    DiceButton() : Button ("dice") {}
    void paintButton (juce::Graphics&, bool, bool) override;
};

// The round play / stop button (.playb).
class PlayButton : public juce::Button
{
public:
    PlayButton() : Button ("play") {}
    void paintButton (juce::Graphics&, bool, bool) override;
};

// A number in a box (.num): click to type, scroll to nudge.
class NumBox : public juce::Label
{
public:
    NumBox (double min, double max, double step);
    std::function<double()> get;
    std::function<void (double)> set;
    std::function<void()> onChange;
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::TextEditor* createEditorComponent() override;
private:
    double lo, hi, step;
};

} // namespace ui
