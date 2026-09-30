#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

/*  "Ask Claude" Splice search.

    The plug-in can't call Claude's Splice connector itself, so it talks to a Claude Code
    session (the "bridge", see .claude/commands/splice-bridge.md) through JSON files:

      ~/Music/ChopShop Library/.bridge/
        requests/<id>.json   written by the plug-in   { id, query, host_bpm, max_results }
        results/<id>.json    written by the bridge    { id, status, message, candidates[], files[] }
        approvals/<id>.json  written by the plug-in   { id, uuids[] }   (user confirmed credit spend)
        bridge.alive         heartbeat touched by the bridge
      ~/Music/ChopShop Library/Splice/   downloaded audio (shared sample library)

    Each side owns its own files, so there are no write races.
*/
namespace splice
{
inline juce::File root()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("ChopShop Library/.bridge");
}
inline juce::File dir (const char* name) { return root().getChildFile (name); }

inline bool writeJsonAtomic (const juce::File& f, const juce::var& v)
{
    f.getParentDirectory().createDirectory();
    auto tmp = f.getSiblingFile (f.getFileName() + ".tmp");
    return tmp.replaceWithText (juce::JSON::toString (v)) && tmp.moveFileTo (f);
}

struct Candidate
{
    juce::String uuid, name, key, type, url;
    double bpm = 0.0, duration = 0.0;
    juce::File file;
};

struct Result
{
    juce::String status, message;
    juce::Array<Candidate> candidates;
};

class Client
{
public:
    juce::String submit (const juce::String& query, double hostBpm)
    {
        const auto id = "req-" + juce::String (juce::Time::currentTimeMillis());
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", id);
        o->setProperty ("query", query);
        o->setProperty ("host_bpm", hostBpm);
        o->setProperty ("max_results", 5);
        o->setProperty ("created", juce::Time::getCurrentTime().toISO8601 (true));
        writeJsonAtomic (dir ("requests").getChildFile (id + ".json"), juce::var (o));
        return id;
    }

    void approve (const juce::String& id, const juce::StringArray& uuids)
    {
        juce::Array<juce::var> arr;
        for (auto& u : uuids)
            arr.add (u);
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", id);
        o->setProperty ("uuids", arr);
        writeJsonAtomic (dir ("approvals").getChildFile (id + ".json"), juce::var (o));
    }

    std::optional<Result> poll (const juce::String& id) const
    {
        auto f = dir ("results").getChildFile (id + ".json");
        if (! f.existsAsFile())
            return std::nullopt;

        auto v = juce::JSON::parse (f);
        if (! v.isObject())
            return std::nullopt;

        Result r;
        r.status = v["status"].toString();
        r.message = v["message"].toString();

        juce::HashMap<juce::String, juce::String> fileFor;
        if (auto* files = v["files"].getArray())
            for (auto& fv : *files)
                fileFor.set (fv["uuid"].toString(), fv["path"].toString());

        if (auto* cands = v["candidates"].getArray())
            for (auto& cv : *cands)
            {
                Candidate c;
                c.uuid = cv["uuid"].toString();
                c.name = cv["name"].toString();
                c.key = cv["key"].toString();
                c.type = cv["type"].toString();
                c.url = cv["url"].toString();
                c.bpm = (double) cv["bpm"];
                c.duration = (double) cv["duration"];
                if (fileFor.contains (c.uuid))
                {
                    juce::File path (fileFor[c.uuid]);
                    if (path.existsAsFile())
                        c.file = path;
                }
                r.candidates.add (c);
            }
        return r;
    }

    /** The most recent request that has results, so reopening the editor restores the list. */
    juce::String latestId() const
    {
        auto files = dir ("results").findChildFiles (juce::File::findFiles, false, "*.json");
        juce::File newest;
        for (auto& f : files)
            if (newest == juce::File() || f.getLastModificationTime() > newest.getLastModificationTime())
                newest = f;
        return newest.existsAsFile() ? newest.getFileNameWithoutExtension() : juce::String();
    }

    bool bridgeOnline() const
    {
        auto hb = root().getChildFile ("bridge.alive");
        return hb.existsAsFile()
            && (juce::Time::getCurrentTime() - hb.getLastModificationTime()).inSeconds() < 180.0;
    }
};

//==============================================================================
class SearchPanel : public juce::Component, private juce::Timer
{
public:
    std::function<double()> getHostBpm;
    std::function<void (const juce::File&)> onPreview, onLoadMain, onLoadPad;

