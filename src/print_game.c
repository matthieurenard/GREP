#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <gvc.h>
#include <list.h>
#include <set.h>
#include <fifo.h>

#include "parser_tmdautmtn.h"


#define NBSUCCS			256
#define MAXNBEDGES		4

enum EdgeType {SUCCEMIT, SUCCSTOPEMIT, SUCCRECCONT, SUCCRECUNCONT};
enum Strat {EMIT, DONTEMIT};


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
	struct State *q;
	char *word;
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
			struct List *succsCont;
			struct List *succsUncont;
			struct Node *predStop;
		} p1;
	};
	char *name;
	int isAccepting;
	int isInitial;
	int isWinning;
} Node;

struct VizNode
{
	Agrec_t h;
	struct Node *n;
};

struct SearchNode
{
	struct State *q;
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
};


static int cmpSymbolChar(void *val, void *pel)
{
	struct SymbolTableEl *el = pel;
	char c = *(char *)val;
	return (el->c == c);
}

static int cmpSymId(void *val, void *pel)
{
	struct SymbolTableEl *el = pel;
	unsigned int id = *(unsigned int *)val;
	return (el->id == id);
}

static int cmpStateId(void *val, void *pState)
{
	struct State *s = pState;
	int id = *(int *)val;
	return (s->parserStateId == id);
}

static int cmpNode(void *val, void *pNode)
{
	struct Node *n = pNode;
	struct SearchNode *s = val;
	return (n->q == s->q && n->owner == s->owner &&
			strcmp(s->word, n->word) == 0);
}

static int cmpLastBufferChar(void *pNode1, void *pNode2)
{
	Node *n1 = pNode1, *n2 = pNode2;
	return (n1->word[strlen(n1->word)-1] == n2->word[strlen(n2->word)-1]);
}

static int notSetIn(struct Set *s, void *el)
{
	return !set_in(s, el);
}

static int eqPtr(void *p1, void *p2)
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

void removeSetFromList(struct List *l, const struct Set *s)
{
	set_applyToAll(s, (void (*)(void*, void*))list_remove, l);
}

void printNode(void *pf, void *pn)
{
	Node *n = pn;
	FILE *out = (pf == NULL) ? stderr : pf;
	fprintf(out, "%s, ", n->name);
}

void symbolTableEl_free(struct SymbolTableEl *el)
{
	free(el->sym);
	free(el);
}

void node_free(Node *n)
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
		list_free(n->p1.succsCont, NULL);
		list_free(n->p1.succsUncont, NULL);
	}
				
	free(n->name);
	free(n->word);
	free(n);
}

void state_free(struct State *s)
{
	free(s->name);
	free(s);
}
	

void createChars(const struct List *l, struct List **psymbolTable, char **pchars)
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


int Node_nameSize(Node *n)
{
	return 6 + strlen(n->q->name) + strlen(n->word) + 1 + 1 + 
		(n->word[0] == '\0');
}

char *Node_name(char *s, Node *n)
{
	if (n->word[0] == '\0')
		sprintf(s, "(%s, %s, %d)", n->q->name, "-", n->owner);
	else
		sprintf(s, "(%s, %s, %d)", n->q->name, n->word, n->owner);
	return s;
}

void node_setWinning(void *dummy, void *pn)
{
	Node *n = pn;
	(void)dummy;
	n->isWinning = 1;
}

void computeStrat(void *dummy, Node *n)
{
	if (n->owner == 0 && n->p0.succEmit != NULL && n->p0.succEmit->isWinning)
		n->p0.strat = EMIT;
}


void setAccepting(Agnode_t *node)
{
	agset(node, "color", "blue");
}

void setInitial(Agnode_t *node)
{
	agset(node, "shape", "square");
}

