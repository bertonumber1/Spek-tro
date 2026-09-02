#pragma once

// The fake-lossless results panel.
//
// Sits beside Spek's spectrogram rather than in a modal of its own: the numbers
// are the argument, but the picture is what you actually recognise, and having to
// dismiss a dialog to see it would defeat the point of putting this inside Spek.
// Selecting a row loads that file into the spectrogram view behind it.

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <wx/wx.h>
#include <wx/listctrl.h>

#include "spek-gate-scan.h"

// Raised when the user picks a row; the window loads that file into the
// spectrogram. The path travels in the event's string.
wxDECLARE_EVENT(SPEK_GATE_FILE_ACTIVATED, wxCommandEvent);

// Rendering a spectrogram means driving the decode pipeline, which the window
// owns, so the panel asks for pictures through this rather than reaching into it.
// Implemented by SpekWindow.
class GateSpectrogramRenderer
{
public:
    virtual ~GateSpectrogramRenderer() {}

    // Render `audio_path` offscreen at the given size and write it to
    // `out_path`, whose extension chooses PNG or JPEG. Returns true on success,
    // otherwise fills `error`. Called on the GUI thread, one file at a time.
    virtual bool render_spectrogram(const wxString& audio_path, const wxString& out_path,
                                    int width, int height, wxString& error) = 0;
};

class SpekGatePanel : public wxPanel
{
public:
    SpekGatePanel(wxWindow *parent, GateSpectrogramRenderer *renderer);
    ~SpekGatePanel() override;

    // Exported pictures are a fixed size rather than the window's, so a report
    // sent to a seller looks the same whatever the sender's window was.
    static const int EXPORT_WIDTH = 1024;
    static const int EXPORT_HEIGHT = 512;

    // Scan a folder, or an explicit list of files the user chose.
    void scan_folder(const wxString& path);
    void scan_files(const wxArrayString& paths);

    bool is_scanning() const { return this->scanning; }
    void stop_scan();

private:
    void build_ui();
    void start_scan(const std::vector<std::string>& paths, const std::string& root);
    void refresh_results();
    void update_buttons();
    std::vector<std::string> checked_paths() const;
    std::vector<std::string> selected_or_checked() const;

    void on_pick_folder(wxCommandEvent&);
    void on_pick_files(wxCommandEvent&);
    void on_stop(wxCommandEvent&);
    void on_select_flagged(wxCommandEvent&);
    void on_save_report(wxCommandEvent&);
    void on_save_spectrograms(wxCommandEvent&);
    void on_export_bundle(wxCommandEvent&);
    void on_quarantine(wxCommandEvent&);
    void on_restore(wxCommandEvent&);
    void on_delete(wxCommandEvent&);
    void on_delete_all_flagged(wxCommandEvent&);
    void on_item_selected(wxListEvent&);
    void on_item_checked(wxListEvent&);
    void on_progress(wxThreadEvent&);
    void on_finished(wxThreadEvent&);

    // Render one result's spectrogram into `dir`. Returns empty on success, or
    // the reason it failed.
    wxString export_one(const GateResult& r, const wxString& dir, const wxString& ext);
    // Shared by "Save Spectrograms" and the export bundle.
    int export_many(const std::vector<std::string>& paths, const wxString& dir,
                    const wxString& ext, std::vector<wxString>& errors);

    GateSpectrogramRenderer *renderer = nullptr;
    wxListCtrl *list = nullptr;
    wxGauge *gauge = nullptr;
    wxStaticText *status = nullptr;
    wxStaticText *summary = nullptr;
    wxCheckBox *recursive = nullptr;
    wxButton *btn_folder = nullptr;
    wxButton *btn_files = nullptr;
    wxButton *btn_stop = nullptr;
    wxButton *btn_select_flagged = nullptr;
    wxButton *btn_report = nullptr;
    wxButton *btn_spectrograms = nullptr;
    wxButton *btn_bundle = nullptr;
    wxButton *btn_quarantine = nullptr;
    wxButton *btn_restore = nullptr;
    wxButton *btn_delete = nullptr;
    wxButton *btn_delete_all = nullptr;

    GateScan scan;
    std::vector<bool> checked;
    std::string scan_root;

    std::unique_ptr<std::thread> worker;
    std::atomic<bool> stop_flag{false};
    bool scanning = false;

    wxString cur_dir;

    wxDECLARE_EVENT_TABLE();
};
