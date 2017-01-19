bin_PROGRAMS = game_enf_offline

BUILT_SOURCES = parser_offline_input.h

AM_YFLAGS = -d

game_enf_offline_CPPFLAGS = -I$(srcdir)/../../include
game_enf_offline_SOURCES = main.c \
						   scanner_offline_input.l \
						   parser_offline_input.y

game_enf_offline_LDADD = ../libenforcer.la

