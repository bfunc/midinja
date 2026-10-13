#include "GridComponent.h"
#include "Theme.h"

#include <algorithm>

void GridComponent::setState (const BrifferProcessor::State& s)
{
    state = s;
    rebuildRows();
    repaint();
}

void GridComponent::setPlayStep (int step)
{
    if (step != playStep)
    {
        playStep = step;
        repaint();
    }
}

void GridComponent::edit (const Edit& fn)
{
    fn (state);
    if (onEdit)
        onEdit (fn);
    rebuildRows();
    repaint();
}

void GridComponent::rebuildRows()
{
    const auto& g = state.gen;
    auto notes = briffer::scaleNotes (g.root, g.scale, std::min (g.lowNote, g.highNote), std::max (g.lowNote, g.highNote));
    for (int i = 0; i < state.pattern.numSteps; ++i)
        if (state.pattern.steps[(size_t) i].on)
            notes.push_back (state.pattern.steps[(size_t) i].note);
    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
    std::reverse (notes.begin(), notes.end());
    if (notes.empty())
        notes.push_back (60);
    rows = notes;
}

juce::Rectangle<float> GridComponent::gridArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft ((float) theme::keyWidth);
}

float GridComponent::colWidth() const { return gridArea().getWidth() / (float) juce::jmax (1, state.pattern.numSteps); }
float GridComponent::rowHeight() const { return gridArea().getHeight() / (float) juce::jmax<size_t> (1, rows.size()); }

int GridComponent::stepAt (float x) const
{
    const int s = (int) std::floor ((x - gridArea().getX()) / colWidth());
    return (s >= 0 && s < state.pattern.numSteps) ? s : -1;
}

int GridComponent::rowAt (float y) const
{
    return juce::jlimit (0, (int) rows.size() - 1, (int) std::floor (y / rowHeight()));
}

void GridComponent::paint (juce::Graphics& g)
{
    const auto area = gridArea();
    const float cw = colWidth(), rh = rowHeight();
    const int n = state.pattern.numSteps;

    g.fillAll (theme::bg);
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().toFloat().withWidth ((float) theme::keyWidth));

    // Rows: key names, zebra, root tint.
    for (size_t r = 0; r < rows.size(); ++r)
    {
        const float y = (float) r * rh;
        const bool isRoot = ((rows[r] - state.gen.root) % 12 + 12) % 12 == 0;
        if (isRoot)
            g.setColour (theme::accent.withAlpha (0.07f));
        else
            g.setColour (r % 2 ? juce::Colours::white.withAlpha (0.02f) : juce::Colours::transparentBlack);
        g.fillRect (area.getX(), y, area.getWidth(), rh);

        g.setColour (isRoot ? theme::accent : theme::ink3);
        g.setFont (juce::FontOptions (juce::jmin (11.0f, rh * 0.8f)).withStyle (isRoot ? "Bold" : "Regular"));
        g.drawText (theme::noteName (rows[r]), juce::Rectangle<float> (0.0f, y, (float) theme::keyWidth - 5.0f, rh),
                    juce::Justification::centredRight);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawHorizontalLine ((int) (y + rh), area.getX(), area.getRight());
    }

    // Playhead column.
    if (playStep >= 0 && playStep < n)
    {
        g.setColour (theme::teal.withAlpha (0.10f));
        g.fillRect (area.getX() + playStep * cw, 0.0f, cw, area.getHeight());
    }

    // Columns.
    for (int s = 0; s <= n; ++s)
    {
        g.setColour (s % 4 == 0 ? juce::Colours::white.withAlpha (0.11f) : juce::Colours::white.withAlpha (0.03f));
        g.drawVerticalLine ((int) (area.getX() + s * cw), 0.0f, area.getHeight());
    }

    // Notes.
    for (int s = 0; s < n; ++s)
    {
        const auto& st = state.pattern.steps[(size_t) s];
        if (! st.on)
            continue;
        const auto it = std::find (rows.begin(), rows.end(), st.note);
        if (it == rows.end())
            continue;
        const float y = (float) (it - rows.begin()) * rh;
        const auto rect = juce::Rectangle<float> (area.getX() + s * cw + 2.0f, y + 2.0f,
                                                  st.length * cw - 4.0f, rh - 4.0f);
        g.setColour (theme::accent.withAlpha (0.55f + 0.45f * st.velocity / 127.0f));
        g.fillRoundedRectangle (rect, 3.5f);

        if (st.repeats > 1) // ratchet dividers inside the note
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            for (int k = 1; k < st.repeats; ++k)
            {
                const float x = rect.getX() + rect.getWidth() * k / st.repeats;
                g.drawLine (x, rect.getY() + 2.0f, x, rect.getBottom() - 2.0f, 1.5f);
            }
        }
        if (st.locked)
        {
            g.setColour (theme::lock);
            g.drawRoundedRectangle (rect.reduced (0.5f), 3.5f, 2.0f);
            // Small padlock in the corner.
            const float sz = juce::jmin (8.0f, rect.getHeight() - 4.0f);
            const auto body = juce::Rectangle<float> (rect.getRight() - sz - 3.0f, rect.getCentreY() - sz * 0.15f, sz, sz * 0.6f);
            g.fillRect (body);
            juce::Path shackle;
            shackle.addCentredArc (body.getCentreX(), body.getY(), sz * 0.3f, sz * 0.35f, 0.0f,
                                   -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (shackle, juce::PathStrokeType (1.3f));
        }
    }

    g.setColour (theme::line);
    g.drawRect (getLocalBounds(), 1);
}

