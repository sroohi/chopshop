#pragma once

#include "Style.h"
#include "../PluginProcessor.h"

/** Sample browser. Scans the ChopShop library folder (~/Music/ChopShop Library, where samples
    fetched from Splice are saved), the Splice desktop app's sounds folder (~/Splice) and any
    folders the user adds. Click to preview, double-click to load and chop, drag onto a pad
    or the waveform. */
class LibraryPanel : public juce::Component,
                     private juce::ListBoxModel,
                     private juce::Thread
{
public:
    struct Item
    {
        juce::File file;
        juce::String name, source, bpm, key;
        double seconds = 0.0;
        bool oneShot = false;
    };

    static juce::File libraryFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("ChopShop Library");
    }
    static juce::File spliceFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Splice");
    }

    explicit LibraryPanel (ChopShopProcessor& p) : juce::Thread ("ChopShop library scan"), proc (p)
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ChopShop";
        o.filenameSuffix = "settings";
        o.folderName = "ChopShop";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);
        formats.registerBasicFormats();

        for (auto* b : { &allBtn, &chopBtn, &spliceBtn })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (77);
            b->onClick = [this] { refilter(); };
            addAndMakeVisible (*b);
        }
        allBtn.setToggleState (true, juce::dontSendNotification);

        search.setTextToShowWhenEmpty ("Search  (name, bpm, key)", ui::col::dim);
        search.setColour (juce::TextEditor::backgroundColourId, ui::col::screen);
        search.setColour (juce::TextEditor::outlineColourId, ui::col::panelEdge);
        search.setColour (juce::TextEditor::focusedOutlineColourId, ui::col::amber);
        search.setColour (juce::TextEditor::textColourId, ui::col::text);
        search.setFont (ui::font (13.0f));
        search.onTextChange = [this] { refilter(); };
        addAndMakeVisible (search);

        list.setModel (this);
        list.setRowHeight (38);
        list.setColour (juce::ListBox::backgroundColourId, ui::col::screen);
        list.setColour (juce::ListBox::outlineColourId, ui::col::panelEdge);
        list.setOutlineThickness (1);
        addAndMakeVisible (list);

        chopLoadBtn.onClick = [this] { loadSelected (false); };
        padLoadBtn.onClick = [this] { loadSelected (true); };
        addBtn.onClick = [this] { addFolder(); };
        rescanBtn.onClick = [this] { rescan(); };
        revealBtn.onClick = [] { libraryFolder().createDirectory(); libraryFolder().revealToUser(); };
        for (auto* b : { &chopLoadBtn, &padLoadBtn, &addBtn, &rescanBtn, &revealBtn })
            addAndMakeVisible (*b);
        chopLoadBtn.setTooltip ("Load the selected sample as the main sample and chop it");
        padLoadBtn.setTooltip ("Load the selected sample onto the selected pad");
        addBtn.setTooltip ("Add a folder to the library");
        rescanBtn.setTooltip ("Rescan library folders");
        revealBtn.setTooltip ("Show the ChopShop Library folder in Finder");

        addAndMakeVisible (status);
        status.setFont (ui::font (10.5f));
        status.setColour (juce::Label::textColourId, ui::col::dim);

        rescan();
    }

    ~LibraryPanel() override
    {
        stopThread (4000);
        proc.stopPreview();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        auto tabs = b.removeFromTop (24);
        const int tw = tabs.getWidth() / 3;
        allBtn.setBounds (tabs.removeFromLeft (tw).reduced (1, 0));
        chopBtn.setBounds (tabs.removeFromLeft (tw).reduced (1, 0));
        spliceBtn.setBounds (tabs.reduced (1, 0));
        b.removeFromTop (6);
        search.setBounds (b.removeFromTop (26));
        b.removeFromTop (6);

        auto bottom = b.removeFromBottom (86);
        status.setBounds (bottom.removeFromBottom (18));
        auto row1 = bottom.removeFromTop (34).reduced (0, 2);
        chopLoadBtn.setBounds (row1.removeFromLeft (row1.getWidth() / 2).reduced (1, 0));
        padLoadBtn.setBounds (row1.reduced (1, 0));
        auto row2 = bottom.removeFromTop (32).reduced (0, 2);
        const int w3 = row2.getWidth() / 3;
        addBtn.setBounds (row2.removeFromLeft (w3).reduced (1, 0));
        revealBtn.setBounds (row2.removeFromLeft (w3).reduced (1, 0));
        rescanBtn.setBounds (row2.reduced (1, 0));

        list.setBounds (b.reduced (0, 2));
    }

    void rescan()
    {
        if (isThreadRunning())
            return;
        status.setText ("Scanning...", juce::dontSendNotification);
        startThread();
    }

