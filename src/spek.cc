#include <wx/cmdline.h>
#include <wx/log.h>
#include <wx/socket.h>

#include "spek-artwork.h"
#include "spek-platform.h"
#include "spek-preferences.h"

#include "spek-window.h"

class Spek: public wxApp
{
public:
    Spek() : wxApp(), window(NULL), quit(false) {}

protected:
    virtual bool OnInit();
    virtual int OnRun();
#ifdef OS_OSX
    virtual void MacOpenFiles(const wxArrayString& files);
#endif

private:
    SpekWindow *window;
    wxString path;
    bool quit;
};

IMPLEMENT_APP(Spek)

bool Spek::OnInit()
{
    wxInitAllImageHandlers();
    wxSocketBase::Initialize();

    spek_artwork_init();
    spek_platform_init();
    SpekPreferences::get().init();

    static const wxCmdLineEntryDesc desc[] = {{
            wxCMD_LINE_SWITCH,
            "h",
            "help",
            "Show this help message",
            wxCMD_LINE_VAL_NONE,
            wxCMD_LINE_OPTION_HELP,
        }, {
            wxCMD_LINE_SWITCH,
            "V",
            "version",
            "Display the version and exit",
            wxCMD_LINE_VAL_NONE,
            wxCMD_LINE_PARAM_OPTIONAL,
        }, {
            wxCMD_LINE_OPTION,
            "c",
            "check",
            "Scan a folder for fake lossless files and show the results",
            wxCMD_LINE_VAL_STRING,
            wxCMD_LINE_PARAM_OPTIONAL,
        }, {
            wxCMD_LINE_PARAM,
            NULL,
            NULL,
            "FILE",
            wxCMD_LINE_VAL_STRING,
            wxCMD_LINE_PARAM_OPTIONAL,
        },
        wxCMD_LINE_DESC_END,
    };

    wxCmdLineParser parser(desc, argc, argv);
    int ret = parser.Parse(true);
    if (ret == 1) {
        return false;
    }
    if (ret == -1) {
        this->quit = true;
        return true;
    }
    if (parser.Found("version")) {
        // TRANSLATORS: the %s is the package version.
        wxPrintf(_("Spek version %s"), PACKAGE_VERSION);
        wxPrintf("\n");
        this->quit = true;
        return true;
    }
    if (parser.GetParamCount()) {
        this->path = parser.GetParam();
    }
    wxString check_dir;
    bool want_check = parser.Found("check", &check_dir);

    this->window = new SpekWindow(this->path);
    this->window->Show(true);
    SetTopWindow(this->window);

    // Queued rather than called directly: the scan posts events back to the
    // panel, which has to be shown and its event loop running first.
    if (want_check && !check_dir.IsEmpty()) {
        this->window->CallAfter([this, check_dir]() {
            this->window->check_folder(check_dir);
        });
    }
    return true;
}

int Spek::OnRun()
{
    if (quit) {
        return 0;
    }

    return wxApp::OnRun();
}

#ifdef OS_OSX
void Spek::MacOpenFiles(const wxArrayString& files)
{
    if (files.GetCount() == 1) {
        this->window->open(files[0]);
    }
}
#endif
