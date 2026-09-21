#include <wx/aboutdlg.h>
#include <wx/artprov.h>
#include <wx/dnd.h>
#include <wx/filename.h>

// WX on WIN doesn't like it when pthread.h is included first.
#include <pthread.h>

#include <spek-utils.h>

#include "spek-artwork.h"
#include "spek-preferences-dialog.h"
#include "spek-preferences.h"
#include "spek-spectrogram.h"

#include <wx/splitter.h>

#include "spek-gate-panel.h"
#include "spek-window.h"

DECLARE_EVENT_TYPE(SPEK_NOTIFY_EVENT, -1)
DEFINE_EVENT_TYPE(SPEK_NOTIFY_EVENT)

enum { ID_CHECK_FAKES = wxID_HIGHEST + 400 };

BEGIN_EVENT_TABLE(SpekWindow, wxFrame)
    EVT_MENU(wxID_OPEN, SpekWindow::on_open)
    EVT_MENU(wxID_SAVE, SpekWindow::on_save)
    EVT_MENU(ID_CHECK_FAKES, SpekWindow::on_check_fakes)
    EVT_COMMAND(-1, SPEK_GATE_FILE_ACTIVATED, SpekWindow::on_gate_file_activated)
    EVT_MENU(wxID_EXIT, SpekWindow::on_exit)
    EVT_MENU(wxID_PREFERENCES, SpekWindow::on_preferences)
    EVT_MENU(wxID_HELP, SpekWindow::on_help)
    EVT_MENU(wxID_ABOUT, SpekWindow::on_about)
END_EVENT_TABLE()

// Forward declarations.

class SpekDropTarget : public wxFileDropTarget
{
public:
    SpekDropTarget(SpekWindow *window) : wxFileDropTarget(), window(window) {}

protected:
    virtual bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames){
        if (filenames.GetCount() == 1) {
            window->open(filenames[0]);
            return true;
        }
        return false;
    }

private:
    SpekWindow *window;
};

SpekWindow::SpekWindow(const wxString& path) :
    wxFrame(NULL, -1, wxEmptyString, wxDefaultPosition, wxDefaultSize), path(path)
{
    this->description = _("Spek-tro - Fake-Lossless Spectrum Analyser");
    SetTitle(this->description);
    SetSize(this->FromDIP(wxSize(640, 480)));

#ifndef OS_OSX
    SetIcons(wxArtProvider::GetIconBundle(ART_SPEK, wxART_FRAME_ICON));
#endif

    wxMenuBar *menu = new wxMenuBar();

    wxMenu *menu_file = new wxMenu();
    wxMenuItem *menu_file_open = new wxMenuItem(menu_file, wxID_OPEN);
    menu_file->Append(menu_file_open);
    wxMenuItem *menu_file_save = new wxMenuItem(menu_file, wxID_SAVE);
    menu_file->Append(menu_file_save);
    menu_file->AppendSeparator();
    wxMenuItem *menu_file_check = new wxMenuItem(
        menu_file, ID_CHECK_FAKES, wxString(_("&Check for Fake Lossless...")) + "\tCtrl-K");
    menu_file->Append(menu_file_check);
    menu_file->AppendSeparator();
    menu_file->Append(wxID_EXIT);
    menu->Append(menu_file, _("&File"));

    wxMenu *menu_edit = new wxMenu();
    wxMenuItem *menu_edit_prefs = new wxMenuItem(menu_edit, wxID_PREFERENCES);
    menu_edit_prefs->SetItemLabel(menu_edit_prefs->GetItemLabelText() + "\tCtrl-E");
    menu_edit->Append(menu_edit_prefs);
    menu->Append(menu_edit, _("&Edit"));

    wxMenu *menu_help = new wxMenu();
    wxMenuItem *menu_help_contents = new wxMenuItem(
        menu_help, wxID_HELP, wxString(_("&Help")) + "\tF1");
    menu_help->Append(menu_help_contents);
    wxMenuItem *menu_help_about = new wxMenuItem(menu_help, wxID_ABOUT);
    menu_help_about->SetItemLabel(menu_help_about->GetItemLabelText() + "\tShift-F1");
    menu_help->Append(menu_help_about);
    menu->Append(menu_help, _("&Help"));

    SetMenuBar(menu);

    wxToolBar *toolbar = CreateToolBar();
    toolbar->AddTool(
        wxID_OPEN,
        wxEmptyString,
        wxArtProvider::GetBitmap(ART_OPEN, wxART_TOOLBAR),
        menu_file_open->GetItemLabelText()
    );
    toolbar->AddTool(
        wxID_SAVE,
        wxEmptyString,
        wxArtProvider::GetBitmap(ART_SAVE, wxART_TOOLBAR),
        menu_file_save->GetItemLabelText()
    );
    toolbar->AddStretchableSpace();
    toolbar->AddTool(
        wxID_HELP,
        wxEmptyString,
        wxArtProvider::GetBitmap(ART_HELP, wxART_TOOLBAR),
        _("Help")
    );
    toolbar->Realize();

    wxSizer *sizer = new wxBoxSizer(wxVERTICAL);


    // The spectrogram alone until a scan is asked for, then the results share
    // the window with it: the numbers are the argument, the picture is what you
    // recognise, and neither is much use without the other.
    this->splitter = new wxSplitterWindow(
        this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxSP_3D | wxSP_LIVE_UPDATE);
    this->splitter->SetMinimumPaneSize(this->FromDIP(120));
    this->spectrogram = new SpekSpectrogram(this->splitter);
    this->gate_panel = new SpekGatePanel(this->splitter, this);
    this->gate_panel->Hide();
    this->splitter->Initialize(this->spectrogram);
    sizer->Add(this->splitter, 1, wxEXPAND);

    this->cur_dir = wxGetHomeDir();

    if (!path.IsEmpty()) {
        open(path);
    }

    SetDropTarget(new SpekDropTarget(this));

    SetSizer(sizer);

    // No update check: this fork has no version endpoint, and asking upstream's
    // would both mislead the user and contact a third party unbidden.
}

