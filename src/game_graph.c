#include "game_graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

#include <list.h>
#include <set.h>
#include <fifo.h>
#include <tree.h>

#include "parser.h"
#include "dbmutils.h"
#include "serialize.h"

#define EVENTSEP		""
#define EVENTSEPSIZE	0	/* strlen(EVENTSEP) */
#define NBSUCCS			256
#define CANARY			0xdeadbeefdeadbeefUL
#define NBLEAVESTYPES	3


enum ContType {CONTROLLABLE, UNCONTROLLABLE};
enum LeavesType {LEAVES_GOOD, LEAVES_BADSTOP, LEAVES_BADEMIT};

struct SymbolTableEl
{
	char *sym;
	unsigned int id;
	char c;
	int size;
	int index;
};

struct State
{
	unsigned int parserStateId;
	char *name;
	int isInitial;
	int isAccepting;
	/* StateEdge[] */
	struct List *contSuccs[NBSUCCS];
	/* StateEdge[] */
	struct List *uncontSuccs[NBSUCCS];
	unsigned int index;
};

struct StateEdge
{
	const struct State *to;
	struct Dbmw *dbm;
	/* Clock[] */
	struct List *resets;
};

struct TimedAutomaton
{
	struct State *states;
	struct State *sinkBadState;
	unsigned int nbStates;
	struct Clock **clocks;
	unsigned int nbClocks;
	/* SymbolTableEl[] */
	const struct List *contsTable;
	/* SymbolTableEl[] */
	const struct List *uncontsTable;
	/* --> Graph.contsEls */
	const struct SymbolTableEl **contsEls;
	/* --> Graph.uncontsEls */
	const struct SymbolTableEl **uncontsEls;
};

struct Zone
{
	const struct State *s;
	struct Dbmw *dbm;
	struct Zone **contSuccs;
	/* Clock *[] */
	struct List **resetsConts;
	struct Zone **uncontSuccs;
	/* Clock *[] */
	struct List **resetsUnconts;
	struct Zone *timeSucc;
	/* ZoneEdge[] */
	struct List *edges;
	unsigned int index;
	const struct TimedAutomaton *a;
	const struct ZoneGraph *zg;
	void *userData;
	char *name;
};

struct ZoneGraph
{
	const struct TimedAutomaton *a;
	/* Zone[] */
	struct List *zones;
	/* Zone *[] */
	struct List **zonesS;
	struct Zone *z0;
	struct Zone *sinkZone;
	unsigned int nbZones;
};

struct ArrayTwo
{
	unsigned int allocSize;
	unsigned int size;
	int **tab;
};

struct StringArray
{
	char *s;
	struct ArrayTwo *array;
	unsigned int size;
	const struct Zone **lasts;
};

typedef struct Node
{
	const struct Zone *z;
	char *word;
	const struct Graph *g;
	char *realWord;
	int owner;
	union
	{
		struct
		{
			struct Node *succEmit;
			struct Node *succStopEmit;
			/* Node *[] */
			struct List *predsEmit;
			/* Node *[] */
			struct List *predsUncont;
			struct Node *predContRcvd;
			struct Node *predTime;
			enum Strat strat;
		} p0;
		struct
		{
			struct Node **succsCont;
			struct Node **succsUncont;
			struct Node *predStop;
			struct Node *succTime;
		} p1;
	};
	int isAccepting;
	int isInitial;
	int isWinning;
	int isLeaf;
	unsigned int index;
	/* Edge *[] */
	struct List *edges;
	void *userData;
} Node;

struct SearchNode
{
	const struct Zone *z;
	char *word;
	int owner;
};

struct Graph
{
	/* SymbolTableEl *[] */
	struct List *contsTable;
	/* SymbolTableEl *[] */
	struct List *uncontsTable;
	struct SymbolTableEl *contsEls[256];
	struct SymbolTableEl *uncontsEls[256];
	char *contsChars;
	char *uncontsChars;
	struct TimedAutomaton *a;
	struct ZoneGraph *zoneGraph;
	/* Node *[] */
	struct List *nodes;
	/* Node *[][2] */
	struct List *nodesP[2];
	struct Node **baseNodes;
	unsigned int nbNodes;
	unsigned int nbConts;
	unsigned int nbUnconts;
	unsigned int nbZones;
};

struct StratNode
{
	const struct Node *n;
	int score;
	struct List *strats;
	struct ListIterator *events;
};

struct Enforcer
{
	const struct Graph *g;
	const struct Node *stratNode;
	const struct Node *realNode;
	struct List *realBuffer;
	struct Fifo *input;
	struct Fifo *output;
	FILE *log;
	int32_t *valuation;
	unsigned int date;
	struct List *strats;
	struct StratNode *strat;
	struct List *leaves[NBLEAVESTYPES];
	struct List *badLeaves;
};

struct TimedEvent
{
	unsigned int date;
	char *event;
};

struct PrivateEvent
{
	char c;
	unsigned int index;
	enum ContType type;
};

struct Clock **clocks;

/* Shortcuts functions for ccontainers */
static int listIn(struct List *l, void *data);
static void listAddNoDouble(struct List *l, void *data);
static void removeSetFromList(struct List *l, const struct Set *s);

/* Comparison functions used with list_search */
#if 0
static int cmpSymbolChar(const void *val, const void *pel);
#endif
static int cmpSymbolLabel(const void *val, const void *pel);
static int cmpSymId(const void *val, const void *pel);
static int cmpEdge(const void *pe1, const void *pe2);
static int cmpZoneIndex(const void *val, const void *pz);
static int cmpNodeIndex(const void *val, const void *pn);
static int eqPtr(const void *p1, const void *p2);

/* To be used with set_applyToAll */
static void node_setWinning(void *dummy, void *pn);
static void node_computeStrat(void *dummy,void *pn);

/*SymbolTableEl */
static struct SymbolTableEl *symbolTableEl_new(char, const char *, unsigned 
		int);
static void symbolTableEl_save(const struct SymbolTableEl *, FILE *);
static struct SymbolTableEl *symbolTableEl_load(FILE *, const struct Graph *);
static void symbolTableEl_free(struct SymbolTableEl *el);

/* Node */
static struct Node *node_new(const struct Graph *, const struct Zone *, const 
		char *word, int owner);
static char *node_computeRealWord(const struct Node *);
static struct Node *node_succCont(const struct Graph *, const struct Node *prev, 
		char cont);
static void node_addEdgeNoDouble(struct Node *n, enum EdgeType type, struct Node 
		*succ);
static void node_addEdgeStop(struct Node *n, struct Node *succ);
static void node_addEdgeCont(struct Node *n, struct Node *succ, int);
static void node_addEdgeUncont(struct Node *n, struct Node *succ, int);
static void node_addEdgeEmit(struct Node *n, struct Node *succ);
static void node_addEdgeTime(struct Node *n, struct Node *succ);
static void node_save(const struct Node *, FILE *);
static struct List *node_loadAll(FILE *, const struct Graph *g);
static void node_free(Node *n);

/* Edge */
static struct Edge *edge_new(enum EdgeType type, struct Node *n);
static void edge_free(struct Edge *e);

/* State */
static void state_save(const struct State *, FILE *);
static void state_loadAll(FILE *, struct TimedAutomaton *a);
static void state_free(struct State *s);

/* StateEdge */
static struct StateEdge *stateEdge_new(const struct TimedAutomaton *, const 
		struct State *, const struct List *guards, const struct List *resets);
static void stateEdge_save(const struct StateEdge *, FILE *);
static struct StateEdge *stateEdge_load(FILE *, const struct TimedAutomaton *);
static void stateEdge_free(struct StateEdge *);

/* Zone */
static struct Zone *zone_new(const struct State *, struct Dbmw *, const struct 
		ZoneGraph *);
static struct Zone *zone_newcp(const struct Zone *);
static int zone_areEqual(const struct Zone *, const struct Zone *);
static struct Zone *zone_nextCont(const struct Zone *, char);
static void zone_addEdge(struct Zone *, const struct Zone *, enum EdgeType );
static void zone_save(const struct Zone *, FILE *);
static struct List *zone_loadAll(FILE *, const struct ZoneGraph *, const struct 
		Graph *);
static void zone_free(struct Zone *);

/* ZoneEdge */
static struct ZoneEdge *zoneEdge_new(enum EdgeType, const struct Zone *);
static int zoneEdge_cmp(const struct ZoneEdge *, const struct ZoneEdge *);
static void zoneEdge_free(struct ZoneEdge *);

/* TimedAutomaton */
static struct TimedAutomaton *timedAutomaton_new(const struct List *contsTable, 
		const struct SymbolTableEl *contsEls[], const struct List *uncontsTable, 
		const struct SymbolTableEl *uncontsEls[], const struct List *states, 
		const struct List *clocks, const struct List *edges);
static void timedAutomaton_save(const struct TimedAutomaton *, FILE *);
static struct TimedAutomaton *timedAutomaton_load(FILE *, const struct Graph *);
static void timedAutomaton_free(struct TimedAutomaton *);

/* ArrayTwo */
static struct ArrayTwo *arraytwo_new(unsigned int size, int defaultVal);
static struct ArrayTwo *arraytwo_newcp(const struct ArrayTwo *);
static void arraytwo_cp(struct ArrayTwo *, const struct ArrayTwo *);
static int arraytwo_cmp(const struct ArrayTwo *, const struct ArrayTwo *);
static void arraytwo_free(struct ArrayTwo *);

/* StringArray */
static struct StringArray *stringArray_new(const struct Graph *);
static struct StringArray *stringArray_newcp(const struct StringArray *);
static struct StringArray *stringArray_newNext(const struct StringArray *prev, 
		const struct Graph *, char);
static void stringArray_free(struct StringArray *);

/* ZoneGraph */
static struct ZoneGraph *zoneGraph_new(const struct TimedAutomaton *);
static struct List *zoneGraph_splitZones2(const struct Zone *, const struct Zone 
		*);
static struct List *zoneGraph_splitZones(const struct Zone *z, const struct List 
		*rho);
static struct List *zoneGraph_pre(const struct Zone *z, const struct List *rho);
static struct List *zoneGraph_post(const struct Zone *z, struct List *rho);
static void zoneGraph_save(const struct ZoneGraph *, FILE *);
static struct ZoneGraph *zoneGraph_load(FILE *, const struct Graph *);
static void zoneGraph_free(struct ZoneGraph *);

/* Graph */
static void graph_createChars(const struct List *l, struct List **psymbolTable, 
		char **pchars, struct SymbolTableEl *[256]);
static struct Tree *graph_computeStrings(const struct Graph *);
static void graph_computeTree(struct Tree *, const struct Graph *);
static void graph_addNodes(struct Graph *, struct Tree *);
static void graph_addNodesRec(struct Graph *g, const struct Zone *z, const 
		struct Tree *stringArrays, struct Node *pred);
static void graph_addEmitEdges(struct Graph *);
static void graph_addUncontEdges(struct Graph *);
static void graph_addTimeEdges(struct Graph *);
static unsigned int graph_contIndex(const struct Graph *g, char c);
static void graph_computeW0(struct Graph *g, struct Set *ret);
static void graph_attr(struct Set *ret, struct Graph *g, int player, const 
		struct Set *U, struct List *nodes);

/* StratNode */
static struct StratNode *stratNode_new(const struct Node *, int, struct List *, 
		struct ListIterator *);
static void stratNode_free(struct StratNode *);

/* Clock save/load */
static void clock_save(const struct Clock *, FILE *);
static struct Clock *clock_load(FILE *);

/* Enforcer */
static unsigned int enforcer_computeDelay(const struct Enforcer *);
#if 0
static void enforcer_computeStratNode(struct Enforcer *);
#endif
#if 0
static int enforcer_computeStratsRec(struct Enforcer *, const struct Node *, 
		struct List *);
#endif
static void enforcer_computeStrats(struct Enforcer *, int);
static void enforcer_storeCont(struct Enforcer *, const struct SymbolTableEl *);
static void enforcer_passUncont(struct Enforcer *, const struct SymbolTableEl *);

/* -------------------------------------------------------------------------- */

/* Helper functions for ccontainers */
static int listIn(struct List *l, void *data)
{
	return (list_search(l, data, eqPtr) != NULL);
}

static void listAddNoDouble(struct List *l, void *data)
{
	if (!listIn(l, data))
		list_append(l, data);
}

static void removeSetFromList(struct List *l, const struct Set *s)
{
	set_applyToAll(s, (void (*)(void*, void*))list_remove, l);
}


/* Functions to use with list_search */
#if 0
static int cmpSymbolChar(const void *val, const void *pel)
{
	const struct SymbolTableEl *el = pel;
	const char c = *(const char *)val;
	return (el->c == c);
}
#endif

static int cmpSymbolLabel(const void *val, const void *pel)
{
	const struct SymbolTableEl *el = pel;
	const char *label = val;
	return (strcmp(label, el->sym) == 0);
}

static int cmpSymId(const void *val, const void *pel)
{
	const struct SymbolTableEl *el = pel;
	const unsigned int id = *(const unsigned int *)val;
	return (el->id == id);
}

static int cmpEdge(const void *pe1, const void *pe2)
{
	const struct Edge *e1 = pe1;
	const struct Edge *e2 = pe2;
	return (e1->type == e2->type && e1->succ == e2->succ);
}

static int cmpZoneIndex(const void *val, const void *pz)
{
	const struct Zone *z = pz;
	const unsigned int index = *(const int *)val;
	return (z->index == index);
}

static int cmpNodeIndex(const void *val, const void *pn)
{
	const struct Node *n = pn;
	const unsigned int index = *(const int *)val;
	return (n->index == index);
}

static int eqPtr(const void *p1, const void *p2)
{
	return (p1 == p2);
}

/* Used with set_applyToAll */
static void node_setWinning(void *dummy, void *pn)
{
	Node *n = pn;
	(void)dummy;
	n->isWinning = 1;
}

static void node_computeStrat(void *dummy, void *pn)
{
	struct Node *n = pn;
	(void)dummy;
	if (n->owner == 0 && n->p0.succEmit != NULL && n->p0.succEmit->isWinning)
		n->p0.strat = STRAT_EMIT;
}


/* SymbolTableEl */
static struct SymbolTableEl *symbolTableEl_new(char c, const char *sym, unsigned 
		int id)
{
	struct SymbolTableEl *el = malloc(sizeof *el);

	if (el == NULL)
	{
		perror("malloc el");
		exit(EXIT_FAILURE);
	}
	el->c = c;
	el->sym = strdup(sym);
	el->id = id;
	el->size = strlen(el->sym);

	return el;
}

static void symbolTableEl_save(const struct SymbolTableEl *el, FILE *f)
{
	save_uint64(f, (uint64_t)el->index);
	save_string(f, el->sym);
	fprintf(f, "%c", el->c);
}

static struct SymbolTableEl *symbolTableEl_load(FILE *f, const struct Graph *g)
{
	struct SymbolTableEl *el = malloc(sizeof *el);

	if (el == NULL)
	{
		perror("malloc symbolTableEl_load:el");
		exit(EXIT_FAILURE);
	}

	el->index = (int)load_uint64(f);
	el->sym = load_string(f);
	el->c = fgetc(f);
	el->id = -1;
	el->size = strlen(el->sym);

	return el;
}

static void symbolTableEl_free(struct SymbolTableEl *el)
{
	free(el->sym);
	free(el);
}


/* Node */
/* Private interface */
static struct Node *node_new(const struct Graph *g, const struct Zone *z, const 
		char *word, int owner)
{
	static unsigned int nextIndex = 0;
	int i;
	struct Node *n = malloc(sizeof *n);

	if (n == NULL)
	{
		perror("malloc node");
		exit(EXIT_FAILURE);
	}

	n->z = z;
	n->word = strdup(word);
	n->owner = owner;
	n->isAccepting = z->s->isAccepting;
	n->isInitial = (z->s->isInitial && word[0] == '\0' && owner == 0 && 
			dbmw_containsZero(z->dbm));
	n->isWinning = 0;
	n->isLeaf = 0;
	n->edges = list_new();
	n->index = nextIndex++;
	n->userData = NULL;
	n->g = g;
	n->realWord = node_computeRealWord(n);
	if (n->owner == 0)
	{
		n->p0.strat = STRAT_DONTEMIT;
		n->p0.succEmit = NULL;
		n->p0.succStopEmit = NULL;
		n->p0.predContRcvd = NULL;
		n->p0.predTime = NULL;
		n->p0.predsEmit = list_new();
		n->p0.predsUncont = list_new();
	}
	else
	{
		n->p1.predStop = NULL;
		n->p1.succsCont = malloc(g->nbConts * sizeof *(n->p1.succsCont));
		if (n->p1.succsCont == NULL)
		{
			perror("malloc node_new:n->p1.succsCont");
			exit(EXIT_FAILURE);
		}
		for (i = 0 ; i < g->nbConts ; i++)
			n->p1.succsCont[i] = NULL;
		n->p1.succsUncont = malloc(g->nbUnconts * sizeof *(n->p1.succsUncont));
		if (n->p1.succsUncont == NULL)
		{
			perror("malloc node_new:n->p1.succsUncont");
			exit(EXIT_FAILURE);
		}
		for (i = 0 ; i < g->nbUnconts ; i++)
			n->p1.succsUncont[i] = NULL;
		n->p1.succTime = NULL;
	}

	return n;
}

