#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([pamix], [1.2], [jenschjoshua@gmail.com])
AC_CONFIG_SRCDIR([include/entry.hpp])
AC_CONFIG_AUX_DIR([tools])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11(,[mandatory])

# Checks for programs.
AC_PROG_CXX

# Checks for libraries.
PKG_CHECK_MODULES([NCURSESW], [ncursesw])
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
