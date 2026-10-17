#include "Widgets.h"

#include <algorithm>
#include <cmath>

namespace ui
{

void paintThumb (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<const midinja::Pattern*>& parts,
                 juce::Colour dot)
{
    int total = 0, lo = 128, hi = -1;
    for (auto* p : parts)
    {
        total += p->numSteps;
        for (int s = 0; s < p->numSteps; ++s)
            if (p->steps[(size_t) s].on)
            {
                lo = std::min (lo, p->steps[(size_t) s].note);
                hi = std::max (hi, p->steps[(size_t) s].note);
            }
    }
    if (hi < 0 || total <= 0)
        return;
    const float span = (float) std::max (1, hi - lo);
    const float w = r.getWidth(), h = r.getHeight();
    g.setColour (dot);
    int off = 0;
    for (auto* p : parts)
    {
        for (int s = 0; s < p->numSteps; ++s)
        {
            const auto& st = p->steps[(size_t) s];
            if (! st.on)
                continue;
            const float x = r.getX() + 2.0f + (float) (off + s) * (w - 6.0f) / (float) total;
            const float dw = std::max (2.0f, (float) st.length * (w - 6.0f) / (float) total - 1.0f);
            const float y = r.getY() + 2.0f + (1.0f - (float) (st.note - lo) / span) * (h - 7.0f);
            g.fillRect (x, y, dw, 2.0f);
        }
        off += p->numSteps;
    }
}

// ---------------------------------------------------------------------------------------------

Knob::Knob (const juce::String& n, double min, double max, double st, bool t, int d)
    : name (n), lo (min), hi (max), step (st), teal (t), diameter (d)
{
    setName (n);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

int Knob::preferredWidth() const
{
    if (! showText)
        return diameter;
    const float a = theme::textWidth (theme::font (11.0f), name);
    const float b = get != nullptr && format != nullptr ? theme::textWidth (theme::font (12.0f, true), format (get())) : 40.0f;
    return diameter + 6 + (int) std::ceil (std::max (a, b)) + 2;
}

void Knob::paint (juce::Graphics& g)
{
    const double v = get != nullptr ? get() : lo;
    const float frac = (float) juce::jlimit (0.0, 1.0, (v - lo) / (hi - lo));
    const float d = (float) diameter;
    const auto ring = juce::Rectangle<float> (0.0f, ((float) getHeight() - d) * 0.5f, d, d);
    const float thick = d * 5.0f / 32.0f;
    const float radius = (d - thick) * 0.5f;
    const float a0 = -juce::MathConstants<float>::pi * 0.75f; // -135 deg
    const float sweep = juce::MathConstants<float>::pi * 1.5f;  // 270 deg

    juce::Path back;
    back.addCentredArc (ring.getCentreX(), ring.getCentreY(), radius, radius, 0.0f, a0, a0 + sweep, true);
    g.setColour (theme::off);
    g.strokePath (back, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    if (frac > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (ring.getCentreX(), ring.getCentreY(), radius, radius, 0.0f, a0, a0 + sweep * frac, true);
        g.setColour (teal ? theme::teal : theme::accent);
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }
    g.setColour (theme::panel2);
    g.fillEllipse (ring.reduced (thick));

    if (showText)
    {
        const int x = diameter + 6;
        g.setColour (theme::ink2);
        g.setFont (theme::font (11.0f));
        g.drawText (name, x, getHeight() / 2 - 14, getWidth() - x, 14, juce::Justification::centredLeft);
        g.setColour (theme::ink);
        g.setFont (theme::font (12.0f, true));
        g.drawText (format != nullptr ? format (v) : juce::String (v), x, getHeight() / 2, getWidth() - x, 14,
                    juce::Justification::centredLeft);
    }
}

void Knob::apply (double v)
{
    v = std::round (v / step) * step;
    v = juce::jlimit (lo, hi, v);
    if (set != nullptr)
        set (v);
    repaint();
    if (onChange)
        onChange();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        apply (resetValue);
        return;
    }
    dragStart = get != nullptr ? get() : lo;
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        return;
    apply (dragStart + (double) -e.getDistanceFromDragStartY() / 150.0 * (hi - lo));
}

void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-6f)
        return;
    apply ((get != nullptr ? get() : lo) + (w.deltaY > 0 ? step : -step));
}

// ---------------------------------------------------------------------------------------------

