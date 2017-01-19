#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <gvc.h>
#include <list.h>
#include <set.h>

#include "parser_tmdautmtn.h"


#define NBSUCCS			256
#define MAXNBEDGES		4

enum EdgeType {SUCCEMIT, SUCCSTOPEMIT, SUCCRECCONT, SUCCRECUNCONT};


struct State
{
	unsigned int parserStateId;
	char *name;
	int isInitial;
	int isAccepting;
	struct State *contSuccs[NBSUCCS];
	struct State *uncontsSuccs[NBSUCCS];
};

typedef struct Node
{
	struct State *q;
	char *word;
	int owner;
	union
	{
		struct
		{
			struct Node *succEmit;
			struct Node *succStopEmit;
		} p0;
		struct
		{
			struct Node **succsCont;
			struct Node **succsUncont;
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


void setAccepting(Agnode_t *node)
{
	agset(node, "color", "blue");
}

void setInitial(Agnode_t *node)
{
	agset(node, "shape", "square");
}

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

void addNodes(struct Graph *g, struct State *s, int maxSize)
{
	char *word = malloc(maxSize + 1);
	Node *n;
	int i;

	word[0] = '\0';
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
		n->word = strdup(word);
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

	free(word);
}

void addEdges(struct Graph *g, int maxWordSize)
{
	Node *n, *dest;
	struct SearchNode sn;
	struct ListIterator *it;

	for (it = listIterator_first(g->nodes) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		n = listIterator_val(it);
		if (n->owner == 0)
		{
			/* Emit a controllable event */
			if (n->word[0] != '\0')
			{
				sn.q = n->q->contSuccs[(unsigned char)n->word[0]];
				sn.owner = 0;
				sn.word = n->word + 1;
				dest = list_search(g->nodes, &sn, cmpNode);
				if (dest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find node (%s, %s, %d).\n", 
							sn.q->name, sn.word, sn.owner);
					exit(EXIT_FAILURE);
				}
				n->p0.succEmit = dest;
			}
			/* Stop emitting (let 1 play) */
			sn.q = n->q;
			sn.owner = 1;
			sn.word = n->word;
			dest = list_search(g->nodes, &sn, cmpNode);
			if (dest == NULL)
			{
				fprintf(stderr, "ERROR: cannot find node (%s, %s, %d) (1).\n", 
						sn.q->name, sn.word, sn.owner);
				exit(EXIT_FAILURE);
			}
			n->p0.succStopEmit = dest;
		}
		else /* n->owner == 1 */
		{
			n->p1.succsCont = malloc(strlen(g->contsChars) * sizeof 
					*(n->p1.succsCont));
			if (n->p1.succsCont == NULL)
			{
				perror("malloc n->p1.succsCont");
				exit(EXIT_FAILURE);
			}
			n->p1.succsUncont = malloc(strlen(g->uncontsChars) * sizeof 
					*(n->p1.succsUncont));
			if (n->p1.succsUncont == NULL)
			{
				perror("malloc n->p1.succsUncont");
				exit(EXIT_FAILURE);
			}
			/* Add a controllable event */
			if (strlen(n->word) < maxWordSize)
			{
				int i, nbConts = strlen(g->contsChars);
				int size = strlen(n->word);
				char *word = malloc(size + 2);

				strcpy(word, n->word);
				word[size + 1] = '\0';

				sn.q = n->q;
				sn.word = word;
				sn.owner = 0;

				for (i = 0 ; i < nbConts ; i++)
				{
					word[size] = g->contsChars[i];
					dest = list_search(g->nodes, &sn, cmpNode);
					if (dest == NULL)
					{
						fprintf(stderr, "ERROR: cannot find node (%s, %s, " 
								"%d) (2).\n", sn.q->name, sn.word, sn.owner);
						exit(EXIT_FAILURE);
					}
					n->p1.succsCont[i] = dest;
				}

				free(word);
			}
			else
			{
				int nbConts = strlen(g->contsChars);
				int i;
				for (i = 0 ; i < nbConts ; i++)
				{
					n->p1.succsCont[i] = NULL;
				}
			}
			/* Receive an uncontrollable event */
			/* if nothing */
			{
				int i, nbUnconts = strlen(g->uncontsChars);

				sn.word = n->word;
				sn.owner = 0;
				
				for (i = 0 ; i < nbUnconts ; i++)
				{
					sn.q = n->q->uncontsSuccs[(unsigned char)g->uncontsChars[i]];

					dest = list_search(g->nodes, &sn, cmpNode);
					if (dest == NULL)
					{
						fprintf(stderr, "ERROR: cannot find node (%s, %s, " 
								"%d) (3).\n", sn.q->name, sn.word, sn.owner);
						exit(EXIT_FAILURE);
					}
					n->p1.succsUncont[i] = dest;
				}
			}
		}
	}
	listIterator_release(it);
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
		}
	}
	listIterator_release(it);
}

