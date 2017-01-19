#include "game_graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <list.h>
#include <set.h>
#include <fifo.h>
#include <tree.h>

#include "parser.h"
#include "dbmutils.h"

#define EVENTSEP		""
#define EVENTSEPSIZE	0	/* strlen(EVENTSEP) */
#define NBSUCCS			256


enum ContType {CONTROLLABLE, UNCONTROLLABLE};

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
	struct List *contSuccs[NBSUCCS];
	struct List *uncontSuccs[NBSUCCS];
	int isLastCreated;
	unsigned int index;
};

struct StateEdge
{
	const struct State *to;
	struct Dbmw *dbm;
	struct List *resets;
};

struct TimedAutomaton
{
	struct State *states;
	struct State *sinkBadState;
	unsigned int nbStates;
	struct Clock **clocks;
	unsigned int nbClocks;
	const struct List *contsTable;
	const struct List *uncontsTable;
	const struct SymbolTableEl **contsEls;
	const struct SymbolTableEl **uncontsEls;
};

struct Zone
{
	const struct State *s;
	struct Dbmw *dbm;
	struct Zone **contSuccs;
	struct List **resetsConts;
	struct Zone **uncontSuccs;
	struct List **resetsUnconts;
	struct Zone *timeSucc;
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
	struct List *zones;
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
	const struct StringArray *sa;
	const struct Graph *g;
	char *realWord;
	int owner;
	union
	{
		struct
		{
			struct Node *succEmit;
			struct Node *succEmitAll;
			struct Node *succStopEmit;
			struct List *predsEmit;
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
	struct List *contsTable;
	struct List *uncontsTable;
	struct SymbolTableEl *contsEls[256];
	struct SymbolTableEl *uncontsEls[256];
	char *contsChars;
	char *uncontsChars;
	struct TimedAutomaton *a;
	struct ZoneGraph *zoneGraph;
	struct List *nodes;
	struct List *nodesP[2];
	struct List *lastCreated;
	struct Node **baseNodes;
	unsigned int nbNodes;
	unsigned int nbConts;
	unsigned int nbUnconts;
	unsigned int nbZones;
};

struct Enforcer
{
	const struct Graph *g;
	const struct Node *stratNode;
	const struct Node *realNode;
	struct Fifo *realBuffer;
	struct Fifo *input;
	struct Fifo *output;
	FILE *log;
	int32_t *valuation;
	unsigned int date;
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

static int cmpSymbolChar(const void *val, const void *pel);
static int cmpSymbolLabel(const void *val, const void *pel);
static int cmpSymId(const void *val, const void *pel);
static int cmpStateId(const void *val, const void *pState);
static int cmpNode(const void *val, const void *pNode);
static int cmpEdge(const void *pe1, const void *pe2);
#if 0
static int cmpLastBufferChar(const void *pNode1, const void *pNode2);
#endif
static int notSetIn(struct Set *s, void *el);
static int eqPtr(const void *p1, const void *p2);
static int listIn(struct List *l, void *data);
static void listAddNoDouble(struct List *l, void *data);
static void removeSetFromList(struct List *l, const struct Set *s);
static void symbolTableEl_free(struct SymbolTableEl *el);
static char *computeRealWord(const struct Graph *g, const char *word);
static struct Node *node_new(const struct Graph *, const struct Zone *, const 
		struct StringArray *sa, int owner);
#if 0
static struct Node *node_nextCont(const struct Graph *g, const struct Node 
		*prev, char cont);
#endif
static struct Node *node_succCont(const struct Graph *, const struct Node *prev, 
		char cont);
static struct Node *node_succUncont(const struct Graph *, const struct Node 
		*prev, char uncont);
static void node_addEdgeNoDouble(struct Node *n, enum EdgeType type, struct Node 
		*succ);
static void node_removeEdge(struct Node *n, enum EdgeType type, struct Node 
		*succ);
static void node_addEdgeStop(struct Node *n, struct Node *succ);
static void node_addEdgeCont(struct Node *n, struct Node *succ, int);
static void node_addEdgeUncont(struct Node *n, struct Node *succ, int);
static void node_addEdgeEmit(struct Node *n, struct Node *succ);
static void node_free(Node *n);
static const struct State *state_nextCont(const struct State *, char c, const 
		struct Dbmw *);
static const struct State *state_nextUncont(const struct State *, char c, const 
		struct Dbmw *);
static void state_free(struct State *s);
static struct StateEdge *stateEdge_new(const struct TimedAutomaton *, const struct 
		State *, const struct List *guards, const struct List *resets);
static void stateEdge_free(struct StateEdge *);
static struct Edge *edge_new(enum EdgeType type, struct Node *n);
static void edge_free(struct Edge *e);
static struct Zone *zone_new(const struct State *, struct Dbmw *, const struct 
		ZoneGraph *);
static struct Zone *zone_newcp(const struct Zone *);
static int zone_areEqual(const struct Zone *, const struct Zone *);
static void zone_free(struct Zone *);
static void createChars(const struct List *l, struct List **psymbolTable, char 
		**pchars, struct SymbolTableEl *[256]);
static unsigned int contIndex(const struct Graph *g, char c);
static unsigned int uncontIndex(const struct Graph *g, char u);
static void node_setWinning(void *dummy, void *pn);
static void computeStrat(void *dummy, Node *n);
static void addEdgesToLast(struct Graph *g);
static void Node_freeWithLinks(struct Graph *g, Node *n);
static void attr(struct Set *ret, struct Graph *g, int player, const struct Set 
		*U, struct List *nodes);
static void computeW0(struct Graph *g, struct Set *ret);
static void addNodes(struct Graph *g, struct Tree *);
static struct TimedAutomaton *timedAutomaton_new(const struct List *contsTable, 
		const struct SymbolTableEl *contsEls[], const struct List *uncontsTable, 
		const struct SymbolTableEl *uncontsEls[], const struct List *states, 
		const struct List *clocks, const struct List *edges);
static struct ArrayTwo *arraytwo_new(unsigned int size, int defaultVal);
static struct ArrayTwo *arraytwo_newcp(const struct ArrayTwo *);
static void arraytwo_cp(struct ArrayTwo *, const struct ArrayTwo *);
static int arraytwo_cmp(const struct ArrayTwo *, const struct ArrayTwo *);
static void arraytwo_free(struct ArrayTwo *);
static void computeTree(struct Tree *, struct Graph *);


static int cmpSymbolChar(const void *val, const void *pel)
{
	const struct SymbolTableEl *el = pel;
	const char c = *(const char *)val;
	return (el->c == c);
}

static int cmpSymbolLabel(const void *val, const void *pel)
{
	const struct SymbolTableEl *el = pel;
	const char *label = val;
	return (strcmp(val, el->sym) == 0);
}

static int cmpSymId(const void *val, const void *pel)
{
	const struct SymbolTableEl *el = pel;
	const unsigned int id = *(const unsigned int *)val;
	return (el->id == id);
}

static int cmpStateId(const void *val, const void *pState)
{
	const struct State *s = pState;
	const int id = *(const int *)val;
	return (s->parserStateId == id);
}

static int cmpNode(const void *val, const void *pNode)
{
	const struct Node *n = pNode;
	const struct SearchNode *s = val;
	return (n->z == s->z && n->owner == s->owner &&
			strcmp(s->word, n->sa->s) == 0);
}

static int cmpEdge(const void *pe1, const void *pe2)
{
	const struct Edge *e1 = pe1;
	const struct Edge *e2 = pe2;
	return (e1->type == e2->type && e1->succ == e2->succ);
}

#if 0
static int cmpLastBufferChar(const void *pNode1, const void *pNode2)
{
	const Node *n1 = pNode1, *n2 = pNode2;
	return (n1->word[strlen(n1->word)-1] == n2->word[strlen(n2->word)-1]);
}
#endif 

static int notSetIn(struct Set *s, void *el)
{
	return !set_in(s, el);
}

static int eqPtr(const void *p1, const void *p2)
{
	return (p1 == p2);
}

static int listIn(struct List *l, void *data)
{
	return (list_search(l, data, eqPtr) != NULL);
}

static void listAddNoDouble(struct List *l, void *data)
{
	if (!listIn(l, data))
		list_add(l, data);
}

static void removeSetFromList(struct List *l, const struct Set *s)
{
	set_applyToAll(s, (void (*)(void*, void*))list_remove, l);
}

static void symbolTableEl_free(struct SymbolTableEl *el)
{
	free(el->sym);
	free(el);
}

static char *computeRealWord(const struct Graph *g, const char *word)
{
	char *label;
	int size = 0, i;
	struct SymbolTableEl **syms;
	struct SymbolTableEl starSym, plusSym;

	starSym.sym = "*";
	starSym.size = 1;
	starSym.c = '*';
	starSym.id = -1;
	plusSym.sym = "+";
	plusSym.size = 1;
	plusSym.c = '+';
	plusSym.id = -1;

	syms = malloc(strlen(word) * sizeof *syms);
	if (syms == NULL)
	{
		perror("malloc syms");
		exit(EXIT_FAILURE);
	}

	i = 0;
	while (word[i] != '\0')
	{
		struct SymbolTableEl *s;
		if (word[i] == '*')
			s = &starSym;
		else if (word[i] == '+')
			s = &plusSym;
		else
		{
			s = list_search(g->contsTable, &(word[i]), cmpSymbolChar);
			if (s == NULL)
			{
				fprintf(stderr, "ERROR: could not find symbol associated to " 
						"%c\n", word[i]);
				exit(EXIT_FAILURE);
			}
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
	while (word[i] != '\0')
	{
		strcat(label, syms[i]->sym);
		i++;
	}

	free(syms);

	return label;
}

static struct Node *node_new(const struct Graph *g, const struct Zone *z, const 
		struct StringArray *sa, int owner)
{
	int i;
	struct Node *n = malloc(sizeof *n);

	if (n == NULL)
	{
		perror("malloc node");
		exit(EXIT_FAILURE);
	}

	n->z = z;
	n->sa = sa;
	n->owner = owner;
	n->isAccepting = z->s->isAccepting;
	n->isInitial = (z->s->isInitial && sa->s[0] == '\0' && owner == 0 && 
			dbmw_containsZero(z->dbm));
	n->isWinning = 0;
	n->isLeaf = 0;
	n->realWord = computeRealWord(g, sa->s);
	n->edges = list_new();
	n->userData = NULL;
	n->g = g;
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

#if 0
static struct Node *node_nextCont(const struct Graph *g, const struct Node 
		*prev, char cont)
{
	int wordSize = strlen(prev->sa->s) + 1;
	char *newWord = malloc(wordSize + 1);
	struct Node *n;

	if (newWord == NULL)
	{
		perror("malloc newWord");
		exit(EXIT_FAILURE);
	}

	strcpy(newWord, prev->word);
	newWord[wordSize-1] = cont;
	newWord[wordSize] = '\0';

	n = node_new(g, prev->q, newWord, prev->owner);

	free(newWord);

	return n;
}
#endif

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

static struct Node *node_succCont(const struct Graph *g, const struct Node *prev, 
		char cont)
{
	if (prev->owner == 0)
		prev = prev->p0.succStopEmit;
	return prev->p1.succsCont[contIndex(g, cont)];
}

static struct Node *node_succUncont(const struct Graph *g, const struct Node 
		*prev, char uncont)
{
	if (prev->owner == 0)
		prev = prev->p0.succStopEmit;
	return prev->p1.succsUncont[uncontIndex(g, uncont)];
}

static void node_addEdgeNoDouble(struct Node *n, enum EdgeType type, struct Node 
		*succ)
{
	struct Edge searchEdge;
	searchEdge.type = type;
	searchEdge.succ = succ;
	if (list_search(n->edges, &searchEdge, cmpEdge) == NULL)
		list_add(n->edges, edge_new(type, succ));
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

static void node_removeEdge(struct Node *n, enum EdgeType type, struct Node 
		*succ)
{
	struct Edge *e, searchEdge;
	searchEdge.type = type;
	searchEdge.succ = succ;
	e = list_search(n->edges, &searchEdge, cmpEdge);
	if (e != NULL)
	{
		list_remove(n->edges, e);
		edge_free(e);
	}
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

	free(n->realWord);
	free(n);
}

static const struct State *state_nextCont(const struct State *s, char c, const struct 
		Dbmw *guards)
{
	struct ListIterator *it;
	struct List *edges = s->contSuccs[(unsigned char)c];

	for (it = listIterator_first(edges) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct StateEdge *e = listIterator_val(it);
		if (dbmw_intersects(e->dbm, guards))
			return e->to;
	}
	listIterator_release(it);
}

static const struct State *state_nextUncont(const struct State *s, char c, const 
		struct Dbmw *guards)
{
	struct ListIterator *it;
	struct List *edges = s->uncontSuccs[(unsigned char)c];

	for (it = listIterator_first(edges) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct StateEdge *e = listIterator_val(it);
		if (dbmw_intersects(e->dbm, guards))
			return e->to;
	}
	listIterator_release(it);
}

static void state_free(struct State *s)
{
	free(s->name);
}

static struct StateEdge *stateEdge_new(const struct TimedAutomaton *a, const struct 
		State *s, const struct List *constraints, const struct List *resets)
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
		const struct ParserClock *pclock = parserClockConstraint_getClock(pconstraint);
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
			list_add(ret->resets, c);
	}
	listIterator_release(it);

	return ret;
}

static void stateEdge_free(struct StateEdge *e)
{
	dbmw_free(e->dbm);
	list_free(e->resets, NULL);
	free(e);
}

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

static void zone_addEdge(struct Zone *z, const struct Zone *succ, enum EdgeType 
		type)
{
	struct ZoneEdge *edge = zoneEdge_new(type, succ);
	if (list_search(z->edges, edge, (int (*)(const void *, const void 
						*))zoneEdge_cmp) == NULL)
		list_add(z->edges, edge);
}

static int zone_areEqual(const struct Zone *z1, const struct Zone *z2)
{
	return (z1->s == z2->s && dbmw_areEqual(z1->dbm, z2->dbm));
}

static struct Zone *zone_nextCont(const struct Zone *z, char c)
{
	struct Zone *ret = z->contSuccs[z->a->contsEls[(unsigned char)c]->index];
	const struct SymbolTableEl *el = z->a->contsEls[(unsigned char)c];
	if (ret == NULL)
		ret = z->zg->sinkZone;

	return ret;
}

static void zone_free(struct Zone *z)
{
	dbmw_free(z->dbm);
	free(z->contSuccs);
	free(z->uncontSuccs);
	list_free(z->edges, NULL);
	free(z);
}

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
	
static void createChars(const struct List *l, struct List **psymbolTable, char 
		**pchars, struct SymbolTableEl *elTable[256])
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
		struct SymbolTableEl *el = malloc(sizeof *el);

		if (el == NULL)
		{
			perror("malloc el");
			exit(EXIT_FAILURE);
		}
		el->c = chars[i];
		el->sym = strdup(parserSymbol_getLabel(s));
		el->id = parserSymbol_getId(s);
		el->size = strlen(el->sym);
		el->index = i;
		list_add(symbolTable, el);
		elTable[(unsigned char)el->c] = el;
	}
	listIterator_release(it);
}

static inline unsigned int contIndex(const struct Graph *g, char c)
{
	return g->contsEls[(unsigned char)c]->index;
}

static inline unsigned int uncontIndex(const struct Graph *g, char u)
{
	return g->uncontsEls[(unsigned char)u]->index;
}

static void node_setWinning(void *dummy, void *pn)
{
	Node *n = pn;
	(void)dummy;
	n->isWinning = 1;
}

static void computeStrat(void *dummy, Node *n)
{
	if (n->owner == 0 && n->p0.succEmit != NULL && n->p0.succEmit->isWinning)
		n->p0.strat = STRAT_EMIT;
}

#if 0
static void addEdgesToLast(struct Graph *g)
{
	struct ListIterator *it;
	struct SearchNode sn;
	struct Node *dest;
	struct Edge *e;
	int i;

	/* Add internal edges (i.e. edges between two nodes in lastCreated) */
	for (it = listIterator_first(g->lastCreated) ; listIterator_hasNext(it) ; it 
			= listIterator_next(it))
	{
		struct Node *n = listIterator_val(it);
		if (n->owner == 0)
		{
			/* (q, w, 0) --> (q, w, 1) */
			sn.q = n->q;
			sn.word = n->word;
			sn.owner = 1;
			dest = list_search(g->lastCreated, &sn, cmpNode);
			if (dest == NULL)
			{
				fprintf(stderr, "ERROR: cannot find node (%s, %s, 1) in "
						"lastCreated.\n", n->q->name, n->word);
				exit(EXIT_FAILURE);
			}
			n->p0.succStopEmit = dest;
			dest->p1.predStop = n;
			list_add(n->edges, edge_new(STOPEMIT, dest));

			if (n->word[0] != '\0')
			{
				char c;

				/* (q, c . w, 0) --> (q after c, w, 0) */
				sn.q = n->q->contSuccs[(unsigned char)n->word[0]];
				sn.word = n->word + 1; /* Remove first event */
				sn.owner = 0;
				dest = list_search(g->nodes, &sn, cmpNode);
				if (dest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find node (%s, %s, 0) in "
							"nodes.\n", n->q->name, n->word + 1);
					exit(EXIT_FAILURE);
				}
				n->p0.succEmit = dest;
				list_add(dest->p0.predsEmit, n);
				list_add(n->edges, edge_new(EMIT, dest));

				/* (q, w.c, 0) <-- (q, w, 1) */
				sn.q = n->q;
				sn.word = n->word;
				c = n->word[strlen(n->word) - 1];
				n->word[strlen(n->word) - 1] = '\0';
				sn.owner = 1;
				dest = list_search(g->nodes, &sn, cmpNode);
				if (dest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find node (%s, %s, 1) in "
							"nodes.\n", sn.q->name, n->word);
					exit(EXIT_FAILURE);
				}
				n->word[strlen(n->word)] = c;
				/* !! dest --> n !! */
				dest->p1.succsCont[contIndex(g, c)] = n;
				n->p0.predContRcvd = dest;
				list_add(dest->edges, edge_new(CONTRCVD, n));
			}
		}
		else
		{
			int size = strlen(g->uncontsChars);
			char c;
			/* (q, w, 1) --> (q after u, w, 0) */
			for (i = 0 ; i < size ; i++)
			{
				sn.q = n->q->uncontsSuccs[(unsigned char)g->uncontsChars[i]];
				sn.word = n->word;
				sn.owner = 0;
				dest = list_search(g->lastCreated, &sn, cmpNode);
				if (dest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find node (%s, %s, 1) in "
							"lastCreated.\n", n->q->name, n->word);
					exit(EXIT_FAILURE);
				}
				list_add(n->p1.succsUncont, dest);
				list_add(dest->p0.predsUncont, n);
				list_add(n->edges, edge_new(UNCONTRCVD, dest));
			}
		}
	}

