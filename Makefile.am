INCLUDES = -I$(srcdir)

bin_PROGRAMS = calfbox

calfbox_SOURCES = config.c fluid.c hwcfg.c io.c main.c module.c tonewheel.c
calfbox_LDADD = $(JACK_DEPS_LIBS) $(GLIB_DEPS_LIBS) $(FLUIDSYNTH_DEPS_LIBS)

AM_CFLAGS = $(JACK_DEPS_CFLAGS) $(GLIB_DEPS_CFLAGS) $(FLUIDSYNTH_DEPS_CFLAGS)

noinst_HEADERS = config-api.h dspmath.h hwcfg.h io.h midi.h module.h onepole-int.h

EXTRA_DIST = cboxrc-example