static char *node_computeRealWord(const struct Node *n)
{
	char *label;
	int size = 0, i;
	struct SymbolTableEl **syms;

	syms = malloc(strlen(n->word) * sizeof *syms);
	if (syms == NULL)
	{
		perror("malloc syms");
		exit(EXIT_FAILURE);
	}

	i = 0;
	while (n->word[i] != '\0')
	{
		struct SymbolTableEl *s;
		s = n->g->contsEls[(unsigned char)n->word[i]];
		if (s == NULL)
		{
			fprintf(stderr, "ERROR: could not find symbol associated to %c\n", 
					n->word[i]);
			exit(EXIT_FAILURE);
		}

		size += s->size;
		syms[i++] = s;
	}

	label = malloc(size + 1);
	if (label == NULL)
	{
		perror("malloc label");
		exit(EXIT_FAILURE);
	}
	label[0] = '\0';
	i = 0;
	while (n->word[i] != '\0')
	{
		strcat(label, syms[i]->sym);
		i++;
	}

	free(syms);

	return label;
}

static struct Node *node_succCont(const struct Graph *g, const struct Node 
		*prev, char cont)
{
	if (prev->owner == 0)
		prev = prev->p0.succStopEmit;
	return prev->p1.succsCont[graph_contIndex(g, cont)];
}

static void node_addEdgeNoDouble(struct Node *n, enum EdgeType type, struct Node 
		*succ)
{
	struct Edge searchEdge;
	searchEdge.type = type;
	searchEdge.succ = succ;
	if (list_search(n->edges, &searchEdge, cmpEdge) == NULL)
		list_append(n->edges, edge_new(type, succ));
}

static void node_addEdgeStop(struct Node *n, struct Node *succ)
{
	n->p0.succStopEmit = succ;
	succ->p1.predStop = n;
	node_addEdgeNoDouble(n, STOPEMIT, succ);
	/* If there is no time successor, add the reverse edge, to represent the end 
	 * of the execution */
	if (succ->z->timeSucc == NULL)
		node_addEdgeNoDouble(succ, UNCONTRCVD, n);
}

static void node_addEdgeCont(struct Node *n, struct Node *succ, int i)
{
	n->p1.succsCont[i] = succ;
	succ->p0.predContRcvd = n;
	node_addEdgeNoDouble(n, CONTRCVD, succ);
}

static void node_addEdgeUncont(struct Node *n, struct Node *succ, int i)
{
	n->p1.succsUncont[i] = succ;
	listAddNoDouble(succ->p0.predsUncont, n);
	node_addEdgeNoDouble(n, UNCONTRCVD, succ);
}

static void node_addEdgeEmit(struct Node *n, struct Node *succ)
{
	n->p0.succEmit = succ;
	listAddNoDouble(succ->p0.predsEmit, n);
	node_addEdgeNoDouble(n, EMIT, succ);
}

static void node_addEdgeTime(struct Node *n, struct Node *succ)
{
	n->p1.succTime = succ;
	succ->p0.predTime = n;
	node_addEdgeNoDouble(n, TIMELPSD, succ);
}

static void node_save(const struct Node *n, FILE *f)
{
	save_uint64(f, (uint64_t)n->index);
	save_uint64(f, (uint64_t)(n->isWinning != 0));
	save_uint64(f, (uint64_t)(n->isLeaf != 0));
	save_uint64(f, (uint64_t)n->owner);

	save_string(f, n->word);

	save_uint64(f, (uint64_t)n->z->index);
	if (n->owner == 0)
	{
		save_uint64(f, (uint64_t)n->p0.succStopEmit->index);
		if (n->p0.succEmit != NULL)
			save_uint64(f, (uint64_t)n->p0.succEmit->index);
		else
			save_uint64(f, (uint64_t)-1);
		save_uint64(f, (uint64_t)n->p0.strat);
	}
	else
	{
		int i;

		for (i = 0 ; i < n->g->nbConts ; i++)
		{
			if (n->p1.succsCont[i] != NULL)
				save_uint64(f, (uint64_t)n->p1.succsCont[i]->index);
			else
				save_uint64(f, (uint64_t)-1);
		}
		for (i = 0 ; i < n->g->nbUnconts ; i++)
		{
			save_uint64(f, (uint64_t)n->p1.succsUncont[i]->index);
		}
		if (n->p1.succTime != NULL)
			save_uint64(f, (uint64_t)n->p1.succTime->index);
		else
			save_uint64(f, (uint64_t)-1);
	}
}

static struct List *node_loadAll(FILE *f, const struct Graph *g)
{
	struct List *nodes = list_new();
	int i, j;
	struct ListIterator *it;
	union Succs
	{
		struct
		{
			unsigned int succStop;
			unsigned int succEmit;
			enum Strat strat;
		}p0;
		struct
		{
			unsigned int *succsCont;
			unsigned int *succsUncont;
			unsigned int succTime;
		}p1;
	};
	union Succs *succs = malloc(g->nbNodes * sizeof *succs);

	if (succs == NULL)
	{
		perror("malloc node_loadAll:succs");
		exit(EXIT_FAILURE);
	}

	for (i = 0 ; i < g->nbNodes ; i++)
	{
		struct Node *n;
		unsigned int index, zoneIndex;
		int owner, isWinning, isLeaf;
		struct Zone *z;
		char *word;

		index = (unsigned int)load_uint64(f);
		isWinning = (int)load_uint64(f);
		isLeaf = (int)load_uint64(f);
		owner = (int)load_uint64(f);
		word = load_string(f);
		zoneIndex = (unsigned int)load_uint64(f);
		if (owner == 0)
		{
			succs[index].p0.succStop = (unsigned int)load_uint64(f);
			succs[index].p0.succEmit = (unsigned int)load_uint64(f);
			succs[index].p0.strat = (enum Strat)load_uint64(f);
		}
		else
		{
			succs[index].p1.succsCont = malloc(g->nbConts * sizeof 
					succs->p1.succsCont);
			if (succs[index].p1.succsCont == NULL)
			{
				perror("malloc succs[index].p1.succsCont");
				exit(EXIT_FAILURE);
			}
			succs[index].p1.succsUncont = malloc(g->nbUnconts * sizeof 
					succs->p1.succsUncont);
			if (succs[index].p1.succsUncont == NULL)
			{
				perror("malloc succs[index].p1.succsUncont");
				exit(EXIT_FAILURE);
			}
			for (j = 0 ; j < g->nbConts ; j++)
			{
				succs[index].p1.succsCont[j] = (unsigned int)load_uint64(f);
			}
			for (j = 0 ; j < g->nbUnconts ; j++)
			{
				succs[index].p1.succsUncont[j] = (unsigned int)load_uint64(f);
			}
			succs[index].p1.succTime = (unsigned int)load_uint64(f);
		}
		z = list_search(g->zoneGraph->zones, &zoneIndex, cmpZoneIndex);
		if (z == NULL)
		{
			fprintf(stderr, "ERROR: node_loadAll: no zone of index %u found\n", 
					zoneIndex);
			exit(EXIT_FAILURE);
		}
		n = node_new(g, z, word, owner);
		n->index = index;
		n->isWinning = isWinning;
		n->isLeaf = isLeaf;
		list_append(nodes, n);
		free(word);
	}

	for (it = listIterator_first(nodes) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Node *n = listIterator_val(it);
		if (n->owner == 0)
		{
			struct Node *succ = list_search(nodes, 
					&(succs[n->index].p0.succStop), cmpNodeIndex);
			if (succ == NULL)
			{
				fprintf(stderr, "ERROR: node_loadAll: No node with index %u for"
						" succStop of %u found\n", succs[n->index].p0.succStop, 
						n->index);
				exit(EXIT_FAILURE);
			}
			node_addEdgeStop(n, succ);
			if (succs[n->index].p0.succEmit != -1)
			{
				succ = list_search(nodes, &(succs[n->index].p0.succEmit), 
						cmpNodeIndex);
				if (succ == NULL)
				{
					fprintf(stderr, "ERROR: node_loadAll: No node with index %u" 
							" for succEmit of %u found\n", 
							succs[n->index].p0.succEmit, n->index);
					exit(EXIT_FAILURE);
				}
				node_addEdgeEmit(n, succ);
				n->p0.strat = succs[n->index].p0.strat;
			}
		}
		else
		{
			struct Node *succ;
			for (i = 0 ; i < g->nbConts ; i++)
			{
				if (succs[n->index].p1.succsCont[i] == -1)
					continue;
				succ = list_search(nodes, &(succs[n->index].p1.succsCont[i]), 
						cmpNodeIndex);
				if (succ == NULL)
				{
					fprintf(stderr, "ERROR: node_loadAll: No node with index %u"
							" for succsCont[%d] of %u found.\n", 
							succs[n->index].p1.succsUncont[i], i, n->index);
					exit(EXIT_FAILURE);
				}
				node_addEdgeCont(n, succ, i);
			}
			for (i = 0 ; i < g->nbUnconts ; i++)
			{
				succ = list_search(nodes, &(succs[n->index].p1.succsUncont[i]), 
						cmpNodeIndex);
				if (succ == NULL)
				{
					fprintf(stderr, "ERROR: node_loadAll: No node with index %u"
							" for succsUncont[%d] of %u found.\n", 
							succs[n->index].p1.succsUncont[i], i, n->index);
					exit(EXIT_FAILURE);
				}
				node_addEdgeUncont(n, succ, i);
			}
			if (succs[n->index].p1.succTime == -1)
				n->p1.succTime = NULL;
			else
			{
				succ = list_search(nodes, &(succs[n->index].p1.succTime), 
						cmpNodeIndex);
				if (succ == NULL)
				{
					fprintf(stderr, "ERROR: node_loadAll: No node with index %u"
							" for succTime of %u found.\n", 
							succs[n->index].p1.succTime, n->index);
					exit(EXIT_FAILURE);
				}
				node_addEdgeTime(n, succ);
			}

			free(succs[n->index].p1.succsCont);
			free(succs[n->index].p1.succsUncont);
		}
	}
	listIterator_release(it);

	free(succs);

	return nodes;
}

static void node_free(Node *n)
{
	if (n->owner == 0)
	{
		list_free(n->p0.predsEmit, NULL);
		list_free(n->p0.predsUncont, NULL);
	}
	else
	{
		free(n->p1.succsCont);
		free(n->p1.succsUncont);
	}

	list_free(n->edges, (void (*)(void *))edge_free);

	free(n->word);
	free(n->realWord);
	free(n);
}

/* Node public interface */
const char *node_stateLabel(const struct Node *n)
{
	return n->z->s->name;
}

const char *node_word(const struct Node *n)
{
	return n->realWord;
}

int node_owner(const struct Node *n)
{
	return n->owner;
}

const char *node_getConstraints(const struct Node *n)
{
	return dbmw_sprint(n->z->dbm, n->g->a->clocks);
}

int node_isAccepting(const struct Node *n)
{
	return n->isAccepting;
}

int node_isInitial(const struct Node *n)
{
	return n->isInitial;
}

int node_isWinning(const struct Node *n)
{
	return n->isWinning;
}

enum Strat node_strat(const struct Node *n)
{
	if (n->owner == 0)
		return n->p0.strat;
	else
		fprintf(stderr, "ERROR: node (%s, %s, %d) is owned by player 1, hence " 
				"has no strategy.\n", n->z->s->name, n->realWord, n->owner);
	return STRAT_DONTEMIT;
}

void node_setData(struct Node *n, void *data)
{
	n->userData = data;
}

void *node_getData(const struct Node *n)
{
	return n->userData;
}

const struct List *node_edges(const struct Node *n)
{
	return n->edges;
}


/* Edge */
static struct Edge *edge_new(enum EdgeType type, struct Node *n)
{
	struct Edge *e = malloc(sizeof *e);

	if (e == NULL)
	{
		perror("malloc e");
		exit(EXIT_FAILURE);
	}
	e->type = type;
	e->succ = n;

	return e;
}

static void edge_free(struct Edge *e)
{
	free(e);
}


/* State */
static void state_save(const struct State *s, FILE *f)
{
	struct ListIterator *it;
	unsigned int c;
	
	save_uint64(f, (uint64_t)s->index);
	save_uint64(f, (uint64_t)(s->isInitial != 0));
	save_uint64(f, (uint64_t)(s->isAccepting != 0));
	save_string(f, s->name);

	for (c = 0 ;  c < NBSUCCS ; c++)
	{
		if (s->contSuccs[c] == NULL)
			continue;
		fprintf(f, "%c", c);
		save_uint64(f, (uint64_t)list_size(s->contSuccs[c]));
		for (it = listIterator_first(s->contSuccs[c]) ; listIterator_hasNext(it) 
				; it = listIterator_next(it))
		{
			struct StateEdge *e = listIterator_val(it);
			stateEdge_save(e, f);
		}
		listIterator_release(it);
	}

	for (c = 0 ; c < NBSUCCS ; c++)
	{
		if (s->uncontSuccs[c] == NULL)
			continue;
		fprintf(f, "%c", c);
		save_uint64(f, (uint64_t)list_size(s->uncontSuccs[c]));
		for (it = listIterator_first(s->uncontSuccs[c]) ; 
				listIterator_hasNext(it) ; it = listIterator_next(it))
		{
			struct StateEdge *e = listIterator_val(it);
			stateEdge_save(e, f);
		}
		listIterator_release(it);
	}
}

static void state_loadAll(FILE *f, struct TimedAutomaton *a)
{
	int i, j, k;
	uint64_t nbSuccs;
	unsigned int n;

	/* a->nbStates + 1 because there is the sink state */
	for (i = 0 ; i < a->nbStates + 1 ; i++)
	{
		a->states[i].index = (unsigned int)load_uint64(f);
		a->states[i].isInitial = load_uint64(f) != 0;
		a->states[i].isAccepting = load_uint64(f) != 0;
		a->states[i].name = load_string(f);

		for (j = 0 ; j < NBSUCCS ; j++)
		{
			a->states[i].contSuccs[j] = NULL;
			a->states[i].uncontSuccs[j] = NULL;
		}

		n = list_size(a->contsTable);
		for (j = 0 ; j < n ; j++)
		{
			char c = fgetc(f);
			a->states[i].contSuccs[(unsigned char)c] = list_new();
			nbSuccs = load_uint64(f);
			for (k = 0 ; k < nbSuccs ; k++)
			{
				list_append(a->states[i].contSuccs[(unsigned char)c], 
						stateEdge_load(f, a));
			}
		}

		n = list_size(a->uncontsTable);
		for (j = 0 ; j < n ; j++)
		{
			char c = fgetc(f);
			a->states[i].uncontSuccs[(unsigned char)c] = list_new();
			nbSuccs = load_uint64(f);
			for (k = 0 ; k < nbSuccs ; k++)
			{
				list_append(a->states[i].uncontSuccs[(unsigned char)c], 
						stateEdge_load(f, a));
			}
		}
	}
	a->sinkBadState = &(a->states[a->nbStates]);
}

static void state_free(struct State *s)
{
	int i;

	for (i = 0 ; i < NBSUCCS ; i++)
	{
		if (s->contSuccs[i] != NULL)
			list_free(s->contSuccs[i], NULL);
		if (s->uncontSuccs[i] != NULL)
			list_free(s->uncontSuccs[i], NULL);
	}

	free(s->name);
}


