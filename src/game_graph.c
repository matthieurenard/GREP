#include "game_graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <list.h>
#include <set.h>
#include <fifo.h>

#include "parser.h"

#define EVENTSEP		""
#define EVENTSEPSIZE	0	/* strlen(EVENTSEP) */
#define NBSUCCS			256


struct State
{
	unsigned int parserStateId;
	char *name;
	int isInitial;
	int isAccepting;
	struct State *contSuccs[NBSUCCS];
	struct State *uncontsSuccs[NBSUCCS];
	int isLastCreated;
	int nbPredsUncont;
	int nbPredsCont;
};

typedef struct Node
{
	const struct State *q;
	char *word;
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
			struct List *succsUncont;
			struct Node *predStop;
		} p1;
	};
	int isAccepting;
	int isInitial;
	int isWinning;
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
};

struct Graph
{
	struct List *contsTable;
	struct List *uncontsTable;
	char *contsChars;
	char *uncontsChars;
	struct List *states;
	struct List *nodes;
	struct List *nodesP[2];
	struct List *lastCreated;
	unsigned int nbConts;
	unsigned int nbUnconts;
};

static int cmpSymbolChar(const void *val, const void *pel);
static int cmpSymId(const void *val, const void *pel);
static int cmpStateId(const void *val, const void *pState);
static int cmpNode(const void *val, const void *pNode);
static int cmpEdge(const void *pe1, const void *pe2);
static int cmpLastBufferChar(const void *pNode1, const void *pNode2);
static int notSetIn(struct Set *s, void *el);
static int eqPtr(const void *p1, const void *p2);
static int listIn(struct List *l, void *data);
static void listAddNoDouble(struct List *l, void *data);
static void removeSetFromList(struct List *l, const struct Set *s);
static void symbolTableEl_free(struct SymbolTableEl *el);
static char *computeRealWord(const struct Graph *g, const char *word);
static struct Node *node_new(const struct Graph *g, const struct State *q, const 
		char *word, int owner);
static struct Node *node_nextCont(const struct Graph *g, const struct Node 
		*prev, char cont);
static void node_addEdgeNoDouble(struct Node *n, enum EdgeType type, struct Node 
		*succ);
static void node_removeEdge(struct Node *n, enum EdgeType type, struct Node 
		*succ);
static void node_free(Node *n);
static void state_free(struct State *s);
static struct Edge *edge_new(enum EdgeType type, struct Node *n);
static void edge_free(struct Edge *e);
static void createChars(const struct List *l, struct List **psymbolTable, char 
		**pchars);
static unsigned int contIndex(const struct Graph *g, char c);
static void node_setWinning(void *dummy, void *pn);
static void computeStrat(void *dummy, Node *n);
static void addEdgesToLast(struct Graph *g);
static void Node_freeWithLinks(struct Graph *g, Node *n);
static void attr(struct Set *ret, struct Graph *g, int player, const struct Set 
		*U, struct List *nodes);
static void computeW0(struct Graph *g, struct Set *ret);
static void minimize(struct Graph *g);
static void addNodes(struct Graph *g);
static void createStates(struct Graph *g, const struct List *states, const 
		struct List *edges);


static int cmpSymbolChar(const void *val, const void *pel)
{
	const struct SymbolTableEl *el = pel;
	const char c = *(const char *)val;
	return (el->c == c);
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
			strcmp(s->word, n->word) == 0);
}

static int cmpEdge(const void *pe1, const void *pe2)
{
	const struct Edge *e1 = pe1;
	const struct Edge *e2 = pe2;
	return (e1->type == e2->type && e1->succ == e2->succ);
}

static int cmpLastBufferChar(const void *pNode1, const void *pNode2)
{
	const Node *n1 = pNode1, *n2 = pNode2;
	return (n1->word[strlen(n1->word)-1] == n2->word[strlen(n2->word)-1]);
}

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
		char *word, int owner)
{
	int i;
	struct Node *n = malloc(sizeof *n);

	if (n == NULL)
	{
		perror("malloc node");
		exit(EXIT_FAILURE);
	}

	n->q = q;
	n->word = strdup(word);
	n->owner = owner;
	n->isAccepting = q->isAccepting;
	n->isInitial = (q->isInitial && word[0] == '\0' && owner == 0);
	n->isWinning = 0;
	n->meta = 0;
	n->realWord = computeRealWord(g, word);
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
		for (i = 0 ; i < g->nbConts ; i++)
			n->p1.succsCont[i] = NULL;
		n->p1.succsUncont = list_new();
	}

	return n;
}