	listIterator_release(it);
}
#endif

static void Node_freeWithLinks(struct Graph *g, Node *n)
{
	node_free(n);
	list_remove(g->nodes, n);
}

static void attr(struct Set *ret, struct Graph *g, int player, const struct Set 
		*U, struct List *nodes)
{
	int stable = 0;
	struct ListIterator *it, *it2;
	int i;

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
				/*
				if (n->owner == 0)
				{
					if ((listIn(nodes, n->p0.succEmit) && set_in(ret, 
									n->p0.succEmit))
						|| (listIn(nodes, n->p0.succStopEmit) && set_in(ret, 
								n->p0.succStopEmit)))
					{
						stable = 0;
						set_add(ret, n);
					}
				}
				else
				{
					struct ListIterator *it2;

					for (i = 0 ; i < g->nbConts ; i++)
					{
						Node *succ = n->p1.succsCont[i];
						if (succ != NULL && listIn(nodes, succ) && set_in(ret, 
									succ))
						{
							stable = 0;
							set_add(ret, n);
							break;
						}
					}

					if (stable)
					{
						for (it2 = listIterator_first(n->p1.succsUncont) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							Node *succ = listIterator_val(it2);
							if (listIn(nodes, succ) && set_in(ret, succ))
							{
								stable = 0;
								set_add(ret, n);
								break;
							}
						}
						listIterator_release(it2);
					}
				}
				*/
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
				/*
				if (n->owner == 0)
				{
					if ((n->word[0] == '\0' ||
							(listIn(nodes, n->p0.succEmit) && 
								set_in(ret, n->p0.succEmit))) 
						&& 
						listIn(nodes, n->p0.succStopEmit) &&
							set_in(ret, n->p0.succStopEmit))
					{
						stable = 0;
						set_add(ret, n);
					}
				}
				else
				{
					int ok = 1;

					for (i = 0 ; i < g->nbConts ; i++)
					{
						Node *succ = n->p1.succsCont[i];
						if (succ != NULL && listIn(nodes, succ) && !set_in(ret, 
									succ))
						{
							ok = 0;
							break;
						}
					}
					if (ok)
					{
						for (it2 = listIterator_first(n->p1.succsUncont) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							Node *succ = listIterator_val(it2);
							if (succ != NULL && listIn(nodes, succ) && 
									!set_in(ret, succ))
							{
								ok = 0;
								break;
							}
						}
						listIterator_release(it2);
					}
				}
			*/
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

static void computeW0(struct Graph *g, struct Set *ret)
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
		list_add(S, n);
		set_add(Sset, n);
		if (n->isAccepting)
			set_add(B, n);
	}
	listIterator_release(it);
	set_reset(ret);

	do
	{
		attr(R, g, 0, B, S);

		/* Tr = S \ B */
		set_reset(Tr);
		for (it = listIterator_first(S) ; listIterator_hasNext(it) ; it = 
				listIterator_next(it))
		{
			set_add(Tr, listIterator_val(it));
		}
		listIterator_release(it);
		set_remove(Tr, (int (*)(const void *, const void *))set_in, R);

		attr(W, g, 1, Tr, S);
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

static void addNodesRec(struct Graph *g, const struct Zone *z, struct Tree 
		*stringArrays, struct Node *pred)
{
	struct StringArray *sa;
	struct Node *n[2];
	int i, nbSons;

	sa = tree_getData(stringArrays);
	for (i = 0 ; i < 2 ; i++)
	{
		n[i] = node_new(g, z, sa, i);
		list_add(g->nodes, n[i]);
		list_add(g->nodesP[i], n[i]);
	}

	node_addEdgeStop(n[0], n[1]);
	if (pred != NULL)
		node_addEdgeCont(pred, n[0], contIndex(g, sa->s[strlen(sa->s) - 1]));
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
		addNodesRec(g, z, tree_getSon(stringArrays, i), n[1]);
	}
}

