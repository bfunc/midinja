#include "LaneComponent.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>

void LaneComponent::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    const char* name = kind == Kind::Velocity ? "VEL" : kind == Kind::Length ? "LEN" : "RATCH";
    g.setColour (theme::ink3);
    g.setFont (theme::font (9.5f));
    g.drawText (name, 0, 0, theme::keyWidth, getHeight(), juce::Justification::centred);

    if (sess == nullptr)
        return;
    if (! active())
    {
        g.setColour (theme::ink3);
        g.setFont (theme::font (11.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("Ratchets off \xC2\xB7 turn on in the left panel")),
                    theme::keyWidth + 6, 0, getWidth() - theme::keyWidth - 6, getHeight(), juce::Justification::centredLeft);
        return;
    }

    const auto& p = sess->doc.shown();
    const int n = std::max (1, p.numSteps);
    const float cw = (float) (getWidth() - theme::keyWidth) / (float) n;
    const float top = 3.0f, bottom = (float) getHeight() - 3.0f, h = bottom - top;
    for (int s = 0; s < n; ++s)
    {
        const auto& st = p.steps[(size_t) s];
        const float x = (float) theme::keyWidth + (float) s * cw + 3.0f, w = std::max (1.0f, cw - 6.0f);
        if (kind == Kind::Repeats)
        {
            const int count = st.on ? st.repeats : 0;
            for (int i = 0; i < midinja::kMaxRepeats; ++i)
            {
                g.setColour (i < count ? theme::purple : theme::off);
                g.fillRoundedRectangle (x, bottom - 6.0f - (float) i * 8.0f, w, 6.0f, 1.0f);
            }
            continue;
        }
        const float v = st.on ? (float) (kind == Kind::Velocity ? st.velocity : st.length) / (float) maxValue() : 0.0f;
        const float bh = st.on ? std::max (0.10f, v) * h : 0.06f * h;
        g.setColour (st.on ? (kind == Kind::Velocity ? theme::teal : theme::blue).withAlpha (0.85f) : theme::off);
        juce::Path bar;
        bar.addRoundedRectangle (x, bottom - bh, w, bh, 3.0f, 3.0f, true, true, false, false);
        g.fillPath (bar);
    }
}

void LaneComponent::setFrom (const juce::MouseEvent& e)
{
    if (sess == nullptr || ! active())
        return;
    auto& p = sess->doc.shown();
    const int n = std::max (1, p.numSteps);
    const float cw = (float) (getWidth() - theme::keyWidth) / (float) n;
    const int s = (int) std::floor (((float) e.x - (float) theme::keyWidth) / cw);
    if (s < 0 || s >= n || ! p.steps[(size_t) s].on)
        return;
    const float frac = juce::jlimit (0.0f, 1.0f, 1.0f - (float) e.y / (float) getHeight());
    const float v = frac * (float) maxValue();
    auto& st = p.steps[(size_t) s];
    switch (kind)
    {
        case Kind::Velocity: st.velocity = std::max (1, (int) std::lround (v)); break;
        case Kind::Length:   st.length = std::max (1, (int) std::lround (v)); midinja::fitLengths (p); break;
        case Kind::Repeats:  st.repeats = juce::jlimit (1, midinja::kMaxRepeats, (int) std::ceil (v)); break;
    }
    changed = true;
    if (onPreview) onPreview();
}

void LaneComponent::mouseUp (const juce::MouseEvent&)
{
    if (changed && onEdit)
        onEdit();
    changed = false;
}