/* StateEdge */
static struct StateEdge *stateEdge_new(const struct TimedAutomaton *a, const 
		struct State *s, const struct List *constraints, const struct List 
		*resets)
{
	struct ListIterator *it;
	struct StateEdge *ret;
	int i;
	struct Clock *clock;

	ret = malloc(sizeof *ret);
	if (ret == NULL)
	{
		perror("malloc stateEdge_new:ret");
		exit(EXIT_FAILURE);
	}

	ret->resets = list_new();
	ret->to = s;
	ret->dbm = dbmw_new(a->nbClocks);

	for (it = listIterator_first(constraints) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		const struct ParserClockConstraint *pconstraint = listIterator_val(it);
		const struct ParserClock *pclock = 
			parserClockConstraint_getClock(pconstraint);
		int pclockId = parserClock_getId(pclock);
		enum ParserRelation rel = parserClockConstraint_getRel(pconstraint);
		int bound = parserClockConstraint_getBound(pconstraint);

		clock = NULL;

		for (i = 1 ; i < a->nbClocks ; i++)
		{
			if (clock_getId(a->clocks[i]) == pclockId)
			{
				clock = a->clocks[i];
				break;
			}
		}
		if (clock == NULL)
		{
			fprintf(stderr, "ERROR: cannot find clock of id %d\n", pclockId);
			exit(EXIT_FAILURE);
		}

		switch (rel)
		{
			case EQ:
				dbmw_constrainClock(ret->dbm, clock, bound);
			break;

			case LT:
			case LEQ:
				dbmw_constrain(ret->dbm, clock, NULL, bound, rel == LT);
			break;

			case GT:
			case GEQ:
				dbmw_constrain(ret->dbm, NULL, clock, -bound, rel == GT);
			break;

			default:
			break;
		}
	}
	listIterator_release(it);

	for (it = listIterator_first(resets) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct ParserClock *pc = listIterator_val(it);
		struct Clock *c = NULL;
		for (i = 0 ; i < a->nbClocks ; i++)
		{
			if (parserClock_getId(pc) == clock_getId(a->clocks[i]))
			{
				c = a->clocks[i];
				break;
			}
		}
		if (c == NULL)
		{
			fprintf(stderr, "ERROR: cannot find clock of ID %d\n", 
					parserClock_getId(pc));
			exit(EXIT_FAILURE);
		}
		else
			list_append(ret->resets, c);
	}
	listIterator_release(it);

	return ret;
}

static void stateEdge_save(const struct StateEdge *e, FILE *f)
{
	struct ListIterator *it;
	dbmw_save(e->dbm, f);
	save_uint64(f, (uint64_t)list_size(e->resets));

	for (it = listIterator_first(e->resets) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Clock *c = listIterator_val(it);
		save_uint64(f, (uint64_t)clock_getIndex(c));
	}
	listIterator_release(it);

	save_uint64(f, (uint64_t)e->to->index);
}

static struct StateEdge *stateEdge_load(FILE *f, const struct TimedAutomaton *a)
{
	struct StateEdge *e = malloc(sizeof *e);
	uint64_t n;
	int i;

	if (e == NULL)
	{
		perror("malloc stateEdge_load:e");
		exit(EXIT_FAILURE);
	}
	
	e->dbm = dbmw_load(f);
	e->resets = list_new();
	n = load_uint64(f);

	for (i = 0 ; i < n ; i++)
	{
		uint64_t clockIndex = load_uint64(f);
		list_append(e->resets, a->clocks[clockIndex]);
	}

	n = load_uint64(f);
	e->to = &(a->states[n]);

	return e;
}

static void stateEdge_free(struct StateEdge *e)
{
	dbmw_free(e->dbm);
	list_free(e->resets, NULL);
	free(e);
}


/* Zone */
/* Zone private interface */
static struct Zone *zone_new(const struct State *s, struct Dbmw *dbm, const
		struct ZoneGraph *zg)
{
	int i, n;
	struct Zone *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc zone_new:ret");
		exit(EXIT_FAILURE);
	}

	ret->dbm = dbm;
	ret->s = s;
	ret->a = zg->a;
	ret->zg = zg;

	ret->timeSucc = NULL;
	n = list_size(ret->a->contsTable);
	ret->contSuccs = malloc(n * sizeof *(ret->contSuccs));
	if (ret->contSuccs == NULL)
	{
		perror("malloc zone_new:ret->contSuccs");
		exit(EXIT_FAILURE);
	}
	ret->resetsConts = malloc(n * sizeof *(ret->resetsConts));
	if (ret->resetsConts == NULL)
	{
		perror("malloc zone_new:ret->resetsConts");
		exit(EXIT_FAILURE);
	}
	for (i = 0 ; i < n ; i++)
	{
		ret->contSuccs[i] = NULL;
		ret->resetsConts[i] = list_new();
	}

	n = list_size(ret->a->uncontsTable);
	ret->uncontSuccs = malloc(n * sizeof *(ret->uncontSuccs));
	if (ret->uncontSuccs == NULL)
	{
		perror("malloc zone_new:ret->uncontSuccs");
		exit(EXIT_FAILURE);
	}
	ret->resetsUnconts = malloc(n * sizeof *(ret->resetsUnconts));
	if (ret->resetsUnconts == NULL)
	{
		perror("malloc zone_new:ret->resetsUnconts");
		exit(EXIT_FAILURE);
	}
	for (i = 0 ; i < n ; i++)
	{
		ret->uncontSuccs[i] = NULL;
		ret->resetsUnconts[i] = list_new();
	}
	ret->edges = list_new();
	ret->userData = NULL;

	return ret;
}

static struct Zone *zone_newcp(const struct Zone *z)
{
	return zone_new(z->s, dbmw_newcp(z->dbm), z->zg);
}

static int zone_areEqual(const struct Zone *z1, const struct Zone *z2)
{
	return (z1->s == z2->s && dbmw_areEqual(z1->dbm, z2->dbm));
}

static struct Zone *zone_nextCont(const struct Zone *z, char c)
{
	struct Zone *ret = z->contSuccs[z->a->contsEls[(unsigned char)c]->index];
	if (ret == NULL)
		ret = z->zg->sinkZone;

	return ret;
}

static void zone_addEdge(struct Zone *z, const struct Zone *succ, enum EdgeType 
		type)
{
	struct ZoneEdge *edge = zoneEdge_new(type, succ);
	if (list_search(z->edges, edge, (int (*)(const void *, const void 
						*))zoneEdge_cmp) == NULL)
		list_append(z->edges, edge);
	else
		zoneEdge_free(edge);
}

static void zone_save(const struct Zone *z, FILE *f)
{
	unsigned int i;
	unsigned int nbConts = list_size(z->a->contsTable);
	unsigned int nbUnconts = list_size(z->a->uncontsTable);

	save_uint64(f, (uint64_t)z->index);
	save_uint64(f, (uint64_t)z->s->index);
	save_uint64(f, (uint64_t)CANARY);
	dbmw_save(z->dbm, f);
	for (i = 0 ; i < nbConts ; i++)
	{
		save_uint64(f, (uint64_t)z->contSuccs[i]->index);
	}
	for (i = 0 ; i < nbConts ; i++)
	{
		struct ListIterator *it;

		save_uint64(f, (uint64_t)list_size(z->resetsConts[i]));
		for (it = listIterator_first(z->resetsConts[i]) ; 
				listIterator_hasNext(it) ; it = listIterator_next(it))
		{
			struct Clock *c = listIterator_val(it);
			save_uint64(f, (uint64_t)clock_getIndex(c));
		}
	}

	for (i = 0 ; i < nbUnconts ; i++)
	{
		save_uint64(f, (uint64_t)z->uncontSuccs[i]->index);
	}
	for (i = 0 ; i < nbUnconts ; i++)
	{
		struct ListIterator *it;

		save_uint64(f, (uint64_t)list_size(z->resetsUnconts[i]));
		for (it = listIterator_first(z->resetsUnconts[i]) ; 
				listIterator_hasNext(it) ; it = listIterator_next(it))
		{
			struct Clock *c = listIterator_val(it);
			save_uint64(f, (uint64_t)clock_getIndex(c));
		}
	}
	if (z->timeSucc != NULL)
		save_uint64(f, (uint64_t)z->timeSucc->index);
	else
		save_uint64(f, (uint64_t)-1);
}

static struct List *zone_loadAll(FILE *f, const struct ZoneGraph *zg, const 
		struct Graph *g)
{
	int i, j, k;
	struct ListIterator *it;
	struct List *zones = list_new();
	unsigned int *contsSuccsIndexes, *uncontsSuccsIndexes, *timeSuccsIndexes;
	unsigned int nbConts = list_size(zg->a->contsTable);
	unsigned int nbUnconts = list_size(zg->a->uncontsTable);

	contsSuccsIndexes = malloc(zg->nbZones * nbConts * sizeof 
			*contsSuccsIndexes);
	if (contsSuccsIndexes == NULL)
	{
		perror("malloc zone_loadAll:contsSuccsIndexes");
		exit(EXIT_FAILURE);
	}

	uncontsSuccsIndexes = malloc(zg->nbZones * nbUnconts * sizeof 
			*uncontsSuccsIndexes);
	if (uncontsSuccsIndexes == NULL)
	{
		perror("malloc zone_loadAll:uncontsSuccsIndexes");
		exit(EXIT_FAILURE);
	}

	timeSuccsIndexes = malloc(zg->nbZones * sizeof *timeSuccsIndexes);
	if (timeSuccsIndexes == NULL)
	{
		perror("malloc zone_loadAll:timeSuccsIndexes");
		exit(EXIT_FAILURE);
	}

	for (i = 0 ; i < zg->nbZones ; i++)
	{
		struct Zone *z;
		struct State *s;
		struct Dbmw *dbm;
		unsigned int index, stateIndex, nbResets;
		char *printedZone;
		uint64_t n;

		index = (unsigned int)load_uint64(f);
		stateIndex = (unsigned int)load_uint64(f);
		s = &(zg->a->states[stateIndex]);
		if ((n = load_uint64(f)) != CANARY)
		{
			fprintf(stderr, "ERROR: Canary %lx expected, %lx instead\n", CANARY, 
					n);
			exit(EXIT_FAILURE);
		}
		dbm = dbmw_load(f);
		z = zone_new(s, dbm, zg);
		z->index = index;
		for (j = 0 ; j < nbConts ; j++)
		{
			contsSuccsIndexes[index * nbConts + j] = (unsigned 
					int)load_uint64(f);
		}
		// TODO Boucle 0 --> nbConts
		for (j = 0 ; j < nbConts ; j++)
		{
			nbResets = (unsigned int)load_uint64(f);
			for (k = 0 ; k < nbResets ; k++)
			{
				unsigned int clockIndex = (unsigned int)load_uint64(f);
				list_append(z->resetsConts[j], zg->a->clocks[clockIndex]);
			}
		}
		for (j = 0 ; j < nbUnconts ; j++)
		{
			uncontsSuccsIndexes[index * nbUnconts + j] = (unsigned 
					int)load_uint64(f);
		}
		for (j = 0 ; j < nbUnconts ; j++)
		{
			nbResets = (unsigned int)load_uint64(f);
			for (k = 0 ; k < nbResets ; k++)
			{
				unsigned int clockIndex = (unsigned int)load_uint64(f);
				list_append(z->resetsUnconts[j], zg->a->clocks[clockIndex]);
			}
		}
		timeSuccsIndexes[i] = (unsigned int)load_uint64(f);

		printedZone = dbmw_sprint(z->dbm, g->a->clocks);
		/* malloc for "s->name, zone" */
		z->name = malloc(strlen(z->s->name) + strlen(printedZone) + 3);
		if (z->name == NULL)
		{
			perror("malloc zoneGraph_loadAll:z->name");
			exit(EXIT_FAILURE);
		}
		sprintf(z->name, "%s, %s", z->s->name, printedZone);
		free(printedZone);

		list_append(zones, z);
	}

	for (it = listIterator_first(zones) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Zone *z = listIterator_val(it);
		for (j = 0 ; j < nbConts ; j++)
		{
			z->contSuccs[j] = list_search(zones, &(contsSuccsIndexes[z->index * 
						nbConts + j]), cmpZoneIndex);
			if (z->contSuccs[j] == NULL)
			{
				fprintf(stderr, "ERROR: No zone of index %u for contsSuccs[%u]"
						" of zone of index %u found\n", 
						contsSuccsIndexes[z->index 
						* nbConts + j], j, z->index);
				exit(EXIT_FAILURE);
			}
			zone_addEdge(z, z->contSuccs[j], CONTRCVD);
		}
		
		for (j = 0 ; j < nbUnconts ; j++)
		{
			z->uncontSuccs[j] = list_search(zones, 
					&(uncontsSuccsIndexes[z->index * nbUnconts + j]), 
					cmpZoneIndex);
			if (z->uncontSuccs[j] == NULL)
			{
				fprintf(stderr, "ERROR: No zone of index %u for"
						" uncontsSuccs[%u] of zone of index %u found\n", 
						uncontsSuccsIndexes[z->index 
						* nbUnconts + j], j, z->index);
				exit(EXIT_FAILURE);
			}
			zone_addEdge(z, z->uncontSuccs[j], UNCONTRCVD);
		}
		
		if (timeSuccsIndexes[z->index] == -1)
			z->timeSucc = NULL;
		else
		{
			z->timeSucc = list_search(zones, &(timeSuccsIndexes[z->index]), 
					cmpZoneIndex);
			if (z->timeSucc == NULL)
			{
				fprintf(stderr, "ERROR: No timeSucc found for zone of index"
						" %u\n", z->index);
				exit(EXIT_FAILURE);
			}
			zone_addEdge(z, z->timeSucc, TIMELPSD);
		}
	}
	listIterator_release(it);

	free(contsSuccsIndexes);
	free(uncontsSuccsIndexes);
	free(timeSuccsIndexes);

	return zones;
}

static void zone_free(struct Zone *z)
{
	int i, n;

	free(z->name);
	dbmw_free(z->dbm);

	free(z->contSuccs);
	n = list_size(z->a->contsTable);
	for (i = 0 ; i < n ; i++)
	{
		list_free(z->resetsConts[i], NULL);
	}
	free(z->resetsConts);
	free(z->uncontSuccs);
	n = list_size(z->a->uncontsTable);
	for (i = 0 ; i < n ; i++)
	{
		list_free(z->resetsUnconts[i], NULL);
	}
	free(z->resetsUnconts);

	list_free(z->edges, (void (*)(void *))zoneEdge_free);

	free(z);
}

/* Zone public interface */
const struct List *zone_getEdges(const struct Zone *z)
{
	return z->edges;
}

char *zone_getName(const struct Zone *z)
{
	char *dbmPrint = dbmw_sprint(z->dbm, z->a->clocks);
	char *name = malloc(strlen(z->s->name) + strlen(dbmPrint) + 3);

	if (name == NULL)
	{
		perror("malloc zone_getName:name");
		exit(EXIT_FAILURE);
	}

	strcpy(name, z->s->name);
	strcat(name, ", ");
	strcat(name, dbmPrint);

	free(dbmPrint);

	return name;
}

void zone_setData(struct Zone *z, void *data)
{
	z->userData = data;
}

void *zone_getData(const struct Zone *z)
{
	return z->userData;
}
	

/* ZoneEdge */
static struct ZoneEdge *zoneEdge_new(enum EdgeType type, const struct Zone *z)
{
	struct ZoneEdge *e = malloc(sizeof *e);

	if (e == NULL)
	{
		perror("malloc zoneEdge_new:e");
		exit(EXIT_FAILURE);
	}

	e->type = type;
	e->succ = z;

	return e;
}

static int zoneEdge_cmp(const struct ZoneEdge *e1, const struct ZoneEdge *e2)
{
	return (e1->type == e2->type && e1->succ == e2->succ);
}

static void zoneEdge_free(struct ZoneEdge *e)
{
	free(e);
}