static void addNodes(struct Graph *g, struct Tree *stringArrays)
{
	struct ListIterator *it;

	for (it = listIterator_first(g->zoneGraph->zones) ; listIterator_hasNext(it) 
			; it = listIterator_next(it))
	{
		struct Zone *z = listIterator_val(it);
		addNodesRec(g, z, stringArrays, NULL);
	}
	listIterator_release(it);

	g->nbNodes = list_size(g->nodes);
}

static void addEmitEdges(struct Graph *g)
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
		if (n->sa->s[0] == '\0')
			continue;

		next = g->baseNodes[n->z->contSuccs[g->contsEls[(unsigned 
				char)(n->sa->s[0])]->index]->index];
		remain = n->sa->s + 1;

		while (*remain != '\0')
		{
			next = node_succCont(g, next, *remain);
			remain++;
		}

		node_addEdgeEmit(n, next);
	}
	listIterator_release(it);
}

static void addUncontEdges(struct Graph *g)
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

			remain = n->sa->s;

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

static void addTimeEdges(struct Graph *g)
{
	struct ListIterator *it;
	char *remain;
	struct Node *next;
	int i;

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

		remain = n->sa->s;

		while (*remain != '\0')
		{
			next = node_succCont(g, next, *remain);
			remain++;
		}

		node_addEdgeTime(n, next);
	}
	listIterator_release(it);
}

