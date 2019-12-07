#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([calfbox],[0.0.3],[wdev@foltman.com])
AC_CONFIG_HEADER([config.h])
LT_INIT([])
LT_LANG([C])

AM_INIT_AUTOMAKE(1.8)

if test "x$prefix" = "xNONE"; then 
  prefix=$ac_default_prefix
fi

# Checks for programs.
AC_PROG_CC_C99
AC_PROG_INSTALL
PKG_PROG_PKG_CONFIG

# Checks for headers.
AC_HEADER_STDC

# Set initial parameters
PYTHON_ENABLED="yes"
NCURSES_ENABLED="yes"
JACK_ENABLED="yes"
FLUIDSYNTH_ENABLED="yes"
LIBSMF_ENABLED="yes"
LIBUSB_ENABLED="yes"
SSE_ENABLED="no"
NEON_ENABLED="no"

# Check options

AC_MSG_CHECKING([whether to enable Python embedding])
AC_ARG_WITH(python,
AC_HELP_STRING([--without-python],[disable Python embedding]),
  [if test "$withval" = "no"; then PYTHON_ENABLED="no"; fi],[])
AC_MSG_RESULT($PYTHON_ENABLED)

AC_MSG_CHECKING([whether to enable ncurses UI])
AC_ARG_WITH(ncurses,
AC_HELP_STRING([--without-ncurses],[disable ncurses ui]),
  [if test "$withval" = "no"; then NCURSES_ENABLED="no"; fi],[])
AC_MSG_RESULT($NCURSES_ENABLED)

AC_MSG_CHECKING([whether to enable JACK I/O])
AC_ARG_WITH(jack,
AC_HELP_STRING([--without-jack],[disable JACK audio and MIDI]),
  [if test "$withval" = "no"; then JACK_ENABLED="no"; fi],[])
AC_MSG_RESULT($JACK_ENABLED)

AC_MSG_CHECKING([whether to enable Fluidsynth])
AC_ARG_WITH(fluidsynth,
AC_HELP_STRING([--without-fluidsynth],[disable use of Fluidsynth]),
  [if test "$withval" = "no"; then FLUIDSYNTH_ENABLED="no"; fi],[])
AC_MSG_RESULT($FLUIDSYNTH_ENABLED)

AC_MSG_CHECKING([whether to enable libsmf])
AC_ARG_WITH(libsmf,
AC_HELP_STRING([--without-libsmf],[disable use of libsmf]),
  [if test "$withval" = "no"; then LIBSMF_ENABLED="no"; fi],[])
AC_MSG_RESULT($LIBSMF_ENABLED)

AC_MSG_CHECKING([whether to enable libusb])
AC_ARG_WITH(libusb,
AC_HELP_STRING([--without-libusb],[disable use of libusb]),
  [if test "$withval" = "no"; then LIBUSB_ENABLED="no"; fi],[])
AC_MSG_RESULT($LIBUSB_ENABLED)

AC_MSG_CHECKING([whether to enable SSE (x86 family only)])
AC_ARG_WITH(sse,
AC_HELP_STRING([--with-sse],[enable use of SSE]),
  [if test "$withval" = "yes"; then SSE_ENABLED="yes"; fi],[])
AC_MSG_RESULT($SSE_ENABLED)

AC_MSG_CHECKING([whether to enable NEON (ARM family only)])
AC_ARG_WITH(neon,
AC_HELP_STRING([--with-neon],[enable use of NEON]),
  [if test "$withval" = "yes"; then NEON_ENABLED="yes"; fi],[])
AC_MSG_RESULT($NEON_ENABLED)

# Check dependencies
AC_CHECK_HEADER(uuid/uuid.h, true, AC_MSG_ERROR([libuuid header (uuid/uuid.h) is required]))
AC_CHECK_LIB(uuid, uuid_unparse, true, AC_MSG_ERROR([libuuid is required]))
PKG_CHECK_MODULES(GLIB_DEPS, glib-2.0 >= 2.6, true, AC_MSG_ERROR([libglib-2.0 is required]))
if test "$LIBUSB_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(LIBUSB_DEPS, libusb-1.0 >= 1.0, true, AC_MSG_ERROR([libusb-1.0 is required]))
fi
PKG_CHECK_MODULES(LIBSNDFILE_DEPS, sndfile, true, AC_MSG_ERROR([libsndfile is required]))
if test "$NCURSES_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(NCURSES_DEPS, ncurses, true, AC_MSG_ERROR([libncurses is required]))
fi

