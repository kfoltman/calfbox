INCLUDES = -I$(srcdir)

bin_PROGRAMS = calfbox

calfbox_SOURCES = config.c fluid.c hwcfg.c io.c main.c menu.c module.c procmain.c tonectl.c tonewheel.c streamplay.c
calfbox_LDADD = $(JACK_DEPS_LIBS) $(GLIB_DEPS_LIBS) $(FLUIDSYNTH_DEPS_LIBS)

AM_CFLAGS = $(JACK_DEPS_CFLAGS) $(GLIB_DEPS_CFLAGS) $(FLUIDSYNTH_DEPS_CFLAGS)

noinst_HEADERS = config-api.h dspmath.h hwcfg.h io.h menu.h midi.h module.h onepole-int.h onepole-float.h procmain.h

EXTRA_DIST = cboxrc-example

