#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Colours and widget styling from web/index.html (the approved reference): dark panels, orange
// notes and dice, teal accents, yellow locks.
namespace theme
{
inline const juce::Colour bg      { 0xff15171c };
inline const juce::Colour panel   { 0xff1d2027 };
inline const juce::Colour panel2  { 0xff242832 };
inline const juce::Colour line    { 0xff2e3340 };
inline const juce::Colour lineHi  { 0xff4a5163 };
inline const juce::Colour ink     { 0xffe6e8ee };
inline const juce::Colour ink2    { 0xff9aa1b2 };
inline const juce::Colour ink3    { 0xff626a7c };
inline const juce::Colour accent  { 0xffff9f43 };
inline const juce::Colour teal    { 0xff5ad1c7 };
inline const juce::Colour lock    { 0xfff5d76e };
inline const juce::Colour ctx     { 0xff8a5a2c }; // web --ctx: the bar or part the primary belongs to
inline const juce::Colour state   { 0xff4a5163 }; // web --state: a lock's frame, never orange
inline const juce::Colour blue    { 0xff7aa2f7 };
inline const juce::Colour purple  { 0xffc792ea };
inline const juce::Colour off     { 0xff333845 };
inline const juce::Colour foot    { 0xff1a1d23 };
inline const juce::Colour outInk  { 0xff454b58 };
inline const juce::Colour outNote { 0xff8a90a0 };
inline const juce::Colour darkInk { 0xff1a1206 };

constexpr int keyWidth = 44; // the note-name column of the grid; the lanes line up with it

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

inline float textWidth (const juce::Font& f, const juce::String& s)
{
    return juce::GlyphArrangement::getStringWidth (f, s);
}

// Small caps label above a control: 10px, uppercase, letter-spaced.
inline void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                       juce::Justification just = juce::Justification::centredLeft)
{
    g.setColour (ink3);
    g.setFont (font (10.0f));
    g.drawFittedText (text.toUpperCase(), r, just, r.getHeight() > 16 ? 2 : 1, 0.8f);
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setDefaultSansSerifTypefaceName ("Segoe UI");
        setColourScheme ({ bg, panel, panel2, line, ink, accent, ink, panel2, ink });
        setColour (juce::ComboBox::backgroundColourId, panel2);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::ComboBox::arrowColourId, ink3);
        setColour (juce::PopupMenu::backgroundColourId, panel2);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, off);
        setColour (juce::PopupMenu::highlightedTextColourId, ink);
        setColour (juce::TextButton::buttonColourId, panel2);
        setColour (juce::TextButton::buttonOnColourId, teal.withAlpha (0.13f));
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, teal);
        setColour (juce::Label::textColourId, ink2);
        setColour (juce::TextEditor::backgroundColourId, panel2);
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::focusedOutlineColourId, teal);
        setColour (juce::TextEditor::highlightColourId, teal.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, ink);
        setColour (juce::TooltipWindow::backgroundColourId, panel2);
        setColour (juce::TooltipWindow::textColourId, ink);
        setColour (juce::TooltipWindow::outlineColourId, lineHi);
        setColour (juce::ScrollBar::thumbColourId, off);
    }

    // .btn: rounded 7px, panel2, 1px line; .on: teal tint and border
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool isHighlighted, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const bool ghost = b.getProperties()["ghost"];
        if (! ghost || on)
        {
            g.setColour (on ? teal.withAlpha (0.13f) : panel2);
            g.fillRoundedRectangle (r, 7.0f);
        }
        if (! ghost)
        {
            g.setColour (on ? teal : isHighlighted ? lineHi : line);
            g.drawRoundedRectangle (r, 7.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12.0f); }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour ((b.getToggleState() ? teal : ink).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.35f));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }

    // .sel: rounded 7px, panel2, 1px line, a small chevron at the right
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
        g.setColour (panel2);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (box.isMouseOver (true) ? lineHi : line);
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        juce::Path p;
        const float cx = (float) width - 12.0f, cy = (float) height * 0.5f;
        p.startNewSubPath (cx - 4.0f, cy - 2.0f);
        p.lineTo (cx, cy + 2.0f);
        p.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (ink3);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setColour (juce::Label::textColourId, ink);
    }

    juce::Font getPopupMenuFont() override { return font (12.0f); }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.setColour (panel2);
        g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 6.0f);
        g.setColour (line);
        g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 6.0f, 1.0f);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override
    {
        const auto f = font (12.0f);
        const int w = juce::jmin (420, (int) textWidth (f, tip) + 20);
        juce::TextLayout tl;
        juce::AttributedString s (tip);
        s.setFont (f);
        tl.createLayout (s, (float) (w - 16));
        const int h = (int) tl.getHeight() + 12;
        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
            .constrainedWithin (parentArea);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        g.setColour (panel2);
        g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 6.0f);
        g.setColour (lineHi);
        g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 6.0f, 1.0f);
        juce::AttributedString s (text);
        s.setFont (font (12.0f));
        s.setColour (ink);
        juce::TextLayout tl;
        tl.createLayout (s, (float) (width - 16));
        tl.draw (g, juce::Rectangle<float> (8.0f, 6.0f, (float) width - 16.0f, (float) height - 12.0f));
    }
};

inline juce::String noteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
}
} // namespace theme
