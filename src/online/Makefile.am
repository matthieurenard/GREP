BUILT_SOURCES = ../parser.h

AM_YFLAGS = -d

bin_PROGRAMS = game_enf_online

game_enf_online_CPPFLAGS = -I$(srcdir)/../../include -I$(builddir)/..
game_enf_online_SOURCES = main.c \
						  ../clock.c \
						  ../dbmutils.c \
						  ../game_graph.c \
						  ../print_game.c \
						  ../parser.y \
						  ../scanner.l \
						  ../../include/clock.h \
						  ../../include/dbmutils.h \
						  ../../include/print_game.h \
						  ../../include/game_graph.h \
						  ../parser.h