/* TimedAutomaton */
static struct TimedAutomaton *timedAutomaton_new(const struct List *contsTable, 
		const struct SymbolTableEl *contsEls[], const struct List *uncontsTable, 
		const struct SymbolTableEl *uncontsEls[], const struct List *states, 
		const struct List *clocks, const struct List *edges)
{
	struct ListIterator *it;
	int i, j;
	struct TimedAutomaton *a = malloc(sizeof *a);;

	if (a == NULL)
	{
		perror("malloc timedAutomaton_new:a");
		exit(EXIT_FAILURE);
	}

	a->contsTable = contsTable;
	a->contsEls = contsEls;
	a->uncontsTable = uncontsTable;
	a->uncontsEls = uncontsEls;
	a->nbStates = list_size(states);
	a->states = malloc((a->nbStates + 1) * sizeof *(a->states));
	if (a->states == NULL)
	{
		perror("malloc a->states");
		exit(EXIT_FAILURE);
	}

	a->nbClocks = list_size(clocks) + 1;
	a->clocks = malloc(a->nbClocks * sizeof *(a->clocks));
	if (a->clocks == NULL)
	{
		perror("malloc timedAutomaton_new:a->clocks");
		exit(EXIT_FAILURE);
	}

	a->clocks[0] = clock_new(-1, 0, "0");
	for (it = listIterator_first(clocks), i = 1 ; listIterator_hasNext(it) ; it 
			= listIterator_next(it), i++)
	{
		struct ParserClock *pclock = listIterator_val(it);
		a->clocks[i] = clock_new(parserClock_getId(pclock), i, 
				parserClock_getName(pclock));
	}
	listIterator_release(it);

	for (it = listIterator_first(states), i = 0 ; listIterator_hasNext(it) ; it 
			= listIterator_next(it), i++)
	{
		struct ParserState *ps = listIterator_val(it);
		struct State *s = &(a->states[i]);
		s->parserStateId = parserState_getId(ps);
		s->name = strdup(parserState_getName(ps));
		s->isInitial = parserState_isInitial(ps);
		s->isAccepting = parserState_isAccepting(ps);
		s->index = i;

		for (j = 0 ; j < NBSUCCS ; j++)
		{
			s->contSuccs[j] = NULL;
			s->uncontSuccs[j] = NULL;
		}
	}
	listIterator_release(it);
	a->sinkBadState = &(a->states[i]);
	a->sinkBadState->parserStateId = -1;
	a->sinkBadState->isAccepting = 0;
	a->sinkBadState->isInitial = 0;
	a->sinkBadState->name = strdup("Bad");
	a->sinkBadState->index = i;
	for (j = 0 ; j < NBSUCCS ; j++)
	{
		a->sinkBadState->uncontSuccs[j] = NULL;
		a->sinkBadState->contSuccs[j] = NULL;
	}

	for (it = listIterator_first(a->contsTable) ; listIterator_hasNext(it) ; it 
			= listIterator_next(it))
	{
		struct SymbolTableEl *el = listIterator_val(it);
		for (i = 0 ; i < a->nbStates + 1 ; i++)
			a->states[i].contSuccs[(unsigned char)el->c] = list_new();
		list_append(a->sinkBadState->contSuccs[(unsigned char)el->c], 
				stateEdge_new(a, a->sinkBadState, list_new(), list_new()));
	}
	listIterator_release(it);

	for (it = listIterator_first(a->uncontsTable) ; listIterator_hasNext(it) ; 
			it = listIterator_next(it))
	{
		struct SymbolTableEl *el = listIterator_val(it);
		for (i = 0 ; i < a->nbStates + 1 ; i++)
			a->states[i].uncontSuccs[(unsigned char)el->c] = list_new();
		list_append(a->sinkBadState->uncontSuccs[(unsigned char)el->c], 
				stateEdge_new(a, a->sinkBadState, list_new(), list_new()));
	}
	listIterator_release(it);

	for (it = listIterator_first(edges) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		const struct ParserEdge *pe = listIterator_val(it);
		const struct ParserSymbol *psym = parserEdge_getSym(pe);
		const struct ParserState *pfrom = parserEdge_getFrom(pe);
		const struct ParserState *pto = parserEdge_getTo(pe);
		int id;
		struct State *from, *to;
		struct SymbolTableEl *el;

		id = parserState_getId(pfrom);
		from = NULL;
		for (i = 0 ; i < a->nbStates ; i++)
		{
			if (a->states[i].parserStateId == id)
				from = &(a->states[i]);
		}
		id = parserState_getId(pto);
		to = NULL;
		for (i = 0 ; i < a->nbStates ; i++)
		{
			if (a->states[i].parserStateId == id)
				to = &(a->states[i]);
		}

		if (from == NULL)
		{
			fprintf(stderr, "ERROR: cannot find state of id %d.\n", 
					parserState_getId(pfrom));
			exit(EXIT_FAILURE);
		}
		if (to == NULL)
		{
			fprintf(stderr, "ERROR: cannot find state of id %d.\n", 
					parserState_getId(pto));
			exit(EXIT_FAILURE);
		}

		id = parserSymbol_getId(psym);
		if (parserSymbol_isCont(psym))
		{
			struct StateEdge *se;

			el = list_search(contsTable, &id, cmpSymId);
			if (el == NULL)
			{
				fprintf(stderr, "ERROR: cannot find symbol of id %d.\n", id);
				exit(EXIT_FAILURE);
			}
			se = stateEdge_new(a, to, parserEdge_getConstraints(pe), 
					parserEdge_getResets(pe));
			list_append(from->contSuccs[(unsigned char)el->c], se);
		}
		else
		{
			struct StateEdge *se;

			el = list_search(uncontsTable, &id, cmpSymId);
			if (el == NULL)
			{
				fprintf(stderr, "ERROR: cannot find symbol of id %d.\n", id);
				exit(EXIT_FAILURE);
			}
			se = stateEdge_new(a, to, parserEdge_getConstraints(pe), 
					parserEdge_getResets(pe));
			list_append(from->uncontSuccs[(unsigned char)el->c], se);
		}
	}
	listIterator_release(it);

	return a;
}

static void timedAutomaton_save(const struct TimedAutomaton *a, FILE *f)
{
	int i;

	save_uint64(f, (uint64_t)a->nbClocks);
	for (i = 0 ; i < a->nbClocks ; i++)
	{
		clock_save(a->clocks[i], f);
	}

	save_uint64(f, (uint64_t)a->nbStates);
	/* Add 1 for the sink state */
	for (i = 0 ; i < a->nbStates + 1 ; i++)
	{
		state_save(&(a->states[i]), f);
	}
}

static struct TimedAutomaton *timedAutomaton_load(FILE *f, const struct Graph 
		*g)
{
	struct TimedAutomaton *a;
	int i;

	a = malloc(sizeof *a);
	if (a == NULL)
	{
		perror("malloc timedAutomaton_load:a");
		exit(EXIT_FAILURE);
	}

	a->contsTable = g->contsTable;
	a->contsEls = (const struct SymbolTableEl **)g->contsEls;
	a->uncontsTable = g->uncontsTable;
	a->uncontsEls = (const struct SymbolTableEl **)g->uncontsEls;

	a->nbClocks = (unsigned int)load_uint64(f);
	a->clocks = malloc(a->nbClocks * sizeof *(a->clocks));
	if (a->clocks == NULL)
	{
		perror("malloc timedAutomaton_load:a->clocks");
		exit(EXIT_FAILURE);
	}
	for (i = 0 ; i < a->nbClocks ; i++)
	{
		struct Clock *c = clock_load(f);
		a->clocks[i] = c;
	}

	a->nbStates = (unsigned int)load_uint64(f);
	a->states = malloc((a->nbStates + 1) * sizeof *(a->states));
	if (a->states == NULL)
	{
		perror("malloc timedAutomaton_load:a->states");
		exit(EXIT_FAILURE);
	}
	state_loadAll(f, a);

	return a;
}

static void timedAutomaton_free(struct TimedAutomaton *a)
{
	int i, j;
	for (i = 0 ; i < a->nbStates + 1 ; i++)
	{
		for (j = 0 ; j < NBSUCCS ; j++)
		{
			struct ListIterator *it;
			if (a->states[i].contSuccs[j] != NULL)
			{
				for (it = listIterator_first(a->states[i].contSuccs[j]) ; 
						listIterator_hasNext(it) ; it = listIterator_next(it))
				{
					struct StateEdge *se = listIterator_val(it);
					stateEdge_free(se);
				}
				listIterator_release(it);
			}
			if (a->states[i].uncontSuccs[j] != NULL)
			{
				for (it = listIterator_first(a->states[i].uncontSuccs[j]) ; 
						listIterator_hasNext(it) ; it = listIterator_next(it))
				{
					struct StateEdge *se = listIterator_val(it);
					stateEdge_free(se);
				}
				listIterator_release(it);
			}
		}
		state_free(&(a->states[i]));
	}
	free(a->states);

	for (i = 0 ; i < a->nbClocks ; i++)
	{
		clock_free(a->clocks[i]);
	}
	free(a->clocks);

	free(a);
}

/* ArrayTwo */
static struct ArrayTwo *arraytwo_new(unsigned int size, int defaultVal)
{
	int i, j;
	struct ArrayTwo *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc arraytwo_new:ret");
		exit(EXIT_FAILURE);
	}
	ret->allocSize = size;
	ret->size = ret->allocSize;

	ret->tab = malloc(ret->allocSize * sizeof *(ret->tab));
	if (ret->tab == NULL)
	{
		perror("malloc arraytwo_new:ret->tab");
		exit(EXIT_FAILURE);
	}
	
	for (i = 0 ; i < size ; i++)
	{
		ret->tab[i] = malloc(size * sizeof *(ret->tab[i]));
		if (ret->tab[i] == NULL)
		{
			perror("malloc arraytwo_new:ret->tab[i]");
			exit(EXIT_FAILURE);
		}

		for (j = 0 ; j < size ; j++)
		{
			ret->tab[i][j] = defaultVal;
		}
	}

	return ret;
}

static struct ArrayTwo *arraytwo_newcp(const struct ArrayTwo *other)
{
	struct ArrayTwo *copy = arraytwo_new(other->size, 0);
	arraytwo_cp(copy, other);

	return copy;
}

static void arraytwo_cp(struct ArrayTwo *dest, const struct ArrayTwo *src)
{
	int i, j;

	if (dest->allocSize < src->size)
	{
		dest->allocSize = src->size;
		dest->tab = realloc(dest->tab, dest->allocSize * sizeof *(dest->tab));
		if (dest->tab == NULL)
		{
			perror("realloc arraytwo_cp:dest->tab");
			exit(EXIT_FAILURE);
		}
		for (i = 0 ; i < dest->allocSize ; i++)
		{
			dest->tab[i] = realloc(dest->tab[i], dest->allocSize * sizeof 
					*(dest->tab[i]));
			if (dest->tab[i] == NULL)
			{
				perror("realloc arraytwo_cp:dest->tab[i]");
				exit(EXIT_FAILURE);
			}
		}
	}

	dest->size = src->size;

	for (i = 0 ; i < dest->size ; i++)
	{
		for (j = 0 ; j < dest->size ; j++)
		{
			dest->tab[i][j] = src->tab[i][j];
		}
	}
}

static int arraytwo_cmp(const struct ArrayTwo *a1, const struct ArrayTwo *a2)
{
	int i, j;

	if (a1->size != a2->size)
		return 0;

	for (i = 0 ; i < a1->size ; i++)
	{
		for (j = 0 ; j < a1->size ; j++)
		{
			if (a1->tab[i][j] != a2->tab[i][j])
				return 0;
		}
	}

	return 1;
}

static void arraytwo_free(struct ArrayTwo *a)
{
	int i;
	for (i = 0 ; i < a->size ; i++)
	{
		free(a->tab[i]);
	}
	free(a->tab);
	free(a);
}

/** Do not call the stringArray_ functions when the graph has been loaded and 
 * not computed, the only information that is stored is the buffer (sa->s), the 
 * other fields are only necessary to compute the graph. In this case, only call 
 * stringArray_load */
static struct StringArray *stringArray_new(const struct Graph *g)
{
	int i;
	struct ListIterator *it;
	unsigned int size = g->zoneGraph->nbZones;
	struct StringArray *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc stringArray_new:ret");
		exit(EXIT_FAILURE);
	}
	
	ret->array = arraytwo_new(size, 0);
	ret->s = strdup("");
	ret->size = size;
	ret->lasts = malloc(ret->size * sizeof *(ret->lasts));
	if (ret->lasts == NULL)
	{
		perror("malloc stringArray_new:ret->lasts");
		exit(EXIT_FAILURE);
	}

	for (it = listIterator_first(g->zoneGraph->zones), i = 0 ; 
			listIterator_hasNext(it) ; it = listIterator_next(it), i++)
	{
		const struct Zone *z = listIterator_val(it);
		ret->array->tab[i][i] = 1;
		ret->lasts[i] = z;
	}
	listIterator_release(it);

	return ret;
}

static struct StringArray *stringArray_newcp(const struct StringArray *sa)
{
	int i;
	struct StringArray *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc stringArray_newcp:ret");
		exit(EXIT_FAILURE);
	}

	ret->size = sa->size;
	ret->array = arraytwo_newcp(sa->array);
	ret->s = strdup(sa->s);
	ret->lasts = malloc(ret->size * sizeof *(ret->lasts));
	if (ret->lasts == NULL)
	{
		perror("malloc stringArray_newcp:ret->lasts");
		exit(EXIT_FAILURE);
	}

	for (i = 0 ; i < ret->size ; i++)
	{
		ret->array->tab[i][i] = 1;
		ret->lasts[i] = sa->lasts[i];
	}
	
	return ret;
}

static struct StringArray *stringArray_newNext(const struct StringArray *prev, 
		const struct Graph *g, char c)
{
	int i;
	struct StringArray *ret = stringArray_newcp(prev);

	if (ret->size != g->zoneGraph->nbZones)
	{
		fprintf(stderr, "ERROR: array not of the same size than the graph\n");
		exit(EXIT_FAILURE);
	}

	ret->s = realloc(ret->s, strlen(ret->s) + 2);
	if (ret->s == NULL)
	{
		perror("realloc stringArray_newNext:ret->s");
		exit(EXIT_FAILURE);
	}

	ret->s[strlen(ret->s) + 1] = '\0';
	ret->s[strlen(ret->s)] = c;

	for (i = 0 ; i < g->zoneGraph->nbZones ; i++)
	{
		const struct Zone *last = ret->lasts[i];
		const struct Zone *next = zone_nextCont(last, c);
		ret->array->tab[i][next->index] = 1;
		ret->lasts[i] = next;
	}

	return ret;
}

static void stringArray_free(struct StringArray *sa)
{
	free(sa->s);
	arraytwo_free(sa->array);
	free(sa);
}


/* ZoneGraph */
/* ZoneGraph private interface */
static struct ZoneGraph *zoneGraph_new(const struct TimedAutomaton *a)
{
	struct List *rho = list_new();
	struct List *alpha = list_new();
	struct List *sigma = list_new();
	struct List *alpha1;
	struct ListIterator *it;
	struct Zone *X, *z;
	int i;
	struct ZoneGraph *zg;
	int sinkZoneReached;

	zg = malloc(sizeof *zg);
	if (zg == NULL)
	{
		perror("malloc zoneGraph_new:zg");
		exit(EXIT_FAILURE);
	}
	zg->a = a;
	zg->zones = list_new();
	zg->zonesS = malloc((a->nbStates + 1) * sizeof *(zg->zonesS));
	if (zg->zonesS == NULL)
	{
		perror("malloc zoneGraph_new:zg->zones");
		exit(EXIT_FAILURE);
	}
	for (i = 0 ; i < a->nbStates ; i++)
	{
		zg->zonesS[i] = list_new();
	}
	zg->z0 = NULL;
	zg->sinkZone = zone_new(a->sinkBadState, dbmw_new(a->nbClocks), zg);

	X = NULL;
	for (i = 0 ; i < a->nbStates ; i++)
	{
		z = zone_new(&(a->states[i]), dbmw_new(a->nbClocks), zg);
		list_append(rho, z);
		if (a->states[i].isInitial)
		{
			struct Zone *z2 = zone_newcp(z);
			list_append(alpha, z2);
			X = z2;
		}
	}

	while (X != NULL)
	{
		alpha1 = zoneGraph_splitZones(X, rho);

		if (list_size(alpha1) == 1)
		{
			struct List *posts;

			if (list_search(sigma, X, (int (*)(const void *, const void 
								*))zone_areEqual) == NULL)
				list_append(sigma, zone_newcp(X));


			posts = zoneGraph_post(X, rho);

			for (it = listIterator_first(posts) ; listIterator_hasNext(it) ; it 
					= listIterator_next(it))
			{
				z = listIterator_val(it);
				if (list_search(alpha, z, (int (*)(const void *, const void 
									*))zone_areEqual) == NULL)
					list_append(alpha, z);
			}
			listIterator_release(it);

			list_free(posts, NULL);

			list_free(alpha1, (void (*)(void *))zone_free);
		}
		else
		{
			struct List *pres;

			list_remove(alpha, X);

			for (it = listIterator_first(alpha1) ; listIterator_hasNext(it) ; it 
					= listIterator_next(it))
			{
				struct Zone *Y = listIterator_val(it);
				if (Y->s->isInitial && dbmw_containsZero(Y->dbm) &&
						list_search(alpha, Y, (int (*)(const void *, const void 
									*))zone_areEqual) == NULL)
				{
					list_append(alpha, zone_newcp(Y));
				}
			}
			listIterator_release(it);

			pres = zoneGraph_pre(X, rho);
			for (it = listIterator_first(pres) ; listIterator_hasNext(it) ; it = 
					listIterator_next(it))
			{
				z = listIterator_val(it);
				struct Zone *z1 = list_search(sigma, z, (int (*)(const void *, 
								const void *))zone_areEqual);
				if (z1 != NULL)
				{
					list_remove(sigma, z1);
					zone_free(z1);
				}
			}
			listIterator_release(it);
			list_free(pres, (void (*)(void *))zone_free);

			z = list_search(rho, X, (int (*)(const void *, const void 
							*))zone_areEqual);
			if (z != NULL)
				list_remove(rho, z);
			for (it = listIterator_first(alpha1) ; listIterator_hasNext(it) ; it 
					= listIterator_next(it))
			{
				z = listIterator_val(it);
				if (list_search(rho, z, (int (*)(const void *, const void 
									*))zone_areEqual) == NULL)
					list_append(rho, zone_newcp(z));
			}
			listIterator_release(it);
			
			list_free(alpha1, (void (*)(void *))zone_free);
		}

		X = NULL;
		for (it = listIterator_first(alpha) ; listIterator_hasNext(it) ; it = 
				listIterator_next(it))
		{
			z = listIterator_val(it);
			if (list_search(sigma, z,
				(int (*)(const void *, const void *))zone_areEqual) ==
					NULL)
			{
				X = z;
				break;
			}
		}
		listIterator_release(it);
	}

