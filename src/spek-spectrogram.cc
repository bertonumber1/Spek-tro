#include <cmath>

#include <wx/dcbuffer.h>

#include "spek-audio.h"
#include "spek-events.h"
#include "spek-fft.h"
#include "spek-platform.h"
#include "spek-ruler.h"
#include "spek-utils.h"

#include "spek-spectrogram.h"

BEGIN_EVENT_TABLE(SpekSpectrogram, wxWindow)
    EVT_CHAR(SpekSpectrogram::on_char)
    EVT_PAINT(SpekSpectrogram::on_paint)
    EVT_SIZE(SpekSpectrogram::on_size)
    SPEK_EVT_HAVE_SAMPLE(SpekSpectrogram::on_have_sample)
END_EVENT_TABLE()

enum
{
    MIN_RANGE = -140,
    MAX_RANGE = 0,
    URANGE = 0,
    LRANGE = -120,
    FFT_BITS = 11,
    MIN_FFT_BITS = 8,
    MAX_FFT_BITS = 14,
};

// Forward declarations.
static wxString trim(wxDC& dc, const wxString& s, int length, bool trim_end);
static int bits_to_bands(int bits);

SpekSpectrogram::SpekSpectrogram(wxWindow *parent) :
    wxWindow(
        parent, -1, wxDefaultPosition, wxDefaultSize,
        wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS
    ),
    audio(new Audio()), // TODO: refactor
    fft(new FFT()),
    pipeline(NULL),
    streams(0),
    stream(0),
    channels(0),
    channel(0),
    window_function(WINDOW_DEFAULT),
    duration(0.0),
    sample_rate(0),
    palette(PALETTE_DEFAULT),
    palette_image(),
    image(1, 1),
    prev_width(-1),
    fft_bits(FFT_BITS),
    urange(URANGE),
    lrange(LRANGE),
    LPAD(this->FromDIP(60)),
    TPAD(this->FromDIP(60)),
    RPAD(this->FromDIP(90)),
    BPAD(this->FromDIP(40)),
    GAP(this->FromDIP(10)),
    RULER(this->FromDIP(10))
{
    this->create_palette();

    SetBackgroundStyle(wxBG_STYLE_CUSTOM);
    SetFocus();
}

SpekSpectrogram::~SpekSpectrogram()
{
    this->stop();
}

void SpekSpectrogram::open(const wxString& path)
{
    this->path = path;
    this->stream = 0;
    this->channel = 0;
    start();
    Refresh();
}

void SpekSpectrogram::save(const wxString& path)
{
    wxSize size = GetClientSize();
    wxBitmap bitmap(size.GetWidth(), size.GetHeight());
    wxMemoryDC dc(bitmap);
    render(dc);
    bitmap.SaveFile(path, wxBITMAP_TYPE_PNG);
}

void SpekSpectrogram::on_char(wxKeyEvent& evt)
{
    switch (evt.GetKeyCode()) {
    case 'c':
        if (this->channels) {
            this->channel = (this->channel + 1) % this->channels;
        }
        break;
    case 'C':
        if (this->channels) {
            this->channel = (this->channel - 1 + this->channels) % this->channels;
        }
        break;
    case 'f':
        this->window_function = (enum window_function) ((this->window_function + 1) % WINDOW_COUNT);
        break;
    case 'F':
        this->window_function =
            (enum window_function) ((this->window_function - 1 + WINDOW_COUNT) % WINDOW_COUNT);
        break;
    case 'l':
        this->lrange = spek_min(this->lrange + 1, this->urange - 1);
        break;
    case 'L':
        this->lrange = spek_max(this->lrange - 1, MIN_RANGE);
        break;
    case 'p':
        this->palette = (enum palette) ((this->palette + 1) % PALETTE_COUNT);
        this->create_palette();
        break;
    case 'P':
        this->palette = (enum palette) ((this->palette - 1 + PALETTE_COUNT) % PALETTE_COUNT);
        this->create_palette();
        break;
    case 's':
        if (this->streams) {
            this->stream = (this->stream + 1) % this->streams;
        }
        break;
    case 'S':
        if (this->streams) {
            this->stream = (this->stream - 1 + this->streams) % this->streams;
        }
        break;
    case 'u':
        this->urange = spek_min(this->urange + 1, MAX_RANGE);
        break;
    case 'U':
        this->urange = spek_max(this->urange - 1, this->lrange + 1);
        break;
    case 'w':
        this->fft_bits = spek_min(this->fft_bits + 1, MAX_FFT_BITS);
        this->create_palette();
        break;
    case 'W':
        this->fft_bits = spek_max(this->fft_bits - 1, MIN_FFT_BITS);
        this->create_palette();
        break;
    default:
        evt.Skip();
        return;
    }

    start();
    Refresh();
}

