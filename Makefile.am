AM_CPPFLAGS = -I$(srcdir) -Wall -D_GNU_SOURCE

bin_PROGRAMS = calfbox

calfbox_SOURCES = \
    app.c \
    appmenu.c \
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
    main.c \
    master.c \
    menu.c \
    menuitem.c \
    meter.c \
    midi.c \
    mididest.c \
    module.c \
    pattern.c \
    pattern-maker.c \
    phaser.c \
    recsrc.c \
    reverb.c \
    rt.c \
    sampler.c \
    sampler_gen.c \
    sampler_layer.c \
    sampler_prg.c \
    scene.c \
    scripting.c \
    seq.c \
    sfzloader.c \
    sfzparser.c \
    song.c \
    streamplay.c \
    streamrec.c \
    tonectl.c \
    tonewheel.c \
    track.c \
    ui.c \
    usbaudio.c \
    usbio.c \
    usbmidi.c \
    usbprobe.c \
    wavebank.c

calfbox_LDADD = $(JACK_DEPS_LIBS) $(GLIB_DEPS_LIBS) $(FLUIDSYNTH_DEPS_LIBS) $(PYTHON_DEPS_LIBS) $(LIBSMF_DEPS_LIBS) $(LIBSNDFILE_DEPS_LIBS) $(LIBUSB_DEPS_LIBS) -lncurses -lpthread -luuid

AM_CFLAGS = $(JACK_DEPS_CFLAGS) $(GLIB_DEPS_CFLAGS) $(FLUIDSYNTH_DEPS_CFLAGS) $(PYTHON_DEPS_CFLAGS) $(LIBSMF_DEPS_CFLAGS) $(LIBSNDFILE_DEPS_CFLAGS) $(LIBUSB_DEPS_CFLAGS)

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
    track.h \
    ui.h \
    usbio_impl.h \
    wavebank.h

EXTRA_DIST = cboxrc-example

