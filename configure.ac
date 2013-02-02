#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# Metadata

AC_INIT([HTML Help Maker],[0.1.3],[http://savannah.nongnu.org/projects/hhm/],[hhm])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([hhm.c])
AC_CONFIG_HEADER([config.h])

# Programs.

AC_PROG_CC

# Libraries.

AC_CHECK_HEADER([lzx_compress.h],,[AC_MSG_ERROR([Cannot find an LZX compressor, please install http://www.speakeasy.org/~russotto/chm/])])
AC_CHECK_LIB([lzxcomp], [lzx_init],,[AC_MSG_ERROR([Cannot find an LZX compressor, please install http://www.speakeasy.org/~russotto/chm/])],[-lm])

# Header files.

AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([limits.h stdlib.h string.h unistd.h])

# Typedefs, structures, and compiler characteristics.

AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Library functions.

AC_FUNC_CLOSEDIR_VOID
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STAT
AC_CHECK_FUNCS([atexit memset strcasecmp])

# Output

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