void SpekWindow::open(const wxString& path)
{
    wxFileName file_name(path);
    if (file_name.FileExists()) {
        this->path = path;
        wxString full_name = file_name.GetFullName();
        // TRANSLATORS: window title, %s is replaced with the file name
        wxString title = wxString::Format(_("Spek - %s"), full_name.c_str());
        SetTitle(title);

        this->spectrogram->open(path);
    }
}

// TODO: s/audio/media/
static const char *audio_extensions[] = {
    "3gp",
    "aac",
    "aif",
    "aifc",
    "aiff",
    "amr",
    "awb",
    "ape",
    "au",
    "dts",
    "flac",
    "flv",
    "gsm",
    "m4a",
    "m4p",
    "mp3",
    "mp4",
    "mp+",
    "mpc",
    "mpp",
    "oga",
    "ogg",
    "opus",
    "ra",
    "ram",
    "snd",
    "wav",
    "wma",
    "wv",
    NULL
};

void SpekWindow::on_open(wxCommandEvent&)
{
    static wxString filters = wxEmptyString;
    static int filter_index = 1;

    if (filters.IsEmpty()) {
        filters.Alloc(1024);
        filters += _("All files");
        filters += "|*.*|";
        filters += _("Audio files");
        filters += "|";
        for (int i = 0; audio_extensions[i]; ++i) {
            if (i) {
                filters += ";";
            }
            filters += "*.";
            filters += wxString::FromAscii(audio_extensions[i]);
        }
        filters.Shrink();
    }

    wxFileDialog *dlg = new wxFileDialog(
        this,
        _("Open File"),
        this->cur_dir,
        wxEmptyString,
        filters,
        wxFD_OPEN
    );
    dlg->SetFilterIndex(filter_index);

    if (dlg->ShowModal() == wxID_OK) {
        this->cur_dir = dlg->GetDirectory();
        filter_index = dlg->GetFilterIndex();
        open(dlg->GetPath());
    }

    dlg->Destroy();
}

