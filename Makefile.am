AM_CPPFLAGS = -I$(srcdir) -Wall -Wsign-compare -D_GNU_SOURCE

AM_CFLAGS = $(JACK_DEPS_CFLAGS) $(GLIB_DEPS_CFLAGS) $(FLUIDSYNTH_DEPS_CFLAGS) $(PYTHON_DEPS_CFLAGS) $(LIBSMF_DEPS_CFLAGS) $(LIBSNDFILE_DEPS_CFLAGS) $(LIBUSB_DEPS_CFLAGS) $(ARCH_OPT_CFLAGS) $(NCURSES_DEPS_CFLAGS)

lib_LTLIBRARIES = libcalfbox.la

bin_PROGRAMS = calfbox
noinst_PROGRAMS = calfbox_tests

calfbox_SOURCES = \
    appmenu.c \
    main.c \
    menu.c \
    menuitem.c \
    ui.c

calfbox_LDADD = libcalfbox.la $(JACK_DEPS_LIBS) $(GLIB_DEPS_LIBS) $(FLUIDSYNTH_DEPS_LIBS) $(PYTHON_DEPS_LIBS) $(LIBSMF_DEPS_LIBS) $(LIBSNDFILE_DEPS_LIBS) $(LIBUSB_DEPS_LIBS) $(NCURSES_DEPS_LIBS) -lpthread -luuid -lm -lrt

calfbox_tests_SOURCES = \
    tests.c

calfbox_tests_LDADD = libcalfbox.la $(GLIB_DEPS_LIBS) -lpthread -lm -lrt

libcalfbox_la_SOURCES = \
    app.c \
    auxbus.c \
    blob.c \
    chorus.c \
    cmd.c \
    compressor.c \
    config-api.c \
    delay.c \
    distortion.c \
    dom.c \
    engine.c \
    eq.c \
    errors.c \
    fbr.c \
    fifo.c \
    fluid.c \
    fuzz.c \
    fxchain.c \
    gate.c \
    hwcfg.c \
    instr.c \
    io.c \
    jackinput.c \
    jackio.c \
    layer.c \
    limiter.c \
    master.c \
    meter.c \
    midi.c \
    mididest.c \
    module.c \
    pattern.c \
    pattern-maker.c \
    phaser.c \
    prefetch_pipe.c \
    recsrc.c \
    reverb.c \
    rt.c \
    sampler.c \
    sampler_channel.c \
    sampler_gen.c \
    sampler_layer.c \
    sampler_nif.c \
    sampler_prevoice.c \
    sampler_prg.c \
    sampler_rll.c \
    sampler_voice.c \
    scene.c \
    scripting.c \
    seq.c \
    seq-adhoc.c \
    sfzloader.c \
    sfzparser.c \
    song.c \
    streamplay.c \
    streamrec.c \
    tarfile.c \
    tonectl.c \
    tonewheel.c \
    track.c \
    usbaudio.c \
    usbio.c \
    usbmidi.c \
    usbprobe.c \
    wavebank.c

libcalfbox_la_LIBADD = $(JACK_DEPS_LIBS) $(GLIB_DEPS_LIBS) $(FLUIDSYNTH_DEPS_LIBS) $(PYTHON_DEPS_LIBS) $(LIBSMF_DEPS_LIBS) $(LIBSNDFILE_DEPS_LIBS) $(LIBUSB_DEPS_LIBS) -lpthread -luuid -lm -lrt

if USE_SSE
ARCH_OPT_CFLAGS=-msse -ffast-math
else
if USE_NEON
ARCH_OPT_CFLAGS=-mfloat-abi=hard -mfpu=neon -ffast-math
endif
endif

noinst_HEADERS = \
    app.h \
    auxbus.h \
    biquad-float.h \
    blob.h \
    cmd.h \
    config-api.h \
    dom.h \
    dspmath.h \
    envelope.h \
    engine.h \
    eq.h \
    errors.h \
    fifo.h \
    hwcfg.h \
    instr.h \
    io.h \
    ioenv.h \
    layer.h \
    master.h \
    menu.h \
    menuitem.h \
    meter.h \
    midi.h \
    mididest.h \
    module.h \
    onepole-int.h \
    onepole-float.h \
    pattern.h \
    pattern-maker.h \
    prefetch_pipe.h \
    recsrc.h \
    rt.h \
    sampler.h \
    sampler_impl.h \
    sampler_layer.h \
    sampler_prg.h \
    scene.h \
    scripting.h \
    seq.h \
    sfzloader.h \
    sfzparser.h \
    song.h \
    stm.h \
    tarfile.h \
    tests.h \
    track.h \
    ui.h \
    usbio_impl.h \
    wavebank.h

EXTRA_DIST = cboxrc-example
