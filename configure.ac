#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([calfbox],[0.0.3],[wdev@foltman.com])
AC_CONFIG_HEADER([config.h])
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
JACK_ENABLED="yes"
FLUIDSYNTH_ENABLED="yes"
LIBSMF_ENABLED="yes"
SSE_ENABLED="no"

# Check options

AC_MSG_CHECKING([whether to enable Python embedding])
AC_ARG_WITH(python,
AC_HELP_STRING([--without-python],[disable Python embedding]),
  [if test "$withval" = "no"; then PYTHON_ENABLED="no"; fi],[])
AC_MSG_RESULT($PYTHON_ENABLED)

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

AC_MSG_CHECKING([whether to enable SSE (x86 family only)])
AC_ARG_WITH(sse,
AC_HELP_STRING([--with-sse],[enable use of SSE]),
  [if test "$withval" = "yes"; then SSE_ENABLED="yes"; fi],[])
AC_MSG_RESULT($SSE_ENABLED)

# Check dependencies
AC_CHECK_HEADER(uuid/uuid.h, true, AC_MSG_ERROR([libuuid header (uuid/uuid.h) is required]))
AC_CHECK_LIB(uuid, uuid_unparse, true, AC_MSG_ERROR([libuuid is required]))
PKG_CHECK_MODULES(GLIB_DEPS, glib-2.0 >= 2.6, true, AC_MSG_ERROR([libglib-2.0 is required]))
PKG_CHECK_MODULES(LIBUSB_DEPS, libusb-1.0 >= 1.0, true, AC_MSG_ERROR([libusb-1.0 is required]))
PKG_CHECK_MODULES(LIBSNDFILE_DEPS, sndfile, true, AC_MSG_ERROR([libsndfile is required]))

if test "$FLUIDSYNTH_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(FLUIDSYNTH_DEPS, fluidsynth >= 1.0.8, true, AC_MSG_ERROR([fluidsynth 1.0.8 is required]))
fi

if test "$LIBSMF_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(LIBSMF_DEPS, smf >= 1.3, true, AC_MSG_ERROR([libsmf 1.3 is required (libsmf.sourceforge.net)]))
fi

if test "$JACK_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(JACK_DEPS, jack >= 0.116.0, true, AC_MSG_ERROR([JACK is required (or use --without-jack)]))
    AC_CHECK_HEADER(jack/jack.h, true, AC_MSG_ERROR([JACK is required (or use --without-jack)]))
fi

if test "$PYTHON_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(PYTHON_DEPS, python3 >= 3.0, true, AC_MSG_ERROR([python 3.0 or newer is required (or use --without-python)]))
fi

# Generate Automake conditionals
AM_CONDITIONAL(USE_PYTHON, test "$PYTHON_ENABLED" = "yes")
AM_CONDITIONAL(USE_JACK, test "$JACK_ENABLED" = "yes")
AM_CONDITIONAL(USE_FLUIDSYNTH, test "$FLUIDSYNTH_ENABLED" = "yes")
AM_CONDITIONAL(USE_LIBSMF, test "$LIBSMF_ENABLED" = "yes")
AM_CONDITIONAL(USE_SSE, test "$SSE_ENABLED" = "yes")

# Generate config.h conditionals
if test "$PYTHON_ENABLED" = "yes"; then
    AC_DEFINE(USE_PYTHON, 1, [Python will be included])
fi
if test "$JACK_ENABLED" = "yes"; then
    AC_DEFINE(USE_JACK, 1, [JACK I/O will be included])
fi
if test "$FLUIDSYNTH_ENABLED" = "yes"; then
    AC_DEFINE(USE_FLUIDSYNTH, 1, [Fluidsynth will be included])
fi
if test "$LIBSMF_ENABLED" = "yes"; then
    AC_DEFINE(USE_LIBSMF, 1, [libsmf will be used])
fi
if test "$SSE_ENABLED" = "yes"; then
    AC_DEFINE(USE_SSE, 1, [x86 Streaming SIMD Extensions will be used])
fi

# Generate files
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