static struct Node *node_nextCont(const struct Graph *g, const struct Node 
		*prev, char cont)
{
	int wordSize = strlen(prev->word) + 1;
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

static void node_addEdgeNoDouble(struct Node *n, enum EdgeType type, struct Node 
		*succ)
{
	struct Edge searchEdge;
	searchEdge.type = type;
	searchEdge.succ = succ;
	if (list_search(n->edges, &searchEdge, cmpEdge) == NULL)
		list_add(n->edges, edge_new(type, succ));
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
		list_free(n->p1.succsUncont, NULL);
	}

	list_free(n->edges, (void (*)(void *))edge_free);

	free(n->word);
	free(n->realWord);
	free(n);
}

static void state_free(struct State *s)
{
	free(s->name);
	free(s);
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

static void minimize(struct Graph *g)
{
	struct Fifo *wait = fifo_empty();
	struct Set *toSuppress = set_empty(NULL);
	struct Set *necessary = set_empty(NULL);
	struct ListIterator *it;
	struct Edge *e, searchEdge;
	int i;

	for (it = listIterator_first(g->lastCreated) ; listIterator_hasNext(it) ; it 
			= listIterator_next(it))
	{
		fifo_enqueue(wait, listIterator_val(it));
	}
	listIterator_release(it);

	while (!fifo_isEmpty(wait))
	{
		struct Node *n = fifo_dequeue(wait);
		if (set_in(toSuppress, n) || set_in(necessary, n))
			continue;
		if (n->owner == 0 && n->p0.predContRcvd != NULL)
		{
			struct Node *prev = n->p0.predContRcvd->p1.predStop;
			struct List *brothers = list_new();
			struct Node *prev1 = prev->p0.succStopEmit;
			int i, same = 1, sameBrothers = 1, sameSuccs = 1;

			for (it = listIterator_first(n->p0.predContRcvd->p1.succsCont) ; 
					listIterator_hasNext(it) ; it = listIterator_next(it))
			{
				Node *brother = listIterator_val(it);
				list_add(brothers, brother);
				if (brother->isWinning != prev->isWinning || 
						brother->p0.strat != prev->p0.strat)
					same = 0;
				if (brother->isWinning != n->isWinning || brother->p0.strat != 
						n->p0.strat)
					sameBrothers = 0;
				if (!brother->meta && brother->word[0] != '\0' && 
						brother->word[1] != '\0')
				{
					Node *prevBrother = 
						list_search(prev->p0.predContRcvd->p1.succsCont, 
								brother, cmpLastBufferChar);
					if (prevBrother != NULL && (prevBrother->isWinning != 
							brother->isWinning || prevBrother->p0.strat != 
							brother->p0.strat))
						sameSuccs = 0;
				}
			}
			listIterator_release(it);

			/* remove all brothers, merge with the previous node (with buffer 
			 * size -1 */
			if (same)
			{
				for (it = listIterator_first(brothers) ; listIterator_hasNext(it) ; 
						it = listIterator_next(it))
				{
					Node *brother = listIterator_val(it);
					set_add(toSuppress, brother);
					set_add(toSuppress, brother->p0.succStopEmit);
				}
				listIterator_release(it);

				fifo_enqueue(wait, prev);
				prev->word = realloc(prev->word, strlen(prev->word) + 2);
				if (prev->word == NULL)
				{
					perror("realloc prev->word");
					exit(EXIT_FAILURE);
				}
				prev->word[strlen(prev->word) + 1] = '\0';
				prev->word[strlen(prev->word)] = '*';
				free(prev->realWord);
				prev->realWord = computeRealWord(g, prev->word);

				if (!prev->meta)
				{
					struct Node *tmp;
					prev->meta = 1;
					tmp = prev->p0.succEmit;
					prev->p0.succsEmit = list_new();
					if (tmp != NULL)
						list_add(prev->p0.succsEmit, tmp);
					prev->p0.predsContMeta = list_new();
				}

				free(prev1->word);
				prev1->word = strdup(prev->word);
				free(prev1->realWord);
				prev1->realWord = strdup(prev->realWord);
				if (!prev1->meta)
				{
					/* Does not change anything for nodes of player 1 */
					prev1->meta = 1;
				}


				/* Remove nodes from the graph (redirect all the brothers to 
				 * prev) 
				 */
				for (it = listIterator_first(brothers) ; listIterator_hasNext(it) ; 
						it = listIterator_next(it))
				{
					Node *brother = listIterator_val(it);
					struct ListIterator *it2;

					/* replace brother --> q by prev --> q */
					if (brother->meta)
					{
						for (it2 = listIterator_first(brother->p0.succsEmit) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							Node *succ = listIterator_val(it2);
							listAddNoDouble(prev->p0.succsEmit, succ);
							node_addEdgeNoDouble(prev, EMIT, succ);
							list_remove(succ->p0.predsEmit, brother);
							listAddNoDouble(succ->p0.predsEmit, prev);
						}
						listIterator_release(it2);
					}
					else
					{
						Node *succ = brother->p0.succEmit;
						listAddNoDouble(prev->p0.succsEmit, succ);
						node_addEdgeNoDouble(prev, EMIT, succ);
						list_remove(succ->p0.predsEmit, brother);
						listAddNoDouble(succ->p0.predsEmit, prev);
					}



					/* Replace q --> brother by q --> prev */
					for (it2 = listIterator_first(brother->p0.predsEmit) ; 
							listIterator_hasNext(it2) ; it2 = 
							listIterator_next(it2))
					{
						Node *pred = listIterator_val(it2);
						if (pred->meta)
						{
							list_remove(pred->p0.succsEmit, brother);
							listAddNoDouble(pred->p0.succsEmit, prev);
						}
						else
							pred->p0.succEmit = prev;
						listAddNoDouble(prev->p0.predsEmit, pred);
						node_removeEdge(pred, EMIT, brother);
						node_addEdgeNoDouble(pred, EMIT, prev);
					}
					listIterator_release(it2);

					for (it2 = listIterator_first(brother->p0.predsUncont) ; 
							listIterator_hasNext(it2) ; it2 = 
							listIterator_next(it2))
					{
						Node *pred = listIterator_val(it2);
						list_remove(pred->p1.succsUncont, brother);
						listAddNoDouble(pred->p1.succsUncont, prev);
						listAddNoDouble(prev->p0.predsUncont, pred);
						node_removeEdge(pred, UNCONTRCVD, brother);
						node_addEdgeNoDouble(pred, UNCONTRCVD, prev);
					}
					listIterator_release(it2);

					if (brother->meta)
					{
						for (it2 = listIterator_first(brother->p0.predsContMeta) 
								; listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							Node *pred = listIterator_val(it2);
							list_remove(pred->p1.succsCont, brother);
							listAddNoDouble(pred->p1.succsCont, prev);
							listAddNoDouble(prev->p0.predsContMeta, pred);
							node_removeEdge(pred, CONTRCVD, brother);
							node_addEdgeNoDouble(pred, CONTRCVD, prev);
						}
						listIterator_release(it2);
					}

					{
						Node *pred = brother->p0.predContRcvd;
						list_remove(pred->p1.succsCont, brother);
						listAddNoDouble(pred->p1.succsCont, prev);
						listAddNoDouble(prev->p0.predsContMeta, pred);
						node_removeEdge(pred, CONTRCVD, brother);
						node_addEdgeNoDouble(pred, CONTRCVD, prev);
					}
				}
				listIterator_release(it);
			}
			else if (sameBrothers)
			{
				Node *n1 = n->p0.succStopEmit;
				for (it = listIterator_first(brothers) ; listIterator_hasNext(it) ; 
						it = listIterator_next(it))
				{
					Node *brother = listIterator_val(it);

					if (brother != n)
					{
						set_add(toSuppress, brother);
						set_add(toSuppress, brother->p0.succStopEmit);
					}
				}
				listIterator_release(it);

				if (n->meta)
				{
					/* Remove '*' */
					n->word[strlen(n->word) - 1] = '\0';
					/* Replace the letter before by '+' */
					n->word[strlen(n->word) - 1] = '+';
					n1->word[strlen(n1->word) - 1] = '\0';
					n1->word[strlen(n1->word) - 1] = '+';
				}
				else
				{
					Node *tmp;
					n->meta = 1;
					tmp = n->p0.succEmit;
					n->p0.succsEmit = list_new();
					list_add(n->p0.succsEmit, tmp);
					n->word[strlen(n->word) - 1] = '?';
					n1->word[strlen(n1->word) - 1] = '?';
				}
				free(n->realWord);
				n->realWord = computeRealWord(g, n->word);
				free(n1->realWord);
				n1->realWord = strdup(n->realWord);

				for (it = listIterator_first(brothers) ; listIterator_hasNext(it) ; 
						it = listIterator_next(it))
				{
					Node *brother = listIterator_val(it);
					Node *brother1 = brother->p0.succStopEmit;
					struct ListIterator *it2;
					if (brother == n)
						continue;

					/* Redirect all on n */
					if (brother->meta)
					{
						for (it2 = listIterator_first(brother->p0.succsEmit) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							Node *brotherSuccEmit = listIterator_val(it2);
							list_remove(brotherSuccEmit->p0.predsEmit, brother);
							listAddNoDouble(brotherSuccEmit->p0.predsEmit, n);
							listAddNoDouble(n->p0.succsEmit, brotherSuccEmit);
							node_addEdgeNoDouble(n, EMIT, brotherSuccEmit);
						}
						listIterator_release(it2);
					}
					else
					{
						Node *brotherSuccEmit = brother->p0.succEmit;
						list_remove(brotherSuccEmit->p0.predsEmit, brother);
						listAddNoDouble(brotherSuccEmit->p0.predsEmit, n);
						listAddNoDouble(n->p0.succsEmit, brotherSuccEmit);
					}

					/* Here, predContRcvd is the same for all brothers */
					list_remove(brother->p0.predContRcvd->p1.succsCont, brother);
					node_removeEdge(brother->p0.predContRcvd, CONTRCVD, brother);

					if (brother->meta)
					{
						for (it2 = listIterator_first(brother->p0.predsContMeta) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							Node *brotherPredCont = listIterator_val(it2);
							list_remove(brotherPredCont->p1.succsCont, brother);
							listAddNoDouble(brotherPredCont->p1.succsCont, n);
							listAddNoDouble(n->p0.predsContMeta, brotherPredCont);
							node_removeEdge(brotherPredCont, CONTRCVD, brother);
							node_addEdgeNoDouble(brotherPredCont, CONTRCVD, n);
						}
						listIterator_release(it2);
					}

					for (it2 = listIterator_first(brother->p0.predsEmit) ; 
							listIterator_hasNext(it2) ; it2 = 
							listIterator_next(it2))
					{
						Node *brotherPredEmit = listIterator_val(it2);
						if (brotherPredEmit->meta)
						{
							list_remove(brotherPredEmit->p0.succsEmit, brother);
							listAddNoDouble(brotherPredEmit->p0.succsEmit, n);
						}
						else
							brotherPredEmit->p0.succEmit = n;
						list_remove(n->p0.predsEmit, brother);
						listAddNoDouble(n->p0.predsEmit, brotherPredEmit);
						node_removeEdge(brotherPredEmit, EMIT, brother);
						node_addEdgeNoDouble(brotherPredEmit, EMIT, n);
					}
					listIterator_release(it2);

					for (it2 = listIterator_first(brother->p0.predsUncont) ; 
							listIterator_hasNext(it2) ; it2 = 
							listIterator_next(it2))
					{
						Node *brotherPredUncont = listIterator_val(it2);
						list_remove(brotherPredUncont->p1.succsUncont, brother);
						listAddNoDouble(brotherPredUncont->p1.succsUncont, n);
						listAddNoDouble(n->p0.predsUncont, brotherPredUncont);
						node_removeEdge(brotherPredUncont, UNCONTRCVD, brother);
						node_addEdgeNoDouble(brotherPredUncont, UNCONTRCVD, n);
					}
					listIterator_release(it2);
				
					for (it2 = listIterator_first(brother1->p1.succsCont) ; 
							listIterator_hasNext(it2) ; it2 = 
							listIterator_next(it2))
					{
						Node *brother1SuccCont = listIterator_val(it2);
						list_remove(brother1SuccCont->p0.predsContMeta, brother1);
						if (brother1SuccCont->p0.predContRcvd == brother1)
							brother1SuccCont->p0.predContRcvd = n1;
						else
							listAddNoDouble(brother1SuccCont->p0.predsContMeta, n1);
						listAddNoDouble(n1->p1.succsCont, brother1SuccCont);
						node_addEdgeNoDouble(n1, CONTRCVD, brother1SuccCont);
					}
					listIterator_release(it2);

					for (it2 = listIterator_first(brother1->p1.succsUncont) ; 
							listIterator_hasNext(it2) ; it2 = 
							listIterator_next(it2))
					{
						Node *brother1SuccUncont = listIterator_val(it2);
						list_remove(brother1SuccUncont->p0.predsUncont, brother1);
						listAddNoDouble(brother1SuccUncont->p0.predsUncont, n1);
						listAddNoDouble(n1->p1.succsUncont, brother1SuccUncont);
						node_addEdgeNoDouble(n1, UNCONTRCVD, brother1SuccUncont);
					}
					listIterator_release(it2);
				}
				listIterator_release(it);
			}
			else if (sameSuccs)
			{
				for (it = listIterator_first(brothers) ; 
						listIterator_hasNext(it) ; it = listIterator_next(it))
				{
					Node *brother = listIterator_val(it);
					Node *brother1 = brother->p0.succStopEmit;
					int wordSize = strlen(brother->word) + 1;

					set_add(necessary, brother);
					set_add(necessary, brother1);

					if (wordSize < 3)
						continue;

					if (brother->word[wordSize - 2] != '*')
					{
						brother->word = realloc(brother->word, wordSize + 1);
						if (brother->word == NULL)
						{
							perror("realloc brother->word");
							exit(EXIT_FAILURE);
						}
						brother->word[wordSize] = '\0';
						brother->word[wordSize - 1] = brother->word[wordSize - 
							2];
						brother->word[wordSize - 2] = '*';
						free(brother->realWord);
						brother->realWord = computeRealWord(g, brother->word);

						free(brother1->word);
						brother1->word = strdup(brother->word);
						free(brother1->realWord);
						brother1->realWord = strdup(brother->realWord);
					}
				}
				listIterator_release(it);
			}
			else
			{
				for (it = listIterator_first(brothers) ; 
						listIterator_hasNext(it) ; it = listIterator_next(it))
				{
					Node *brother = listIterator_val(it);
					set_add(necessary, brother);
				}
				listIterator_release(it);
			}
			list_free(brothers, NULL);
		}
	}

	set_applyToAll(toSuppress, (void (*)(void *, void *))Node_freeWithLinks, g);

	set_free(toSuppress);
	set_free(necessary);
	fifo_free(wait);
}

static void addNodes(struct Graph *g)
{
	const char *emptyWord = "";
	Node *n;
	int i, wordSize = 0, changed;
	struct ListIterator *it;
	struct List *newNodes = list_new(), *tmpList;
	struct Set *W0 = set_empty(NULL);

	for (it = listIterator_first(g->states) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct State *s = listIterator_val(it);
		
		for (i = 0 ; i <= 1 ; i++)
		{
			n = node_new(g, s, "", i);
			list_add(g->nodes, n);
			list_add(g->nodesP[i], n);
			list_add(g->lastCreated, n);
		}
	}
	listIterator_release(it);
	addEdgesToLast(g);

	wordSize = 1;
	do
	{
		for (it = listIterator_first(g->lastCreated) ; listIterator_hasNext(it) ; it 
				= listIterator_next(it))
		{
			struct Node *src = listIterator_val(it);
			
			for (i = 0 ; i < g->nbConts ; i++)
			{
				n = node_nextCont(g, src, g->contsChars[i]);
				list_add(g->nodes, n);
				list_add(g->nodesP[n->owner], n);
				list_add(newNodes, n);
			}
		}
		listIterator_release(it);

		list_cleanup(g->lastCreated, NULL);
		tmpList = g->lastCreated;
		g->lastCreated = newNodes;
		newNodes = tmpList;

		addEdgesToLast(g);
		computeW0(g, W0);
		set_applyToAll(W0, node_setWinning, NULL);
		set_applyToAll(W0, (void (*)(void *, void *))computeStrat, NULL);

		changed = 0;
		for (it = listIterator_first(g->lastCreated) ; listIterator_hasNext(it) 
				; it = listIterator_next(it))
		{
			Node *n = listIterator_val(it);
			if (n->owner == 0 && n->p0.predContRcvd != NULL)
			{
				struct Node *prev = n->p0.predContRcvd->p1.predStop;
				if (prev->isWinning != n->isWinning || prev->p0.strat != n->p0.strat)
				{
					if (wordSize <= 1)
						changed = 1;
					else
					{
						struct ListIterator *it2;
						for (i = 0 ; i < g->nbConts ; i++)
						{
							Node *brother = n->p0.predContRcvd->p1.succsCont[i];
							Node *prevBrother = prev->p0.predContRcvd->p1.succsCont[i];
							if (brother->isWinning != prevBrother->isWinning || 
									brother->p0.strat != prevBrother->p0.strat)
							{
								changed = 1;
								break;
							}
						}

					}
					if (changed)
						break;
				}
			}
		}
		listIterator_release(it);

		wordSize++;
	} while (changed);

	list_free(newNodes, NULL);
	set_free(W0);
}

static void createStates(struct Graph *g, const struct List *states, const 
		struct List *edges)
{
	struct ListIterator *it;
	int i;

	g->states = list_new();

	for (it = listIterator_first(states) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct ParserState *ps = listIterator_val(it);
		struct State *s = malloc(sizeof *s);
		s->parserStateId = parserState_getId(ps);
		s->name = strdup(parserState_getName(ps));
		s->isInitial = parserState_isInitial(ps);
		s->isAccepting = parserState_isAccepting(ps);
		s->nbPredsCont = 0;
		s->nbPredsUncont = 0;

		for (i = 0 ; i < NBSUCCS ; i++)
		{
			s->contSuccs[i] = NULL;
			s->uncontsSuccs[i] = NULL;
		}

		list_add(g->states, s);
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
		from = list_search(g->states, &id, cmpStateId);
		id = parserState_getId(pto);
		to = list_search(g->states, &id, cmpStateId);

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
			el = list_search(g->contsTable, &id, cmpSymId);
			if (el == NULL)
			{
				fprintf(stderr, "ERROR: cannot find symbol of id %d.\n", id);
				exit(EXIT_FAILURE);
			}
			from->contSuccs[(unsigned char)el->c] = to;
			to->nbPredsCont++;
		}
		else
		{
			el = list_search(g->uncontsTable, &id, cmpSymId);
			if (el == NULL)
			{
				fprintf(stderr, "ERROR: cannot find symbol of id %d.\n", id);
				exit(EXIT_FAILURE);
			}
			from->uncontsSuccs[(unsigned char)el->c] = to;
			to->nbPredsUncont++;
		}
	}
	listIterator_release(it);
}