if test "$FLUIDSYNTH_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(FLUIDSYNTH_DEPS, fluidsynth >= 1.0.8, true, AC_MSG_ERROR([fluidsynth 1.0.8 is required]))
fi

if test "$LIBSMF_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(LIBSMF_DEPS, smf >= 1.3, true, AC_MSG_ERROR([libsmf 1.3 is required (libsmf.sourceforge.net)]))
fi

if test "$JACK_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(JACK_DEPS, jack >= 0.116.0, true, AC_MSG_ERROR([JACK is required (or use --without-jack)]))
    AC_CHECK_HEADER(jack/jack.h, true, AC_MSG_ERROR([JACK is required (or use --without-jack)]))
    PKG_CHECK_MODULES(JACK_RENAME_PORT, jack >= 0.124.2 jack < 1.9.0, JACK_HAS_RENAME="yes", JACK_HAS_RENAME="no")
    PKG_CHECK_MODULES(JACK2_RENAME_PORT, jack >= 1.9.11, JACK_HAS_RENAME="yes", JACK_HAS_RENAME_DUMMY="no")
fi

if test "$PYTHON_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(PYTHON_DEPS, python3 >= 3.0, true, AC_MSG_ERROR([python 3.0 or newer is required (or use --without-python)]))
fi

# Generate Automake conditionals
AM_CONDITIONAL(USE_PYTHON, test "$PYTHON_ENABLED" = "yes")
AM_CONDITIONAL(USE_NCURSES, test "$NCURSES_ENABLED" = "yes")
AM_CONDITIONAL(USE_JACK, test "$JACK_ENABLED" = "yes")
AM_CONDITIONAL(USE_FLUIDSYNTH, test "$FLUIDSYNTH_ENABLED" = "yes")
AM_CONDITIONAL(USE_LIBSMF, test "$LIBSMF_ENABLED" = "yes")
AM_CONDITIONAL(USE_LIBUSB, test "$LIBUSB_ENABLED" = "yes")
AM_CONDITIONAL(USE_SSE, test "$SSE_ENABLED" = "yes")
AM_CONDITIONAL(USE_NEON, test "$NEON_ENABLED" = "yes")

# Generate config.h conditionals
if test "$PYTHON_ENABLED" = "yes"; then
    AC_DEFINE(USE_PYTHON, 1, [Python will be included])
fi
if test "$NCURSES_ENABLED" = "yes"; then
    AC_DEFINE(USE_NCURSES, 1, [ncurses will be included])
fi
if test "$JACK_ENABLED" = "yes"; then
    AC_DEFINE(USE_JACK, 1, [JACK I/O will be included])
fi
if test "$JACK_HAS_RENAME" = "yes"; then
  AC_DEFINE(JACK_HAS_RENAME, 1, [JACK function jack_port_rename should be used instead of jack_port_set_name])
fi
if test "$FLUIDSYNTH_ENABLED" = "yes"; then
    AC_DEFINE(USE_FLUIDSYNTH, 1, [Fluidsynth will be included])
fi
if test "$LIBSMF_ENABLED" = "yes"; then
    AC_DEFINE(USE_LIBSMF, 1, [libsmf will be used])
fi
if test "$LIBUSB_ENABLED" = "yes"; then
    AC_DEFINE(USE_LIBUSB, 1, [libusb will be used])
fi
if test "$LIBUSB_ENABLED" = "no" && test "$JACK_ENABLED" = "no"; then
    AC_MSG_ERROR([Neither JACK nor libusb are enabled, at least one is required])
fi
if test "$SSE_ENABLED" = "yes"; then
    AC_DEFINE(USE_SSE, 1, [x86 Streaming SIMD Extensions will be used])
fi
if test "$NEON_ENABLED" = "yes"; then
    AC_DEFINE(USE_NEON, 1, [ARM NEON SIMD Extensions will be used])
fi

# Generate files
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
