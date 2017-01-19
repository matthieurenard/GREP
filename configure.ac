#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([enfrocer_game], [1.0], [mrenard@labri.fr])
AC_CONFIG_AUX_DIR([autotools])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([src/print_game.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_YACC
AC_PROG_LEX

LT_INIT

# Checks for libraries.
AC_CHECK_LIB([ccontainers], [list_new], ,
	[AC_MSG_ERROR([Cannot link against ccontainers, please set LDFLAGS according 
	 to your installation (LDFLAGS=-L/path/to/libccontainers/directory)])])
AC_CHECK_LIB([l], [yywrap]) 
AC_CHECK_LIB([gvc], [gvRender], ,
	 [AC_MSG_ERROR([Cannot link against libgvc. Please add the path to libgvc to 
	  LDFLAGS (option -L)])])
AC_CHECK_LIB([cgraph], [agset], ,
	 [AC_MSG_ERROR([Cannot link against libcgraph. Please add the path to 
	  libcgraph to LDFLAGS (option -L)])])

# Checks for header files.
AC_FUNC_ALLOCA
AC_CHECK_HEADERS([libintl.h malloc.h stddef.h stdlib.h string.h])
AC_CHECK_HEADER([gvc.h], , [AC_MSG_ERROR([cannot find gvc.h. Please set CPPFLAGS\
 according to your installation of GVC. One might want to add `pkg-config libgvc\
 --cflags` to CPPFLAGS and `pkg-config libgvc --libs` to LDLIBS.])])
AC_CHECK_HEADER([list.h], , [AC_MSG_ERROR([Cannot find list.h, provided by\
 libccontainers. Set CPPFLAGS (and probably LDFLAGS) according to your\
 installation])])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([strdup])

AC_CONFIG_FILES([Makefile
				 src/Makefile])
AC_OUTPUT