static void addEmitAllEdges(struct Graph *g)
{
	struct ListIterator *it;

	for (it = listIterator_first(g->nodes) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct Node *n = listIterator_val(it);
		if (n->owner == 0 && n->isLeaf)
		{
			n->p0.succEmitAll = n;
			while (n->p0.succEmitAll->p0.succEmit != NULL)
			{
				n->p0.succEmitAll = n->p0.succEmitAll->p0.succEmit;
			}
		}
	}
	listIterator_release(it);
}

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
		list_add(a->sinkBadState->contSuccs[(unsigned char)el->c], 
				stateEdge_new(a, a->sinkBadState, list_new(), list_new()));
	}
	listIterator_release(it);

	for (it = listIterator_first(a->uncontsTable) ; listIterator_hasNext(it) ; it 
			= listIterator_next(it))
	{
		struct SymbolTableEl *el = listIterator_val(it);
		for (i = 0 ; i < a->nbStates + 1 ; i++)
			a->states[i].uncontSuccs[(unsigned char)el->c] = list_new();
		list_add(a->sinkBadState->uncontSuccs[(unsigned char)el->c], 
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
			list_add(from->contSuccs[(unsigned char)el->c], se);
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
			list_add(from->uncontSuccs[(unsigned char)el->c], se);
		}
	}
	listIterator_release(it);

	return a;
}