RangeSlider::RangeSlider (int min, int max) : lo (min), hi (max)
{
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

float RangeSlider::xOf (int v) const
{
    const float w = (float) getWidth() - 12.0f;
    return 6.0f + (float) (v - lo) / (float) (hi - lo) * w;
}

int RangeSlider::valueAt (float x) const
{
    const float w = (float) getWidth() - 12.0f;
    return (int) std::lround ((double) lo + (double) (x - 6.0f) / (double) w * (double) (hi - lo));
}

void RangeSlider::paint (juce::Graphics& g)
{
    if (get == nullptr)
        return;
    const auto [a, b] = get();
    const float alpha = dimmed ? 0.35f : 1.0f;
    const float ty = 9.0f;
    g.setColour (theme::off.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (6.0f, ty, (float) getWidth() - 12.0f, 6.0f, 3.0f);
    g.setColour (theme::teal.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (xOf (a), ty, std::max (0.0f, xOf (b) - xOf (a)), 6.0f, 3.0f);
    g.setColour (theme::ink.withMultipliedAlpha (alpha));
    for (int v : { a, b })
        g.fillRoundedRectangle (xOf (v) - 6.0f, ty - 4.0f, 12.0f, 14.0f, 4.0f);
    g.setColour (theme::ink2.withMultipliedAlpha (alpha));
    g.setFont (theme::font (10.5f));
    const auto labels = juce::Rectangle<int> (0, 20, getWidth(), 14);
    g.drawText (format != nullptr ? format (a) : juce::String (a), labels, juce::Justification::centredLeft);
    g.drawText (format != nullptr ? format (b) : juce::String (b), labels, juce::Justification::centredRight);
}

void RangeSlider::mouseDown (const juce::MouseEvent& e)
{
    handle = -1;
    if (dimmed || get == nullptr)
        return;
    const auto [a, b] = get();
    const float x = (float) e.x;
    handle = std::abs (x - xOf (a)) <= std::abs (x - xOf (b)) ? 0 : 1;
    if (a == b && x > xOf (a))
        handle = 1;
    mouseDrag (e);
}

void RangeSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (handle < 0 || get == nullptr || set == nullptr)
        return;
    auto [a, b] = get();
    int v = valueAt ((float) e.x);
    if (handle == 0)
        a = std::min (std::max (lo, v), b);
    else
        b = std::max (std::min (hi, v), a);
    set (a, b);
    repaint();
    if (onChange)
        onChange();
}

// ---------------------------------------------------------------------------------------------

int Seg::preferredWidth() const
{
    int w = 2;
    for (const auto& it : items)
    {
        w += 9 + (int) std::ceil (theme::textWidth (theme::font (12.0f, true), it.text)) + 9;
        if (it.suffix.isNotEmpty())
            w += 4 + (int) std::ceil (theme::textWidth (theme::font (12.0f), it.suffix));
    }
    return w;
}

int Seg::itemAt (int x, int* suffixStart) const
{
    int px = 1;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& it = items[(size_t) i];
        const int tw = (int) std::ceil (theme::textWidth (theme::font (12.0f, true), it.text));
        const int sw = it.suffix.isNotEmpty() ? 4 + (int) std::ceil (theme::textWidth (theme::font (12.0f), it.suffix)) : 0;
        const int w = 9 + tw + sw + 9;
        if (x >= px && x < px + w)
        {
            if (suffixStart != nullptr)
                *suffixStart = sw > 0 ? px + 9 + tw + 4 : -1;
            return i;
        }
        px += w;
    }
    return -1;
}

void Seg::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 8.0f);
    int px = 1;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& it = items[(size_t) i];
        const bool isOn = i == on;
        const int tw = (int) std::ceil (theme::textWidth (theme::font (12.0f, true), it.text));
        const int sw = it.suffix.isNotEmpty() ? 4 + (int) std::ceil (theme::textWidth (theme::font (12.0f), it.suffix)) : 0;
        const int w = 9 + tw + sw + 9;
        if (isOn)
        {
            g.setColour (theme::accent.withAlpha (0.13f));
            g.fillRect (px, 1, w, getHeight() - 2);
        }
        g.setColour (isOn ? theme::accent : theme::ink2);
        g.setFont (theme::font (12.0f, isOn));
        g.drawText (it.text, px + 9, 0, tw + 2, getHeight(), juce::Justification::centredLeft);
        if (sw > 0)
        {
            g.setColour (theme::ink3);
            g.setFont (theme::font (12.0f));
            g.drawText (it.suffix, px + 9 + tw + 4, 0, sw, getHeight(), juce::Justification::centredLeft);
        }
        px += w;
    }
    g.setColour (theme::line);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

void Seg::mouseDown (const juce::MouseEvent& e)
{
    int sx = -1;
    const int i = itemAt (e.x, &sx);
    if (i < 0)
        return;
    if (sx >= 0 && e.x >= sx && onSuffixClick)
        onSuffixClick (i);
    on = i;
    repaint();
    if (onSelect)
        onSelect (i);
}

