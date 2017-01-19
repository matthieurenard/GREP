BUILT_SOURCES = parser.h

AM_YFLAGS = -d

bin_PROGRAMS = print_game

print_game_CPPFLAGS = -I$(srcdir)/../include

print_game_SOURCES = print_game.c \
					 game_graph.c \
					 game_graph.h \
					 parser.y \
					 scanner.l