	for (it = listIterator_first(rho) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		z = listIterator_val(it);
		list_append(zg->zones, z);
		list_append(zg->zonesS[z->s->index], z);
	}
	listIterator_release(it);

	list_append(zg->zones, zg->sinkZone);
	zg->zonesS[a->sinkBadState->index] = list_new();
	list_append(zg->zonesS[a->sinkBadState->index], zg->sinkZone);

	sinkZoneReached = 0;

	for (it = listIterator_first(zg->zones), i = 0 ; listIterator_hasNext(it) ; 
			it = listIterator_next(it), i++)
	{
		struct ListIterator *it2;
		char *printedZone;

		z = listIterator_val(it);
		z->index = i;

		printedZone = dbmw_sprint(z->dbm, a->clocks);
		/* malloc for "s->name, zone" */
		z->name = malloc(strlen(z->s->name) + strlen(printedZone) + 3);
		if (z->name == NULL)
		{
			perror("malloc zoneGraph_new:z->name");
			exit(EXIT_FAILURE);
		}
		sprintf(z->name, "%s, %s", z->s->name, printedZone);
		free(printedZone);

		if (z->s->isInitial && dbmw_containsZero(z->dbm))
			zg->z0 = z;

		for (it2 = listIterator_first(zg->zonesS[z->s->index]) ; 
				listIterator_hasNext(it2) ; it2 = listIterator_next(it2))
		{
			struct Dbmw *ztmp;
			struct Zone *z2 = listIterator_val(it2);
			if (z2 == z)
				continue;
			ztmp = dbmw_upTo(z->dbm, z2->dbm);
			if (ztmp != NULL)
			{
				z->timeSucc = z2;
				zone_addEdge(z, z2, TIMELPSD);
				dbmw_free(ztmp);
			}
		}
		listIterator_release(it2);

		for (it2 = listIterator_first(a->contsTable) ; listIterator_hasNext(it2) 
				; it2 = listIterator_next(it2))
		{
			struct SymbolTableEl *el = listIterator_val(it2);
			struct ListIterator *it3;
			int found = 0;

			for (it3 = listIterator_first(z->s->contSuccs[(unsigned char)el->c]) 
					; listIterator_hasNext(it3) ; it3 = listIterator_next(it3))
			{
				struct StateEdge *se = listIterator_val(it3);
				struct Dbmw *dbmtmp = dbmw_newcp(z->dbm);
				if (dbmw_intersection(dbmtmp, se->dbm))
				{
					struct ListIterator *it4;
					for (it4 = listIterator_first(se->resets) ; 
							listIterator_hasNext(it4) ; it4 = 
							listIterator_next(it4))
					{
						struct Clock *c = listIterator_val(it4);
						dbmw_reset(dbmtmp, c);
					}
					listIterator_release(it4);

					for (it4 = listIterator_first(zg->zonesS[se->to->index]) ; 
							listIterator_hasNext(it4) ; it4 = 
							listIterator_next(it4))
					{
						struct Zone *z2 = listIterator_val(it4);
						if (dbmw_intersects(dbmtmp, z2->dbm))
						{
							z->contSuccs[el->index] = z2;
							list_appendList(z->resetsConts[el->index], se->resets, 
									NULL);
							zone_addEdge(z, z2, CONTRCVD);
							found = 1;
							break;
						}
					}
					listIterator_release(it4);
				}
				dbmw_free(dbmtmp);
			}
			listIterator_release(it3);

			if (!found)
			{
				z->contSuccs[el->index] = zg->sinkZone;
				zone_addEdge(z, zg->sinkZone, CONTRCVD);
				sinkZoneReached = 1;
			}
		}
		listIterator_release(it2);

		for (it2 = listIterator_first(a->uncontsTable) ; 
				listIterator_hasNext(it2) ; it2 = listIterator_next(it2))
		{
			struct SymbolTableEl *el = listIterator_val(it2);
			struct ListIterator *it3;
			int found = 0;

			for (it3 = listIterator_first(z->s->uncontSuccs[(unsigned 
							char)el->c]) ; listIterator_hasNext(it3) ; it3 = 
					listIterator_next(it3))
			{
				struct StateEdge *se = listIterator_val(it3);
				struct Dbmw *dbmtmp = dbmw_newcp(z->dbm);
				if (dbmw_intersection(dbmtmp, se->dbm))
				{
					struct ListIterator *it4;
					for (it4 = listIterator_first(se->resets) ; 
							listIterator_hasNext(it4) ; it4 = 
							listIterator_next(it4))
					{
						struct Clock *c = listIterator_val(it4);
						dbmw_reset(dbmtmp, c);
					}
					listIterator_release(it4);

					for (it4 = listIterator_first(zg->zonesS[se->to->index]) ; 
							listIterator_hasNext(it4) ; it4 = 
							listIterator_next(it4))
					{
						struct Zone *z2 = listIterator_val(it4);
						if (dbmw_intersects(dbmtmp, z2->dbm))
						{
							z->uncontSuccs[el->index] = z2;
							list_appendList(z->resetsUnconts[el->index], 
									se->resets, NULL);
							zone_addEdge(z, z2, UNCONTRCVD);
							found = 1;
							break;
						}
					}
					listIterator_release(it4);
				}
				dbmw_free(dbmtmp);
			}
			listIterator_release(it3);

			if (!found)
			{
				z->uncontSuccs[el->index] = zg->sinkZone;
				zone_addEdge(z, zg->sinkZone, UNCONTRCVD);
				sinkZoneReached = 1;
			}
		}
		listIterator_release(it2);
	}
	listIterator_release(it);

	if (!sinkZoneReached)
	{
		struct Zone *z = zg->sinkZone;
		list_remove(zg->zonesS[a->sinkBadState->index], zg->sinkZone);
		list_remove(zg->zones, zg->sinkZone);
		zone_free(z);
		zg->sinkZone = NULL;
	}


	zg->nbZones = list_size(zg->zones);

	list_free(rho, NULL);

	return zg;
}

/*
 * returns a list of DBMs, not of zones
 */
static struct List *zoneGraph_splitZones2(const struct Zone *z0, const struct 
		Zone *z1)
{
	struct List *ret;
	struct List *dbms = list_new();
	int i;
	struct ListIterator *it;

	list_append(dbms, dbmw_newcp(z0->dbm));
	for (i = 0 ; i < 256 ; i++)
	{
		if (z0->s->contSuccs[i] != NULL)
		{
			for (it = listIterator_first(z0->s->contSuccs[i]) ; 
					listIterator_hasNext(it) ; it = listIterator_next(it))
			{
				struct StateEdge *se = listIterator_val(it);
				if (se->to == z1->s)
				{
					struct Dbmw *ztmp = dbmw_newcp(z1->dbm);
					struct Dbmw *z;
					struct ListIterator *it2;

					for (it2 = listIterator_first(se->resets) ; 
							listIterator_hasNext(it2) ; it2 = 
							listIterator_next(it2))
					{
						struct Clock *c = listIterator_val(it2);
						dbmw_freeClock(ztmp, c);
					}
					listIterator_release(it2);

					if (dbmw_intersection(ztmp, se->dbm) &&
							dbmw_intersection(ztmp, z0->dbm))
					{
						z = dbmw_upTo(z0->dbm, ztmp);
						if (z != NULL)
							list_append(dbms, z);
					}
					dbmw_free(ztmp);
				}
			}
			listIterator_release(it);
		}
		if (z0->s->uncontSuccs[i] != NULL)
		{
			for (it = listIterator_first(z0->s->uncontSuccs[i]) ; 
					listIterator_hasNext(it) ; it = listIterator_next(it))
			{
				struct StateEdge *se = listIterator_val(it);
				if (se->to == z1->s)
				{
					struct Dbmw *ztmp = dbmw_newcp(z1->dbm);
					struct Dbmw *z;
					struct ListIterator *it2;

					for (it2 = listIterator_first(se->resets) ; 
							listIterator_hasNext(it2) ; it2 = 
							listIterator_next(it2))
					{
						struct Clock *c = listIterator_val(it2);
						dbmw_freeClock(ztmp, c);
					}
					listIterator_release(it2);

					if (dbmw_intersection(ztmp, se->dbm) &&
							dbmw_intersection(ztmp, z0->dbm))
					{
						z = dbmw_upTo(z0->dbm, ztmp);
						if (z != NULL)
							list_append(dbms, z);
					}
					dbmw_free(ztmp);
				}
			}
			listIterator_release(it);
		}
	}

	if (z0->s == z1->s)
	{
		struct Dbmw *z = dbmw_upTo(z0->dbm, z1->dbm);
		if (z != NULL)
			list_append(dbms, z);
	}

	ret = dbmw_partition(dbms);
	list_free(dbms, (void (*)(void *))dbmw_free);

	return ret;
}

static struct List *zoneGraph_splitZones(const struct Zone *z, const struct List 
		*rho)
{
	struct ListIterator *it;
	struct List *splits = list_new();
	struct List *partition;
	struct List *ret = list_new();

	for (it = listIterator_first(rho) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Zone *z1 = listIterator_val(it);
		struct ListIterator *it2;
		struct List *part = zoneGraph_splitZones2(z, z1);

		for (it2 = listIterator_first(part) ; listIterator_hasNext(it2) ; it2 = 
				listIterator_next(it2))
		{
			struct Dbmw *z2 = listIterator_val(it2);
			list_append(splits, z2);
		}
		listIterator_release(it2);
	}
	listIterator_release(it);

	partition = dbmw_partition(splits);

	list_free(splits, (void (*)(void *))dbmw_free);

	for (it = listIterator_first(partition) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Dbmw *dbm = listIterator_val(it);
		list_append(ret, zone_new(z->s, dbm, z->zg));
	}
	listIterator_release(it);

	list_free(partition, NULL);

	return ret;
}

static struct List *zoneGraph_pre(const struct Zone *z, const struct List *rho)
{
	int i;
	struct ListIterator *it;
	struct List *ret = list_new();

	for (it = listIterator_first(rho) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Zone *z2 = listIterator_val(it);
		if (z2->s == z->s)
		{
			struct Dbmw *dbm = dbmw_upTo(z2->dbm, z->dbm);
			if (dbm != NULL)
			{
				list_append(ret, zone_newcp(z2));
			}
		}
		
		for (i = 0 ; i < 256 ; i++)
		{
			if (z2->s->contSuccs[i] != NULL)
			{
				struct ListIterator *it2;
				for (it2 = listIterator_first(z2->s->contSuccs[i]) ; 
						listIterator_hasNext(it2) ; it2 = 
						listIterator_next(it2))
				{
					struct StateEdge *se = listIterator_val(it2);
					if (se->to == z->s)
					{
						struct Dbmw *ztmp = dbmw_newcp(z2->dbm);
						struct ListIterator *it2;

						dbmw_intersection(ztmp, se->dbm);
						for (it2 = listIterator_first(se->resets) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							struct Clock *c = listIterator_val(it2);
							dbmw_reset(ztmp, c);
						}
						listIterator_release(it2);

						dbmw_intersection(ztmp, z->dbm);
						if (!dbmw_isEmpty(ztmp))
							list_append(ret, zone_newcp(z2));
						dbmw_free(ztmp);
					}
				}
				listIterator_release(it2);
			}
			if (z2->s->uncontSuccs[i] != NULL)
			{
				struct ListIterator *it2;
				for (it2 = listIterator_first(z2->s->uncontSuccs[i]) ; 
						listIterator_hasNext(it2) ; it2 = 
						listIterator_next(it2))
				{
					struct StateEdge *se = listIterator_val(it2);
					if (se->to == z->s)
					{
						struct Dbmw *ztmp = dbmw_newcp(z2->dbm);
						struct ListIterator *it2;

						dbmw_intersection(ztmp, se->dbm);
						for (it2 = listIterator_first(se->resets) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							struct Clock *c = listIterator_val(it2);
							dbmw_reset(ztmp, c);
						}
						listIterator_release(it2);

						dbmw_intersection(ztmp, z->dbm);
						if (!dbmw_isEmpty(ztmp))
							list_append(ret, zone_newcp(z2));
						dbmw_free(ztmp);
					}
				}
				listIterator_release(it2);
			}
		}
	}
	listIterator_release(it);

	return ret;
}

static struct List *zoneGraph_post(const struct Zone *z, struct List *rho)
{
	int i;
	struct ListIterator *it;
	struct List *ret = list_new();

	for (it = listIterator_first(rho) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Zone *z2 = listIterator_val(it);
		if (z2->s == z->s)
		{
			struct Dbmw *dbm = dbmw_upTo(z->dbm, z2->dbm);
			if (dbm != NULL)
			{
				list_append(ret, zone_newcp(z2));
				dbmw_free(dbm);
			}
		}
		
		for (i = 0 ; i < 256 ; i++)
		{
			if (z->s->contSuccs[i] != NULL)
			{
				struct ListIterator *it2;
				for (it2 = listIterator_first(z->s->contSuccs[i]) ; 
						listIterator_hasNext(it2) ; it2 = 
						listIterator_next(it2))
				{
					struct StateEdge *se = listIterator_val(it2);
					if (se->to == z2->s)
					{
						struct Dbmw *ztmp = dbmw_newcp(z->dbm);
						struct ListIterator *it2;

						dbmw_intersection(ztmp, se->dbm);
						for (it2 = listIterator_first(se->resets) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							struct Clock *c = listIterator_val(it2);
							dbmw_reset(ztmp, c);
						}
						listIterator_release(it2);

						dbmw_intersection(ztmp, z2->dbm);
						if (!dbmw_isEmpty(ztmp))
							list_append(ret, zone_newcp(z2));
						dbmw_free(ztmp);
					}
				}
				listIterator_release(it2);
			}
			if (z->s->uncontSuccs[i] != NULL)
			{
				struct ListIterator *it2;
				for (it2 = listIterator_first(z2->s->uncontSuccs[i]) ; 
						listIterator_hasNext(it2) ; it2 = 
						listIterator_next(it2))
				{
					struct StateEdge *se = listIterator_val(it2);
					if (se->to == z2->s)
					{
						struct Dbmw *ztmp = dbmw_newcp(z->dbm);
						struct ListIterator *it2;

						dbmw_intersection(ztmp, se->dbm);
						for (it2 = listIterator_first(se->resets) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							struct Clock *c = listIterator_val(it2);
							dbmw_reset(ztmp, c);
						}
						listIterator_release(it2);

						dbmw_intersection(ztmp, z2->dbm);
						if (!dbmw_isEmpty(ztmp))
							list_append(ret, zone_newcp(z2));
						dbmw_free(ztmp);
					}
				}
				listIterator_release(it2);
			}
		}
	}
	listIterator_release(it);

	return ret;
}

static void zoneGraph_save(const struct ZoneGraph *zg, FILE *f)
{
	struct ListIterator *it;
	save_uint64(f, (uint64_t)zg->nbZones);

	save_uint64(f, (uint64_t)zg->z0->index);
	if (zg->sinkZone != NULL)
		save_uint64(f, (uint64_t)zg->sinkZone->index);
	else
		save_uint64(f, (uint64_t)-1);

	for (it = listIterator_first(zg->zones) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Zone *z = listIterator_val(it);
		zone_save(z, f);
	}
	listIterator_release(it);
}

static struct ZoneGraph *zoneGraph_load(FILE *f, const struct Graph *g)
{
	struct ZoneGraph *zg = malloc(sizeof *zg);
	unsigned int z0Index, sinkZoneIndex;

	if (zg == NULL)
	{
		perror("malloc zoneGraph_load:zg");
		exit(EXIT_FAILURE);
	}

	zg->a = g->a;
	zg->nbZones = (unsigned int)load_uint64(f);

	z0Index = (unsigned int)load_uint64(f);
	sinkZoneIndex = (unsigned int)load_uint64(f);
	zg->zones = zone_loadAll(f, zg, g);
	/* Discarded, not necessary when the graph is already computed */
	zg->zonesS = NULL;
	