#if 0
void addNodesRec(struct Graph *g, struct State *s, int maxSize, char *word)
{
	int size = strlen(word);
	Node *n;
	char *pCont;
	int i;


	if (size >= maxSize)
		return;

	pCont = g->contsChars;
	while (*pCont != '\0')
	{
		word[size] = *pCont;
		word[size+1] = '\0';

		for (i = 0 ; i <= 1 ; i++)
		{
			n = malloc(sizeof *n);

			if (n == NULL)
			{
				perror("malloc n");
				exit(EXIT_FAILURE);
			}
			n->q = s;
			n->owner = i;
			n->word = strdup(word);
			n->isAccepting = s->isAccepting;
			n->isInitial = 0;
			n->name = malloc(Node_nameSize(n));
			if (n->name == NULL)
			{
				perror("malloc n->name");
				exit(EXIT_FAILURE);
			}
			Node_name(n->name, n);
			n->isWinning = 0;
			list_add(g->nodes, n);
			list_add(g->nodesP[i], n);
		}

		addNodesRec(g, s, maxSize, word);

		pCont++;
	}

	word[size] = '\0';
}
#endif 

void addEdgesToLast(struct Graph *g)
{
	struct ListIterator *it;
	struct SearchNode sn;
	struct Node *dest;
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
				/* !! dest <--> n !! */
				list_add(dest->p1.succsCont, n);
				n->p0.predContRcvd = dest;
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
			}
		}
	}

	listIterator_release(it);
}

void Node_allocSuccPreds(struct Node *n, struct Graph *g)
{
	int i;

	if (n->owner == 0)
	{
		n->p0.strat = DONTEMIT;
		n->p0.succEmit = NULL;
		n->p0.succStopEmit = NULL;
		n->p0.predContRcvd = NULL;
		n->p0.predsEmit = list_new();
		n->p0.predsUncont = list_new();
	}
	else
	{
		int size = strlen(g->contsChars);

		n->p1.predStop = NULL;
		n->p1.succsCont = list_new();
		n->p1.succsUncont = list_new();
	}

}

void Node_freeWithLinks(struct Graph *g, Node *n)
{
	/* TODO: FREE NODES ! MEMORY LEAK HERE */
	node_free(n);
	list_remove(g->nodes, n);
}

void attr(struct Set *ret, struct Graph *g, int player, const struct Set *U, 
		struct List *nodes)
{
	int stable = 0;
	struct ListIterator *it;

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
					int i;
					struct ListIterator *it2;

					for (it2 = listIterator_first(n->p1.succsCont) ; 
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

					if (stable)
					{
						for (it2 = listIterator_first(n->p1.succsUncont) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							Node *succ = listIterator_val(it2);
							if (listIn(nodes, succ) && 
									set_in(ret, succ))
							{
								stable = 0;
								set_add(ret, n);
								break;
							}
						}
						listIterator_release(it2);
					}
				}
			}
			/* n->owner != owner */
			else
			{
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
					int i;
					struct ListIterator *it2;

					for (it2 = listIterator_first(n->p1.succsCont) ; 
							listIterator_hasNext(it2) ; it2 = 
							listIterator_next(it2))
					{
						Node *succ = listIterator_val(it2);
						if (listIn(nodes, succ) && !set_in(ret, succ))
						{
							ok = 0;
							break;
						}
					}
					listIterator_release(it2);
					if (ok)
					{
						for (it2 = listIterator_first(n->p1.succsUncont) ; 
								listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							Node *succ = listIterator_val(it2);
							if (listIn(nodes, succ) && !set_in(ret, succ))
							{
								ok = 0;
								break;
							}
						}
						listIterator_release(it2);
						if (ok)
						{
							stable = 0;
							set_add(ret, n);
						}
					}
				}
			}
		}
		listIterator_release(it);
	}
}

void computeW0(struct Graph *g, struct Set *ret)
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

