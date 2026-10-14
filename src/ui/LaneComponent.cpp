#include "LaneComponent.h"
#include "Theme.h"

int LaneComponent::maxValue() const
{
    switch (kind)
    {
        case Kind::Velocity: return 127;
        case Kind::Length:   return midinja::kMaxLength;
        case Kind::Repeats:  return midinja::kMaxRepeats;
    }
    return 127;
}

int LaneComponent::valueOf (const midinja::Step& st) const
{
    switch (kind)
    {
        case Kind::Velocity: return st.velocity;
        case Kind::Length:   return st.length;
        case Kind::Repeats:  return st.repeats;
    }
    return 0;
}

void LaneComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    const char* label = kind == Kind::Velocity ? "VEL" : (kind == Kind::Length ? "LEN" : "RPT");
    g.setColour (theme::ink3);
    g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
    g.drawText (label, juce::Rectangle<float> (0.0f, 0.0f, (float) theme::keyWidth, bounds.getHeight()),
                juce::Justification::centred);

    const int n = state.pattern.numSteps;
    const float x0 = (float) theme::keyWidth;
    const float cw = (bounds.getWidth() - x0) / (float) juce::jmax (1, n);
    const auto inner = bounds.reduced (0.0f, 4.0f);
    const float alpha = enabled() ? 1.0f : 0.35f;

    if (playStep >= 0 && playStep < n)
    {
        g.setColour (theme::teal.withAlpha (0.08f));
        g.fillRect (x0 + playStep * cw, bounds.getY(), cw, bounds.getHeight());
    }

    for (int s = 0; s < n; ++s)
    {
        const auto& st = state.pattern.steps[(size_t) s];
        const float x = x0 + s * cw + 3.0f;
        const float w = cw - 6.0f;
        if (kind == Kind::Repeats)
        {
            // Four segments, lit up to the step's repeat count.
            const float segH = (inner.getHeight() - 6.0f) / 4.0f;
            for (int k = 0; k < midinja::kMaxRepeats; ++k)
            {
                const bool lit = st.on && k < st.repeats;
                g.setColour ((lit ? theme::purple : theme::off).withMultipliedAlpha (alpha));
                const float y = inner.getBottom() - (k + 1) * segH - k * 2.0f;
                g.fillRoundedRectangle (x, y, w, segH, 1.5f);
            }
            continue;
        }
        if (! st.on)
        {
            g.setColour (theme::off.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (x, inner.getBottom() - 4.0f, w, 4.0f, 1.5f);
            continue;
        }
        const float frac = (float) valueOf (st) / (float) maxValue();
        const float h = juce::jmax (4.0f, inner.getHeight() * frac);
        g.setColour ((kind == Kind::Velocity ? theme::teal : theme::blue).withAlpha (0.85f * alpha));
        g.fillRoundedRectangle (x, inner.getBottom() - h, w, h, 2.0f);
    }

    if (! enabled())
    {
        g.setColour (theme::ink2);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("OFF  -  turn Repeats on (left panel) to add ratchets",
                    bounds.withTrimmedLeft (x0).reduced (10.0f, 0.0f), juce::Justification::centredRight);
    }
}

void LaneComponent::setFrom (const juce::MouseEvent& e)
{
    if (! enabled())
        return;
    const int n = state.pattern.numSteps;
    const float x0 = (float) theme::keyWidth;
    const float cw = ((float) getWidth() - x0) / (float) juce::jmax (1, n);
    const int s = (int) std::floor (((float) e.x - x0) / cw);
    if (s < 0 || s >= n || ! state.pattern.steps[(size_t) s].on)
        return;

    const float top = 4.0f, bottom = (float) getHeight() - 4.0f;
    const float frac = juce::jlimit (0.0f, 1.0f, (bottom - (float) e.y) / (bottom - top));
    const int maxV = maxValue();
    const int value = juce::jlimit (1, maxV, (int) std::ceil (frac * maxV));
    if (value == valueOf (state.pattern.steps[(size_t) s]))
        return;

    const Kind k = kind;
    Edit fn = [s, value, k] (MidinjaProcessor::State& x) {
        auto& st = x.pattern.steps[(size_t) s];
        if (k == Kind::Velocity) st.velocity = value;
        else if (k == Kind::Length) { st.length = value; midinja::fitLengths (x.pattern); }
        else st.repeats = value;
    };
    fn (state);
    if (onEdit)
        onEdit (fn);
    repaint();
}
