#ifndef GAME_GRAPH_H
#define GAME_GRAPH_H

#include <list.h>
#include <stdio.h>

struct Node;
struct Graph;
struct Enforcer;

enum EdgeType {EMIT, STOPEMIT, CONTRCVD, UNCONTRCVD, TIMELPSD};
enum Strat {STRAT_EMIT, STRAT_DONTEMIT};

struct Edge
{
	enum EdgeType type;
	struct Node *succ;
};

struct Event
{
	const char *label;
};

struct Graph *graph_newFromAutomaton(const char *filename);
const struct List *graph_nodes(const struct Graph *);
void graph_free(struct Graph *);

const char *node_stateLabel(const struct Node *);
const char *node_word(const struct Node *);
int node_owner(const struct Node *);
const char *node_getConstraints(const struct Node *);
int node_isAccepting(const struct Node *);
int node_isInitial(const struct Node *);
int node_isWinning(const struct Node *);
enum Strat node_strat(const struct Node *);
void node_setData(struct Node *, void *);
void *node_getData(const struct Node *);
const struct List *node_edges(const struct Node *);

struct Enforcer *enforcer_new(const struct Graph *, FILE *);
enum Strat enforcer_getStrat(const struct Enforcer *);
void enforcer_eventRcvd(struct Enforcer *, const struct Event *);
void enforcer_emit(struct Enforcer *);
void enforcer_free(struct Enforcer *);

#endif

