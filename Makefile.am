INCLUDES = -I$(srcdir)

bin_PROGRAMS = calfbox

calfbox_SOURCES = io.c main.c module.c
calfbox_LDADD = $(JACK_DEPS_LIBS)

AM_CXXFLAGS = $(JACK_DEPS_CFLAGS)

noinst_HEADERS = dspmath.h io.h module.h