    SearchPanel()
    {
        box.setTextToShowWhenEmpty ("Ask Claude:  \"chopped drum breaks at 90 BPM\"", juce::Colour (0xff6c6d72));
        box.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0f12));
        box.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2c2d31));
        box.setColour (juce::TextEditor::focusedOutlineColourId, accent);
        box.setColour (juce::TextEditor::textColourId, juce::Colour (0xffd9d9dc));
        box.setFont (juce::FontOptions (14.0f));
        box.onReturnKey = [this] { search(); };
        addAndMakeVisible (box);

        askBtn.onClick = [this] { search(); };
        addAndMakeVisible (askBtn);

        downloadBtn.onClick = [this] { confirmDownload(); };
        addAndMakeVisible (downloadBtn);

        status.setColour (juce::Label::textColourId, juce::Colour (0xff8a8b90));
        status.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (status);

        for (int i = 0; i < maxRows; ++i)
        {
            auto* r = rows.add (new Row (*this));
            addChildComponent (r);
        }

        currentId = client.latestId();
        online = client.bridgeOnline();
        refresh();
        startTimer (700);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (online ? juce::Colour (0xff3fd07a) : juce::Colour (0xff5a5b60));
        g.fillEllipse (ledArea.toFloat());
        g.setColour (juce::Colour (0xff8a8b90));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (online ? "CLAUDE BRIDGE ONLINE" : "BRIDGE OFFLINE", ledArea.withWidth (160).translated (14, 0),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        auto top = b.removeFromTop (30);
        askBtn.setBounds (top.removeFromRight (110));
        top.removeFromRight (6);
        box.setBounds (top);
        b.removeFromTop (6);

        auto line = b.removeFromTop (18);
        ledArea = line.removeFromLeft (10).withSizeKeepingCentre (8, 8);
        line.removeFromLeft (170);
        status.setBounds (line);
        b.removeFromTop (4);

        auto bottom = b.removeFromBottom (26);
        downloadBtn.setBounds (bottom.removeFromRight (240));
        b.removeFromBottom (4);

        const int rh = juce::jmax (22, b.getHeight() / maxRows);
        for (auto* r : rows)
            r->setBounds (b.removeFromTop (rh).reduced (0, 1));
    }