juce::String Seg::getTooltip()
{
    const int i = itemAt (hover.x);
    if (i >= 0 && items[(size_t) i].tip.isNotEmpty())
        return items[(size_t) i].tip;
    return tooltip;
}

// ---------------------------------------------------------------------------------------------

void Switch::paint (juce::Graphics& g)
{
    const float y = ((float) getHeight() - 16.0f) * 0.5f;
    g.setColour (on ? theme::teal.withAlpha (0.33f) : theme::off);
    g.fillRoundedRectangle (0.0f, y, 30.0f, 16.0f, 8.0f);
    g.setColour (on ? theme::teal : theme::ink2);
    g.fillEllipse (on ? 16.0f : 2.0f, y + 2.0f, 12.0f, 12.0f);
    g.setColour (on ? theme::teal : theme::ink2);
    g.setFont (theme::font (12.0f));
    g.drawText (text, 37, 0, getWidth() - 37, getHeight(), juce::Justification::centredLeft);
}

void Pill::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff2a2e38));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? theme::teal : theme::line);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setColour (on ? theme::teal : theme::ink3);
    g.setFont (theme::font (9.5f));
    g.drawText (on ? "ON" : "OFF", getLocalBounds(), juce::Justification::centred);
}

// ---------------------------------------------------------------------------------------------

IconButton::IconButton (Icon i, const juce::String& txt, bool gh) : Button (txt), icon (i), label (txt), ghost (gh) {}

int IconButton::preferredWidth() const
{
    if (label.isEmpty())
        return ghost ? 22 : 30;
    return 10 + 14 + 6 + (int) std::ceil (theme::textWidth (theme::font (12.0f), label)) + 10;
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const float alpha = isEnabled() ? 1.0f : 0.25f;
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    if (! ghost)
    {
        g.setColour (theme::panel2);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (over ? theme::lineHi : theme::line);
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }
    else if (over && isEnabled())
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (r, 4.0f);
    }
    juce::ignoreUnused (down);

    const float cx = label.isEmpty() ? (float) getWidth() * 0.5f : 17.0f;
    const float cy = (float) getHeight() * 0.5f;
    g.setColour ((ghost ? theme::ink2 : theme::ink).withMultipliedAlpha (alpha));
    juce::Path p;
    switch (icon)
    {
        case Prev:
            p.addTriangle (cx + 3.0f, cy - 4.5f, cx + 3.0f, cy + 4.5f, cx - 4.0f, cy);
            g.fillPath (p);
            break;
        case Next:
            p.addTriangle (cx - 3.0f, cy - 4.5f, cx - 3.0f, cy + 4.5f, cx + 4.0f, cy);
            g.fillPath (p);
            break;
        case Undo:
        case Redo:
        {
            const float dir = icon == Undo ? 1.0f : -1.0f;
            p.addCentredArc (cx, cy + 1.0f, 5.5f, 5.5f, 0.0f, juce::MathConstants<float>::pi * (icon == Undo ? -0.1f : 0.1f),
                             juce::MathConstants<float>::pi * (icon == Undo ? 1.35f : -1.35f), true);
            g.strokePath (p, juce::PathStrokeType (1.6f));
            juce::Path head;
            const float hx = cx - dir * 5.0f, hy = cy - 3.0f;
            head.addTriangle (hx - dir * 3.0f, hy - 3.0f, hx - dir * 3.0f, hy + 3.5f, hx + dir * 2.5f, hy + 0.5f);
            g.fillPath (head);
            break;
        }
        case Close:
            g.drawLine (cx - 3.5f, cy - 3.5f, cx + 3.5f, cy + 3.5f, 1.4f);
            g.drawLine (cx - 3.5f, cy + 3.5f, cx + 3.5f, cy - 3.5f, 1.4f);
            break;
        case Import:
        case Export:
        {
            const float d = icon == Import ? -1.0f : 1.0f;
            g.drawLine (cx, cy - 5.0f, cx, cy + 3.0f, 1.5f);
            p.addTriangle (cx - 4.0f, cy + d * -1.0f, cx + 4.0f, cy + d * -1.0f, cx, cy + d * 3.5f);
            g.fillPath (p);
            g.drawLine (cx - 6.0f, cy + 6.0f, cx + 6.0f, cy + 6.0f, 1.5f);
            break;
        }
        case Drag:
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 2; ++col)
                    g.fillEllipse (cx - 4.0f + (float) col * 5.0f, cy - 6.0f + (float) row * 5.0f, 2.6f, 2.6f);
            break;
        case Dice:
        {
            const auto d = juce::Rectangle<float> (cx - 6.5f, cy - 6.5f, 13.0f, 13.0f);
            g.drawRoundedRectangle (d, 3.0f, 1.3f);
            for (auto [fx, fy] : { std::pair { 0.27f, 0.27f }, std::pair { 0.73f, 0.27f }, std::pair { 0.5f, 0.5f },
                                   std::pair { 0.27f, 0.73f }, std::pair { 0.73f, 0.73f } })
                g.fillEllipse (d.getX() + fx * d.getWidth() - 1.2f, d.getY() + fy * d.getHeight() - 1.2f, 2.4f, 2.4f);
            break;
        }
    }
    if (label.isNotEmpty())
    {
        g.setFont (theme::font (12.0f));
        g.drawText (label, 30, 0, getWidth() - 36, getHeight(), juce::Justification::centredLeft);
    }
}