void minimize(struct Graph *g)
{
	struct Fifo *wait = fifo_empty();
	struct Set *toSuppress = set_empty(NULL);
	struct Set *necessary = set_empty(NULL);
	struct ListIterator *it;

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

			for (it = listIterator_first(n->p0.predContRcvd->p1.succsCont) ; listIterator_hasNext(it) ; 
					it = listIterator_next(it))
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
				free(prev->name);
				prev->name = malloc(Node_nameSize(prev));
				if (prev->name == NULL)
				{
					perror("malloc prev->name");
					exit(EXIT_FAILURE);
				}
				Node_name(prev->name, prev);
				if (!prev->meta)
				{
					struct Node *tmp;
					prev->meta = 1;
					/*
					tmp = prev->p0.succStopEmit;
					prev->p0.succsStopEmit = list_new();
					list_add(prev->p0.succsStopEmit, tmp);
					*/
					tmp = prev->p0.succEmit;
					prev->p0.succsEmit = list_new();
					if (tmp != NULL)
						list_add(prev->p0.succsEmit, tmp);
					prev->p0.predsContMeta = list_new();
				}

				if (prev1->word[0] != '\0')
					free(prev1->word);
				prev1->word = strdup(prev->word);
				free(prev1->name);
				prev1->name = malloc(Node_nameSize(prev1));
				if (prev1->name == NULL)
				{
					perror("malloc prev1->name");
					exit(EXIT_FAILURE);
				}
				Node_name(prev1->name, prev1);
				if (!prev1->meta)
				{
					struct Node *tmp;
					prev1->meta = 1;
					/*
					tmp = prev1->p1.predStop;
					prev1->p1.predsStop = list_new();
					list_add(prev1->p1.predsStop, tmp);
					*/
					
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
						for (it2 = listIterator_first(brother->p0.succsEmit) ; listIterator_hasNext(it2) ; 
								it2 = listIterator_next(it2))
						{
							Node *succ = listIterator_val(it2);
							listAddNoDouble(prev->p0.succsEmit, succ);
							list_remove(succ->p0.predsEmit, brother);
							listAddNoDouble(succ->p0.predsEmit, prev);
						}
						listIterator_release(it2);

						/*
						for (it2 = listIterator_first(brother->p0.succsStopEmit) 
								; listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							Node *succ = listIterator_val(it2);
							listAddNoDouble(prev->p0.succsStopEmit, succ);
							if (succ->meta)
							{
								list_remove(succ->p1.predsStop, brother);
								listAddNoDouble(succ->p1.predsStop, prev);
							}
							else
								succ->p1.predStop = prev;
						}
						listIterator_release(it2);
						*/
					}
					else
					{
						Node *succ = brother->p0.succEmit;
						listAddNoDouble(prev->p0.succsEmit, succ);
						list_remove(succ->p0.predsEmit, brother);
						listAddNoDouble(succ->p0.predsEmit, prev);
					}



					/* Replace q --> brother by q --> prev */
					for (it2 = listIterator_first(brother->p0.predsEmit) ; listIterator_hasNext(it2) ; 
							it2 = listIterator_next(it2))
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
					}
					listIterator_release(it2);

					for (it2 = listIterator_first(brother->p0.predsUncont) ; listIterator_hasNext(it2) ; 
							it2 = listIterator_next(it2))
					{
						Node *pred = listIterator_val(it2);
						list_remove(pred->p1.succsUncont, brother);
						listAddNoDouble(pred->p1.succsUncont, prev);
						listAddNoDouble(prev->p0.predsUncont, pred);
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
						}
						listIterator_release(it2);
					}

					{
						Node *pred = brother->p0.predContRcvd;
						list_remove(pred->p1.succsCont, brother);
						listAddNoDouble(pred->p1.succsCont, prev);
						listAddNoDouble(prev->p0.predsContMeta, pred);
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
				n->name = realloc(n->name, Node_nameSize(n));
				if (n->name == NULL)
				{
					perror("realloc n->name");
					exit(EXIT_FAILURE);
				}
				Node_name(n->name, n);
				n1->name = realloc(n1->name, Node_nameSize(n1));
				if (n1->name == NULL)
				{
					perror("realloc n1->name");
					exit(EXIT_FAILURE);
				}
				Node_name(n1->name, n1);

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
						free(brother->name);
						brother->name = malloc(Node_nameSize(brother));
						if (brother->name == NULL)
						{
							perror("malloc brother->name");
							exit(EXIT_FAILURE);
						}
						Node_name(brother->name, brother);

						free(brother1->word);
						brother1->word = strdup(brother->word);
						free(brother1->name);
						brother1->name = malloc(Node_nameSize(brother1));
						if (brother1->name == NULL)
						{
							perror("malloc brother1->name");
							exit(EXIT_FAILURE);
						}
						Node_name(brother1->name, brother1);
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

void addNodes(struct Graph *g)
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
			n = malloc(sizeof *n);
			if (n == NULL)
			{
				perror("malloc n");
				exit(EXIT_FAILURE);
			}
			n->q = s;
			n->owner = i;
			n->isAccepting = s->isAccepting;
			n->isInitial = (s->isInitial && n->owner == 1);
			n->meta = 0;
			n->word = strdup(emptyWord);
			n->name = malloc(Node_nameSize(n));
			if (n->name == NULL)
			{
				perror("malloc n->name");
				exit(EXIT_FAILURE);
			}
			Node_name(n->name, n);
			n->isWinning = 0;
			Node_allocSuccPreds(n, g);
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
			int i, nSuccs = strlen(g->contsChars);
			
			for (i = 0 ; i < nSuccs ; i++)
			{
				n = malloc(sizeof *n);
				if (n == NULL)
				{
					perror("malloc n");
					exit(EXIT_FAILURE);
				}
				n->q = src->q;
				n->word = malloc(wordSize + 1);
				if (n->word == NULL)
				{
					perror("malloc n->word");
					exit(EXIT_FAILURE);
				}
				strcpy(n->word, src->word);
				n->word[wordSize-1] = g->contsChars[i];
				n->word[wordSize] = '\0';
				n->owner = src->owner;
				n->meta = 0;
				n->name = malloc(Node_nameSize(n));
				if (n->name == NULL)
				{
					perror("malloc n->name");
					exit(EXIT_FAILURE);
				}
				Node_name(n->name, n);
				n->isAccepting = n->q->isAccepting;
				n->isInitial = 0;
				n->isWinning = 0;
				Node_allocSuccPreds(n, g);
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
						for (it2 = 
								listIterator_first(n->p0.predContRcvd->p1.succsCont) 
								; listIterator_hasNext(it2) ; it2 = 
								listIterator_next(it2))
						{
							Node *brother = listIterator_val(it2);
							Node *prevBrother = 
								list_search(prev->p0.predContRcvd->p1.succsCont, 
										brother, cmpLastBufferChar);
							if (brother->isWinning != prevBrother->isWinning || 
									brother->p0.strat != prevBrother->p0.strat)
							{
								changed = 1;
								break;
							}
						}
						listIterator_release(it2);

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
}

void createStates(struct Graph *g, const struct List *states, const struct List 
		*edges)
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

void createGraphFromAutomaton(struct Graph *g, const char *filename)
{
	const struct List *pstates = NULL;
	const struct List *pconts = NULL;
	const struct List *punconts = NULL;
	const struct List *pedges = NULL;
	struct ListIterator *it;
	

	parseFile(filename);

	pstates = parser_getStates();
	pconts = parser_getConts();
	punconts = parser_getUnconts();
	pedges = parser_getEdges();

	g->nodes = list_new();
	g->nodesP[0] = list_new();
	g->nodesP[1] = list_new();
	g->lastCreated = list_new();
	createChars(pconts, &(g->contsTable), &(g->contsChars));
	createChars(punconts, &(g->uncontsTable), &(g->uncontsChars));
	createStates(g, pstates, pedges);

	addNodes(g);

	parser_cleanup();
}

void Gnode_setLabel(Agnode_t *gnode, struct Graph *g)
{
	char *label;
	int size = 0, i;
	struct VizNode *viz = (struct VizNode *)aggetrec(gnode, "Node", FALSE);
	Node *n;
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

	if (viz == NULL)
	{
		fprintf(stderr, "ERROR: No node associated to gnode %s\n", 
				agnameof(gnode));
		exit(EXIT_FAILURE);
	}

	n = viz->n;
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
		if (n->word[i] == '*')
			s = &starSym;
		else if (n->word[i] == '+')
			s = &plusSym;
		else
		{

			s = list_search(g->contsTable, &(n->word[i]), 
					cmpSymbolChar);
			if (s == NULL)
			{
				fprintf(stderr, "ERROR: could not find symbol associated to %c\n", 
						n->word[i]);
				exit(EXIT_FAILURE);
			}
		}

		size += s->size;
		syms[i++] = s;
	}
	if (n->word[0] == '\0');
		size++;

	/* 7 = strlen("(, , 0)") */
	size += strlen(n->q->name) + 7;

	label = malloc(size + 1);
	if (label == NULL)
	{
		perror("malloc label");
		exit(EXIT_FAILURE);
	}
	label[0] = '(';
	label[1] = '\0';
	strcat(label, n->q->name);
	strcat(label, ", ");
	i = 0;
	while (n->word[i] != '\0')
	{
		strcat(label, syms[i]->sym);
		i++;
	}
	if (n->word[0] == '\0')
		strcat(label, "-");
	strcat(label, ", ");
	strcat(label, (n->owner == 0) ? "0)" : "1)");

	agset(gnode, "label", label);

	free(syms);
	free(label);
}

