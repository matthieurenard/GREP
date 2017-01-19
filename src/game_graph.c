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

struct State
{
	unsigned int parserStateId;
	char *name;
	int isInitial;
	int isAccepting;
	struct List *contSuccs[NBSUCCS];
	struct List *uncontsSuccs[NBSUCCS];
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
	unsigned int nbStates;
	struct Clock **clocks;
	unsigned int nbClocks;
	struct List *contsTable;
	struct List *uncontsTable;
};

struct Zone
{
	const struct State *s;
	struct Dbmw *dbm;
	struct Zone **contSuccs;
	struct Zone **uncontSuccs;
	struct Zone *timeSucc;
};

struct ZoneGraph
{
	struct TimedAutomaton *a;
	struct List *zones;
	struct Zone *z0;
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
	int *lasts;
};

typedef struct Node
{
	const struct State *q;
	const struct StringArray *sa;
	struct Dbmw *dbm;
	char *realWord;
	int owner;
	int meta;
	union
	{
		struct
		{
			union
			{
				struct Node *succEmit;
				struct List *succsEmit;
			};
			struct Node *succEmitAll;
			struct Node *succStopEmit;
			struct List *predsEmit;
			struct List *predsUncont;
			struct Node *predContRcvd;
			struct List *predsContMeta;
			enum Strat strat;
		} p0;
		struct
		{
			struct Node **succsCont;
			struct Node **succsUncont;
			struct Node *predStop;
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
	const struct State *q;
	char *word;
	int owner;
};

struct SymbolTableEl
{
	char *sym;
	unsigned int id;
	char c;
	int size;
	int index;
};

struct Graph
{
	struct List *contsTable;
	struct List *uncontsTable;
	char *contsChars;
	char *uncontsChars;
	struct TimedAutomaton *a;
	struct List *nodes;
	struct List *nodesP[2];
	struct List *lastCreated;
	struct Node **baseNodes;
	unsigned int nbStates;
	unsigned int nbConts;
	unsigned int nbUnconts;
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
};

struct PrivateEvent
{
	char c;
	unsigned int index;
	enum ContType type;
};


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
static struct Node *node_new(const struct Graph *g, const struct State *q, const 
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
static void createChars(const struct List *l, struct List **psymbolTable, char 
		**pchars);
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
		const struct List *uncontsTable, const struct List *states, const struct 
		List *edges);
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
	return (n->q == s->q && n->owner == s->owner &&
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

static struct Node *node_new(const struct Graph *g, const struct State *q, const 
		struct StringArray *sa, int owner)
{
	int i;
	struct Node *n = malloc(sizeof *n);

	if (n == NULL)
	{
		perror("malloc node");
		exit(EXIT_FAILURE);
	}

	n->q = q;
	n->sa = sa;
	n->owner = owner;
	n->isAccepting = q->isAccepting;
	n->isInitial = (q->isInitial && sa->s[0] == '\0' && owner == 0);
	n->isWinning = 0;
	n->isLeaf = 0;
	n->meta = 0;
	n->realWord = computeRealWord(g, sa->s);
	n->edges = list_new();
	n->userData = NULL;
	if (n->owner == 0)
	{
		n->p0.strat = STRAT_DONTEMIT;
		n->p0.succEmit = NULL;
		n->p0.succStopEmit = NULL;
		n->p0.predContRcvd = NULL;
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
	return n->q->name;
}

const char *node_word(const struct Node *n)
{
	return n->realWord;
}

int node_owner(const struct Node *n)
{
	return n->owner;
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
				"has no strategy.\n", n->q->name, n->realWord, n->owner);
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
	/* Add the reverse edge, to represent the end of the execution */
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
		if (n->meta)
		{
			list_free(n->p0.succsEmit, NULL);
			list_free(n->p0.predsContMeta, NULL);
		}
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
	struct List *edges = s->uncontsSuccs[(unsigned char)c];

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

		for (i = 0 ; i < a->nbClocks ; i++)
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
				dbmw_constrainClock(ret->dbm, clock, 
						parserClockConstraint_getBound(pconstraint));
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
	
static void createChars(const struct List *l, struct List **psymbolTable, char **pchars)
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
	}
	listIterator_release(it);
}

static unsigned int contIndex(const struct Graph *g, char c)
{
	int i;

	for (i = 0 ; i < g->nbConts ; i++)
	{
		if (g->contsChars[i] == c)
			return i;
	}

	return -1;
}

static unsigned int uncontIndex(const struct Graph *g, char u)
{
	int i;

	for (i = 0 ; i < g->nbUnconts ; i++)
	{
		if (g->uncontsChars[i] == u)
			return i;
	}

	return -1;
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
		for (it = listIterator_first(S) ; listIterator_hasNext(it) ; it = listIterator_next(it))
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


static void addNodesRec(struct Graph *g, const struct State *s, struct Tree 
		*stringArrays, struct Node *pred)
{
	struct StringArray *sa;
	struct Node *n[2];
	int i, nbSons;

	sa = tree_getData(stringArrays);
	for (i = 0 ; i < 2 ; i++)
	{
		n[i] = node_new(g, s, sa, i);
		list_add(g->nodes, n[i]);
		list_add(g->nodesP[i], n[i]);
	}

	node_addEdgeStop(n[0], n[1]);
	if (pred != NULL)
		node_addEdgeCont(pred, n[0], contIndex(g, sa->s[strlen(sa->s) - 1]));
	else
		g->baseNodes[s->index] = n[0];
	nbSons = tree_getNbSons(stringArrays);
	if (nbSons == 0)
	{
		n[0]->isLeaf = 1;
		n[1]->isLeaf = 1;
	}
	for (i = 0 ; i < nbSons ; i++)
	{
		addNodesRec(g, s, tree_getSon(stringArrays, i), n[1]);
	}
}

static void addNodes(struct Graph *g, struct Tree *stringArrays)
{
	int i;

	for (i = 0 ; i < g->nbStates ; i++)
	{
		addNodesRec(g, &(g->a->states[i]), stringArrays, NULL);
	}
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

		next = g->baseNodes[state_nextCont(n->q, n->sa->s[0])->index];
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
			next = g->baseNodes[state_nextUncont(n->q, g->uncontsChars[i])->index];

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
		const struct List *uncontsTable, const struct List *states, const struct 
		List *edges)
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
	a->uncontsTable = uncontsTable;
	a->nbStates = list_size(states);
	a->states = malloc(a->nbStates * sizeof *(a->states));
	if (g->states == NULL)
	{
		perror("malloc g->states");
		exit(EXIT_FAILURE);
	}

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
			s->uncontsSuccs[j] = NULL;
		}
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
		for (i = 0 ; i < g->nbStates ; i++)
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
			if (from->contSuccs == NULL)
				from->contSuccs = list_new();
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
			if (from->uncontsSuccs == NULL)
				from->uncontSuccs = list_new();
			se = stateEdge_new(a, to, parserEdge_getConstraints(pe), 
					parserEdge_getResets(pe));
			from->uncontsSuccs[(unsigned char)el->c] = to;
		}
	}
	listIterator_release(it);

	return a;
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

