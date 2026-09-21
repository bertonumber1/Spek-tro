#pragma once

#include <wx/wx.h>

#include "spek-gate-panel.h"

class SpekSpectrogram;
class wxSplitterWindow;

// The window also renders spectrograms on the panel's behalf: the panel decides
// *what* to export, but the pipeline and the palette live here.
class SpekWindow : public wxFrame, public GateSpectrogramRenderer
{
public:
    SpekWindow(const wxString& path);
    void open(const wxString& path);
    // Reveal the fake-lossless panel and scan `path` straight away — a
    // folder (recursive scan) or a single file (e.g. a right-click "Check
    // with Spek-tro" on one .wav), whichever `path` turns out to be.
    void check_folder(const wxString& path);

    bool render_spectrogram(const wxString& audio_path, const wxString& out_path,
                            int width, int height, wxString& error) override;

private:
    void on_open(wxCommandEvent& event);
    void on_save(wxCommandEvent& event);
    void on_exit(wxCommandEvent& event);
    void on_preferences(wxCommandEvent& event);
    void on_help(wxCommandEvent& event);
    void on_about(wxCommandEvent& event);
    void on_check_fakes(wxCommandEvent& event);
    void on_gate_file_activated(wxCommandEvent& event);

    SpekSpectrogram *spectrogram;
    SpekGatePanel *gate_panel;
    wxSplitterWindow *splitter;
    wxString path;
    wxString cur_dir;
    wxString description;

    DECLARE_EVENT_TABLE()
};