void drawGraph(struct Graph *g)
{
	Agraph_t *gviz;
	GVC_t *gvc;
	Agnode_t *gnode, *gdest;
	Agedge_t *gedge;
	struct ListIterator *it;
	Node *n;
	struct VizNode *nviz;
	int i;

	gviz = agopen("G", Agdirected, NULL);
	agattr(gviz, AGNODE, "color", "black");
	agattr(gviz, AGNODE, "shape", "ellipse");
	agattr(gviz, AGNODE, "peripheries", "1");
	agattr(gviz, AGNODE, "label", "node");
	agattr(gviz, AGEDGE, "color", "black");

	for (it = listIterator_first(g->nodes) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		n = listIterator_val(it);
		gnode = agnode(gviz, n->name, TRUE);
		if (n->isAccepting)
			agset(gnode, "peripheries", "2");
		if (n->isInitial)
			agset(gnode, "shape", "square");
		if (n->isWinning)
			agset(gnode, "color", "green");
		if (n->owner == 0 && n->p0.strat == EMIT)
			agset(gnode, "color", "blue");
		nviz = agbindrec(gnode, "Node", sizeof *nviz, FALSE);
		nviz->n = n;
	}
	listIterator_release(it);

	for (gnode = agfstnode(gviz) ; gnode != NULL ; gnode = agnxtnode(gviz, gnode))
	{
		Gnode_setLabel(gnode, g);
		nviz = (struct VizNode *)aggetrec(gnode, "Node", FALSE);
		if (nviz == NULL)
		{
			fprintf(stderr, "ERROR: no node associated to graphviz node %s\n", 
					agnameof(gnode));
			exit(EXIT_FAILURE);
		}
		n = nviz->n;
		if (n->owner == 0)
		{
			if (!n->meta && n->p0.succEmit != NULL)
			{
				gdest = agnode(gviz, n->p0.succEmit->name, FALSE);
				if (gdest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find gnode %s (1).\n", 
							n->p0.succEmit->name);
					exit(EXIT_FAILURE);
				}
				gedge = agedge(gviz, gnode, gdest, "emit", TRUE);
				agset(gedge, "color", "green");
			}
			if (n->meta)
			{
				for (it = listIterator_first(n->p0.succsEmit) ; 
						listIterator_hasNext(it) ; it = listIterator_next(it))
				{
					Node *succ = listIterator_val(it);
					gdest = agnode(gviz, succ->name, FALSE);
					if (gdest == NULL)
					{
						fprintf(stderr, "ERROR: cannot find gnode %s from %s "
								"(1.1).\n", succ->name, n->name);
						exit(EXIT_FAILURE);
					}
					gedge = agedge(gviz, gnode, gdest, succ->name, TRUE);
					agset(gedge, "color", "green");
				}
				listIterator_release(it);
				/*
				for (it = listIterator_first(n->p0.succsStopEmit) ; 
						listIterator_hasNext(it) ; it = listIterator_next(it))
				{
					Node *succ = listIterator_val(it);
					gdest = agnode(gviz, succ->name, FALSE);
					if (gdest == NULL)
					{
						fprintf(stderr, "ERROR: cannot find gnode %s (1.2).\n", 
								succ->name);
						exit(EXIT_FAILURE);
					}
					gedge = agedge(gviz, gnode, gdest, succ->name, TRUE);
					agset(gedge, "color", "blue");
				}
				listIterator_release(it);
				*/
			}

			{
				gdest = agnode(gviz, n->p0.succStopEmit->name, FALSE);
				if (gdest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find gnode %s (2).\n", 
							n->p0.succStopEmit->name);
					exit(EXIT_FAILURE);
				}
				gedge = agedge(gviz, gnode, gdest, "stopEmit", TRUE);
				agset(gedge, "color", "blue");
			}
		}
		else /* n->owner == 1 */
		{
			for (it = listIterator_first(n->p1.succsCont) ; 
					listIterator_hasNext(it) ; it = listIterator_next(it))
			{
				Node *succ = listIterator_val(it);
				gdest = agnode(gviz, succ->name, FALSE);
				if (gdest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find gnode %s from %s (3).\n", 
							succ->name, n->name);
					exit(EXIT_FAILURE);
				}
				gedge = agedge(gviz, gnode, gdest, "cont", TRUE);
				agset(gedge, "color", "orange");
			}
			listIterator_release(it);

			for (it = listIterator_first(n->p1.succsUncont) ; 
					listIterator_hasNext(it) ; it = listIterator_next(it))
			{
				Node *succ = listIterator_val(it);
				gdest = agnode(gviz, succ->name, FALSE);
				if (gdest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find gnode %s from %s " 
							"(4).\n", succ->name, n->name);
					exit(EXIT_FAILURE);
				}
				gedge = agedge(gviz, gnode, gdest, "uncont", TRUE);
				agset(gedge, "color", "red");
			}
			listIterator_release(it);
		}
	}

	gvc = gvContext();
	gvLayout(gvc, gviz, "dot");
	gvRender(gvc, gviz, "png", stdout);
	gvFreeLayout(gvc, gviz);

	agclose(gviz);
	gvFreeContext(gvc);
}

void graph_clean(struct Graph *g)
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
}

int main(int argc, char *argv[])
{
	struct Graph g;
	char *filename;
	int maxBufferSize;
	struct Set *W = set_empty(NULL);
	struct ListIterator *it;

	if (argc < 2)
	{
		fprintf(stderr, "Usage : %s <filename>\n", argv[0]);
		fprintf(stderr, "Where <filename> is an automaton file\n");
		exit(EXIT_FAILURE);
	}

	filename = argv[1];

	createGraphFromAutomaton(&g, filename);
	for (it = listIterator_first(g.nodes) ; listIterator_hasNext(it) ; it = listIterator_next(it))
	{
		Node *n = listIterator_val(it);
		if (n->isAccepting)
			set_add(W, n);
	}
	listIterator_release(it);
	computeW0(&g, W);
	minimize(&g);
	drawGraph(&g);

	graph_clean(&g);

	return EXIT_SUCCESS;
}

