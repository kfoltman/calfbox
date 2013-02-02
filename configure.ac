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

# Checks for headers.
AC_HEADER_STDC

# Set initial parameters
PYTHON_ENABLED="yes"

# Check options

AC_ARG_WITH(python,
AC_HELP_STRING([--without-python],[disable Python embedding]),
  [if test "$withval" = "no"; then PYTHON_ENABLED="no"; fi],[])
AC_MSG_RESULT($PYTHON_ENABLED)


# Check dependencies
PKG_CHECK_MODULES(JACK_DEPS, jack >= 0.116.0, true, AC_MSG_ERROR([JACK is required]))
AC_CHECK_HEADER(jack/jack.h, true, AC_MSG_ERROR([JACK is required]))
AC_CHECK_HEADER(uuid/uuid.h, true, AC_MSG_ERROR([libuuid header (uuid/uuid.h) is required]))
AC_CHECK_LIB(uuid, uuid_unparse, true, AC_MSG_ERROR([libuuid is required]))
PKG_CHECK_MODULES(GLIB_DEPS, glib-2.0 >= 2.6, true, AC_MSG_ERROR([libglib-2.0 is required]))
PKG_CHECK_MODULES(FLUIDSYNTH_DEPS, fluidsynth >= 1.0.8, true, AC_MSG_ERROR([fluidsynth 1.0.8 is required]))
PKG_CHECK_MODULES(LIBUSB_DEPS, libusb-1.0 >= 1.0, true, AC_MSG_ERROR([libusb-1.0 is required]))
PKG_CHECK_MODULES(LIBSMF_DEPS, smf >= 1.3, true, AC_MSG_ERROR([libsmf 1.3 is required (libsmf.sourceforge.net)]))
PKG_CHECK_MODULES(LIBSNDFILE_DEPS, sndfile, true, AC_MSG_ERROR([libsndfile is required]))

if test "$PYTHON_ENABLED" = "yes"; then
    PKG_CHECK_MODULES(PYTHON_DEPS, python-2.7 >= 2.7, true, AC_MSG_ERROR([python 2.7 or newer is required (or use --without-python)]))
fi

# Generate Automake conditionals
AM_CONDITIONAL(USE_PYTHON, test "$PYTHON_ENABLED" = "yes")

# Generate config.h conditionals
if test "$PYTHON_ENABLED" = "yes"; then
    AC_DEFINE(USE_PYTHON, 1, [Python will be included])
fi


# Generate files
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
