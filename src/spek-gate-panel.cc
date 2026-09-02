#include "spek-gate-panel.h"

#include <wx/dirdlg.h>
#include <wx/progdlg.h>
#include <wx/wrapsizer.h>
#include <wx/datetime.h>
#include <wx/filedlg.h>
#include <wx/filename.h>

#include <algorithm>
#include <fstream>

wxDEFINE_EVENT(SPEK_GATE_FILE_ACTIVATED, wxCommandEvent);

// Internal, panel-to-itself: the worker thread cannot touch widgets, so it posts
// these and the GUI thread does the drawing.
wxDEFINE_EVENT(GATE_EVT_PROGRESS, wxThreadEvent);
wxDEFINE_EVENT(GATE_EVT_FINISHED, wxThreadEvent);

enum {
    ID_PICK_FOLDER = wxID_HIGHEST + 300,
    ID_PICK_FILES,
    ID_STOP,
    ID_SELECT_FLAGGED,
    ID_REPORT,
    ID_SPECTROGRAMS,
    ID_BUNDLE,
    ID_QUARANTINE,
    ID_RESTORE,
    ID_DELETE,
    ID_DELETE_ALL,
    ID_LIST,
};

wxBEGIN_EVENT_TABLE(SpekGatePanel, wxPanel)
    EVT_BUTTON(ID_PICK_FOLDER, SpekGatePanel::on_pick_folder)
    EVT_BUTTON(ID_PICK_FILES, SpekGatePanel::on_pick_files)
    EVT_BUTTON(ID_STOP, SpekGatePanel::on_stop)
    EVT_BUTTON(ID_SELECT_FLAGGED, SpekGatePanel::on_select_flagged)
    EVT_BUTTON(ID_REPORT, SpekGatePanel::on_save_report)
    EVT_BUTTON(ID_SPECTROGRAMS, SpekGatePanel::on_save_spectrograms)
    EVT_BUTTON(ID_BUNDLE, SpekGatePanel::on_export_bundle)
    EVT_BUTTON(ID_QUARANTINE, SpekGatePanel::on_quarantine)
    EVT_BUTTON(ID_RESTORE, SpekGatePanel::on_restore)
    EVT_BUTTON(ID_DELETE, SpekGatePanel::on_delete)
    EVT_BUTTON(ID_DELETE_ALL, SpekGatePanel::on_delete_all_flagged)
    EVT_LIST_ITEM_SELECTED(ID_LIST, SpekGatePanel::on_item_selected)
    EVT_LIST_ITEM_CHECKED(ID_LIST, SpekGatePanel::on_item_checked)
    EVT_LIST_ITEM_UNCHECKED(ID_LIST, SpekGatePanel::on_item_checked)
    EVT_THREAD(GATE_EVT_PROGRESS, SpekGatePanel::on_progress)
    EVT_THREAD(GATE_EVT_FINISHED, SpekGatePanel::on_finished)
wxEND_EVENT_TABLE()

SpekGatePanel::SpekGatePanel(wxWindow *parent, GateSpectrogramRenderer *renderer)
    : wxPanel(parent, wxID_ANY), renderer(renderer)
{
    build_ui();
    update_buttons();
}

SpekGatePanel::~SpekGatePanel()
{
    stop_scan();
    if (this->worker && this->worker->joinable()) {
        this->worker->join();
    }
}