	zg->z0 = list_search(zg->zones, &z0Index, cmpZoneIndex);
	if (zg->z0 == NULL)
	{
		fprintf(stderr, "ERROR: No zone of index %u for z0 was found\n", 
				z0Index);
		exit(EXIT_FAILURE);
	}
	if (sinkZoneIndex != -1)
	{
		zg->sinkZone = list_search(zg->zones, &sinkZoneIndex, cmpZoneIndex);
		if (zg->sinkZone == NULL)
		{
			fprintf(stderr, "ERROR: No zone of index %u for sinkZone was" 
					" found\n", sinkZoneIndex);
			exit(EXIT_FAILURE);
		}
	}
	else
		zg->sinkZone = NULL;

	return zg;
}

static void zoneGraph_free(struct ZoneGraph *zg)
{
	int i;

	if (zg->zonesS != NULL)
	{
		for (i = 0 ; i < zg->a->nbStates ; i++)
		{
			list_free(zg->zonesS[i], NULL);
		}
	}

	list_free(zg->zones, (void (*)(void *))zone_free);

	free(zg);
}

/* ZoneGraph public interface */
const struct List *zoneGraph_getZones(const struct ZoneGraph *zg)
{
	return zg->zones;
}


/* Graph */
/* Graph private interface */
/* Helper functions to create the graph */
static void graph_createChars(const struct List *l, struct List **psymbolTable, 
		char **pchars, struct SymbolTableEl *elTable[256])
{
	int size = list_size(l);
	int i;
	struct ListIterator *it;
	char *chars;
	struct List *symbolTable;
	const char *symLabels = 
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

	*psymbolTable = list_new();
	symbolTable = *psymbolTable;


	if (size > 52)
	{
		fprintf(stderr, "Too many events (max. 52).\n");
		exit(EXIT_FAILURE);
	}

	*pchars = malloc(size + 1);
	chars = *pchars;
	if (chars == NULL)
	{
		perror("malloc chars");
		exit(EXIT_FAILURE);
	}
	strncpy(chars, symLabels, size);
	chars[size] = '\0';

	for (it = listIterator_first(l), i = 0 ; listIterator_hasNext(it) ; it = 
			listIterator_next(it), i++)
	{
		struct ParserSymbol *s = listIterator_val(it);
		struct SymbolTableEl *el = symbolTableEl_new(chars[i], 
				parserSymbol_getLabel(s), parserSymbol_getId(s));
		el->index = i;
		list_append(symbolTable, el);
		elTable[(unsigned char)el->c] = el;
	}
	listIterator_release(it);
}

static struct Tree *graph_computeStrings(const struct Graph *g)
{
	struct Tree *t;
	struct StringArray *root = stringArray_new(g);

	t = tree_new(root);
	graph_computeTree(t, g);

	return t;
}

static void graph_computeTree(struct Tree *t, const struct Graph *g)
{
	struct StringArray *sap = tree_getData(t), *sa;
	struct Tree *son;
	int i;

	for (i = 0 ; i < g->nbConts ; i++)
	{
		sa = stringArray_newNext(sap, g, g->contsChars[i]);

		son = tree_new(sa);
		tree_addSon(t, son);
		if (!arraytwo_cmp(sa->array, sap->array))
			graph_computeTree(son, g);
	}
}

static void graph_addNodes(struct Graph *g, struct Tree *stringArrays)
{
	struct ListIterator *it;

	for (it = listIterator_first(g->zoneGraph->zones) ; listIterator_hasNext(it) 
			; it = listIterator_next(it))
	{
		struct Zone *z = listIterator_val(it);
		graph_addNodesRec(g, z, stringArrays, NULL);
	}
	listIterator_release(it);

	g->nbNodes = list_size(g->nodes);
}

static void graph_addNodesRec(struct Graph *g, const struct Zone *z, const 
		struct Tree *stringArrays, struct Node *pred)
{
	struct StringArray *sa;
	struct Node *n[2];
	int i, nbSons;

	sa = tree_getData(stringArrays);
	for (i = 0 ; i < 2 ; i++)
	{
		n[i] = node_new(g, z, sa->s, i);
		list_append(g->nodes, n[i]);
		list_append(g->nodesP[i], n[i]);
	}

	node_addEdgeStop(n[0], n[1]);
	if (pred != NULL)
		node_addEdgeCont(pred, n[0], graph_contIndex(g, sa->s[strlen(sa->s) - 
					1]));
	else
		g->baseNodes[z->index] = n[0];
	nbSons = tree_getNbSons(stringArrays);
	if (nbSons == 0)
	{
		n[0]->isLeaf = 1;
		n[1]->isLeaf = 1;
	}
	for (i = 0 ; i < nbSons ; i++)
	{
		graph_addNodesRec(g, z, tree_getSon(stringArrays, i), n[1]);
	}
}

static void graph_addEmitEdges(struct Graph *g)
{
	struct ListIterator *it;
	char *remain;
	struct Node *next;

	for (it = listIterator_first(g->nodes) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Node *n = listIterator_val(it);

		if (n->owner == 1)
			continue;

		/* it is impossible to emit something from a node that has nothing to be 
		 * emitted */
		if (n->word[0] == '\0')
			continue;

		next = g->baseNodes[n->z->contSuccs[g->contsEls[(unsigned 
				char)(n->word[0])]->index]->index];
		remain = n->word + 1;

		while (*remain != '\0')
		{
			next = node_succCont(g, next, *remain);
			remain++;
		}

		node_addEdgeEmit(n, next);
	}
	listIterator_release(it);
}

static void graph_addUncontEdges(struct Graph *g)
{
	struct ListIterator *it;
	char *remain;
	struct Node *next;
	int i;

	for (it = listIterator_first(g->nodes) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Node *n = listIterator_val(it);

		if (n->owner == 0)
			continue;

		for (i = 0 ;  i < g->nbUnconts ; i++)
		{
			next = g->baseNodes[n->z->uncontSuccs[i]->index];

			/* Do not loop */
			if (next == n)
				continue;

			remain = n->word;

			while (*remain != '\0')
			{
				next = node_succCont(g, next, *remain);
				remain++;
			}

			node_addEdgeUncont(n, next, i);
		}
	}
	listIterator_release(it);
}

static void graph_addTimeEdges(struct Graph *g)
{
	struct ListIterator *it;
	char *remain;
	struct Node *next;

	for (it = listIterator_first(g->nodes) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Node *n = listIterator_val(it);

		if (n->owner == 0 || n->z->timeSucc == NULL)
			continue;

		next = g->baseNodes[n->z->timeSucc->index];

		/* Do not loop */
		if (next == n)
			continue;

		remain = n->word;

		while (*remain != '\0')
		{
			next = node_succCont(g, next, *remain);
			remain++;
		}

		node_addEdgeTime(n, next);
	}
	listIterator_release(it);
}

/* Helper function to access the index of a controllable event */
static inline unsigned int graph_contIndex(const struct Graph *g, char c)
{
	return g->contsEls[(unsigned char)c]->index;
}

/* Buchi game computation */
static void graph_computeW0(struct Graph *g, struct Set *ret)
{
	struct List *S = list_new();
	struct ListIterator *it;
	struct Set *B = set_empty(NULL);
	struct Set *R = set_empty(NULL);
	struct Set *Tr = set_empty(NULL);
	struct Set *W = set_empty(NULL);
	struct Set *Sset = set_empty(NULL);

	for (it = listIterator_first(g->nodes) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Node *n = listIterator_val(it);
		list_append(S, n);
		set_add(Sset, n);
		if (n->isAccepting)
			set_add(B, n);
	}
	listIterator_release(it);
	set_reset(ret);

	do
	{
		graph_attr(R, g, 0, B, S);

		/* Tr = S \ B */
		set_reset(Tr);
		for (it = listIterator_first(S) ; listIterator_hasNext(it) ; it = 
				listIterator_next(it))
		{
			set_add(Tr, listIterator_val(it));
		}
		listIterator_release(it);
		set_remove(Tr, (int (*)(const void *, const void *))set_in, R);

		graph_attr(W, g, 1, Tr, S);
		removeSetFromList(S, W);

		set_remove(B, (int (*)(const void *, const void *))set_in, W);
		set_applyToAll(W, (void (*)(void *, void *))set_add, ret);
	} while (!set_isEmpty(W));

	/* ret = S \ ret */
	set_copy(W, ret, NULL);
	set_copy(ret, Sset, NULL);
	set_remove(ret, (int (*)(const void *, const void *))set_in, W);

	list_free(S, NULL);
	set_free(B);
	set_free(R);
	set_free(Tr);
	set_free(W);
	set_free(Sset);
}

static void graph_attr(struct Set *ret, struct Graph *g, int player, const 
		struct Set *U, struct List *nodes)
{
	int stable = 0;
	struct ListIterator *it, *it2;

	set_reset(ret);
	set_copy(ret, U, NULL);

	while (!stable)
	{
		stable = 1;

		for (it = listIterator_first(nodes) ; listIterator_hasNext(it) ; it = 
				listIterator_next(it))
		{
			struct Node *n = listIterator_val(it);
			if (set_in(ret, n))
				continue;
			if (n->owner == player)
			{
				for (it2 = listIterator_first(n->edges) ; 
						listIterator_hasNext(it2) ; it2 = 
						listIterator_next(it2))
				{
					struct Edge *e = listIterator_val(it2);
					if (listIn(nodes, e->succ) && set_in(ret, e->succ))
					{
						stable = 0;
						set_add(ret, n);
						break;
					}
				}
				listIterator_release(it2);
			}
			/* n->owner != player */
			else
			{
				int ok = 1;

				for (it2 = listIterator_first(n->edges) ; 
						listIterator_hasNext(it2) ; it2 = 
						listIterator_next(it2))
				{
					struct Edge *e = listIterator_val(it2);
					if (listIn(nodes, e->succ) && !set_in(ret, e->succ))
						ok = 0;
				}
				listIterator_release(it2);

				if (ok)
				{
					stable = 0;
					set_add(ret, n);
				}
			}
		}
		listIterator_release(it);
	}
}

/* Graph public interface */
struct Graph *graph_newFromAutomaton(const char *filename)
{
	const struct List *pstates = NULL;
	const struct List *pconts = NULL;
	const struct List *punconts = NULL;
	const struct List *pedges = NULL;
	const struct List *pclocks = NULL;
	struct Tree *strings;
	struct Graph *g = malloc(sizeof *g);
	struct Set *W0 = set_empty(NULL);
	int i;

	if (g == NULL)
	{
		perror("malloc g");
		exit(EXIT_FAILURE);
	}
	

	parseFile(filename);

	pstates = parser_getStates();
	pconts = parser_getConts();
	punconts = parser_getUnconts();
	pedges = parser_getEdges();
	pclocks = parser_getClocks();

	g->nodes = list_new();
	g->nodesP[0] = list_new();
	g->nodesP[1] = list_new();
	g->nbConts = parser_getNbConts();
	g->nbUnconts = parser_getNbUnconts();

	for (i = 0 ; i < 256 ; i++)
	{
		g->contsEls[i] = NULL;
		g->uncontsEls[i] = NULL;
	}
	graph_createChars(pconts, &(g->contsTable), &(g->contsChars), g->contsEls);
	graph_createChars(punconts, &(g->uncontsTable), &(g->uncontsChars), 
			g->uncontsEls);

	g->a = timedAutomaton_new(g->contsTable, (const struct SymbolTableEl 
				**)g->contsEls, g->uncontsTable, (const struct SymbolTableEl 
					**)g->uncontsEls, pstates, pclocks, pedges);
	clocks = g->a->clocks;
	g->zoneGraph = zoneGraph_new(g->a);
	g->baseNodes = malloc(g->zoneGraph->nbZones * sizeof (*g->baseNodes));
	if (g->baseNodes == NULL)
	{
		perror("malloc graph_newFromAutomaton:g->baseNodes");
		exit(EXIT_FAILURE);
	}

	strings = graph_computeStrings(g);
	graph_addNodes(g, strings);
	tree_free(strings, (void (*)(void *))stringArray_free);
	graph_addEmitEdges(g);
	graph_addUncontEdges(g);
	graph_addTimeEdges(g);
	graph_computeW0(g, W0);
	
	set_applyToAll(W0, node_setWinning, NULL);
	set_applyToAll(W0, (void (*)(void *, void *))node_computeStrat, NULL);
	parser_cleanup();

	return g;
}

const struct List *graph_getNodes(const struct Graph *g)
{
	return g->nodes;
}

const struct ZoneGraph *graph_getZoneGraph(const struct Graph *g)
{
	return g->zoneGraph;
}

void graph_save(const struct Graph *g, const char *filename)
{
	FILE *file = fopen(filename, "w");
	struct ListIterator *it;

	if (file == NULL)
	{
		perror("fopen graph_save:file");
		exit(EXIT_FAILURE);
	}


	save_uint64(file, (uint64_t)g->nbConts);
	for (it = listIterator_first(g->contsTable) ; listIterator_hasNext(it) ; it 
			= listIterator_next(it))
	{
		struct SymbolTableEl *s = listIterator_val(it);
		symbolTableEl_save(s, file);
	}
	listIterator_release(it);

	save_uint64(file, (uint64_t)g->nbUnconts);
	for (it = listIterator_first(g->uncontsTable) ; listIterator_hasNext(it) ; 
			it = listIterator_next(it))
	{
		struct SymbolTableEl *s = listIterator_val(it);
		symbolTableEl_save(s, file);
	}
	listIterator_release(it);

	save_uint64(file, (uint64_t)CANARY);

	timedAutomaton_save(g->a, file);

	save_uint64(file, (uint64_t)CANARY);

	zoneGraph_save(g->zoneGraph, file);

	save_uint64(file, (uint64_t)CANARY);

	save_uint64(file, (uint64_t)g->nbNodes);
	for (it = listIterator_first(g->nodes) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Node *n = listIterator_val(it);
		node_save(n, file);
	}
	listIterator_release(it);

	save_uint64(file, (uint64_t)CANARY);

	fclose(file);
}

struct Graph *graph_load(const char *filename)
{
	FILE *f;
	struct Graph *g;
	int i;
	uint64_t n;
	struct ListIterator *it;

	if ((f = fopen(filename, "r")) == NULL)
	{
		perror("fopen graph_load");
		exit(EXIT_FAILURE);
	}

	g = malloc(sizeof *g);
	if (g == NULL)
	{
		perror("malloc graph_load:g");
		exit(EXIT_FAILURE);
	}

	g->nbConts = (unsigned int)load_uint64(f);
	g->contsChars = malloc(g->nbConts + 1);
	if (g->contsChars == NULL)
	{
		perror("malloc graph_load:g->contsChars");
		exit(EXIT_FAILURE);
	}
	g->contsTable = list_new();
	for (i = 0 ; i < NBSUCCS ; i++)
	{
		g->contsEls[i] = NULL;
	}
	for (i = 0 ; i < g->nbConts ; i++)
	{
		struct SymbolTableEl *el = symbolTableEl_load(f, g);
		list_append(g->contsTable, el);
		g->contsEls[(unsigned char)el->c] = el;
		g->contsChars[i] = el->c;
	}

	g->nbUnconts = (unsigned int)load_uint64(f);
	g->uncontsChars = malloc(g->nbUnconts + 1);
	if (g->uncontsChars == NULL)
	{
		perror("malloc graph_load:g->uncontsChars");
		exit(EXIT_FAILURE);
	}
	g->uncontsTable = list_new();
	for (i = 0 ; i < NBSUCCS ; i++)
	{
		g->uncontsEls[i] = NULL;
	}
	for (i = 0 ; i < g->nbUnconts ; i++)
	{
		struct SymbolTableEl *el = symbolTableEl_load(f, g);
		list_append(g->uncontsTable, el);
		g->uncontsEls[(unsigned char)el->c] = el;
		g->uncontsChars[el->index] = el->c;
	}

	n = load_uint64(f);
	if (n != CANARY)
	{
		fprintf(stderr, "ERROR: Expecting %lx after uncontrollable in file"
			   " %s, but vaue is %lx\n", CANARY, filename, n);
		exit(EXIT_FAILURE);
	}

	g->a = timedAutomaton_load(f, g);

	n = load_uint64(f);
	if (n != CANARY)
	{
		fprintf(stderr, "ERROR: Expecting %lx after timed automaton in file"
				" %s, but value is %lx\n", CANARY, filename, n);
		exit(EXIT_FAILURE);
	}

	g->zoneGraph = zoneGraph_load(f, g);
	g->nbZones = g->zoneGraph->nbZones;

	n = load_uint64(f);
	if (n != CANARY)
	{
		fprintf(stderr, "ERROR: Expecting 0 after zone graph in file %s\n", 
				filename);
		exit(EXIT_FAILURE);
	}

	g->nbNodes = (unsigned int)load_uint64(f);
	g->nodesP[0] = list_new();
	g->nodesP[1] = list_new();
	g->baseNodes = malloc(g->zoneGraph->nbZones * sizeof (*g->baseNodes));
	if (g->baseNodes == NULL)
	{
		perror("malloc graph_newFromAutomaton:g->baseNodes");
		exit(EXIT_FAILURE);
	}