static struct StringArray *stringArray_new(unsigned int size)
{
	int i;
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

	for (i = 0 ; i < ret->size ; i++)
	{
		ret->array->tab[i][i] = 1;
		ret->lasts[i] = i;
	}

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

	if (ret->size != g->nbStates)
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

	for (i = 0 ; i < g->nbStates ; i++)
	{
		struct State *last = &(g->states[ret->lasts[i]]);
		struct State *next = state_nextCont(last, c);
		ret->array->tab[i][next->index] = 1;
		ret->lasts[i] = next->index;
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
	struct StringArray *root = stringArray_new(g->nbStates);

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
	createChars(pconts, &(g->contsTable), &(g->contsChars));
	createChars(punconts, &(g->uncontsTable), &(g->uncontsChars));
	g->a = timedAutomaton_new(g->contsTable, g->uncontsTable, pstates, pedges);
	g->baseNodes = malloc(g->nbStates * sizeof *(g->baseNodes));
	if (g->baseNodes == NULL)
	{
		perror("malloc graph_newFromAutomaton:g->baseNodes");
		exit(EXIT_FAILURE);
	}

	strings = computeStrings(g);
	addNodes(g, strings);
	addEmitEdges(g);
	addUncontEdges(g);
	addEmitAllEdges(g);
	computeW0(g, W0);
	//minimize(g);
	
	set_applyToAll(W0, node_setWinning, NULL);
	set_applyToAll(W0, (void (*)(void *, void *))computeStrat, NULL);

	parser_cleanup();

	return g;
}

const struct List *graph_nodes(const struct Graph *g)
{
	return g->nodes;
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

	for (i = 0 ; i < g->nbStates ; i++)
	{
		state_free(&(g->states[i]));
	}
	free(g->states);
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

	for (i = 0 ; i < g->nbStates ; i++)
	{
		if (g->baseNodes[i]->isInitial)
		{
			initialNode = g->baseNodes[i];
			break;
		}
	}
	ret->realNode = initialNode;
	ret->stratNode = initialNode;

	fprintf(ret->log, "Enforcer initialized.\n");

	return ret;
}

enum Strat enforcer_getStrat(const struct Enforcer *e)
{
	fprintf(e->log, "enforcer_getStrat: ");
	fprintf(e->log, "(%s, %s) - (%s, %s)", e->realNode->q->name, 
			e->realNode->sa->s, e->stratNode->q->name, e->stratNode->sa->s);
	fprintf(e->log, "- strat: %s\n", (e->stratNode->p0.strat == EMIT) ? "emit" : 
			"dontemit");
	fprintf(e->log, "%d\n", e->stratNode->owner);
	if (e->stratNode->p0.strat == EMIT && e->realNode->sa->s[0] != '\0')
		return STRAT_EMIT;
	return STRAT_DONTEMIT;
}

void enforcer_eventRcvd(struct Enforcer *e, const struct Event *event)
{
	struct SymbolTableEl *el;
	char c;
	struct PrivateEvent *pe;
	struct Fifo *fifo;


	el = list_search(e->g->contsTable, event->label, cmpSymbolLabel);
	if (el != NULL)
	{
		fifo_enqueue(e->input, strdup(event->label));
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
		el = list_search(e->g->uncontsTable, event->label, cmpSymbolLabel);
		if (el == NULL)
		{
			fprintf(e->log, "ERROR: event %s unknown. Ignoring...\n", 
					event->label);
			return;
		}

		fifo_enqueue(e->input, strdup(event->label));
		fifo_enqueue(e->output, strdup(el->sym));

		c = el->c;

		e->realNode = e->realNode->p0.succStopEmit->p1.succsUncont[el->index];

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
	fprintf(e->log, "Event received : %s\n", event->label);
	fprintf(e->log, "(%s, %s) - (%s, %s)\n", e->realNode->q->name, 
			e->realNode->sa->s, e->stratNode->q->name, e->stratNode->sa->s);
}

void enforcer_emit(struct Enforcer *e)
{
	struct PrivateEvent *pe;
	char *label;
	struct SymbolTableEl *sym;

	if (e->realNode->sa->s[0] == '\0')
	{
		fprintf(e->log, "ERROR: cannot emit with an empty buffer.\n");
		exit(EXIT_FAILURE);
	}
	
	sym = list_search(e->g->contsTable, &(e->realNode->sa->s[0]), 
			cmpSymbolChar);
	if (sym == NULL)
	{
		fprintf(e->log, "ERROR: could not find symbol associated to char %c\n", 
				e->realNode->sa->s[0]);
		exit(EXIT_FAILURE);
	}

	fprintf(e->log, "%s\n", sym->sym);
	fifo_enqueue(e->output, strdup(sym->sym));

	if (e->stratNode == e->realNode && fifo_isEmpty(e->realBuffer))
		e->stratNode = e->stratNode->p0.succEmit;

	e->realNode = e->realNode->p0.succEmit;
	while (!fifo_isEmpty(e->realBuffer) && !e->realNode->isLeaf)
	{
		pe = fifo_dequeue(e->realBuffer);
		e->realNode = e->realNode->p0.succStopEmit->p1.succsCont[pe->index];
	}

	fprintf(e->log, "(%s, %s) - (%s, %s)\n", e->realNode->q->name, 
			e->realNode->sa->s, e->stratNode->q->name, e->stratNode->sa->s);
}

void enforcer_free(struct Enforcer *e)
{
	fprintf(e->log, "Shutting down the enforcer...\n");
	fprintf(e->log, "Summary of the execution:\n");

	fprintf(e->log, "Input: ");
	while (!fifo_isEmpty(e->input))
	{
		char *label = fifo_dequeue(e->input);
		fprintf(e->log, "%s ", label);
		free(label);
	}
	fifo_free(e->input);

	fprintf(e->log, "\nOutput: ");
	while (!fifo_isEmpty(e->output))
	{
		char *label = fifo_dequeue(e->output);
		fprintf(e->log, "%s ", label);
		free(label);
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

	fprintf(e->log, "Enforcer shutdown.\n");
}


