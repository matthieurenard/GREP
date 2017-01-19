bin_PROGRAMS = game_enf_offline

BUILT_SOURCES = ../parser.h \
				parser_offline_input.h

AM_YFLAGS = -d

game_enf_offline_CPPFLAGS = -I$(srcdir)/../../include -I$(builddir)/..
game_enf_offline_SOURCES =	main.c \
							parser_offline_input.y \
							scanner_offline_input.l \
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