void SpekWindow::on_save(wxCommandEvent&)
{
    static wxString filters = wxEmptyString;

    if (filters.IsEmpty()) {
        filters = _("PNG images");
        filters += "|*.png";
    }

    wxFileDialog *dlg = new wxFileDialog(
        this,
        _("Save Spectrogram"),
        this->cur_dir,
        wxEmptyString,
        filters,
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT
    );

    // Suggested name is <file_name>.png
    wxString name = _("Untitled");
    if (!this->path.IsEmpty()) {
        wxFileName file_name(this->path);
        name = file_name.GetFullName();
    }
    name += ".png";
    dlg->SetFilename(name);

    if (dlg->ShowModal() == wxID_OK) {
        this->cur_dir = dlg->GetDirectory();
        this->spectrogram->save(dlg->GetPath());
    }

    dlg->Destroy();
}

void SpekWindow::on_exit(wxCommandEvent&)
{
    Close(true);
}

void SpekWindow::on_preferences(wxCommandEvent&)
{
    SpekPreferencesDialog dlg(this);
    dlg.ShowModal();
}

void SpekWindow::on_help(wxCommandEvent&)
{
    wxLaunchDefaultBrowser("https://github.com/bertonumber1/Spek-tro#readme");
}

void SpekWindow::on_about(wxCommandEvent&)
{
    wxAboutDialogInfo info;
    info.AddDeveloper("Alexander Kojevnikov");
    info.AddDeveloper("Andreas Cadhalpun");
    info.AddDeveloper("Colin Watson");
    info.AddDeveloper("Daniel Hams");
    info.AddDeveloper("Elias Ojala");
    info.AddDeveloper("Fabian Deutsch");
    info.AddDeveloper("Guillaume Fourrier");
    info.AddDeveloper("Jakov Smolic");
    info.AddDeveloper("Jonathan Gonzalez V");
    info.AddDeveloper("Matteo Bini");
    info.AddDeveloper("Mike Wang");
    info.AddDeveloper("Simon Ruderich");
    info.AddDeveloper("Stefan Kost");
    info.AddDeveloper("Thibault North");
    info.AddDeveloper("Wyatt J. Brown");
    info.AddArtist("Olga Vasylevska");
    // TRANSLATORS: Add your name here
    wxString translator = _("translator-credits");
    if (translator != "translator-credits") {
        info.AddTranslator(translator);
    }
    info.SetName("Spek-tro");
    info.SetVersion(PACKAGE_VERSION);
    info.SetCopyright(_("Based on Spek, (c) 2010-2013 Alexander Kojevnikov and contributors"));
    info.SetDescription(this->description);
#ifdef OS_UNIX
    info.SetWebSite("https://github.com/bertonumber1/Spek-tro", _("Spek-tro on GitHub"));
    info.SetIcon(wxArtProvider::GetIcon("spek", wxART_OTHER, wxSize(128, 128)));
#endif
    wxAboutBox(info);
}

// ---- fake-lossless panel --------------------------------------------------------

void SpekWindow::on_check_fakes(wxCommandEvent&)
{
    if (this->splitter->IsSplit()) {
        // Toggling off leaves the results in place, so bringing the panel back
        // does not mean scanning the folder again.
        this->splitter->Unsplit(this->gate_panel);
        return;
    }
    this->gate_panel->Show();
    this->splitter->SplitHorizontally(
        this->spectrogram, this->gate_panel, this->FromDIP(220));
}

void SpekWindow::on_gate_file_activated(wxCommandEvent& event)
{
    // A row was picked: show that file in the spectrogram above the list.
    wxString path = event.GetString();
    if (!path.IsEmpty()) {
        open(path);
    }
}

bool SpekWindow::render_spectrogram(const wxString& audio_path, const wxString& out_path,
                                    int width, int height, wxString& error)
{
    return this->spectrogram->render_offscreen(audio_path, out_path, width, height, error);
}

void SpekWindow::check_folder(const wxString& path)
{
    if (!this->splitter->IsSplit()) {
        this->gate_panel->Show();
        this->splitter->SplitHorizontally(
            this->spectrogram, this->gate_panel, this->FromDIP(220));
    }
    // scan_folder() walks a directory; a right-click "Check with Spek-tro"
    // on a single file hands us a file path instead, and gate_find_audio_
    // files() silently returns nothing for anything that isn't a directory
    // — so a lone .wav would just report "no files found" without this check.
    if (wxFileName::FileExists(path)) {
        wxArrayString one;
        one.Add(path);
        this->gate_panel->scan_files(one);
    } else {
        this->gate_panel->scan_folder(path);
    }
}
