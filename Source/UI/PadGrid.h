#pragma once

#include "Style.h"
#include "../PluginProcessor.h"

/** MPC-style 4x4 pad matrix. Pad 1 is bottom-left, pad 16 top-right. */
class PadGrid : public juce::Component,
                public juce::FileDragAndDropTarget,
                public juce::DragAndDropTarget,
                private juce::Timer
{
public:
    explicit PadGrid (ChopShopProcessor& p) : proc (p) { startTimerHz (30); }

    std::function<void (int)> onSelectPad;

    void setKit (Kit::Ptr k)
    {
        kit = std::move (k);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int sel = proc.getSelectedPad();
        for (int pad = 0; pad < ChopShopProcessor::numPads; ++pad)
        {
            auto r = padBounds (pad);
            const float glow = display[pad];
            const bool hasSample = kit != nullptr && kit->region (pad).valid();
            const auto tint = padColour (pad);

            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 7.0f);

            juce::ColourGradient body (ui::col::pad.brighter (0.1f), r.getX(), r.getY(), ui::col::pad.darker (0.35f),
                                       r.getX(), r.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, 7.0f);

            if (glow > 0.01f)
            {
                g.setColour (tint.withAlpha (0.85f * glow));
                g.fillRoundedRectangle (r, 7.0f);
                g.setColour (tint.withAlpha (0.35f * glow));
                g.drawRoundedRectangle (r.expanded (2.0f), 9.0f, 3.0f);
            }

            if (pad == dropHover)
            {
                g.setColour (ui::col::amber.withAlpha (0.25f));
                g.fillRoundedRectangle (r, 7.0f);
            }

            g.setColour (pad == sel ? ui::col::amber : juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, pad == sel ? 2.0f : 1.0f);

            auto inner = r.reduced (8.0f, 6.0f);
            g.setColour (hasSample ? tint : ui::col::faint);
            g.fillRoundedRectangle (inner.getX(), inner.getY() + 2.0f, 14.0f, 3.0f, 1.5f);

            g.setColour (glow > 0.3f ? juce::Colours::white : ui::col::text);
            g.setFont (ui::font (15.0f, true));
            g.drawText (juce::String (pad + 1), inner.removeFromTop (22.0f), juce::Justification::topRight);

            g.setFont (ui::font (10.0f, true));
            g.setColour (ui::col::dim);
            auto bottom = inner.removeFromBottom (13.0f);
            g.drawText (juce::MidiMessage::getMidiNoteName (ChopShopProcessor::padNoteBase + pad, true, true, 3),
                        bottom, juce::Justification::bottomRight);
            g.setColour (hasSample ? ui::col::text.withAlpha (0.85f) : ui::col::faint);
            g.drawFittedText (padLabel (pad), bottom.withTrimmedRight (24).toNearestInt(), juce::Justification::bottomLeft, 1, 0.7f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int pad = padAt (e.position);
        if (pad < 0)
            return;
        select (pad);

        if (e.mods.isPopupMenu())
        {
            showMenu (pad);
            return;
        }
        // Harder hits towards the top of the pad, like pressing a real pad firmly.
        auto r = padBounds (pad);
        const float vel = juce::jmap (1.0f - (e.position.y - r.getY()) / r.getHeight(), 0.45f, 1.0f);
        proc.padNoteOn (pad, vel);
        heldPad = pad;
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (heldPad >= 0)
            proc.padNoteOff (heldPad);
        heldPad = -1;
    }

    // Files from Finder / Splice desktop app
    bool isInterestedInFileDrag (const juce::StringArray& files) override { return proc.isSupportedAudioFile (files[0]); }
    void fileDragMove (const juce::StringArray&, int x, int y) override { setDropHover (padAt ({ (float) x, (float) y })); }
    void fileDragExit (const juce::StringArray&) override { setDropHover (-1); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        dropOnPad (padAt ({ (float) x, (float) y }), files[0]);
    }

    // Items dragged from the Library panel
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.isString() && proc.isSupportedAudioFile (d.description.toString());
    }
    void itemDragMove (const SourceDetails& d) override { setDropHover (padAt (d.localPosition.toFloat())); }
    void itemDragExit (const SourceDetails&) override { setDropHover (-1); }
    void itemDropped (const SourceDetails& d) override
    {
        dropOnPad (padAt (d.localPosition.toFloat()), d.description.toString());
    }

private:
    juce::Rectangle<float> padBounds (int pad) const
    {
        const float gap = 8.0f;
        const float w = ((float) getWidth() - gap * 3.0f) / 4.0f;
        const float h = ((float) getHeight() - gap * 3.0f) / 4.0f;
        const int row = 3 - pad / 4, colm = pad % 4;
        return { (float) colm * (w + gap), (float) row * (h + gap), w, h };
    }

    int padAt (juce::Point<float> pt) const
    {
        for (int pad = 0; pad < ChopShopProcessor::numPads; ++pad)
            if (padBounds (pad).contains (pt))
                return pad;
        return -1;
    }

    juce::Colour padColour (int pad) const
    {
        if (kit != nullptr && kit->padSample[(size_t) pad] == nullptr && kit->padSlice[(size_t) pad] >= 0)
            return ui::sliceColour (kit->padSlice[(size_t) pad]);
        return ui::col::accent;
    }

    juce::String padLabel (int pad) const
    {
        if (kit == nullptr)
            return "EMPTY";
        if (auto& s = kit->padSample[(size_t) pad])
            return s->name.toUpperCase();
        const int sl = kit->padSlice[(size_t) pad];
        return sl >= 0 && sl < kit->numSlices() ? "SLICE " + juce::String (sl + 1) : juce::String ("EMPTY");
    }

    void select (int pad)
    {
        proc.setSelectedPad (pad);
        if (onSelectPad) onSelectPad (pad);
        repaint();
    }

    void setDropHover (int pad)
    {
        if (pad != dropHover) { dropHover = pad; repaint(); }
    }

    void dropOnPad (int pad, const juce::String& path)
    {
        setDropHover (-1);
        if (pad >= 0 && proc.loadPadSample (pad, juce::File (path)))
            select (pad);
    }

    void showMenu (int pad)
    {
        juce::PopupMenu m;
        m.addSectionHeader ("PAD " + juce::String (pad + 1));
        m.addItem (1, "Load sample to pad...");
        m.addItem (2, "Clear pad sample", kit != nullptr && kit->padSample[(size_t) pad] != nullptr);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<PadGrid> (this), pad] (int r)
                         {
                             if (safe == nullptr) return;
                             if (r == 1) safe->chooseFile (pad);
                             if (r == 2) safe->proc.clearPadSample (pad);
                         });
    }

    void chooseFile (int pad)
    {
        chooser = std::make_unique<juce::FileChooser> ("Load sample to pad " + juce::String (pad + 1), juce::File(),
                                                       params::audioWildcard);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, pad] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      proc.loadPadSample (pad, fc.getResult());
                              });
    }

    void timerCallback() override
    {
        bool changed = false;
        for (int pad = 0; pad < ChopShopProcessor::numPads; ++pad)
        {
            const float hit = proc.padGlow[pad].exchange (0.0f);
            const float next = juce::jmax (hit, display[pad] * 0.84f);
            const float v = next < 0.01f ? 0.0f : next;
            if (v != display[pad]) { display[pad] = v; changed = true; }
        }
        const int sel = proc.getSelectedPad();
        if (sel != lastSel) { lastSel = sel; changed = true; }
        if (changed) repaint();
    }

    ChopShopProcessor& proc;
    Kit::Ptr kit;
    float display[ChopShopProcessor::numPads] {};
    int heldPad = -1, dropHover = -1, lastSel = -1;
    std::unique_ptr<juce::FileChooser> chooser;
};