private:
    //== ListBoxModel ============================================================
    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) shown.size())
            return;
        const auto& it = items[(size_t) shown[(size_t) row]];
        auto r = juce::Rectangle<int> (0, 0, w, h);

        if (selected)
        {
            g.setColour (ui::col::accent.withAlpha (0.22f));
            g.fillRect (r);
            g.setColour (ui::col::accent);
            g.fillRect (r.removeFromLeft (3));
        }
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine (h - 1, 0.0f, (float) w);

        auto inner = juce::Rectangle<int> (8, 3, w - 14, h - 6);
        auto top = inner.removeFromTop (17);

        const bool playing = (int) row == previewRow && proc.previewPlaying.load();
        const auto badgeCol = it.source == "SPLICE" ? juce::Colour (0xff3f8cff) : (it.source == "CHOPSHOP" ? ui::col::accent : ui::col::dim);
        auto badge = top.removeFromRight (it.source == "CHOPSHOP" ? 58 : 44).toFloat().reduced (0, 2);
        g.setColour (badgeCol.withAlpha (0.2f));
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (badgeCol);
        g.setFont (ui::font (8.5f, true));
        g.drawText (it.source, badge, juce::Justification::centred);

        g.setColour (playing ? ui::col::amber : ui::col::text);
        g.setFont (ui::font (12.0f, true));
        g.drawFittedText ((playing ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 ")) : juce::String()) + it.name,
                          top.withTrimmedRight (4), juce::Justification::centredLeft, 1, 0.85f);

        juce::StringArray meta;
        meta.add (it.oneShot ? "ONE-SHOT" : "LOOP");
        if (it.bpm.isNotEmpty()) meta.add (it.bpm + " BPM");
        if (it.key.isNotEmpty()) meta.add (it.key);
        if (it.seconds > 0.0) meta.add (juce::String (it.seconds, 1) + "s");
        g.setColour (ui::col::dim);
        g.setFont (ui::font (10.5f));
        g.drawText (meta.joinIntoString (juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))), inner,
                    juce::Justification::centredLeft);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) shown.size() || e.mods.isPopupMenu())
            return;
        if (row == previewRow && proc.previewPlaying.load())
        {
            proc.stopPreview();
            previewRow = -1;
        }
        else
        {
            proc.previewFile (items[(size_t) shown[(size_t) row]].file);
            previewRow = row;
            startPreviewWatch();
        }
        list.repaint();
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        list.selectRow (row);
        loadSelected (false);
    }

    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        if (rows.size() > 0 && rows[0] < (int) shown.size())
            return items[(size_t) shown[(size_t) rows[0]]].file.getFullPathName();
        return {};
    }

    juce::String getTooltipForRow (int row) override
    {
        return row < (int) shown.size() ? items[(size_t) shown[(size_t) row]].file.getFullPathName() : juce::String();
    }

    //== Actions ================================================================
    void loadSelected (bool toPad)
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) shown.size())
            return;
        const auto file = items[(size_t) shown[(size_t) row]].file;
        proc.stopPreview();
        previewRow = -1;
        if (toPad)
            proc.loadPadSample (proc.getSelectedPad(), file);
        else
            proc.loadMainSample (file);
        list.repaint();
    }

    void addFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a folder to the ChopShop library");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto dir = fc.getResult();
                                  if (! dir.isDirectory())
                                      return;
                                  auto folders = extraFolders();
                                  folders.addIfNotAlreadyThere (dir.getFullPathName());
                                  settings->setValue ("extraFolders", folders.joinIntoString ("\n"));
                                  settings->saveIfNeeded();
                                  rescan();
                              });
    }

    juce::StringArray extraFolders() const
    {
        return juce::StringArray::fromLines (settings->getValue ("extraFolders"));
    }

    // Keeps the "now playing" marker on the row in sync with the audio thread.
    void startPreviewWatch()
    {
        juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<LibraryPanel> (this)]
        {
            if (safe == nullptr) return;
            safe->list.repaint();
            if (safe->proc.previewPlaying.load())
                safe->startPreviewWatch();
            else
                safe->previewRow = -1;
        });
    }

    void refilter()
    {
        const auto q = search.getText().trim().toLowerCase();
        const auto tokens = juce::StringArray::fromTokens (q, " ", "");
        shown.clear();
        for (int i = 0; i < (int) items.size(); ++i)
        {
            const auto& it = items[(size_t) i];
            if (chopBtn.getToggleState() && it.source != "CHOPSHOP") continue;
            if (spliceBtn.getToggleState() && it.source != "SPLICE") continue;
            const auto hay = (it.name + " " + it.bpm + " " + it.key + " " + (it.oneShot ? "one-shot oneshot" : "loop")).toLowerCase();
            bool match = true;
            for (auto& t : tokens)
                if (! hay.contains (t)) { match = false; break; }
            if (match)
                shown.push_back (i);
        }
        previewRow = -1;
        list.updateContent();
        list.repaint();
        status.setText (juce::String ((int) shown.size()) + " of " + juce::String ((int) items.size()) + " samples",
                        juce::dontSendNotification);
    }

    //== Scanning (background thread) ===========================================
    static void parseName (Item& it)
    {
        // Sample-pack names usually carry tempo and key as tokens, e.g. "rss_90_loop_cali_Gmin".
        const auto tokens = juce::StringArray::fromTokens (it.file.getFileNameWithoutExtension(), "_- ().", "");
        for (auto& t : tokens)
        {
            if (it.bpm.isEmpty() && t.containsOnly ("0123456789") && t.getIntValue() >= 60 && t.getIntValue() <= 200)
                it.bpm = t;
            else if (it.key.isEmpty() && t.length() <= 5 && juce::String ("ABCDEFG").containsChar (t[0]))
            {
                const auto rest = t.substring (1).replace ("#", "").replace ("b", "");
                if (rest == "" || rest == "m" || rest == "min" || rest == "maj")
                    if (t.length() > 1 || tokens.size() < 3)
                        it.key = t;
            }
        }
        const auto lower = it.name.toLowerCase();
        it.oneShot = lower.contains ("one_shot") || lower.contains ("oneshot") || lower.contains ("one shot")
                     || (it.seconds > 0.0 && it.seconds < 2.0 && it.bpm.isEmpty());
    }

    void run() override
    {
        struct Root { juce::File dir; juce::String source; };
        std::vector<Root> roots { { libraryFolder(), "CHOPSHOP" }, { spliceFolder(), "SPLICE" } };
        for (auto& f : extraFolders())
            roots.push_back ({ juce::File (f), "FOLDER" });

        libraryFolder().createDirectory();

        std::vector<Item> found;
        juce::StringArray seen;
        for (auto& root : roots)
        {
            if (! root.dir.isDirectory())
                continue;
            for (const auto& entry : juce::RangedDirectoryIterator (root.dir, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf;*.ogg"))
            {
                if (threadShouldExit() || found.size() >= 20000)
                    break;
                const auto file = entry.getFile();
                if (seen.contains (file.getFullPathName()))
                    continue;
                seen.add (file.getFullPathName());

                Item it;
                it.file = file;
                it.name = file.getFileNameWithoutExtension();
                // Anything under a folder named "Splice" (downloads from Splice) is tagged as Splice.
                it.source = file.getFullPathName().contains ("/Splice/") ? juce::String ("SPLICE") : root.source;
                if (auto* r = formats.createReaderFor (file))
                {
                    it.seconds = (double) r->lengthInSamples / juce::jmax (1.0, r->sampleRate);
                    delete r;
                }
                parseName (it);
                found.push_back (std::move (it));
            }
        }

        std::sort (found.begin(), found.end(), [] (const Item& a, const Item& b)
        {
            if (a.source != b.source) return a.source < b.source;
            return a.name.compareIgnoreCase (b.name) < 0;
        });

        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LibraryPanel> (this), found = std::move (found)]() mutable
        {
            if (safe == nullptr) return;
            safe->items = std::move (found);
            safe->refilter();
        });
    }

    ChopShopProcessor& proc;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::AudioFormatManager formats;
    std::vector<Item> items;
    std::vector<int> shown;
    int previewRow = -1;

    juce::TextButton allBtn { "ALL" }, chopBtn { "CHOPSHOP" }, spliceBtn { "SPLICE" };
    juce::TextEditor search;
    juce::ListBox list { "Library" };
    juce::TextButton chopLoadBtn { "LOAD + CHOP" }, padLoadBtn { "TO PAD" };
    juce::TextButton addBtn { "+ FOLDER" }, revealBtn { "FINDER" }, rescanBtn { "RESCAN" };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
};
