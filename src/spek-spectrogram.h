#pragma once

#include <memory>

#include <wx/wx.h>

#include "spek-palette.h"
#include "spek-pipeline.h"

class Audio;
class FFT;
class SpekHaveSampleEvent;
struct spek_pipeline;

class SpekSpectrogram : public wxWindow
{
public:
    SpekSpectrogram(wxWindow *parent);
    ~SpekSpectrogram();
    void open(const wxString& path);
    void save(const wxString& path);

    // Render any file, at any size, without disturbing what is on screen.
    //
    // Spek's save() captures the on-screen widget, so the picture's size and
    // detail depend on how big the user's window happened to be, and the file has
    // to be the one already loaded. Exporting a whole release needs neither of
    // those, so this drives its own pipeline to completion, draws into an
    // offscreen bitmap, and puts the previous state back. The extension of
    // `out_path` chooses PNG or JPEG.
    bool render_offscreen(const wxString& audio_path, const wxString& out_path,
                          int width, int height, wxString& error);

private:
    void on_char(wxKeyEvent& evt);
    void on_paint(wxPaintEvent& evt);
    void on_size(wxSizeEvent& evt);
    void on_have_sample(SpekHaveSampleEvent& evt);
    void render(wxDC& dc);

    void start();
    void stop();

    void create_palette();

    std::unique_ptr<Audio> audio;
    std::unique_ptr<FFT> fft;
    spek_pipeline *pipeline;
    int streams;
    int stream;
    int channels;
    int channel;
    enum window_function window_function;
    wxString path;
    wxString desc;
    double duration;
    int sample_rate;
    enum palette palette;
    wxImage palette_image;
    wxImage image;
    int prev_width;
    int fft_bits;
    int urange;
    int lrange;
    const int LPAD;
    const int TPAD;
    const int RPAD;
    const int BPAD;
    const int GAP;
    const int RULER;

    DECLARE_EVENT_TABLE()
};