struct Graph *graph_newFromAutomaton(const char *filename)
{
	const struct List *pstates = NULL;
	const struct List *pconts = NULL;
	const struct List *punconts = NULL;
	const struct List *pedges = NULL;
	struct ListIterator *it;
	struct Graph *g = malloc(sizeof *g);

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

	g->nodes = list_new();
	g->nodesP[0] = list_new();
	g->nodesP[1] = list_new();
	g->lastCreated = list_new();
	g->nbConts = parser_getNbConts();
	g->nbUnconts = parser_getNbUnconts();
	createChars(pconts, &(g->contsTable), &(g->contsChars));
	createChars(punconts, &(g->uncontsTable), &(g->uncontsChars));
	createStates(g, pstates, pedges);

	addNodes(g);
	minimize(g);

	parser_cleanup();

	return g;
}

const struct List *graph_nodes(const struct Graph *g)
{
	return g->nodes;
}

void graph_free(struct Graph *g)
{
	list_free(g->contsTable, (void (*)(void *))symbolTableEl_free);
	list_free(g->uncontsTable, (void (*)(void *))symbolTableEl_free);
	list_free(g->lastCreated, NULL);
	list_free(g->nodesP[0], NULL);
	list_free(g->nodesP[1], NULL);
	list_free(g->nodes, (void (*)(void *))node_free);
	list_free(g->states, (void (*)(void *))state_free);
	free(g->contsChars);
	free(g->uncontsChars);
	free(g);
}


