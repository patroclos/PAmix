#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([pamix], [1.5], [jenschjoshua@gmail.com])
AC_CONFIG_SRCDIR([include/entry.hpp])
AC_CONFIG_AUX_DIR([tools])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11()

# Checks for programs.
AC_PROG_CXX

# Check options
AC_MSG_CHECKING([--enable-unicode argument])
AC_ARG_ENABLE([unicode], AS_HELP_STRING([--enable-unicode], [Enable unicode support.]), [enable_unicode=$enableval], [enable_unicode="yes"])
AC_MSG_RESULT([$enable_unicode])
if test "$enable_unicode" = "yes"; then
	AC_DEFINE([FEAT_UNICODE], [1], [Enable unicode support])
fi

# Checks for libraries.

AS_IF([test "$enable_unicode" = "yes"], [PKG_CHECK_MODULES([NCURSES], [ncursesw])], [PKG_CHECK_MODULES([NCURSES], [ncurses])])

AC_SEARCH_LIBS([use_default_colors], [ncurses curses],
	AC_DEFINE(HAVE_USE_DEFAULT_COLORS, 1, [Define to 1 if the curses library has the function `use_default_colors`. ]))

PKG_CHECK_MODULES([PULSEAUDIO], [libpulse])

# Checks for header files.
AC_CHECK_HEADERS([locale.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([memset setlocale])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
