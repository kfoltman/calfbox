INCLUDES = -I$(srcdir)

bin_PROGRAMS = calfbox

calfbox_SOURCES = app.c chorus.c cmd.c config.c delay.c eq.c errors.c fbr.c fluid.c fxchain.c hwcfg.c instr.c io.c jackinput.c layer.c main.c master.c menu.c menuitem.c midi.c module.c pattern.c pattern-maker.c phaser.c procmain.c reverb.c sampler.c scene.c scripting.c sfzloader.c sfzparser.c song.c streamplay.c tonectl.c tonewheel.c ui.c
calfbox_LDADD = $(JACK_DEPS_LIBS) $(GLIB_DEPS_LIBS) $(FLUIDSYNTH_DEPS_LIBS) $(PYTHON_DEPS_LIBS)

AM_CFLAGS = $(JACK_DEPS_CFLAGS) $(GLIB_DEPS_CFLAGS) $(FLUIDSYNTH_DEPS_CFLAGS) $(PYTHON_DEPS_CFLAGS)

noinst_HEADERS = app.h biquad-float.h cmd.h config-api.h dspmath.h eq.h errors.h hwcfg.h instr.h io.h layer.h master.h menu.h menuitem.h midi.h module.h onepole-int.h onepole-float.h pattern.h pattern-maker.h procmain.h sampler.h scene.h scripting.h sfzparser.h song.h ui.h

EXTRA_DIST = cboxrc-example