	g->nodes = node_loadAll(f, g);
	for (it = listIterator_first(g->nodes) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Node *n = listIterator_val(it);
		if (n->owner != 0 && n->owner != 1)
		{
			fprintf(stderr, "ERROR: owner must be 0 or 1\n");
			exit(EXIT_FAILURE);
		}
		list_append(g->nodesP[n->owner], n);
		if (n->owner == 0 && n->word[0] == '\0')
			g->baseNodes[n->z->index] = n;
	}
	listIterator_release(it);

	n = load_uint64(f);
	if (n != CANARY)
	{
		fprintf(stderr, "ERROR: Expecting %lx after nodes (before EOF) in %s,"
				" but value is %lx\n", CANARY, filename, n);
		exit(EXIT_FAILURE);
	}

	if (fgetc(f) != EOF)
	{
		fprintf(stderr, "ERROR: Graph is loaded, but EOF is not reached\n");
	}

	fclose(f);

	return g;
}

void graph_free(struct Graph *g)
{
	list_free(g->nodesP[0], NULL);
	list_free(g->nodesP[1], NULL);
	list_free(g->nodes, (void (*)(void *))node_free);
	free(g->baseNodes);

	zoneGraph_free(g->zoneGraph);
	timedAutomaton_free(g->a);

	list_free(g->contsTable, (void (*)(void *))symbolTableEl_free);
	list_free(g->uncontsTable, (void (*)(void *))symbolTableEl_free);

	free(g->contsChars);
	free(g->uncontsChars);
	free(g);
}


/* StratNode */
static struct StratNode *stratNode_new(const struct Node *n, int score, struct 
		List *l, struct ListIterator *it)
{
	struct StratNode *sn = malloc(sizeof *sn);

	if (sn == NULL)
	{
		perror("malloc stratNode_new:sn");
		exit(EXIT_FAILURE);
	}

	sn->n = n;
	sn->score = score;
	sn->strats = l;
	sn->events = it;

	return sn;
}

static void stratNode_free(struct StratNode *sn)
{
	list_free(sn->strats, NULL);
	listIterator_release(sn->events);
	free(sn);
}


/* Clock save/load functions (here to be static) */
static void clock_save(const struct Clock *c, FILE *f)
{
	save_uint64(f, (uint64_t)clock_getIndex(c));
	save_string(f, clock_getName(c));
}

static struct Clock *clock_load(FILE *f)
{
	unsigned int index = (unsigned int)load_uint64(f);
	char *name = load_string(f);
	struct Clock *c = clock_new(-1, index, name);

	free(name);

	return c;
}


/* Enforcer */
/* Enforcer DEBUG */
#if 0
static void enforcer_printValuation(struct Enforcer *e)
{
	int i;

	fprintf(stderr, "{");
	for (i = 1 ; i < e->g->a->nbClocks ; i++)
	{
		fprintf(stderr, "%d", e->valuation[i]);
		if (i < e->g->a->nbClocks - 1)
			fprintf(stderr, ", ");
	}
	fprintf(stderr, "}");
}
#endif

#if 0
static void printEvent(FILE *f, const struct PrivateEvent *pe)
{
	if (pe->c == 'b')
		fprintf(f, "r");
	else
		fprintf(f, "%c", pe->c);
}
#endif

/* Enforcer private interface */
static unsigned int enforcer_computeDelay(const struct Enforcer *e)
{
	if (e->realNode->p0.succStopEmit->p1.succTime != NULL)
	{
		return dbmw_distance(e->valuation, 
				e->realNode->p0.succStopEmit->p1.succTime->z->dbm);
	}

	return 0;
}

#if 0
static int enforcer_computeStratsRec(struct Enforcer *e, const struct Node *n, 
		struct List *strats)
{
	struct List *stratsEmit, *stratsWait;
	int scoreEmit, scoreWait;
	enum Strat *strat;

	strat = malloc(sizeof *strat);
	if (strat == NULL)
	{
		perror("malloc enforcer_computeStratsRec:strat");
		exit(EXIT_FAILURE);
	}

	stratsEmit = list_new();
	if (n->p0.succEmit != NULL && n->p0.succEmit->isWinning)
	{
		struct List *events = list_new();
		struct Node *next = n->p0.succEmit;

		while (!list_isEmpty(e->realBuffer) && !next->isLeaf)
		{
			struct PrivateEvent *pe = list_removeHead(e->realBuffer);
			list_append(events, pe);
			next = next->p0.succStopEmit->p1.succsCont[pe->index];
		}
		scoreEmit = enforcer_computeStratsRec(e, next, stratsEmit);

		/* Add one to scoreEmit, corresponding to the first event being 
		 * emitted (not taken into account by the previous call to 
		 * enforcer_computeStratsRec */
		scoreEmit++;

		/* list_concatList frees e->realBuffer here */
		list_concatList(events, e->realBuffer);
		e->realBuffer = events;
	}
	else
		scoreEmit = -1;

	stratsWait = list_new();
	if (n->p0.succStopEmit->p1.succTime != NULL && 
			n->p0.succStopEmit->p1.succTime->isWinning)
	{
		scoreWait = enforcer_computeStratsRec(e, 
				n->p0.succStopEmit->p1.succTime, stratsWait);
	}
	else
		scoreWait = 0;

	if (scoreWait > scoreEmit)
	{
		*strat = STRAT_DONTEMIT;
		list_cleanup(strats, free);
		list_addHead(stratsWait, strat);
		list_concatList(strats, stratsWait);
		list_free(stratsEmit, free);

		return scoreWait;
	}
	else
	{
		*strat = STRAT_EMIT;
		list_cleanup(strats, free);
		list_addHead(stratsEmit, strat);
		list_concatList(strats, stratsEmit);
		list_free(stratsWait, free);

		return scoreEmit;
	}
}
#endif

static void enforcer_computeStrats(struct Enforcer *e, int clear)
{
	struct ListIterator *it;
	struct ListIterator *firstEvent;
	struct List *goodLeaves = list_new();
	int i;

#ifdef ENFORCER_PRINT_LOG
	fprintf(e->log, "Computing strat...");
#endif

	if (!e->realNode->isWinning || list_isEmpty(e->realBuffer))
	{
		e->strat = NULL;
		return;
	}

	firstEvent = listIterator_first(e->realBuffer);
	i = 0;
	while (e->realNode->word[i++] != '\0')
	{
		if (!listIterator_hasNext(firstEvent))
		{
			fprintf(stderr, "ERROR: realBuffer not matching realNode\n");
			fprintf(stderr, "realBuffer: ");
			for (it = listIterator_first(e->realBuffer) ; 
					listIterator_hasNext(it) ; it = listIterator_next(it))
			{
				struct PrivateEvent *pe = listIterator_val(it);
				struct SymbolTableEl *sym = e->g->contsEls[(unsigned char)pe->c];
				fprintf(stderr, "%s ", sym->sym);
			}
			listIterator_release(it);
			fprintf(stderr, "\nrealNode: (%s, %s)\n", e->realNode->z->name, e->realNode->realWord);
			enforcer_free(e);
			exit(EXIT_FAILURE);
		}

		firstEvent = listIterator_next(firstEvent);
	}

	/* Put something in enforcer_new instead ? */
	for (i = 0 ; i < NBLEAVESTYPES ; i++)
	{
		clear = clear || list_isEmpty(e->leaves[i]);
	}

	if (clear)
	{
		for (i = 0 ; i < NBLEAVESTYPES ; i++)
		{
			list_cleanup(e->leaves[i], (void (*)(void *))stratNode_free);
		}

		list_addHead(e->leaves[LEAVES_GOOD], stratNode_new(e->realNode, 0, 
					list_new(), listIterator_cp(firstEvent)));

		/*
		if (e->realNode->p0.succEmit != NULL && 
				e->realNode->p0.succEmit->isWinning)
		{
			struct ListIterator *it = listIterator_cp(firstEvent);
			struct Node *next = e->realNode->p0.succEmit;

			while (!next->isLeaf && listIterator_hasNext(it))
			{
				struct PrivateEvent *pe = listIterator_val(it);
				next = next->p0.succStopEmit->p1.succsCont[pe->index];
				it = listIterator_next(it);
			}
			list_append(e->leaves, stratNode_new(next, 1, 
						list_append(list_new(), (void *)(uintptr_t)STRAT_EMIT), 
						it));
		}
		if (e->realNode->p0.succStopEmit->p1.succTime != NULL && 
				e->realNode->p0.succStopEmit->p1.succTime->isWinning)
		{
			list_addHead(e->leaves, 
					stratNode_new(e->realNode->p0.succStopEmit->p1.succTime, 0, 
						list_append(list_new(), (void 
								*)(uintptr_t)STRAT_DONTEMIT), 
						listIterator_cp(firstEvent)));
		}
		*/
	}
	else /* Controllable event received */
	{
		struct List *newLeaves = list_new();
		struct List *tmp;
		struct ListIterator *it;

		for (it = listIterator_first(e->leaves[LEAVES_GOOD]) ; 
				listIterator_hasNext(it) ; it = listIterator_next(it))
		{
			struct StratNode *sn = listIterator_val(it);
			while (!sn->n->isLeaf && listIterator_hasNext(sn->events))
			{
				struct PrivateEvent *pe = listIterator_val(sn->events);
				sn->n = sn->n->p0.succStopEmit->p1.succsCont[pe->index];
				sn->events = listIterator_next(sn->events);
			}
		}
		listIterator_release(it);
		while (!list_isEmpty(e->leaves[LEAVES_BADSTOP]))
		{
			struct StratNode *sn = list_removeHead(e->leaves[LEAVES_BADSTOP]);

			while (!sn->n->isLeaf && listIterator_hasNext(sn->events))
			{
				struct PrivateEvent *pe = listIterator_val(sn->events);
				sn->n = sn->n->p0.succStopEmit->p1.succsCont[pe->index];
				sn->events = listIterator_next(sn->events);
			}

			if (sn->n->p0.succStopEmit->p1.succTime->isWinning)
			{
				list_addHead(e->leaves[LEAVES_GOOD], 
						stratNode_new(sn->n->p0.succStopEmit->p1.succTime, 
							sn->score, list_append(list_newcp(sn->strats, NULL), (void 
									*)(uintptr_t)STRAT_DONTEMIT), 
							listIterator_cp(sn->events)));
				stratNode_free(sn);
			}
			else if (!sn->n->isLeaf)
				list_addHead(newLeaves, sn);
			else
				stratNode_free(sn);
		}
		tmp = e->leaves[LEAVES_BADSTOP];
		e->leaves[LEAVES_BADSTOP] = newLeaves;
		newLeaves = tmp;
		list_cleanup(newLeaves, NULL);

		while (!list_isEmpty(e->leaves[LEAVES_BADEMIT]))
		{
			struct StratNode *sn = list_removeHead(e->leaves[LEAVES_BADEMIT]);

			while (!sn->n->isLeaf && listIterator_hasNext(sn->events))
			{
				struct PrivateEvent *pe = listIterator_val(sn->events);
				sn->n = sn->n->p0.succStopEmit->p1.succsCont[pe->index];
				sn->events = listIterator_next(sn->events);
			}

			if (sn->n->p0.succEmit->isWinning)
			{
				list_addHead(e->leaves[LEAVES_GOOD], 
						stratNode_new(sn->n->p0.succEmit, sn->score + 1, 
							list_append(list_newcp(sn->strats, NULL), (void 
									*)(uintptr_t)STRAT_EMIT), 
							listIterator_cp(sn->events)));
				stratNode_free(sn);
			}
			else if (!sn->n->isLeaf)
				list_addHead(newLeaves, sn);
			else
				stratNode_free(sn);
		}
		list_free(e->leaves[LEAVES_BADEMIT], NULL);
		e->leaves[LEAVES_BADEMIT] = newLeaves;
	}
			
	while (!list_isEmpty(e->leaves[LEAVES_GOOD]))
	{
		struct StratNode *sn = list_removeHead(e->leaves[LEAVES_GOOD]);

		/* If the node is (Z, -) such that up(Z) = Z, then in the graph there is 
		 * a loop between the same node of both players */
		if (sn->n->p0.succEmit == NULL && sn->n->p0.succStopEmit->p1.succTime == 
				NULL)
		{
			list_addHead(goodLeaves, sn);
			continue;
		}

		if (sn->n->p0.succEmit != NULL)
		{
			struct ListIterator *it = listIterator_cp(sn->events);
			struct Node *next = sn->n->p0.succEmit;

			while (!next->isLeaf && listIterator_hasNext(it))
			{
				struct PrivateEvent *pe = listIterator_val(it);
				next = next->p0.succStopEmit->p1.succsCont[pe->index];
				it = listIterator_next(it);
			}
			if (next->isWinning)
				list_append(e->leaves[LEAVES_GOOD], stratNode_new(next, 
							sn->score + 1, list_append(list_newcp(sn->strats, 
									NULL), (uintptr_t)STRAT_EMIT), it));
			else if (!listIterator_hasNext(it) && !next->isLeaf)
			{
				list_append(e->leaves[LEAVES_BADEMIT], sn);
				listIterator_release(it);
			}
			else
				listIterator_release(it);
		}

		if (sn->n->p0.succStopEmit->p1.succTime != NULL) 
		{
			struct ListIterator *it = listIterator_cp(sn->events);

			if (sn->n->p0.succStopEmit->p1.succTime->isWinning)
			{
				list_addHead(e->leaves[LEAVES_GOOD], 
						stratNode_new(sn->n->p0.succStopEmit->p1.succTime, 
							sn->score,  list_append(list_newcp(sn->strats, NULL), 
								(void *)(uintptr_t)STRAT_DONTEMIT), it));
			}
			else if (!sn->n->p0.succStopEmit->p1.succTime->isLeaf)
			{
				list_append(e->leaves[LEAVES_BADSTOP], sn);
				listIterator_release(it);
			}
			else
				listIterator_release(it);
		}
	}

	list_free(e->leaves[LEAVES_GOOD], NULL);
	e->leaves[LEAVES_GOOD] = goodLeaves;

	e->strat = NULL;
	for (i = 0 ; i < NBLEAVESTYPES ; i++)
	{
		for (it = listIterator_first(e->leaves[i]) ; listIterator_hasNext(it) ; 
				it = listIterator_next(it))
		{
			struct StratNode *sn = listIterator_val(it);
			struct ListIterator *itSn, *itMax;

			if (e->strat == NULL || e->strat->score < sn->score)
				e->strat = sn;
			else if (e->strat->score == sn->score)
			{
				for (itSn = listIterator_first(sn->strats), itMax = 
						listIterator_first(e->strat->strats) ; 
						listIterator_hasNext(itSn) && 
						listIterator_hasNext(itMax)
						; itSn = listIterator_next(itSn), itMax = 
						listIterator_next(itMax))
				{
					enum Strat stratSn = (enum 
							Strat)((uintptr_t)listIterator_val(itSn));
					enum Strat stratMax = (enum 
							Strat)((uintptr_t)listIterator_val(itMax));

					if (stratSn != stratMax && stratSn == STRAT_EMIT)
					{
						e->strat = sn;
						break;
					}
				}

				if (!listIterator_hasNext(itSn) && listIterator_hasNext(itMax))
					e->strat = sn;

				listIterator_release(itSn);
				listIterator_release(itMax);
			}
		}
	}

	/*
	list_cleanup(e->strats, free);
	enforcer_computeStratsRec(e, e->realNode, e->strats);
	*/

#ifdef ENFORCER_PRINT_LOG
	fprintf(e->log, "Done.\n");
	fprintf(e->log, "strat: %s\n", (e->strat != NULL && 
				!list_isEmpty(e->strat->strats) && (enum 
					Strat)list_first(e->strat->strats) == STRAT_EMIT) ? "emit" : 
			"dontemit");
#endif
}