void GridComponent::mouseDown (const juce::MouseEvent& e)
{
    dragStep = -1;
    dragged = false;
    const int s = stepAt ((float) e.x);
    if (s < 0)
        return;
    const int note = rows[(size_t) rowAt ((float) e.y)];
    const auto st = state.pattern.steps[(size_t) s];

    if (e.mods.isPopupMenu())
    {
        if (st.on)
            edit ([s] (BrifferProcessor::State& x) {
                auto& p = x.pattern;
                int next = s + 1;
                while (next < p.numSteps && ! p.steps[(size_t) next].on)
                    ++next;
                const int room = juce::jmin (briffer::kMaxLength, next - s);
                auto& len = p.steps[(size_t) s].length;
                len = len + 1 > room ? 1 : len + 1;
            });
        return;
    }

    if (e.mods.isShiftDown())
    {
        if (st.on)
            edit ([s] (BrifferProcessor::State& x) { x.pattern.steps[(size_t) s].locked = ! x.pattern.steps[(size_t) s].locked; });
        return;
    }

    dragStep = s;
    if (st.on && st.note == note)
        return; // a click (no drag) removes it on mouseUp

    dragged = true;
    if (st.on)
    {
        edit ([s, note] (BrifferProcessor::State& x) { x.pattern.steps[(size_t) s].note = note; });
    }
    else
    {
        edit ([s, note] (BrifferProcessor::State& x) {
            auto& t = x.pattern.steps[(size_t) s];
            t = briffer::Step {};
            t.on = true;
            t.note = note;
            briffer::fitLengths (x.pattern); // an earlier long note now stops here
        });
    }
}

void GridComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0 || e.getDistanceFromDragStart() < 3)
        return;
    const int note = rows[(size_t) rowAt ((float) e.y)];
    dragged = true;
    const int s = dragStep;
    if (state.pattern.steps[(size_t) s].note != note)
        edit ([s, note] (BrifferProcessor::State& x) { x.pattern.steps[(size_t) s].note = note; });
}

void GridComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragStep >= 0 && ! dragged)
    {
        const int s = dragStep;
        edit ([s] (BrifferProcessor::State& x) {
            x.pattern.steps[(size_t) s].on = false;
            x.pattern.steps[(size_t) s].locked = false;
        });
    }
    dragStep = -1;
    dragged = false;
}