void SpekSpectrogram::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    render(dc);
}

void SpekSpectrogram::on_size(wxSizeEvent&)
{
    wxSize size = GetClientSize();
    bool width_changed = this->prev_width != size.GetWidth();
    this->prev_width = size.GetWidth();

    if (width_changed) {
        start();
    }
}

void SpekSpectrogram::on_have_sample(SpekHaveSampleEvent& event)
{
    int bands = event.get_bands();
    int sample = event.get_sample();
    const float *values = event.get_values();

    if (sample == -1) {
        this->stop();
        return;
    }

    // TODO: check image size, quit if wrong.
    double range = this->urange - this->lrange;
    for (int y = 0; y < bands; y++) {
        double value = fmin(this->urange, fmax(this->lrange, values[y]));
        double level = (value - this->lrange) / range;
        uint32_t color = spek_palette(this->palette, level);
        this->image.SetRGB(
            sample,
            bands - y - 1,
            color >> 16,
            (color >> 8) & 0xFF,
            color & 0xFF
        );
    }

    // TODO: refresh only one pixel column
    this->Refresh();
}

static wxString time_formatter(int unit)
{
    // TODO: i18n
    return wxString::Format("%d:%02d", unit / 60, unit % 60);
}

static wxString freq_formatter(int unit)
{
    return wxString::Format(_("%d kHz"), unit / 1000);
}

static wxString density_formatter(int unit)
{
    return wxString::Format(_("%d dB"), -unit);
}

void SpekSpectrogram::render(wxDC& dc)
{
    wxSize size = GetClientSize();
    int w = size.GetWidth();
    int h = size.GetHeight();

    // Initialise.
    dc.SetBackground(*wxBLACK_BRUSH);
    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.SetPen(*wxWHITE_PEN);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetTextForeground(wxColour(255, 255, 255));
    wxFont normal_font = wxFont(
        (int)round(9 * spek_platform_font_scale()),
        wxFONTFAMILY_SWISS,
        wxFONTSTYLE_NORMAL,
        wxFONTWEIGHT_NORMAL
    );
    wxFont large_font = wxFont(normal_font);
    large_font.SetPointSize((int)round(10 * spek_platform_font_scale()));
    large_font.SetWeight(wxFONTWEIGHT_BOLD);
    wxFont small_font = wxFont(normal_font);
    small_font.SetPointSize((int)round(8 * spek_platform_font_scale()));
    dc.SetFont(normal_font);
    int normal_height = dc.GetTextExtent("dummy").GetHeight();
    dc.SetFont(large_font);
    int large_height = dc.GetTextExtent("dummy").GetHeight();
    dc.SetFont(small_font);
    int small_height = dc.GetTextExtent("dummy").GetHeight();

    // Clean the background.
    dc.Clear();

    // Spek version
    dc.SetFont(large_font);
    wxString package_name(PACKAGE_NAME);
    dc.DrawText(
        package_name,
        w - RPAD + GAP,
        TPAD - 2 * GAP - normal_height - large_height
    );
    int package_name_width = dc.GetTextExtent(package_name + " ").GetWidth();
    dc.SetFont(small_font);
    dc.DrawText(
        PACKAGE_VERSION,
        w - RPAD + GAP + package_name_width,
        TPAD - 2 * GAP - normal_height - small_height
    );

    if (this->image.GetWidth() > 1 && this->image.GetHeight() > 1 &&
        w - LPAD - RPAD > 0 && h - TPAD - BPAD > 0) {
        // Draw the spectrogram.
        wxBitmap bmp(this->image.Scale(w - LPAD - RPAD, h - TPAD - BPAD));
        dc.DrawBitmap(bmp, LPAD, TPAD);

        // File name.
        dc.SetFont(large_font);
        dc.DrawText(
            trim(dc, this->path, w - LPAD - RPAD, false),
            LPAD,
            TPAD - 2 * GAP - normal_height - large_height
        );

        // File properties.
        dc.SetFont(normal_font);
        dc.DrawText(
            trim(dc, this->desc, w - LPAD - RPAD, true),
            LPAD,
            TPAD - GAP - normal_height
        );

        // Prepare to draw the rulers.
        dc.SetFont(small_font);

        if (this->duration) {
            // Time ruler.
            int time_factors[] = {1, 2, 5, 10, 20, 30, 1*60, 2*60, 5*60, 10*60, 20*60, 30*60, 0};
            SpekRuler time_ruler(
                LPAD,
                h - BPAD,
                SpekRuler::BOTTOM,
                // TODO: i18n
                "00:00",
                time_factors,
                0,
                (int)this->duration,
                1.5,
                (w - LPAD - RPAD) / this->duration,
                0.0,
                time_formatter
                );
            time_ruler.draw(dc);
        }

        if (this->sample_rate) {
            // Frequency ruler.
            int freq = this->sample_rate / 2;
            int freq_factors[] = {1000, 2000, 5000, 10000, 20000, 0};
            SpekRuler freq_ruler(
                LPAD,
                TPAD,
                SpekRuler::LEFT,
                // TRANSLATORS: keep "00" unchanged, it's used to calc the text width
                _("00 kHz"),
                freq_factors,
                0,
                freq,
                3.0,
                (h - TPAD - BPAD) / (double)freq,
                0.0,
                freq_formatter
                );
            freq_ruler.draw(dc);
        }
    }

    // Border around the spectrogram.
    dc.DrawRectangle(LPAD, TPAD, w - LPAD - RPAD, h - TPAD - BPAD);

    // The palette.
    if (h - TPAD - BPAD > 0) {
        wxBitmap bmp(this->palette_image.Scale(RULER, h - TPAD - BPAD + 1));
        dc.DrawBitmap(bmp, w - RPAD + GAP, TPAD);

        // Prepare to draw the ruler.
        dc.SetFont(small_font);

        // Spectral density.
        int density_factors[] = {1, 2, 5, 10, 20, 50, 0};
        SpekRuler density_ruler(
            w - RPAD + GAP + RULER,
            TPAD,
            SpekRuler::RIGHT,
            // TRANSLATORS: keep "-00" unchanged, it's used to calc the text width
            _("-00 dB"),
            density_factors,
            -this->urange,
            -this->lrange,
            3.0,
            (h - TPAD - BPAD) / (double)(this->lrange - this->urange),
            h - TPAD - BPAD,
            density_formatter
        );
        density_ruler.draw(dc);
    }
}