void timedAutomaton_free(struct TimedAutomaton *a)
{
	int i, j;
	for (i = 0 ; i < a->nbStates ; i++)
	{
		for (j = 0 ; j < 256 ; j++)
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
	for (i = 1 ; i < a->nbClocks ; i++)
	{
		clock_free(a->clocks[i]);
	}
	free(a);
}

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
		struct Graph *g, char c)
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

static void stringArray_print(const struct StringArray *sa)
{
	int i, j;

	printf("%s\n", (sa->s[0] == '\0') ? "-" : sa->s);
	
	for (i = 0 ; i < sa->array->size ; i++)
	{
		for (j = 0 ; j < sa->array->size ; j++)
		{
			printf("%d ", sa->array->tab[i][j]);
		}
		printf("\n");
	}
}

static void stringArray_free(struct StringArray *sa)
{
	free(sa->s);
	arraytwo_free(sa->array);
	free(sa);
}

static void printTree(struct Tree *t)
{
	struct StringArray *sa;
	struct Fifo *fifo = fifo_empty();
	int level = 0;
	int i;

	fifo_enqueue(fifo, t);

	while (!fifo_isEmpty(fifo))
	{
		t = fifo_dequeue(fifo);
		sa = tree_getData(t);

		if (strlen(sa->s) != level)
		{
			printf("\n\n");
			level++;
		}

		stringArray_print(sa);

		for (i = 0 ; i < tree_getNbSons(t) ; i++)
		{
			fifo_enqueue(fifo, tree_getSon(t, i));
		}
	}

	fifo_free(fifo);
}

