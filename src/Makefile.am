SUBDIRS = . online offline

noinst_LTLIBRARIES = libenforcer.la

AM_YFLAGS = -d

BUILT_SOURCES = parser.h

libenforcer_la_CPPFLAGS = -I$(srcdir)/../include
libenforcer_la_SOURCES = clock.c \
						dbmutils.c \
						game_graph.c \
						print_game.c \
						serialize.c \
						scanner.l \
						parser.y \
						clock.h \
						dbmutils.h \
						game_graph.h \
						print_game.h \
						serialize.h 