void IconButton::mouseDrag (const juce::MouseEvent& e)
{
    if (onDragStart && ! dragStarted && e.getDistanceFromDragStart() > 6)
    {
        dragStarted = true;
        onDragStart();
        return;
    }
    Button::mouseDrag (e);
}

void DiceButton::paintButton (juce::Graphics& g, bool, bool down)
{
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    if (down)
        r.translate (0.0f, 1.0f);
    if (isEnabled())
    {
        g.setColour (theme::accent.withAlpha (0.2f));
        g.fillRoundedRectangle (r.expanded (3.0f), 11.0f);
    }
    g.setColour (theme::accent.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (theme::darkInk.withMultipliedAlpha (alpha));
    const auto d = r.reduced (r.getWidth() * 0.2f);
    g.drawRoundedRectangle (d, 4.0f, 2.0f);
    const float dot = d.getWidth() * 0.18f;
    for (auto [fx, fy] : { std::pair { 0.26f, 0.26f }, std::pair { 0.74f, 0.26f }, std::pair { 0.5f, 0.5f },
                           std::pair { 0.26f, 0.74f }, std::pair { 0.74f, 0.74f } })
        g.fillEllipse (d.getX() + fx * d.getWidth() - dot * 0.5f, d.getY() + fy * d.getHeight() - dot * 0.5f, dot, dot);
}

void PlayButton::paintButton (juce::Graphics& g, bool, bool)
{
    const auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    g.setColour (on ? theme::accent : theme::teal);
    g.fillEllipse (r);
    g.setColour (theme::bg);
    const float cx = r.getCentreX(), cy = r.getCentreY();
    if (on)
        g.fillRect (cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
    else
    {
        juce::Path p;
        p.addTriangle (cx - 4.5f, cy - 6.5f, cx - 4.5f, cy + 6.5f, cx + 7.0f, cy);
        g.fillPath (p);
    }
}

// ---------------------------------------------------------------------------------------------

NumBox::NumBox (double min, double max, double st) : lo (min), hi (max), step (st)
{
    setEditable (true, false, false);
    setJustificationType (juce::Justification::centred);
    setFont (theme::font (12.0f));
    setColour (juce::Label::textColourId, theme::ink);
    setColour (juce::Label::textWhenEditingColourId, theme::ink);
    setColour (juce::Label::backgroundWhenEditingColourId, theme::panel2);
    setColour (juce::Label::outlineWhenEditingColourId, theme::teal);
    onTextChange = [this] {
        if (set == nullptr)
            return;
        const double v = getText().getDoubleValue();
        if (getText().trim().isNotEmpty())
            set (juce::jlimit (lo, hi, std::round (v / step) * step));
        refresh();
        if (onChange)
            onChange();
    };
}

void NumBox::refresh()
{
    if (get != nullptr)
    {
        const double v = get();
        setText (step >= 1.0 ? juce::String ((juce::int64) std::llround (v)) : juce::String (v, 2), juce::dontSendNotification);
    }
}

void NumBox::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel2);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    if (! isBeingEdited())
    {
        g.setColour (isEnabled() ? theme::ink : theme::ink3);
        g.setFont (getFont());
        g.drawText (getText(), getLocalBounds(), getJustificationType());
    }
}

void NumBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (get == nullptr || set == nullptr || std::abs (w.deltaY) < 1.0e-6f || ! isEnabled())
        return;
    set (juce::jlimit (lo, hi, get() + (w.deltaY > 0 ? step : -step)));
    refresh();
    if (onChange)
        onChange();
}

juce::TextEditor* NumBox::createEditorComponent()
{
    auto* ed = Label::createEditorComponent();
    ed->setJustification (juce::Justification::centred);
    ed->setIndents (4, 0);
    ed->setFont (getFont());
    return ed;
}

} // namespace ui
