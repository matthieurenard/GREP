#ifndef PRINT_GAME_H
#define PRINT_GAME_H

#include <stdio.h>
#include "game_graph.h"

void drawGraph(const struct Graph *, FILE *);
void drawZoneGraph(const struct ZoneGraph *, FILE *);

#endif
