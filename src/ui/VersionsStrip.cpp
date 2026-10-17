#include "VersionsStrip.h"
#include "Theme.h"
#include "Widgets.h"

int VersionsStrip::thumbAt (juce::Point<int> pt) const
{
    for (int k = 0; k < 3; ++k)
        if (thumbRect (k).contains (pt))
            return k;
    return -1;
}

int VersionsStrip::versionAt (int k) const
{
    if (sess == nullptr)
        return -1;
    const int i = sess->histEnd - midinja::Session::kPerPage + k;
    return i >= 0 && i < (int) sess->versions.size() ? i : -1;
}

void VersionsStrip::paint (juce::Graphics& g)
{
    if (sess == nullptr)
        return;
    sess->clampHist();
    const bool canPrev = sess->histEnd > midinja::Session::kPerPage;
    const bool canNext = sess->histEnd < (int) sess->versions.size();
    const float cy = (float) getHeight() * 0.5f;
    juce::Path prev, next;
    prev.addTriangle (12.0f, cy - 4.5f, 12.0f, cy + 4.5f, 5.0f, cy);
    next.addTriangle ((float) getWidth() - 12.0f, cy - 4.5f, (float) getWidth() - 12.0f, cy + 4.5f, (float) getWidth() - 5.0f, cy);
    g.setColour (theme::ink2.withAlpha (canPrev ? 1.0f : 0.25f));
    g.fillPath (prev);
    g.setColour (theme::ink2.withAlpha (canNext ? 1.0f : 0.25f));
    g.fillPath (next);

    for (int k = 0; k < 3; ++k)
    {
        const auto r = thumbRect (k).toFloat().reduced (0.5f);
        const int i = versionAt (k);
        if (i < 0)
        {
            g.setColour (theme::panel.withAlpha (0.25f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (theme::line.withAlpha (0.25f));
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
            continue;
        }
        const auto& v = sess->versions[(size_t) i];
        const bool cur = i == sess->vi;
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (cur ? theme::accent : theme::line);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        std::vector<const midinja::Pattern*> ps;
        const auto& h = v.snap;
        if (h.arr.empty())
            ps.push_back (&h.parts[(size_t) h.cur]);
        else
            for (int part : h.arr)
                ps.push_back (&h.parts[(size_t) part]);
        ui::paintThumb (g, r, ps, cur ? theme::accent : theme::ink3);
        if (v.star)
        {
            juce::Path star;
            star.addStar (juce::Point<float> (r.getRight() - 5.0f, r.getY() + 5.0f), 5, 1.8f, 4.0f);
            g.setColour (theme::lock);
            g.fillPath (star);
        }
    }
}

void VersionsStrip::mouseDown (const juce::MouseEvent& e)
{
    if (sess == nullptr)
        return;
    if (e.x < arrowW)
    {
        if (sess->histEnd > midinja::Session::kPerPage && onPrev) onPrev();
        return;
    }
    if (e.x >= getWidth() - arrowW)
    {
        if (sess->histEnd < (int) sess->versions.size() && onNext) onNext();
        return;
    }
    const int k = thumbAt (e.getPosition());
    const int i = k >= 0 ? versionAt (k) : -1;
    if (i >= 0 && i != sess->vi && onGoto)
        onGoto (i);
}

void VersionsStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int k = thumbAt (e.getPosition());
    const int i = k >= 0 ? versionAt (k) : -1;
    if (i >= 0 && onStar)
        onStar (i);
}

juce::String VersionsStrip::getTooltip()
{
    if (sess == nullptr)
        return {};
    if (hover.x < arrowW) return "Older";
    if (hover.x >= getWidth() - arrowW) return "Newer";
    const int k = thumbAt (hover);
    const int i = k >= 0 ? versionAt (k) : -1;
    if (i < 0)
        return {};
    return "Version " + juce::String (i + 1) + " of " + juce::String ((int) sess->versions.size())
         + ": a roll or an import, with your edits to it. Click to switch to it, double-click to star (starred ones are kept)";
}