static void pipeline_cb(int bands, int sample, float *values, void *cb_data)
{
    SpekHaveSampleEvent event(bands, sample, values, false);
    SpekSpectrogram *s = (SpekSpectrogram *)cb_data;
    wxPostEvent(s, event);
}

void SpekSpectrogram::start()
{
    if (this->path.IsEmpty()) {
        return;
    }

    this->stop();

    // The number of samples is the number of pixels available for the image.
    // The number of bands is fixed, FFT results are very different for
    // different values but we need some consistency.
    wxSize size = GetClientSize();
    int samples = size.GetWidth() - LPAD - RPAD;
    if (samples > 0) {
        this->image.Create(samples, bits_to_bands(this->fft_bits));
        this->pipeline = spek_pipeline_open(
            this->audio->open(std::string(this->path.utf8_str()), this->stream),
            this->fft->create(this->fft_bits),
            this->stream,
            this->channel,
            this->window_function,
            samples,
            pipeline_cb,
            this
        );
        spek_pipeline_start(this->pipeline);
        // TODO: extract conversion into a utility function.
        this->desc = wxString::FromUTF8(spek_pipeline_desc(this->pipeline).c_str());
        this->streams = spek_pipeline_streams(this->pipeline);
        this->channels = spek_pipeline_channels(this->pipeline);
        this->duration = spek_pipeline_duration(this->pipeline);
        this->sample_rate = spek_pipeline_sample_rate(this->pipeline);
    } else {
        this->image.Create(1, 1);
    }
}

void SpekSpectrogram::stop()
{
    if (this->pipeline) {
        spek_pipeline_close(this->pipeline);
        this->pipeline = NULL;

        // Make sure all have_sample events are processed before returning.
        wxApp::GetInstance()->ProcessPendingEvents();
    }
}

void SpekSpectrogram::create_palette()
{
    this->palette_image.Create(RULER, bits_to_bands(this->fft_bits));
    for (int y = 0; y < bits_to_bands(this->fft_bits); y++) {
        uint32_t color = spek_palette(this->palette, y / (double)bits_to_bands(this->fft_bits));
        this->palette_image.SetRGB(
            wxRect(0, bits_to_bands(this->fft_bits) - y - 1, RULER, 1),
            color >> 16,
            (color >> 8) & 0xFF,
            color & 0xFF
        );
    }
}

