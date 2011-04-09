INCLUDES = -I$(srcdir)

bin_PROGRAMS = calfbox

calfbox_SOURCES = chorus.c config.c delay.c eq.c fluid.c hwcfg.c instr.c io.c main.c menu.c module.c phaser.c procmain.c tonectl.c tonewheel.c streamplay.c ui.c
calfbox_LDADD = $(JACK_DEPS_LIBS) $(GLIB_DEPS_LIBS) $(FLUIDSYNTH_DEPS_LIBS)

AM_CFLAGS = $(JACK_DEPS_CFLAGS) $(GLIB_DEPS_CFLAGS) $(FLUIDSYNTH_DEPS_CFLAGS)

noinst_HEADERS = biquad-float.h config-api.h dspmath.h hwcfg.h instr.h io.h menu.h midi.h module.h onepole-int.h onepole-float.h procmain.h ui.h

EXTRA_DIST = cboxrc-example