void createGraphFromAutomaton(struct Graph *g, const char *filename, int maxWordSize)
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
	createChars(pconts, &(g->contsTable), &(g->contsChars));
	createChars(punconts, &(g->uncontsTable), &(g->uncontsChars));
	createStates(g, pstates, pedges);

	for (it = listIterator_first(g->states) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct State *s = listIterator_val(it);
		addNodes(g, s, maxWordSize);
	}
	listIterator_release(it);

	addEdges(g, maxWordSize);
}

void attr(struct Set *ret, struct Graph *g, int player, const struct Set *U, 
		struct List *nodes)
{
	int stable = 0;
	struct ListIterator *it;

	set_reset(ret);
	set_copy(ret, U, NULL);

	fprintf(stderr, "attr 1\n");
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
					int i, size;
					size = strlen(g->contsChars);
					for (i = 0 ; i < size ; i++)
					{
						if (listIn(nodes, n->p1.succsCont[i]) && set_in(ret, 
									n->p1.succsCont[i]))
						{
							stable = 0;
							set_add(ret, n);
							break;
						}
					}
					if (stable)
					{
						size = strlen(g->uncontsChars);
						for (i = 0 ; i < size ; i++)
						{
							if (listIn(nodes, n->p1.succsUncont[i]) && 
									set_in(ret, n->p1.succsUncont[i]))
							{
								stable = 0;
								set_add(ret, n);
								break;
							}
						}
					}
				}
			}
			/* n->owner != owner */
			else
			{
				if (n->owner == 0)
				{
					if ((n->word[0] == '\0' ||(listIn(nodes, n->p0.succEmit) 
									&& set_in(ret, n->p0.succEmit))) && 
							listIn(nodes, n->p0.succStopEmit) && set_in(ret, 
								n->p0.succStopEmit))
					{
						stable = 0;
						set_add(ret, n);
					}
				}
				else
				{
					int ok = 1;
					int size = strlen(g->contsChars);
					int i;

					for (i = 0 ; i < size ; i++)
					{
						if (listIn(nodes, n->p1.succsCont[i]) && !set_in(ret, 
									n->p1.succsCont[i]))
						{
							ok = 0;
							break;
						}
					}
					if (ok)
					{
						size = strlen(g->uncontsChars);
						for (i = 0 ; i < size ; i++)
						{
							if (listIn(nodes, n->p1.succsUncont[i]) && 
									!set_in(ret, n->p1.succsUncont[i]))
							{
								ok = 0;
								break;
							}
						}
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
	fprintf(stderr, "attr(%d, {", player);
	set_applyToAll(U, printNode, stderr);
	fprintf(stderr, "}) = {");
	set_applyToAll(ret, printNode, stderr);
	fprintf(stderr, "}\n");

	fprintf(stderr, "attr - end\n");
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

	fprintf(stderr, "1\n");

	do
	{
		attr(R, g, 0, B, S);

		/* Tr = S \ B */
		set_reset(Tr);
		for (it = listIterator_first(S) ; listIterator_hasNext(it) ; it = listIterator_next(it))
		{
			set_add(Tr, listIterator_val(it));
		}
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

	fprintf(stderr, "2\n");
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
		nviz = agbindrec(gnode, "Node", sizeof *nviz, FALSE);
		nviz->n = n;
	}
	listIterator_release(it);

	for (gnode = agfstnode(gviz) ; gnode != NULL ; gnode = agnxtnode(gviz, gnode))
	{
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
			if (n->word[0] != '\0')
			{
				gdest = agnode(gviz, n->p0.succEmit->name, FALSE);
				if (gdest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find gnode %s.\n", 
							n->p0.succEmit->name);
					exit(EXIT_FAILURE);
				}
				gedge = agedge(gviz, gnode, gdest, "emit", TRUE);
				agset(gedge, "color", "green");
			}

			gdest = agnode(gviz, n->p0.succStopEmit->name, FALSE);
			if (gdest == NULL)
			{
				fprintf(stderr, "ERROR: cannot find gnode %s.\n", 
						n->p0.succStopEmit->name);
				exit(EXIT_FAILURE);
			}
			gedge = agedge(gviz, gnode, gdest, "stopEmit", TRUE);
			agset(gedge, "color", "blue");
		}
		else /* n->owner == 1 */
		{
			int nbChars = strlen(g->contsChars);
			for (i = 0 ; i < nbChars ; i++)
			{
				if (n->p1.succsCont[i] != NULL) /* Max buffer size */
				{
					gdest = agnode(gviz, n->p1.succsCont[i]->name, FALSE);
					if (gdest == NULL)
					{
						fprintf(stderr, "ERROR: cannot find gnode %s.\n", 
								n->p1.succsCont[i]->name);
						exit(EXIT_FAILURE);
					}
					gedge = agedge(gviz, gnode, gdest, "cont", TRUE);
					agset(gedge, "color", "orange");
				}
			}

			nbChars = strlen(g->uncontsChars);
			for (i = 0 ; i < nbChars ; i++)
			{
				gdest = agnode(gviz, n->p1.succsUncont[i]->name, FALSE);
				if (gdest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find gnode %s.\n", 
							n->p1.succsUncont[i]->name);
					exit(EXIT_FAILURE);
				}
				gedge = agedge(gviz, gnode, gdest, "uncont", TRUE);
				agset(gedge, "color", "red");
			}
		}
	}

	gvc = gvContext();
	gvLayout(gvc, gviz, "dot");
	gvRender(gvc, gviz, "png", stdout);
	gvFreeLayout(gvc, gviz);

	gvFreeContext(gvc);
	agclose(gviz);
}

int main(int argc, char *argv[])
{
	struct Graph g;
	char *filename;
	int maxBufferSize;
	struct Set *W = set_empty(NULL);
	struct ListIterator *it;

	if (argc < 3)
	{
		fprintf(stderr, "Usage : %s <filename> <bufferSize>\n", argv[0]);
		fprintf(stderr, "Where <filename> is an automaton file and <bufferSize>" 
				" is the maximal size of controllable events present in the buffer" 
				" of the enforcement mechanism.\n");
		exit(EXIT_FAILURE);
	}

	filename = argv[1];
	maxBufferSize = atoi(argv[2]);

	createGraphFromAutomaton(&g, filename, maxBufferSize);
	for (it = listIterator_first(g.nodes) ; listIterator_hasNext(it) ; it = listIterator_next(it))
	{
		Node *n = listIterator_val(it);
		if (n->isAccepting)
			set_add(W, n);
	}
	listIterator_release(it);
	computeW0(&g, W);
	set_applyToAll(W, node_setWinning, NULL);
	drawGraph(&g);
	
	return EXIT_SUCCESS;
}

