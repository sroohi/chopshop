#pragma once

#include "Style.h"
#include "../PluginProcessor.h"

/** Sample display with slice markers. Drag a marker to move it, double-click to add one,
    right-click a marker to delete it, click a slice to audition its pad. */
class WaveformView : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     public juce::DragAndDropTarget
{
public:
    explicit WaveformView (ChopShopProcessor& p) : proc (p) {}

    std::function<void (int)> onSelectPad;

    void setKit (Kit::Ptr k)
    {
        kit = std::move (k);
        slices = kit != nullptr ? kit->slices : std::vector<int>();
        rebuildPeaks();
        repaint();
    }

    void refreshPlayhead()
    {
        const int ph = proc.playheadPos.load();
        if (ph != lastPlayhead || ph >= 0)
        {
            lastPlayhead = ph;
            repaint();
        }
    }

    void resized() override { rebuildPeaks(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (ui::col::screen);
        g.fillRoundedRectangle (b, 6.0f);

        if (dropHover)
        {
            g.setColour (ui::col::amber.withAlpha (0.12f));
            g.fillRoundedRectangle (b, 6.0f);
        }

        if (kit == nullptr || kit->main == nullptr || length() == 0)
        {
            g.setColour (ui::col::dim);
            g.setFont (ui::font (14.0f));
            g.drawText ("Drop a sample here, drag one from the Library, or click LOAD", b, juce::Justification::centred);
            return;
        }

        const auto wave = waveArea();
        const int selPad = proc.getSelectedPad();
        const int selSlice = kit->padSample[(size_t) selPad] == nullptr ? kit->padSlice[(size_t) selPad] : -1;

        // Slice regions
        for (int s = 0; s < (int) slices.size(); ++s)
        {
            const float x0 = sampleToX (slices[(size_t) s]);
            const float x1 = sampleToX (s + 1 < (int) slices.size() ? slices[(size_t) s + 1] : length());
            g.setColour (ui::sliceColour (s).withAlpha (s == selSlice ? 0.22f : 0.07f));
            g.fillRect (juce::Rectangle<float> (x0, wave.getY(), x1 - x0, wave.getHeight()));
        }

        // Waveform
        const float mid = wave.getCentreY();
        const float halfH = wave.getHeight() * 0.48f;
        for (int x = 0; x < (int) peaks.size(); ++x)
        {
            const float px = wave.getX() + (float) x;
            const int s = sliceAtSample (xToSample (px));
            g.setColour (ui::sliceColour (juce::jmax (0, s)).withAlpha (0.9f));
            const auto [lo, hi] = peaks[(size_t) x];
            g.drawVerticalLine ((int) px, mid - hi * halfH, mid - lo * halfH + 1.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) mid, wave.getX(), wave.getRight());

        // Markers
        for (int s = 0; s < (int) slices.size(); ++s)
        {
            const float x = sampleToX (slices[(size_t) s]);
            const auto c = ui::sliceColour (s);
            g.setColour (c.withAlpha (s == dragIndex ? 1.0f : 0.8f));
            g.drawLine (x, 14.0f, x, b.getBottom() - 2.0f, s == dragIndex ? 2.0f : 1.2f);

            auto tag = juce::Rectangle<float> (x, 1.0f, 20.0f, 13.0f);
            g.fillRoundedRectangle (tag, 3.0f);
            g.setColour (juce::Colours::black);
            g.setFont (ui::font (9.5f, true));
            g.drawText (juce::String (s + 1), tag, juce::Justification::centred);
        }

        // Playhead
        const int ph = proc.playheadPos.load();
        if (ph >= 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawVerticalLine ((int) sampleToX (ph), wave.getY(), wave.getBottom());
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (markerAt (e.position.x) > 0 ? juce::MouseCursor::LeftRightResizeCursor
                                                    : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (length() == 0)
            return;
        const int m = markerAt (e.position.x);

        if (e.mods.isPopupMenu())
        {
            if (m > 0)
            {
                slices.erase (slices.begin() + m);
                proc.setSlices (slices);
            }
            return;
        }

        if (m > 0)
        {
            dragIndex = m;
            return;
        }

        const int s = sliceAtSample (xToSample (e.position.x));
        const int pad = kit->padForSlice (s);
        if (pad >= 0)
        {
            proc.setSelectedPad (pad);
            if (onSelectPad) onSelectPad (pad);
            proc.padNoteOn (pad, 0.85f);
            auditionPad = pad;
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIndex <= 0)
            return;
        const int lo = slices[(size_t) dragIndex - 1] + 64;
        const int hi = (dragIndex + 1 < (int) slices.size() ? slices[(size_t) dragIndex + 1] : length()) - 64;
        if (hi > lo)
            slices[(size_t) dragIndex] = juce::jlimit (lo, hi, xToSample (e.position.x));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragIndex > 0)
            proc.setSlices (slices);
        dragIndex = -1;
        if (auditionPad >= 0)
            proc.padNoteOff (auditionPad);
        auditionPad = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (length() == 0 || markerAt (e.position.x) >= 0 || (int) slices.size() >= Kit::numPads)
            return;
        slices.push_back (xToSample (e.position.x));
        proc.setSlices (slices);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override { return proc.isSupportedAudioFile (files[0]); }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dropHover = false;
        proc.loadMainSample (juce::File (files[0]));
    }

    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.isString() && proc.isSupportedAudioFile (d.description.toString());
    }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHover = false;
        proc.loadMainSample (juce::File (d.description.toString()));
    }

private:
    int length() const { return kit != nullptr && kit->main != nullptr ? kit->main->length() : 0; }