private:
    static constexpr int maxRows = 5;
    const juce::Colour accent { 0xffe8412c };

    struct Row : public juce::Component
    {
        explicit Row (SearchPanel& o) : owner (o)
        {
            pick.onClick = [this] { owner.updateDownloadButton(); };
            addAndMakeVisible (pick);
            for (auto* b : { &play, &toMain, &toPad, &web })
                addChildComponent (b);
            play.onClick = [this] { if (owner.onPreview) owner.onPreview (cand.file); };
            toMain.onClick = [this] { if (owner.onLoadMain) owner.onLoadMain (cand.file); };
            toPad.onClick = [this] { if (owner.onLoadPad) owner.onLoadPad (cand.file); };
            web.onClick = [this] { juce::URL (cand.url).launchInDefaultBrowser(); };
        }

        void set (const Candidate& c)
        {
            cand = c;
            const bool have = c.file.existsAsFile();
            pick.setVisible (! have);
            play.setVisible (have);
            toMain.setVisible (have);
            toPad.setVisible (have);
            web.setVisible (! have && c.url.isNotEmpty());
            setMouseCursor (have ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            resized();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xff1d1e21));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            auto t = textArea;
            g.setColour (juce::Colour (0xffd9d9dc));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (cand.name, t.removeFromTop (t.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);
            juce::StringArray meta;
            if (cand.bpm > 0) meta.add (juce::String (juce::roundToInt (cand.bpm)) + " BPM");
            if (cand.key.isNotEmpty()) meta.add (cand.key);
            if (cand.type.isNotEmpty()) meta.add (cand.type);
            if (cand.duration > 0) meta.add (juce::String (cand.duration, 1) + " s");
            if (cand.file.existsAsFile()) meta.add ("downloaded - drag me");
            g.setColour (juce::Colour (0xff8a8b90));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (meta.joinIntoString ("  |  "), t, juce::Justification::topLeft, true);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (6, 2);
            pick.setBounds (b.removeFromLeft (24));
            if (cand.file.existsAsFile())
            {
                toPad.setBounds (b.removeFromRight (52).reduced (1, 3));
                toMain.setBounds (b.removeFromRight (58).reduced (1, 3));
                play.setBounds (b.removeFromRight (30).reduced (1, 3));
            }
            else if (web.isVisible())
                web.setBounds (b.removeFromRight (70).reduced (1, 3));
            textArea = b.withTrimmedLeft (4);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            // Drag the downloaded file onto a pad, the waveform, or straight into Logic.
            if (cand.file.existsAsFile() && e.getDistanceFromDragStart() > 6 && ! dragging)
            {
                dragging = true;
                juce::DragAndDropContainer::performExternalDragDropOfFiles ({ cand.file.getFullPathName() }, false, this,
                                                                            [this] { dragging = false; });
            }
        }

        SearchPanel& owner;
        Candidate cand;
        juce::ToggleButton pick;
        juce::TextButton play { juce::String::fromUTF8 ("\xe2\x96\xb6") }, toMain { "MAIN" }, toPad { "PAD" }, web { "SPLICE" };
        juce::Rectangle<int> textArea;
        bool dragging = false;
    };

    void search()
    {
        const auto q = box.getText().trim();
        if (q.isEmpty())
            return;
        currentId = client.submit (q, getHostBpm ? getHostBpm() : 0.0);
        submittedAt = juce::Time::getCurrentTime();
        last = {};
        showRows();
        status.setText ("Sent to Claude: \"" + q + "\"", juce::dontSendNotification);
        updateDownloadButton();
    }

    juce::StringArray selectedUuids() const
    {
        juce::StringArray u;
        for (auto* r : rows)
            if (r->isVisible() && r->pick.isVisible() && r->pick.getToggleState())
                u.add (r->cand.uuid);
        return u;
    }

    void updateDownloadButton()
    {
        const int n = selectedUuids().size();
        downloadBtn.setEnabled (n > 0 && last.status == "results");
        downloadBtn.setButtonText (n > 0 ? "DOWNLOAD " + juce::String (n) + " (max " + juce::String (n) + " credit"
                                               + (n > 1 ? "s)" : ")")
                                         : "SELECT SAMPLES TO DOWNLOAD");
    }

    void confirmDownload()
    {
        const auto uuids = selectedUuids();
        if (uuids.isEmpty() || currentId.isEmpty())
            return;

        const auto msg = "Download " + juce::String (uuids.size()) + " sample(s) from Splice?\n\n"
                         "The first download of each sound spends one of your Splice credits "
                         "(up to " + juce::String (uuids.size()) + "). Re-downloads are free.";
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Spend Splice credits?", msg,
                                            "Download", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([this, uuids, id = currentId] (int ok)
                                            {
                                                if (ok != 0)
                                                {
                                                    client.approve (id, uuids);
                                                    status.setText ("Approved - Claude is downloading...", juce::dontSendNotification);
                                                    downloadBtn.setEnabled (false);
                                                }
                                            }));
    }

    void showRows()
    {
        for (int i = 0; i < rows.size(); ++i)
        {
            const bool vis = i < last.candidates.size();
            rows[i]->setVisible (vis);
            if (vis)
                rows[i]->set (last.candidates[i]);
        }
    }

    void refresh()
    {
        if (currentId.isEmpty())
        {
            status.setText ("Describe what you're after and press Return.", juce::dontSendNotification);
            updateDownloadButton();
            return;
        }

        if (auto r = client.poll (currentId))
        {
            const bool changed = r->status != last.status || r->candidates.size() != last.candidates.size()
                              || r->message != last.message || countFiles (*r) != countFiles (last);
            if (changed)
            {
                // keep checkbox selections across polls
                const auto sel = selectedUuids();
                last = *r;
                showRows();
                for (auto* row : rows)
                    row->pick.setToggleState (sel.contains (row->cand.uuid), juce::dontSendNotification);

                juce::String s;
                if (r->status == "searching")        s = "Claude is searching Splice...";
                else if (r->status == "results")     s = juce::String (r->candidates.size()) + " suggestions - tick the ones to download";
                else if (r->status == "downloading") s = "Downloading...";
                else if (r->status == "done")        s = "Ready - preview, load, or drag onto a pad";
                else if (r->status == "error")       s = "Claude: " + r->message;
                if (r->message.isNotEmpty() && r->status != "error")
                    s << "  (" << r->message << ")";
                status.setText (s, juce::dontSendNotification);
                updateDownloadButton();
            }
        }
        else if (submittedAt != juce::Time() && (juce::Time::getCurrentTime() - submittedAt).inSeconds() > 15.0 && ! online)
        {
            status.setText ("Waiting for Claude - run  /splice-bridge  in Claude Code (in ~/ChopShop)", juce::dontSendNotification);
        }
    }

    static int countFiles (const Result& r)
    {
        int n = 0;
        for (auto& c : r.candidates)
            n += c.file.existsAsFile() ? 1 : 0;
        return n;
    }

    void timerCallback() override
    {
        const bool nowOnline = client.bridgeOnline();
        if (nowOnline != online)
        {
            online = nowOnline;
            repaint();
        }
        refresh();
    }

    Client client;
    juce::TextEditor box;
    juce::TextButton askBtn { "ASK CLAUDE" }, downloadBtn { "SELECT SAMPLES TO DOWNLOAD" };
    juce::Label status;
    juce::OwnedArray<Row> rows;
    juce::Rectangle<int> ledArea;
    juce::String currentId;
    juce::Time submittedAt;
    Result last;
    bool online = false;
};
} // namespace splice