#if 0
static void enforcer_computeStratNode(struct Enforcer *e)
{
	struct PrivateEvent *pe;
	struct List *l;

	if (list_isEmpty(e->realBuffer))
	{
		e->stratNode = e->realNode;
		if (e->stratNode->p0.succEmit != NULL)
			e->stratNode = e->stratNode->p0.succEmit;
		return;
	}

	l = list_new();
	pe = list_removeHead(e->realBuffer);
	list_addHead(l, pe);
	e->stratNode = e->realNode;
	while (e->stratNode->isLeaf && e->stratNode->p0.succEmit->isWinning)
		e->stratNode = e->stratNode->p0.succEmit;
	if (!e->stratNode->isLeaf)
		e->stratNode = 
			e->stratNode->p0.succStopEmit->p1.succsCont[graph_contIndex(e->g, 
					pe->c)];
	while (!list_isEmpty(e->realBuffer) && !e->stratNode->isWinning)
	{
		pe = list_removeHead(e->realBuffer);
		list_append(l, pe);

		while (e->stratNode->isLeaf && e->stratNode->p0.succEmit->isWinning)
			e->stratNode = e->stratNode->p0.succEmit;
		if (!e->stratNode->isLeaf)
			e->stratNode = e->stratNode->p0.succStopEmit
				->p1.succsCont[graph_contIndex(e->g, pe->c)];
	}
	while (!list_isEmpty(e->realBuffer) && e->stratNode->p0.succEmit != NULL && 
			e->stratNode->p0.succEmit->isWinning)
	{
		pe = list_removeHead(e->realBuffer);
		list_append(l, pe);

		while (e->stratNode->isLeaf && e->stratNode->p0.succEmit->isWinning)
			e->stratNode = e->stratNode->p0.succEmit;
		if (!e->stratNode->isLeaf)
			e->stratNode = 
				e->stratNode->p0.succStopEmit->p1.succsCont[graph_contIndex(e->g, 
						pe->c)];
	}
	while (!list_isEmpty(e->realBuffer))
	{
		pe = list_removeHead(e->realBuffer);
		list_append(l, pe);
		if (!e->stratNode->isLeaf)
			e->stratNode = 
				e->stratNode->p0.succStopEmit->p1.succsCont[graph_contIndex(e->g, 
						pe->c)];
	}

	if (e->stratNode == e->realNode && e->realNode->p0.succEmit != NULL)
		e->stratNode = e->realNode->p0.succEmit;

	list_free(e->realBuffer, NULL);
	e->realBuffer = l;
}
#endif

static void enforcer_storeCont(struct Enforcer *e, const struct SymbolTableEl 
		*el)
{
	char c;
	struct PrivateEvent *pe;

	c = el->c;
	if (!e->realNode->isLeaf)
		e->realNode = e->realNode->p0.succStopEmit->p1.succsCont[el->index];

	pe = malloc(sizeof *pe);
	if (pe == NULL)
	{
		perror("malloc enforcer_eventRcvd:pe");
		exit(EXIT_FAILURE);
	}

	pe->c = c;
	pe->index = el->index;
	pe->type = CONTROLLABLE;
	list_append(e->realBuffer, pe);

	/*
	   while (e->stratNode->isLeaf && e->stratNode->p0.succEmit->isWinning)
	   e->stratNode = e->stratNode->p0.succEmit;

	   if (!e->stratNode->isLeaf)
	   e->stratNode = e->stratNode->p0.succStopEmit->p1.succsCont[el->index];
	   if (e->stratNode == e->realNode && e->realNode->p0.succEmit != NULL)
	   e->stratNode = e->realNode->p0.succEmit;
	   */

	enforcer_computeStrats(e, 0);
}

static void enforcer_passUncont(struct Enforcer *e, const struct 
		SymbolTableEl *el)
{
	struct ListIterator *it;
	struct TimedEvent *te;

	te = malloc(sizeof *te);
	if (te == NULL)
	{
		perror("malloc enforcer_eventRcvd:te");
		exit(EXIT_FAILURE);
	}
	te->date = e->date;
	te->event = strdup(el->sym);
	fifo_enqueue(e->output, te);

	for (it = 
			listIterator_first(e->realNode->p0.succStopEmit->z->resetsUnconts[el->index]) 
			; listIterator_hasNext(it) ; it = listIterator_next(it))
	{
		struct Clock *c = listIterator_val(it);
		e->valuation[clock_getIndex(c)] = 0;
	}
	listIterator_release(it);
	e->realNode = e->realNode->p0.succStopEmit->p1.succsUncont[el->index];

	it = listIterator_first(e->realBuffer);
	while (!e->realNode->isLeaf && listIterator_hasNext(it))
	{
		struct PrivateEvent *pe = listIterator_val(it);
		e->realNode = e->realNode->p0.succStopEmit->p1.succsCont[pe->index];
		it = listIterator_next(it);
	}
	listIterator_release(it);

	/*
	enforcer_computeStratNode(e);
	*/
	enforcer_computeStrats(e, 1);
}

/* Enforcer public interface */
struct Enforcer *enforcer_new(const struct Graph *g, FILE *logFile)
{
	struct Node *initialNode;
	int i;
	struct Enforcer *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc enforcer_new:ret");
		exit(EXIT_FAILURE);
	}

	ret->g = g;
	ret->realBuffer = list_new();
	ret->input = fifo_empty();
	ret->output = fifo_empty();
	ret->strats = list_new();
	ret->strat = NULL;
	ret->log = logFile;

	for (i = 0 ; i < NBLEAVESTYPES ; i++)
	{
		ret->leaves[i] = list_new();
	}

	initialNode = NULL;
	for (i = 0 ; i < g->nbNodes ; i++)
	{
		if (g->baseNodes[i]->isInitial)
		{
			initialNode = g->baseNodes[i];
			break;
		}
	}
	ret->realNode = initialNode;
	ret->stratNode = initialNode;
	ret->valuation = malloc(g->a->nbClocks * sizeof *(ret->valuation));
	if (ret->valuation == NULL)
	{
		perror("malloc enforcer_nex:ret->valuation");
		exit(EXIT_FAILURE);
	}
	for (i = 0 ; i < g->a->nbClocks ; i++)
	{
		ret->valuation[i] = 0;
	}

	ret->date = 0;

	fprintf(ret->log, "Enforcer initialized.\n");

	return ret;
}

enum Strat enforcer_getStrat(const struct Enforcer *e)
{
	enum Strat ret;
	
#if 0
#ifdef ENFORCER_PRINT_LOG
	fprintf(e->log, "enforcer_getStrat: ");
	fprintf(e->log, "(%s, %s) - (%s, %s)", e->realNode->z->name, 
			e->realNode->realWord, e->stratNode->z->name, 
			e->stratNode->realWord);
	fprintf(e->log, "- strat: %s\n", (e->stratNode->isWinning && e->stratNode != 
				e->realNode) ? "emit" : "dontemit");
#endif
#endif
	if (e->strat == NULL)
	{
#ifdef ENFORCER_PRINT_LOG
	fprintf(e->log, "getStrat: dontemit (empty)\n");
#endif

		return STRAT_DONTEMIT;
	}

	ret = (enum Strat)((uintptr_t)list_first(e->strat->strats));

#ifdef ENFORCER_PRINT_LOG
	fprintf(e->log, "getStrat: %s\n", (ret == STRAT_EMIT) ? "emit" : "dontemit");
#endif

	return ret;
}

unsigned int enforcer_eventRcvd(struct Enforcer *e, const struct Event *event)
{
	struct SymbolTableEl *el;
	struct TimedEvent *te;

#ifdef ENFORCER_PRINT_LOG
	fprintf(e->log, "Event received : (%u, %s)\n", e->date, event->label);
	fprintf(e->log, "(%s, %s)\n", e->realNode->z->name, 
			e->realNode->realWord);
	/*
	fprintf(e->log, "(%s, %s + ", e->realNode->z->name, 
			e->realNode->realWord);
	fifo_print(e->realBuffer, (void (*)(const void *))printEvent);
	fprintf(e->log, ") - (%s, %s)\n", e->stratNode->z->name, e->stratNode->realWord);
	*/
#endif

	te = malloc(sizeof *te);
	if (te == NULL)
	{
		perror("malloc enforcer_eventRcvd:te");
		exit(EXIT_FAILURE);
	}
	te->date = e->date;
	te->event = strdup(event->label);
	fifo_enqueue(e->input, te);

	el = list_search(e->g->contsTable, event->label, cmpSymbolLabel);
	if (el != NULL)
		enforcer_storeCont(e, el);
	else
	{
		el = list_search(e->g->uncontsTable, event->label, cmpSymbolLabel);
		if (el == NULL)
		{
			fprintf(e->log, "ERROR: event %s unknown. Ignoring...\n", 
					event->label);
			return 0;
		}
		enforcer_passUncont(e, el);

	}

	return enforcer_computeDelay(e);
}

unsigned int enforcer_emit(struct Enforcer *e)
{
	struct PrivateEvent *event;
	struct SymbolTableEl *sym;
	struct TimedEvent *te;
	struct ListIterator *it;
	//struct Dbmw *prevZone = e->realNode->z->dbm;
	struct List *leavesToSuppress, *leavesStillGood;
	int i;

	if (list_isEmpty(e->realBuffer))
	{
		fprintf(e->log, "ERROR: cannot emit with an empty buffer.\n");
		enforcer_free(e);
		exit(EXIT_FAILURE);
	}
	
	event = list_removeHead(e->realBuffer);
	sym = e->g->contsEls[(unsigned char)event->c];
	if (sym == NULL)
	{
		fprintf(e->log, "ERROR: could not find symbol associated to char %c\n", 
				e->realNode->word[0]);
		exit(EXIT_FAILURE);
	}

#ifdef ENFORCER_PRINT_LOG
	fprintf(e->log, "emitting (%u, %s)\n", e->date, sym->sym);
#endif

	te = malloc(sizeof *te);
	if (te == NULL)
	{
		perror("malloc enforcer_emit:te");
		exit(EXIT_FAILURE);
	}
	te->date = e->date;
	te->event = strdup(sym->sym);
	fifo_enqueue(e->output, te);

	for (it = listIterator_first(e->realNode->z->resetsConts[sym->index]) ; 
			listIterator_hasNext(it) ; it = listIterator_next(it))
	{
		struct Clock *c = listIterator_val(it);
		e->valuation[clock_getIndex(c)] = 0;
	}
	listIterator_release(it);

	e->realNode = e->realNode->p0.succEmit;
	it = listIterator_first(e->realBuffer);
	i = 0;
	while (e->realNode->word[i++] != '\0')
		it = listIterator_next(it);
	
	while (listIterator_hasNext(it) && !e->realNode->isLeaf)
	{
		struct PrivateEvent *pe = listIterator_val(it);
		e->realNode = e->realNode->p0.succStopEmit->p1.succsCont[pe->index];
		it = listIterator_next(it);
	}
	listIterator_release(it);

	leavesToSuppress = list_new();
	leavesStillGood = list_new();
	for (i = 0 ; i < NBLEAVESTYPES ; i++)
	{
		struct List *tmp;
		
		for (it = listIterator_first(e->leaves[i]) ; listIterator_hasNext(it) ; 
				it = listIterator_next(it))
		{
			struct StratNode *sn = listIterator_val(it);
			enum Strat st;

			if (list_isEmpty(sn->strats))
			{
				list_addHead(leavesToSuppress, sn);
				if (e->strat == sn)
					e->strat = NULL;
				continue;
			}
			st = (enum Strat)((uintptr_t)list_first(sn->strats));
			if (st != STRAT_EMIT)
				list_addHead(leavesToSuppress, sn);
			else
			{
				list_removeHead(sn->strats);
				if (list_isEmpty(sn->strats))
				{
					list_addHead(leavesToSuppress, sn);
					if (e->strat == sn)
						e->strat = NULL;
				}
				else
					list_addHead(leavesStillGood, sn);
			}
		}
		listIterator_release(it);

		list_cleanup(leavesToSuppress, (void (*)(void *))stratNode_free);
		list_cleanup(e->leaves[i], NULL);
		tmp = e->leaves[i];
		e->leaves[i] = leavesStillGood;
		leavesStillGood = tmp;
	}
	list_free(leavesToSuppress, NULL);
	list_free(leavesStillGood, NULL);

	if (e->strat == NULL)
		enforcer_computeStrats(e, 1);

	/*
	if (e->stratNode == e->realNode || !dbmw_isPointIncluded(prevZone, 
				e->valuation))
		enforcer_computeStratNode(e);
	*/

#ifdef ENFORCER_PRINT_LOG
	fprintf(e->log, "emit: (%s, %s", e->realNode->z->name, 
			e->realNode->realWord);
	//fifo_print(e->realBuffer, (void (*)(const void *))printEvent);
	fprintf(e->log, ") - (%s, %s)\n", e->stratNode->z->name, e->stratNode->realWord);
#endif

	return enforcer_computeDelay(e);
}

unsigned int enforcer_delay(struct Enforcer *e, unsigned int delay)
{
	int i, changed = 0;
	struct ListIterator *it;
	struct List *leavesBad, *leavesStillGood;
	
	for (i = 1 ; i < e->g->a->nbClocks ; i++)
	{
		e->valuation[i] += delay;
	}

	leavesBad = list_new();
	leavesStillGood = list_new();
	while (!dbmw_isPointIncluded(e->realNode->z->dbm, e->valuation))
	{
		struct List *tmp;

		changed = 1;
#ifdef ENFORCER_PRINT_LOG
		//fprintf(e->log, "Switching to next zone\n");
#endif
		e->realNode = e->realNode->p0.succStopEmit->p1.succTime;

		for (i = 0 ; i < NBLEAVESTYPES ; i++)
		{
			for (it = listIterator_first(e->leaves[i]) ; 
					listIterator_hasNext(it) ; it = listIterator_next(it))
			{
				struct StratNode *sn = listIterator_val(it);
				enum Strat st;

				if (list_isEmpty(sn->strats))
				{
					list_addHead(leavesBad, sn);
					continue;
				}
				
				st = (enum Strat)((uintptr_t)list_first(sn->strats));

				if (st == STRAT_EMIT)
					list_append(leavesBad, sn);
				else
				{
					list_removeHead(sn->strats);
					if (list_isEmpty(sn->strats))
					{
						list_append(leavesBad, sn);
						if (e->strat == sn)
							e->strat = NULL;
					}
					else
						list_append(leavesStillGood, sn);
				}
			}
			list_cleanup(leavesBad, (void (*)(void *))stratNode_free);
			list_cleanup(e->leaves[i], NULL);
			tmp = e->leaves[i];
			e->leaves[i] = leavesStillGood;
			leavesStillGood = tmp;
		}

		if (e->realNode == NULL)
		{
			fprintf(stderr, "ERROR: zone unreachable\n");
			exit(EXIT_FAILURE);
		}
	}
	list_free(leavesStillGood, NULL);
	list_free(leavesBad, (void (*)(void *))stratNode_free);

	if (changed)
		/*
		enforcer_computeStratNode(e);
		*/
		enforcer_computeStrats(e, 0);

	e->date += delay;

#ifdef ENFORCER_PRINT_LOG
	fprintf(e->log, "delay(%u, t = %u): (%s, %s) - (%s, %s)\n", delay, e->date,
			e->realNode->z->name, e->realNode->realWord, e->stratNode->z->name, 
			e->stratNode->realWord);
#endif

	return enforcer_computeDelay(e);
}

void enforcer_free(struct Enforcer *e)
{
	FILE *out = e->log;
	int i;
	/* char *s; */

	fprintf(e->log, "Shutting down the enforcer...\n");
	fprintf(e->log, "Summary of the execution:\n");

	fprintf(e->log, "Input: ");
	while (!fifo_isEmpty(e->input))
	{
		struct TimedEvent *te = fifo_dequeue(e->input);
		fprintf(e->log, "(%d, %s) ", te->date, te->event);
		free(te->event);
		free(te);
	}
	fifo_free(e->input);

	fprintf(e->log, "\nOutput: ");
	while (!fifo_isEmpty(e->output))
	{
		struct TimedEvent *te = fifo_dequeue(e->output);
		fprintf(e->log, "(%d, %s) ", te->date, te->event);
		free(te->event);
		free(te);
	}
	fifo_free(e->output);

	fprintf(e->log, "\nRemaining events in the buffer: ");
	/*
	s = e->realNode->word;
	while (*s != '\0')
	{
		struct SymbolTableEl *el = e->g->contsEls[(unsigned char)*(s++)];
		fprintf(e->log, "%s ", el->sym);
	}
	*/
	while (!list_isEmpty(e->realBuffer))
	{
		struct PrivateEvent *pe = list_removeHead(e->realBuffer);
		struct SymbolTableEl *el = e->g->contsEls[(unsigned char)pe->c];
		if (el == NULL)
		{
			fprintf(e->log, "No symbol found for character %c. Aborting.\n", 
					pe->c);
			exit(EXIT_FAILURE);
		}
		fprintf(e->log, "%s ", el->sym);
		free(pe);
	}
	fprintf(e->log, "\n");
	list_free(e->realBuffer, NULL);
	free(e->valuation);

	for (i = 0 ; i < NBLEAVESTYPES ; i++)
	{
		list_free(e->leaves[i], (void (*)(void *))stratNode_free);
	}

	fprintf(e->log, "VERDICT: %s\n", (e->realNode->isAccepting) ? "WIN" : 
			"LOSS");
	free(e);

	fprintf(out, "Enforcer shutdown.\n");
}


