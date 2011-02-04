#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.63)
AC_INIT([calfbox],[0.0.2],[wdev@foltman.com])
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

PKG_CHECK_MODULES(JACK_DEPS, jack >= 0.116.0, true, AC_MSG_ERROR([JACK is required]))
AC_CHECK_HEADER(jack/jack.h, true, AC_MSG_ERROR([JACK is required]))
PKG_CHECK_MODULES(GLIB_DEPS, glib-2.0 >= 2.6, true, AC_MSG_ERROR([libglib-2.0 is required]))

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