    juce::Rectangle<float> waveArea() const { return getLocalBounds().toFloat().withTrimmedTop (16.0f).reduced (1.0f, 4.0f); }

    float sampleToX (int s) const
    {
        const auto w = waveArea();
        return w.getX() + w.getWidth() * (float) s / (float) juce::jmax (1, length());
    }

    int xToSample (float x) const
    {
        const auto w = waveArea();
        return juce::jlimit (0, juce::jmax (0, length() - 1), (int) ((x - w.getX()) / w.getWidth() * (float) length()));
    }

    int sliceAtSample (int pos) const
    {
        int s = -1;
        for (int i = 0; i < (int) slices.size(); ++i)
            if (slices[(size_t) i] <= pos) s = i;
        return s;
    }

    int markerAt (float x) const
    {
        for (int i = 0; i < (int) slices.size(); ++i)
            if (std::abs (sampleToX (slices[(size_t) i]) - x) < 5.0f)
                return i;
        return -1;
    }

    void rebuildPeaks()
    {
        peaks.clear();
        const int len = length();
        const int w = (int) waveArea().getWidth();
        if (len == 0 || w <= 0)
            return;
        const auto& buf = kit->main->buffer;
        const float* l = buf.getReadPointer (0);
        const float* r = buf.getReadPointer (1);
        float peak = 0.0f;
        for (int x = 0; x < w; ++x)
        {
            const int a = (int) ((juce::int64) len * x / w);
            const int e = juce::jmax (a + 1, (int) ((juce::int64) len * (x + 1) / w));
            float lo = 1.0f, hi = -1.0f;
            for (int i = a; i < e; ++i)
            {
                const float v = (l[i] + r[i]) * 0.5f;
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
            }
            peaks.push_back ({ lo, hi });
            peak = juce::jmax (peak, std::abs (lo), std::abs (hi));
        }
        if (peak > 0.0f)
            for (auto& pk : peaks)
                pk = { pk.first / peak, pk.second / peak };
    }

    ChopShopProcessor& proc;
    Kit::Ptr kit;
    std::vector<int> slices;
    std::vector<std::pair<float, float>> peaks;
    int dragIndex = -1, auditionPad = -1, lastPlayhead = -1;
    bool dropHover = false;
};
