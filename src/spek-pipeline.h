#pragma once

#include <memory>
#include <string>

class AudioFile;
class FFTPlan;
struct spek_pipeline;

enum window_function {
    WINDOW_HANN,
    WINDOW_HAMMING,
    WINDOW_BLACKMAN_HARRIS,
    WINDOW_COUNT,
    WINDOW_DEFAULT = WINDOW_HANN,
};

typedef void (*spek_pipeline_cb)(int bands, int sample, float *values, void *cb_data);

struct spek_pipeline * spek_pipeline_open(
    std::unique_ptr<AudioFile> file,
    std::unique_ptr<FFTPlan> fft,
    int stream,
    int channel,
    enum window_function window_function,
    int samples,
    spek_pipeline_cb cb,
    void *cb_data
);

void spek_pipeline_start(struct spek_pipeline *pipeline);

// Wait for the pipeline to finish reading the whole file.
//
// spek_pipeline_close() *cancels* — it raises `quit` before joining, which is
// right for a window showing a file the user just navigated away from, and wrong
// for rendering a picture that has to be complete. Offscreen export calls this
// first, then close().
void spek_pipeline_finish(struct spek_pipeline *pipeline);

void spek_pipeline_close(struct spek_pipeline *pipeline);

std::string spek_pipeline_desc(const struct spek_pipeline *pipeline);
int spek_pipeline_streams(const struct spek_pipeline *pipeline);
int spek_pipeline_channels(const struct spek_pipeline *pipeline);
double spek_pipeline_duration(const struct spek_pipeline *pipeline);
int spek_pipeline_sample_rate(const struct spek_pipeline *pipeline);