// Trim `s` so that it fits into `length`.
static wxString trim(wxDC& dc, const wxString& s, int length, bool trim_end)
{
    if (length <= 0) {
        return wxEmptyString;
    }

    // Check if the entire string fits.
    wxSize size = dc.GetTextExtent(s);
    if (size.GetWidth() <= length) {
        return s;
    }

    // Binary search FTW!
    wxString fix("...");
    int i = 0;
    int k = s.length();
    while (k - i > 1) {
        int j = (i + k) / 2;
        size = dc.GetTextExtent(trim_end ? s.substr(0, j) + fix : fix + s.substr(j));
        if (trim_end != (size.GetWidth() > length)) {
            i = j;
        } else {
            k = j;
        }
    }

    return trim_end ? s.substr(0, i) + fix : fix + s.substr(k);
}

// TODO: test
static int bits_to_bands(int bits) {
    return (1 << (bits - 1)) + 1;
}

// ---- offscreen rendering --------------------------------------------------------

namespace {

// Writes straight into the image instead of posting an event per column. The
// offscreen render runs the pipeline to completion before drawing anything, so
// there is no window to repaint and nothing to marshal to the GUI thread.
struct OffscreenSink
{
    wxImage *image;
    int urange;
    int lrange;
    enum palette palette;
};

void offscreen_cb(int bands, int sample, float *values, void *cb_data)
{
    OffscreenSink *sink = (OffscreenSink *)cb_data;
    if (sample < 0 || sample >= sink->image->GetWidth()) {
        return;
    }
    double range = sink->urange - sink->lrange;
    for (int y = 0; y < bands && y < sink->image->GetHeight(); y++) {
        double value = fmin(sink->urange, fmax(sink->lrange, values[y]));
        double level = (value - sink->lrange) / range;
        uint32_t color = spek_palette(sink->palette, level);
        sink->image->SetRGB(sample, bands - y - 1,
                            color >> 16, (color >> 8) & 0xFF, color & 0xFF);
    }
}

} // namespace

bool SpekSpectrogram::render_offscreen(const wxString& audio_path, const wxString& out_path,
                                       int width, int height, wxString& error)
{
    int samples = width - LPAD - RPAD;
    if (samples <= 0) {
        error = _("export size is too small");
        return false;
    }

    // Everything render() reads from the object, so the on-screen file can be put
    // back exactly as it was.
    wxString saved_path = this->path;
    wxString saved_desc = this->desc;
    wxImage saved_image = this->image;
    double saved_duration = this->duration;
    int saved_sample_rate = this->sample_rate;
    int saved_streams = this->streams;
    int saved_channels = this->channels;
    int saved_stream = this->stream;
    int saved_channel = this->channel;

    // A half-drawn on-screen render must not keep writing into members we are
    // about to borrow.
    this->stop();

    bool ok = false;
    wxImage work(samples, bits_to_bands(this->fft_bits));
    OffscreenSink sink{&work, this->urange, this->lrange, this->palette};

    spek_pipeline *p = spek_pipeline_open(
        this->audio->open(std::string(audio_path.utf8_str()), 0),
        this->fft->create(this->fft_bits),
        0,
        0,
        this->window_function,
        samples,
        offscreen_cb,
        &sink
    );

    if (p) {
        spek_pipeline_start(p);
        spek_pipeline_finish(p);   // wait for the whole file, do not cancel it

        this->path = audio_path;
        this->image = work;
        this->desc = wxString::FromUTF8(spek_pipeline_desc(p).c_str());
        this->streams = spek_pipeline_streams(p);
        this->channels = spek_pipeline_channels(p);
        this->duration = spek_pipeline_duration(p);
        this->sample_rate = spek_pipeline_sample_rate(p);
        this->stream = 0;
        this->channel = 0;

        if (this->sample_rate > 0) {
            wxBitmap bitmap(width, height);
            {
                wxMemoryDC dc(bitmap);
                render(dc);
            }
            wxBitmapType type = out_path.Lower().EndsWith(".jpg") ||
                                out_path.Lower().EndsWith(".jpeg")
                                    ? wxBITMAP_TYPE_JPEG
                                    : wxBITMAP_TYPE_PNG;
            ok = bitmap.SaveFile(out_path, type);
            if (!ok) {
                error = _("could not write the image file");
            }
        } else {
            error = _("could not decode that file");
        }
        spek_pipeline_close(p);
    } else {
        error = _("could not open that file");
    }

    // Put the window back the way it was, whatever happened above.
    this->path = saved_path;
    this->desc = saved_desc;
    this->image = saved_image;
    this->duration = saved_duration;
    this->sample_rate = saved_sample_rate;
    this->streams = saved_streams;
    this->channels = saved_channels;
    this->stream = saved_stream;
    this->channel = saved_channel;
    if (!this->path.IsEmpty()) {
        start();
    }
    Refresh();

    return ok;
}
