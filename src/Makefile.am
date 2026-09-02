noinst_LIBRARIES = libspek.a

libspek_a_SOURCES = \
	spek-audio.cc \
	spek-audio.h \
	spek-fft.cc \
	spek-fft.h \
	spek-gate.cc \
	spek-gate.h \
	spek-gate-decode.cc \
	spek-gate-scan.cc \
	spek-gate-scan.h \
	spek-palette.cc \
	spek-palette.h \
	spek-pipeline.cc \
	spek-pipeline.h \
	spek-utils.cc \
	spek-utils.h

libspek_a_CPPFLAGS = \
	-include config.h \
	-pthread \
	$(WX_CPPFLAGS)

libspek_a_CXXFLAGS = \
	$(AVFORMAT_CFLAGS) \
	$(AVCODEC_CFLAGS) \
	$(AVUTIL_CFLAGS) \
	$(WX_CXXFLAGS_ONLY)

bin_PROGRAMS = spek

spek_SOURCES = \
	spek-artwork.cc \
	spek-artwork.h \
	spek-events.cc \
	spek-events.h \
	spek-gate-panel.cc \
	spek-gate-panel.h \
	spek-platform.cc \
	spek-platform.h \
	spek-preferences-dialog.cc \
	spek-preferences-dialog.h \
	spek-preferences.cc \
	spek-preferences.h \
	spek-ruler.cc \
	spek-ruler.h \
	spek-spectrogram.cc \
	spek-spectrogram.h \
	spek-window.cc \
	spek-window.h \
	spek.cc

spek_CPPFLAGS = \
	-include config.h \
	-pthread \
	$(WX_CPPFLAGS)

spek_CXXFLAGS = \
	$(WX_CXXFLAGS_ONLY)

spek_LDADD = \
	libspek.a \
	$(AVFORMAT_LIBS) \
	$(AVCODEC_LIBS) \
	$(AVUTIL_LIBS) \
	$(WX_LIBS)

spek_LDFLAGS = \
	-pthread

# The icon and the DPI-aware manifest. windres output is an object file, so it
# is linked in rather than passed as a flag.
if OS_WIN
spek_LDADD += spek-res.$(OBJEXT)
spek_DEPENDENCIES = spek-res.$(OBJEXT)

spek-res.$(OBJEXT): $(top_srcdir)/dist/win/spek.rc
	$(WX_RESCOMP) -I$(top_srcdir)/dist/win $(top_srcdir)/dist/win/spek.rc -O coff -o $@

CLEANFILES = spek-res.$(OBJEXT)
endif