static struct Tree *computeStrings(struct Graph *g)
{
	struct Tree *t;
	int i, j;
	struct StringArray *root = stringArray_new(g);

	t = tree_new(root);
	computeTree(t, g);

	//printTree(t);
	
	return t;
}

static void computeTree(struct Tree *t, struct Graph *g)
{
	struct ListIterator *it;
	struct StringArray *sap = tree_getData(t), *sa;
	struct Tree *son;
	int i, j;

	for (i = 0 ; i < g->nbConts ; i++)
	{
		sa = stringArray_newNext(sap, g, g->contsChars[i]);

		son = tree_new(sa);
		tree_addSon(t, son);
		if (!arraytwo_cmp(sa->array, sap->array))
			computeTree(son, g);
	}
}

/*
 * returns a list of DBMs, not of zones
 */
struct List *splitZones2(const struct Zone *z0, const struct Zone *z1)
{
	struct List *ret;
	struct List *dbms = list_new();
	int i;
	struct ListIterator *it;

	list_add(dbms, dbmw_newcp(z0->dbm));
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
							list_add(dbms, z);
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
							list_add(dbms, z);
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
			list_add(dbms, z);
	}

	ret = dbmw_partition(dbms);
	list_free(dbms, (void (*)(void *))dbmw_free);

	return ret;
}

struct List *splitZones(const struct Zone *z, const struct List *rho)
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
		struct List *part = splitZones2(z, z1);

		for (it2 = listIterator_first(part) ; listIterator_hasNext(it2) ; it2 = 
				listIterator_next(it2))
		{
			struct Dbmw *z2 = listIterator_val(it2);
			list_add(splits, z2);
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
		list_add(ret, zone_new(z->s, dbm, z->zg));
	}
	listIterator_release(it);

	list_free(partition, NULL);

	return ret;
}

struct List *pre(const struct Zone *z, const struct List *rho)
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
				list_add(ret, zone_newcp(z2));
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
							list_add(ret, zone_newcp(z2));
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
							list_add(ret, zone_newcp(z2));
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

struct List *post(const struct Zone *z, struct List *rho)
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
				list_add(ret, zone_newcp(z2));
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
							list_add(ret, zone_newcp(z2));
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
							list_add(ret, zone_newcp(z2));
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

struct ZoneGraph *zoneGraph_new(const struct TimedAutomaton *a)
{
	struct List *rho = list_new();
	struct List *alpha = list_new();
	struct List *sigma = list_new();
	struct List *alpha1;
	struct ListIterator *it;
	struct Zone *X, *z;
	int i, ok;
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

	for (i = 0 ; i < a->nbStates ; i++)
	{
		z = zone_new(&(a->states[i]), dbmw_new(a->nbClocks), zg);
		list_add(rho, z);
		if (a->states[i].isInitial)
		{
			struct Zone *z2 = zone_newcp(z);
			list_add(alpha, z2);
			X = z2;
		}
	}

	while (X != NULL)
	{
		alpha1 = splitZones(X, rho);

		if (list_size(alpha1) == 1)
		{
			struct List *posts;

			if (list_search(sigma, X, (int (*)(const void *, const void 
								*))zone_areEqual) == NULL)
				list_add(sigma, zone_newcp(X));


			posts = post(X, rho);

			for (it = listIterator_first(posts) ; listIterator_hasNext(it) ; it 
					= listIterator_next(it))
			{
				z = listIterator_val(it);
				if (list_search(alpha, z, (int (*)(const void *, const void 
									*))zone_areEqual) == NULL)
					list_add(alpha, z);
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
					list_add(alpha, zone_newcp(Y));
				}
			}
			listIterator_release(it);

			pres = pre(X, rho);
			for (it = listIterator_first(pres) ; listIterator_hasNext(it) ; it 
					= listIterator_next(it))
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

			z = list_search(rho, X, (int (*)(const void *, const void *))zone_areEqual);
			if (z != NULL)
				list_remove(rho, z);
			for (it = listIterator_first(alpha1) ; listIterator_hasNext(it) ; it 
					= listIterator_next(it))
			{
				z = listIterator_val(it);
				if (list_search(rho, z, (int (*)(const void *, const void 
									*))zone_areEqual) == NULL)
					list_add(rho, zone_newcp(z));
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
		list_add(zg->zones, z);
		list_add(zg->zonesS[z->s->index], z);
	}
	listIterator_release(it);

	list_add(zg->zones, zg->sinkZone);
	zg->zonesS[a->sinkBadState->index] = list_new();
	list_add(zg->zonesS[a->sinkBadState->index], zg->sinkZone);

	sinkZoneReached = 0;

	for (it = listIterator_first(zg->zones), i = 0 ; listIterator_hasNext(it) ; it = 
			listIterator_next(it), i++)
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
							list_addList(z->resetsConts[el->index], se->resets, 
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
				z->resetsConts = NULL;
				zone_addEdge(z, zg->sinkZone, CONTRCVD);
				sinkZoneReached = 1;
			}
		}
		listIterator_release(it2);

