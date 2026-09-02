#pragma once

#include <wx/wx.h>

class SpekHaveSampleEvent: public wxEvent
{
public:
    SpekHaveSampleEvent(int bands, int sample, float *values, bool free_values);
    SpekHaveSampleEvent(const SpekHaveSampleEvent& other);
    ~SpekHaveSampleEvent();

    int get_bands() const { return this->bands; }
    int get_sample() const { return this->sample; }
    const float *get_values() const { return this->values; }

    wxEvent *Clone() const { return new SpekHaveSampleEvent(*this); }

private:
    int bands;
    int sample;
    float *values;
    bool free_values;
};

typedef void (wxEvtHandler::*SpekHaveSampleEventFunction)(SpekHaveSampleEvent&);

// Declared with the modern macro so this builds against both a shared and a
// static wxWidgets; the 2.8-era DECLARE_EVENT_TYPE resolves to dllimport
// under WXUSINGDLL and cannot link.
wxDECLARE_EVENT(SPEK_HAVE_SAMPLE, SpekHaveSampleEvent);

#define SPEK_EVT_HAVE_SAMPLE(fn) \
    wx__DECLARE_EVT0(SPEK_HAVE_SAMPLE, \
    (wxObjectEventFunction) (SpekHaveSampleEventFunction) &fn)