void SpekGatePanel::build_ui()
{
    auto *root = new wxBoxSizer(wxVERTICAL);

    // ---- what to scan ---------------------------------------------------------
    auto *controls = new wxBoxSizer(wxHORIZONTAL);
    this->btn_folder = new wxButton(this, ID_PICK_FOLDER, _("Scan Folder…"));
    this->btn_files = new wxButton(this, ID_PICK_FILES, _("Scan Files…"));
    this->recursive = new wxCheckBox(this, wxID_ANY, _("Include subfolders"));
    this->recursive->SetValue(true);
    this->btn_stop = new wxButton(this, ID_STOP, _("Stop"));

    controls->Add(this->btn_folder, 0, wxALL, 4);
    controls->Add(this->btn_files, 0, wxALL, 4);
    controls->Add(this->recursive, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    controls->AddStretchSpacer();
    controls->Add(this->btn_stop, 0, wxALL, 4);
    root->Add(controls, 0, wxEXPAND);

    // ---- progress -------------------------------------------------------------
    this->gauge = new wxGauge(this, wxID_ANY, 100);
    this->status = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(this->gauge, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
    root->Add(this->status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    this->summary = new wxStaticText(this, wxID_ANY, _("Nothing scanned yet."));
    root->Add(this->summary, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    // ---- results --------------------------------------------------------------
    this->list = new wxListCtrl(this, ID_LIST, wxDefaultPosition, wxDefaultSize,
                                wxLC_REPORT | wxLC_SINGLE_SEL);
    this->list->EnableCheckBoxes(true);
    this->list->AppendColumn(_("Verdict"), wxLIST_FORMAT_LEFT, 110);
    this->list->AppendColumn(_("Conf"), wxLIST_FORMAT_RIGHT, 55);
    this->list->AppendColumn(_("Track"), wxLIST_FORMAT_LEFT, 260);
    this->list->AppendColumn(_("Cutoff"), wxLIST_FORMAT_RIGHT, 80);
    this->list->AppendColumn(_("Wall"), wxLIST_FORMAT_RIGHT, 70);
    this->list->AppendColumn(_("Above"), wxLIST_FORMAT_RIGHT, 80);
    this->list->AppendColumn(_("Why"), wxLIST_FORMAT_LEFT, 420);
    root->Add(this->list, 1, wxEXPAND | wxALL, 4);

    // ---- what to do with the results -----------------------------------------
    auto *actions = new wxWrapSizer(wxHORIZONTAL);
    this->btn_select_flagged = new wxButton(this, ID_SELECT_FLAGGED, _("Select All Flagged"));
    this->btn_report = new wxButton(this, ID_REPORT, _("Save Report…"));
    this->btn_spectrograms = new wxButton(this, ID_SPECTROGRAMS, _("Save Spectrograms…"));
    this->btn_bundle = new wxButton(this, ID_BUNDLE, _("Export Bundle…"));
    this->btn_quarantine = new wxButton(this, ID_QUARANTINE, _("Quarantine"));
    this->btn_restore = new wxButton(this, ID_RESTORE, _("Put Quarantined Back"));
    this->btn_delete = new wxButton(this, ID_DELETE, _("Delete"));
    this->btn_delete_all = new wxButton(this, ID_DELETE_ALL, _("Delete All Flagged"));

    for (wxButton *b : {this->btn_select_flagged, this->btn_report, this->btn_spectrograms,
                        this->btn_bundle, this->btn_quarantine, this->btn_restore,
                        this->btn_delete, this->btn_delete_all}) {
        actions->Add(b, 0, wxALL, 3);
    }
    root->Add(actions, 0, wxEXPAND);

    SetSizer(root);
}

// ---- starting a scan -----------------------------------------------------------

void SpekGatePanel::on_pick_folder(wxCommandEvent&)
{
    wxDirDialog dlg(this, _("Choose a folder to check"), this->cur_dir,
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        this->cur_dir = dlg.GetPath();
        scan_folder(dlg.GetPath());
    }
}

void SpekGatePanel::on_pick_files(wxCommandEvent&)
{
    wxFileDialog dlg(this, _("Choose files to check"), this->cur_dir, wxEmptyString,
                     _("Lossless audio") +
                         "|*.flac;*.wav;*.aiff;*.aif;*.alac;*.m4a;*.ape;*.wv;*.tta"
                         "|" + _("All files") + "|*",
                     wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        wxArrayString paths;
        dlg.GetPaths(paths);
        this->cur_dir = dlg.GetDirectory();
        scan_files(paths);
    }
}

void SpekGatePanel::scan_folder(const wxString& path)
{
    std::vector<std::string> files =
        gate_find_audio_files(std::string(path.utf8_str()), this->recursive->GetValue());
    if (files.empty()) {
        wxMessageBox(_("No lossless audio files found in that folder."),
                     _("Nothing to check"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    start_scan(files, std::string(path.utf8_str()));
}

void SpekGatePanel::scan_files(const wxArrayString& paths)
{
    std::vector<std::string> files;
    for (const auto& p : paths) {
        files.push_back(std::string(p.utf8_str()));
    }
    if (files.empty()) {
        return;
    }
    // With a hand-picked list there is no single scanned folder, so the common
    // parent becomes the root the file actions are allowed to touch.
    wxFileName first(paths[0]);
    wxString common = first.GetPath();
    for (const auto& p : paths) {
        wxFileName f(p);
        while (!common.IsEmpty() && !f.GetFullPath().StartsWith(common)) {
            wxFileName up(common);
            up.RemoveLastDir();
            wxString parent = up.GetPath();
            if (parent == common) {
                break;
            }
            common = parent;
        }
    }
    start_scan(files, std::string(common.utf8_str()));
}

void SpekGatePanel::start_scan(const std::vector<std::string>& paths, const std::string& root)
{
    if (this->scanning) {
        return;
    }
    if (this->worker && this->worker->joinable()) {
        this->worker->join();
    }

    this->scanning = true;
    this->stop_flag = false;
    this->scan_root = root;
    this->scan = GateScan();
    this->checked.clear();
    this->list->DeleteAllItems();
    this->gauge->SetRange((int)paths.size());
    this->gauge->SetValue(0);
    update_buttons();

    this->worker.reset(new std::thread([this, paths, root]() {
        GateScan result = gate_scan_paths(
            paths, root,
            [this](int done, int total, const std::string& name) {
                auto *evt = new wxThreadEvent(GATE_EVT_PROGRESS);
                evt->SetInt(done);
                evt->SetExtraLong(total);
                evt->SetString(wxString::FromUTF8(name.c_str()));
                wxQueueEvent(this, evt);
            },
            [this]() { return this->stop_flag.load(); });

        auto *done_evt = new wxThreadEvent(GATE_EVT_FINISHED);
        // The scan is moved across on the event so the GUI thread owns it.
        done_evt->SetPayload(result);
        wxQueueEvent(this, done_evt);
    }));
}

void SpekGatePanel::stop_scan()
{
    this->stop_flag = true;
}

void SpekGatePanel::on_stop(wxCommandEvent&)
{
    stop_scan();
    this->status->SetLabel(_("Stopping…"));
}

void SpekGatePanel::on_progress(wxThreadEvent& evt)
{
    int done = evt.GetInt();
    int total = (int)evt.GetExtraLong();
    this->gauge->SetRange(total);
    this->gauge->SetValue(done);
    this->status->SetLabel(wxString::Format(_("Checking %d of %d — %s"),
                                            done, total,
                                            wxFileName(evt.GetString()).GetFullName()));
}

void SpekGatePanel::on_finished(wxThreadEvent& evt)
{
    this->scan = evt.GetPayload<GateScan>();
    this->scanning = false;
    this->checked.assign(this->scan.results.size(), false);
    this->gauge->SetValue(this->gauge->GetRange());
    this->status->SetLabel(this->scan.stopped ? _("Stopped.") : _("Done."));
    refresh_results();
    update_buttons();
}

// ---- results -------------------------------------------------------------------

void SpekGatePanel::refresh_results()
{
    this->list->DeleteAllItems();
    for (size_t i = 0; i < this->scan.results.size(); i++) {
        const GateResult& r = this->scan.results[i];
        long idx = this->list->InsertItem((long)i, wxString::FromUTF8(gate_verdict_name(r.verdict)));
        this->list->SetItem(idx, 1, wxString::Format("%d%%", r.confidence));

        // Prefer the tags: "03. Untitled.flac" is no use when the point is to go
        // back to a seller.
        wxString track = wxString::FromUTF8(r.name.c_str());
        if (!r.title.empty()) {
            track = wxString::FromUTF8(r.artist.empty()
                                           ? r.title.c_str()
                                           : (r.artist + " — " + r.title).c_str());
        }
        this->list->SetItem(idx, 2, track);
        this->list->SetItem(idx, 3, r.cutoff_hz > 0
                                        ? wxString::Format("%.1f kHz", r.cutoff_hz / 1000.0)
                                        : wxString());
        this->list->SetItem(idx, 4, r.wall_db != 0.0
                                        ? wxString::Format("%.0f dB", r.wall_db)
                                        : wxString());
        this->list->SetItem(idx, 5, r.above_db != 0.0
                                        ? wxString::Format("%.0f dB", r.above_db)
                                        : wxString());

        wxString why;
        if (!r.error.empty()) {
            why = wxString::FromUTF8(r.error.c_str());
        } else {
            for (size_t j = 0; j < r.reasons.size(); j++) {
                if (j) why += "; ";
                why += wxString::FromUTF8(r.reasons[j].c_str());
            }
            if (!r.estimated_source.empty()) {
                why += " (" + wxString::FromUTF8(r.estimated_source.c_str()) + ")";
            }
        }
        this->list->SetItem(idx, 6, why);

        // Colour carries the verdict at a glance; the text still says it, so this
        // stays readable for anyone who cannot separate the hues.
        switch (r.verdict) {
        case GateVerdict::LOSSY:
            this->list->SetItemTextColour(idx, wxColour(190, 30, 30));
            break;
        case GateVerdict::PADDED:
        case GateVerdict::UPSAMPLED:
        case GateVerdict::SUSPECT:
            this->list->SetItemTextColour(idx, wxColour(180, 110, 0));
            break;
        case GateVerdict::UNREADABLE:
            this->list->SetItemTextColour(idx, wxColour(120, 120, 120));
            break;
        default:
            break;
        }
    }

    wxString parts;
    static const GateVerdict order[] = {
        GateVerdict::LOSSY, GateVerdict::PADDED, GateVerdict::UPSAMPLED,
        GateVerdict::SUSPECT, GateVerdict::UNREADABLE, GateVerdict::CLEAN,
        GateVerdict::LOSSY_FORMAT,
    };
    for (GateVerdict v : order) {
        int n = this->scan.count_of(v);
        if (n) {
            if (!parts.IsEmpty()) parts += "   ";
            parts += wxString::Format("%s %d", gate_verdict_name(v), n);
        }
    }
    this->summary->SetLabel(parts.IsEmpty()
                                ? _("Nothing scanned yet.")
                                : wxString::Format(_("%d files:   %s"),
                                                   this->scan.scanned, parts));
    Layout();
}

void SpekGatePanel::on_item_selected(wxListEvent& evt)
{
    long idx = evt.GetIndex();
    if (idx < 0 || idx >= (long)this->scan.results.size()) {
        return;
    }
    // Hand the path to the window, which owns the spectrogram.
    auto *out = new wxCommandEvent(SPEK_GATE_FILE_ACTIVATED);
    out->SetString(wxString::FromUTF8(this->scan.results[idx].path.c_str()));
    wxQueueEvent(GetParent(), out);
    update_buttons();
}

void SpekGatePanel::on_item_checked(wxListEvent& evt)
{
    long idx = evt.GetIndex();
    if (idx >= 0 && idx < (long)this->checked.size()) {
        this->checked[idx] = this->list->IsItemChecked(idx);
    }
    update_buttons();
}

std::vector<std::string> SpekGatePanel::checked_paths() const
{
    std::vector<std::string> out;
    for (size_t i = 0; i < this->scan.results.size() && i < this->checked.size(); i++) {
        if (this->checked[i]) {
            out.push_back(this->scan.results[i].path);
        }
    }
    return out;
}

// Ticked rows if any are ticked, otherwise whatever row is highlighted — so the
// buttons do the obvious thing whether you tick or just click.
std::vector<std::string> SpekGatePanel::selected_or_checked() const
{
    std::vector<std::string> out = checked_paths();
    if (!out.empty()) {
        return out;
    }
    long idx = this->list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (idx >= 0 && idx < (long)this->scan.results.size()) {
        out.push_back(this->scan.results[idx].path);
    }
    return out;
}

void SpekGatePanel::on_select_flagged(wxCommandEvent&)
{
    for (size_t i = 0; i < this->scan.results.size(); i++) {
        bool flag = false;
        switch (this->scan.results[i].verdict) {
        case GateVerdict::LOSSY:
        case GateVerdict::PADDED:
        case GateVerdict::UPSAMPLED:
        case GateVerdict::SUSPECT:
            flag = true;
            break;
        default:
            break;
        }
        this->checked[i] = flag;
        this->list->CheckItem((long)i, flag);
    }
    update_buttons();
}

void SpekGatePanel::update_buttons()
{
    bool busy = this->scanning;
    bool have = !this->scan.results.empty();
    size_t picked = selected_or_checked().size();

    this->btn_folder->Enable(!busy);
    this->btn_files->Enable(!busy);
    this->recursive->Enable(!busy);
    this->btn_stop->Enable(busy);
    this->btn_select_flagged->Enable(!busy && have);
    this->btn_report->Enable(!busy && have);
    this->btn_spectrograms->Enable(!busy && picked > 0);
    this->btn_bundle->Enable(!busy && have);
    this->btn_quarantine->Enable(!busy && picked > 0);
    this->btn_restore->Enable(!busy && !this->scan_root.empty());
    this->btn_delete->Enable(!busy && picked > 0);
    this->btn_delete_all->Enable(!busy && !this->scan.flagged().empty());

    size_t flagged = this->scan.flagged().size();
    this->btn_delete_all->SetLabel(flagged
                                       ? wxString::Format(_("Delete All Flagged (%d)"), (int)flagged)
                                       : _("Delete All Flagged"));
}

// ---- exporting -----------------------------------------------------------------

wxString SpekGatePanel::export_one(const GateResult& r, const wxString& dir,
                                   const wxString& ext)
{
    if (!this->renderer) {
        return _("no spectrogram renderer available");
    }
    wxString base = wxString::FromUTF8(gate_export_basename(r).c_str());
    wxFileName out(dir, base, ext);
    // Two files called "lossy - Artist - Track.png" are no use as evidence.
    int n = 2;
    while (out.FileExists() && n < 10000) {
        out.SetName(wxString::Format("%s (%d)", base, n++));
    }
    wxString error;
    if (!this->renderer->render_spectrogram(wxString::FromUTF8(r.path.c_str()),
                                            out.GetFullPath(),
                                            EXPORT_WIDTH, EXPORT_HEIGHT, error)) {
        return error.IsEmpty() ? _("render failed") : error;
    }
    return wxEmptyString;
}

int SpekGatePanel::export_many(const std::vector<std::string>& paths, const wxString& dir,
                               const wxString& ext, std::vector<wxString>& errors)
{
    int saved = 0;
    wxProgressDialog progress(_("Saving spectrograms"), _("Rendering..."),
                              (int)paths.size(), this,
                              wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT);
    for (size_t i = 0; i < paths.size(); i++) {
        const GateResult *found = nullptr;
        for (const auto& r : this->scan.results) {
            if (r.path == paths[i]) {
                found = &r;
                break;
            }
        }
        if (!found) {
            continue;
        }
        if (!progress.Update((int)i, wxString::FromUTF8(found->name.c_str()))) {
            break;
        }
        wxString err = export_one(*found, dir, ext);
        if (err.IsEmpty()) {
            saved++;
        } else {
            errors.push_back(wxString::FromUTF8(found->name.c_str()) + ": " + err);
        }
    }
    return saved;
}

void SpekGatePanel::on_save_report(wxCommandEvent&)
{
    wxFileDialog dlg(this, _("Save Report"), this->cur_dir, "transcode-scan.txt",
                     _("Text files") + "|*.txt|" + _("All files") + "|*",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }
    std::string text = gate_format_report(this->scan);
    std::ofstream out(std::string(dlg.GetPath().utf8_str()), std::ios::binary);
    if (!out) {
        wxMessageBox(_("Could not write the report."), _("Save failed"),
                     wxOK | wxICON_ERROR, this);
        return;
    }
    out << text;
    this->status->SetLabel(wxString::Format(_("Report saved to %s"), dlg.GetPath()));
}

void SpekGatePanel::on_save_spectrograms(wxCommandEvent&)
{
    std::vector<std::string> paths = selected_or_checked();
    if (paths.empty()) {
        return;
    }

    // One picture goes to a file dialog; several go to a folder. Asking for a
    // filename once per track would be unusable for a whole release.
    if (paths.size() == 1) {
        const GateResult *r = nullptr;
        for (const auto& x : this->scan.results) {
            if (x.path == paths[0]) {
                r = &x;
                break;
            }
        }
        if (!r) {
            return;
        }
        wxString suggested = wxString::FromUTF8(gate_export_basename(*r).c_str()) + ".png";
        wxFileDialog dlg(this, _("Save Spectrogram"), this->cur_dir, suggested,
                         _("PNG image") + "|*.png|" + _("JPEG image") + "|*.jpg",
                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }
        wxString error;
        if (!this->renderer ||
            !this->renderer->render_spectrogram(wxString::FromUTF8(r->path.c_str()),
                                                dlg.GetPath(), EXPORT_WIDTH, EXPORT_HEIGHT,
                                                error)) {
            wxMessageBox(error.IsEmpty() ? _("Could not render that file.") : error,
                         _("Save failed"), wxOK | wxICON_ERROR, this);
            return;
        }
        this->status->SetLabel(wxString::Format(_("Saved %s"), dlg.GetPath()));
        return;
    }

    wxDirDialog dlg(this, _("Save spectrograms into which folder?"), this->cur_dir,
                    wxDD_DEFAULT_STYLE);
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }
    std::vector<wxString> errors;
    int saved = export_many(paths, dlg.GetPath(), "png", errors);
    wxString msg = wxString::Format(_("Saved %d of %d spectrograms."), saved, (int)paths.size());
    for (const auto& e : errors) {
        msg += "\n" + e;
    }
    wxMessageBox(msg, _("Save spectrograms"), wxOK | wxICON_INFORMATION, this);
}

void SpekGatePanel::on_export_bundle(wxCommandEvent&)
{
    if (this->scan.results.empty()) {
        return;
    }
    wxDirDialog dlg(this, _("Export the scan into which folder?"), this->cur_dir,
                    wxDD_DEFAULT_STYLE);
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }

    // Everything needed to make the case, in one folder: the numbers as text, the
    // same numbers as data, and a picture of every file that was flagged.
    wxFileName base(dlg.GetPath(), wxEmptyString);
    base.AppendDir(wxDateTime::Now().Format("spektro-scan-%Y%m%d-%H%M"));
    if (!base.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        wxMessageBox(_("Could not create the export folder."), _("Export failed"),
                     wxOK | wxICON_ERROR, this);
        return;
    }
    wxString dir = base.GetPath();

    {
        std::ofstream out(std::string(wxFileName(dir, "report.txt").GetFullPath().utf8_str()),
                          std::ios::binary);
        out << gate_format_report(this->scan);
    }
    {
        std::ofstream out(std::string(wxFileName(dir, "results.json").GetFullPath().utf8_str()),
                          std::ios::binary);
        out << gate_format_json(this->scan);
    }

    // Pictures of the flagged files only: a folder of clean spectrograms proves
    // nothing, and a big release would take minutes to render.
    std::vector<std::string> paths;
    for (const GateResult *r : this->scan.flagged()) {
        paths.push_back(r->path);
    }
    wxFileName pics(dir, wxEmptyString);
    pics.AppendDir("spectrograms");
    int saved = 0;
    std::vector<wxString> errors;
    if (!paths.empty() && pics.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        saved = export_many(paths, pics.GetPath(), "png", errors);
    }

    wxString msg = wxString::Format(
        _("Exported to:\n%s\n\nreport.txt, results.json and %d spectrogram(s)."),
        dir, saved);
    if (!errors.empty()) {
        msg += wxString::Format(_("\n\n%d could not be rendered."), (int)errors.size());
    }
    wxMessageBox(msg, _("Export bundle"), wxOK | wxICON_INFORMATION, this);
    this->status->SetLabel(wxString::Format(_("Exported to %s"), dir));
}

// ---- file actions ---------------------------------------------------------------

void SpekGatePanel::on_quarantine(wxCommandEvent&)
{
    std::vector<std::string> paths = selected_or_checked();
    if (paths.empty()) {
        return;
    }
    wxString question = wxString::Format(
        _("Move %d file(s) into %s inside the scanned folder?\n\n"
          "This can be undone with Put Quarantined Back."),
        (int)paths.size(), wxString::FromUTF8(GATE_QUARANTINE_DIRNAME));
    if (wxMessageBox(question, _("Quarantine"), wxYES_NO | wxICON_QUESTION, this) != wxYES) {
        return;
    }
    GateActionResult res = gate_quarantine_files(paths, this->scan_root);
    wxString msg = wxString::Format(_("Moved %d file(s) to:\n%s"), res.done,
                                    wxString::FromUTF8(res.dir.c_str()));
    for (const auto& f : res.failed) {
        msg += "\n\n" + wxString::FromUTF8(f.first.c_str()) + "\n  " +
               wxString::FromUTF8(f.second.c_str());
    }
    wxMessageBox(msg, _("Quarantine"), wxOK | wxICON_INFORMATION, this);
}

void SpekGatePanel::on_restore(wxCommandEvent&)
{
    if (this->scan_root.empty()) {
        return;
    }
    std::string qdir = this->scan_root + "/" + GATE_QUARANTINE_DIRNAME;
    GateActionResult res = gate_restore_quarantined(qdir);
    wxString msg = res.done
                       ? wxString::Format(_("Put %d file(s) back."), res.done)
                       : _("There was nothing to restore.");
    for (const auto& f : res.failed) {
        msg += "\n\n" + wxString::FromUTF8(f.first.c_str()) + "\n  " +
               wxString::FromUTF8(f.second.c_str());
    }
    wxMessageBox(msg, _("Put quarantined back"), wxOK | wxICON_INFORMATION, this);
}

// Deletion is the one action with no undo, so it names the files and asks twice,
// with the second question worded differently from the first.
static bool confirm_delete(wxWindow *parent, const std::vector<std::string>& paths)
{
    wxString names;
    for (size_t i = 0; i < paths.size() && i < 12; i++) {
        names += "\n  " + wxFileName(wxString::FromUTF8(paths[i].c_str())).GetFullName();
    }
    if (paths.size() > 12) {
        names += wxString::Format(_("\n  ...and %d more"), (int)paths.size() - 12);
    }
    wxString first = wxString::Format(
        _("Permanently delete %d file(s)?%s\n\n"
          "This cannot be undone. Consider Quarantine instead."),
        (int)paths.size(), names);
    if (wxMessageBox(first, _("Delete files"),
                     wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, parent) != wxYES) {
        return false;
    }
    return wxMessageBox(_("Really delete? There is no undo."), _("Confirm delete"),
                        wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, parent) == wxYES;
}

void SpekGatePanel::on_delete(wxCommandEvent&)
{
    std::vector<std::string> paths = selected_or_checked();
    if (paths.empty() || !confirm_delete(this, paths)) {
        return;
    }
    GateActionResult res = gate_delete_files(paths, this->scan_root);
    wxString msg = wxString::Format(_("Deleted %d file(s)."), res.done);
    for (const auto& f : res.failed) {
        msg += "\n\n" + wxString::FromUTF8(f.first.c_str()) + "\n  " +
               wxString::FromUTF8(f.second.c_str());
    }
    wxMessageBox(msg, _("Delete"), wxOK | wxICON_INFORMATION, this);
}

void SpekGatePanel::on_delete_all_flagged(wxCommandEvent&)
{
    std::vector<std::string> paths;
    for (const GateResult *r : this->scan.flagged()) {
        paths.push_back(r->path);
    }
    if (paths.empty() || !confirm_delete(this, paths)) {
        return;
    }
    GateActionResult res = gate_delete_files(paths, this->scan_root);
    wxMessageBox(wxString::Format(_("Deleted %d of %d flagged file(s)."),
                                  res.done, (int)paths.size()),
                 _("Delete all flagged"), wxOK | wxICON_INFORMATION, this);
}