		for (it2 = listIterator_first(a->uncontsTable) ; listIterator_hasNext(it2) 
				; it2 = listIterator_next(it2))
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
							list_addList(z->resetsUnconts[el->index], 
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
		list_remove(zg->zones, zg->sinkZone);

	zg->nbZones = list_size(zg->zones);

	list_free(rho, NULL);

	return zg;
}

const struct List *zoneGraph_getZones(const struct ZoneGraph *zg)
{
	return zg->zones;
}

void zoneGraph_free(struct ZoneGraph *zg)
{
	int i;

	for (i = 0 ; i < zg->a->nbStates ; i++)
	{
		list_free(zg->zonesS[i], NULL);
	}

	list_free(zg->zones, (void (*)(void *))zone_free);

	free(zg);
}

struct Graph *graph_newFromAutomaton(const char *filename)
{
	const struct List *pstates = NULL;
	const struct List *pconts = NULL;
	const struct List *punconts = NULL;
	const struct List *pedges = NULL;
	const struct List *pclocks = NULL;
	struct Tree *strings;
	struct ListIterator *it;
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
	g->lastCreated = list_new();
	g->nbConts = parser_getNbConts();
	g->nbUnconts = parser_getNbUnconts();

	for (i = 0 ; i < 256 ; i++)
	{
		g->contsEls[i] = NULL;
		g->uncontsEls[i] = NULL;
	}
	createChars(pconts, &(g->contsTable), &(g->contsChars), g->contsEls);
	createChars(punconts, &(g->uncontsTable), &(g->uncontsChars), 
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

	strings = computeStrings(g);
	addNodes(g, strings);
	addEmitEdges(g);
	addUncontEdges(g);
	addTimeEdges(g);
	addEmitAllEdges(g);
	computeW0(g, W0);
	
	set_applyToAll(W0, node_setWinning, NULL);
	set_applyToAll(W0, (void (*)(void *, void *))computeStrat, NULL);
	parser_cleanup();

	return g;
}

const struct List *graph_nodes(const struct Graph *g)
{
	return g->nodes;
}

const struct ZoneGraph *graph_getZoneGraph(const struct Graph *g)
{
	return g->zoneGraph;
}

void graph_free(struct Graph *g)
{
	int i;

	list_free(g->contsTable, (void (*)(void *))symbolTableEl_free);
	list_free(g->uncontsTable, (void (*)(void *))symbolTableEl_free);
	list_free(g->lastCreated, NULL);
	list_free(g->nodesP[0], NULL);
	list_free(g->nodesP[1], NULL);
	list_free(g->nodes, (void (*)(void *))node_free);

	zoneGraph_free(g->zoneGraph);
	timedAutomaton_free(g->a);
	free(g->contsChars);
	free(g->uncontsChars);
	free(g);
}


/* Enforcer */
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
	ret->realBuffer = fifo_empty();
	ret->input = fifo_empty();
	ret->output = fifo_empty();
	ret->log = logFile;

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
	fprintf(e->log, "enforcer_getStrat: ");
	fprintf(e->log, "(%s, %s) - (%s, %s)", e->realNode->z->name, 
			e->realNode->sa->s, e->stratNode->z->name, e->stratNode->sa->s);
	fprintf(e->log, "- strat: %s\n", (e->stratNode->p0.strat == EMIT) ? "emit" : 
			"dontemit");
	fprintf(e->log, "%d\n", e->stratNode->owner);
	if (e->stratNode->p0.strat == EMIT && e->realNode->sa->s[0] != '\0')
		return STRAT_EMIT;
	return STRAT_DONTEMIT;
}

static void enforcer_computeStratNode(struct Enforcer *e)
{
	struct PrivateEvent *pe;
	struct Fifo *fifo;

	e->stratNode = e->realNode;
	fifo = fifo_empty();
	while (!fifo_isEmpty(e->realBuffer))
	{
		pe = fifo_dequeue(e->realBuffer);
		fifo_enqueue(fifo, pe);

		if (e->stratNode->isLeaf)
			e->stratNode = e->stratNode->p0.succEmitAll;
		e->stratNode = e->stratNode->p0.succStopEmit
			->p1.succsCont[contIndex(e->g, pe->c)];
	}

	fifo_free(e->realBuffer);
	e->realBuffer = fifo;
}

static unsigned int enforcer_computeDelay(const struct Enforcer *e)
{
	if (e->realNode->p0.succStopEmit->p1.succTime != NULL)
		return dbmw_distance(e->valuation, 
				e->realNode->p0.succStopEmit->p1.succTime->z->dbm);

	return 0;
}

unsigned int enforcer_eventRcvd(struct Enforcer *e, const struct Event *event)
{
	struct SymbolTableEl *el;
	char c;
	struct PrivateEvent *pe;
	struct TimedEvent *te;
	struct Fifo *fifo;


	el = list_search(e->g->contsTable, event->label, cmpSymbolLabel);
	if (el != NULL)
	{
		te = malloc(sizeof *te);
		if (te == NULL)
		{
			perror("malloc enforcer_eventRcvd:te");
			exit(EXIT_FAILURE);
		}
		te->date = e->date;
		te->event = strdup(el->sym);
		fifo_enqueue(e->input, te);

		c = el->c;
		if (!e->realNode->isLeaf)
			e->realNode = e->realNode->p0.succStopEmit->p1.succsCont[el->index];
		else
		{
			pe = malloc(sizeof *pe);
			if (pe == NULL)
			{
				perror("malloc enforcer_eventRcvd:pe");
				exit(EXIT_FAILURE);
			}

			pe->c = c;
			pe->index = el->index;
			pe->type = CONTROLLABLE;
			fifo_enqueue(e->realBuffer, pe);
		}

		if (e->stratNode->isLeaf)
			e->stratNode = e->stratNode->p0.succEmitAll;
		
		e->stratNode = e->stratNode->p0.succStopEmit->p1.succsCont[el->index];
	}
	else
	{
		struct ListIterator *it;
		el = list_search(e->g->uncontsTable, event->label, cmpSymbolLabel);
		if (el == NULL)
		{
			fprintf(e->log, "ERROR: event %s unknown. Ignoring...\n", 
					event->label);
			return 0;
		}

		te = malloc(sizeof *te);
		if (te == NULL)
		{
			perror("malloc enforcer_eventRcvd:te");
			exit(EXIT_FAILURE);
		}
		te->date = e->date;
		te->event = strdup(el->sym);
		fifo_enqueue(e->input, te);

		te = malloc(sizeof *te);
		if (te == NULL)
		{
			perror("malloc enforcer_eventRcvd:te");
			exit(EXIT_FAILURE);
		}
		te->date = e->date;
		te->event = strdup(el->sym);
		fifo_enqueue(e->output, te);

		c = el->c;

		for (it = 
				listIterator_first(e->realNode->p0.succStopEmit->z->resetsUnconts[el->index]) 
				; listIterator_hasNext(it) ; it = listIterator_next(it))
		{
			struct Clock *c = listIterator_val(it);
			e->valuation[clock_getIndex(c)] = 0;
		}
		listIterator_release(it);
		e->realNode = e->realNode->p0.succStopEmit->p1.succsUncont[el->index];
		enforcer_computeStratNode(e);
	}
	fprintf(e->log, "Event received : (%u, %s)\n", e->date, event->label);
	fprintf(e->log, "(%s, %s) - (%s, %s)\n", e->realNode->z->name, 
			e->realNode->sa->s, e->stratNode->z->name, e->stratNode->sa->s);

	return enforcer_computeDelay(e);
}

unsigned int enforcer_emit(struct Enforcer *e)
{
	struct PrivateEvent *pe;
	char *label;
	struct SymbolTableEl *sym;
	struct TimedEvent *te;
	struct ListIterator *it;

	if (e->realNode->sa->s[0] == '\0')
	{
		fprintf(e->log, "ERROR: cannot emit with an empty buffer.\n");
		exit(EXIT_FAILURE);
	}
	
	sym = e->g->contsEls[(unsigned char)e->realNode->sa->s[0]];
	if (sym == NULL)
	{
		fprintf(e->log, "ERROR: could not find symbol associated to char %c\n", 
				e->realNode->sa->s[0]);
		exit(EXIT_FAILURE);
	}

	fprintf(e->log, "(%u, %s)\n", e->date, sym->sym);

	te = malloc(sizeof *te);
	if (te == NULL)
	{
		perror("malloc enforcer_emit:te");
		exit(EXIT_FAILURE);
	}
	te->date = e->date;
	te->event = strdup(sym->sym);
	fifo_enqueue(e->output, te);

	if (e->stratNode == e->realNode && fifo_isEmpty(e->realBuffer))
		e->stratNode = e->stratNode->p0.succEmit;

	for (it = 
			listIterator_first(e->realNode->z->resetsConts[e->g->contsEls[(unsigned 
					char)e->realNode->sa->s[0]]->index]) ; 
			listIterator_hasNext(it) ; it = listIterator_next(it))
	{
		struct Clock *c = listIterator_val(it);
		e->valuation[clock_getIndex(c)] = 0;
	}
	listIterator_release(it);
	e->realNode = e->realNode->p0.succEmit;
	while (!fifo_isEmpty(e->realBuffer) && !e->realNode->isLeaf)
	{
		pe = fifo_dequeue(e->realBuffer);
		e->realNode = e->realNode->p0.succStopEmit->p1.succsCont[pe->index];
	}

	fprintf(e->log, "(%s, %s) - (%s, %s)\n", e->realNode->z->name, 
			e->realNode->sa->s, e->stratNode->z->name, e->stratNode->sa->s);

	return enforcer_computeDelay(e);
}

unsigned int enforcer_delay(struct Enforcer *e, unsigned int delay)
{
	int i, changed = 0;
	for (i = 1 ; i < e->g->a->nbClocks ; i++)
		e->valuation[i] += delay;

	while (!dbmw_isPointIncluded(e->realNode->z->dbm, e->valuation))
	{
		changed = 1;
		fprintf(e->log, "Switching to next zone\n");
		e->realNode = e->realNode->p0.succStopEmit->p1.succTime;
		if (e->realNode == NULL)
		{
			fprintf(stderr, "ERROR: zone unreachable\n");
			exit(EXIT_FAILURE);
		}
	}

	if (changed)
		enforcer_computeStratNode(e);

	e->date += delay;

	return enforcer_computeDelay(e);
}

void enforcer_free(struct Enforcer *e)
{
	FILE *out = e->log;

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
	while (!fifo_isEmpty(e->realBuffer))
	{
		struct PrivateEvent *pe = fifo_dequeue(e->realBuffer);
		struct SymbolTableEl *el = list_search(e->g->contsTable, &(pe->c), 
				cmpSymbolChar);
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
	fifo_free(e->realBuffer);

	fprintf(e->log, "VERDICT: %s\n", (e->realNode->isAccepting) ? "WIN" : 
			"LOSS");
	free(e);

	fprintf(out, "Enforcer shutdown.\n");
}


