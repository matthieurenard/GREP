#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <gvc.h>
#include <list.h>

#include "parser.h"


#define NBSUCCS			256


struct State
{
	unsigned int parserStateId;
	char *name;
	int isInitial;
	int isAccepting;
	struct State *contSuccs[NBSUCCS];
	struct State *uncontsSuccs[NBSUCCS];
};

typedef struct
{
	Agrec_t h;
	struct State *q;
	char *word;
	int owner;
} Node;

struct SymbolTableEl
{
	char *sym;
	unsigned int id;
	char c;
};

struct List *contsTable;
struct List *uncontsTable;
char *contsChars;
char *uncontsChars;
struct List *states;
const char *symLabels = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";


int cmpSymbolChar(void *val, void *pel)
{
	struct SymbolTableEl *el = pel;
	char c = *(char *)val;
	return (el->c == c);
}

int cmpSymId(void *val, void *pel)
{
	struct SymbolTableEl *el = pel;
	unsigned int id = *(unsigned int *)val;
	return (el->id == id);
}

int cmpStateId(void *val, void *pState)
{
	struct State *s = pState;
	int id = *(int *)val;
	return (s->parserStateId == id);
}

void createChars(const struct List *l, struct List **psymbolTable, char **pchars)
{
	int size = list_size(l);
	int i;
	struct ListIterator *it;
	char *chars;
	struct List *symbolTable;

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
		sprintf(s, "%s, %s, %d)", n->q->name, "-", n->owner);
	else
		sprintf(s, "(%s, %s, %d)", n->q->name, n->word, n->owner);
	return s;
}

int nodeNameSizeFromState(struct State *s, char *word, int owner)
{
	return 6 + strlen(s->name) + strlen(word) + 1 + 1 + (word[0] == '\0');
}

char *nodeNameFromState(char *name, struct State *s, char *word, int owner)
{
	if (word[0] == '\0')
		sprintf(name, "(%s, %s, %d)", s->name, "-", owner);
	else
		sprintf(name, "(%s, %s, %d)", s->name, word, owner);
	return name;
}

void setAccepting(Agnode_t *node)
{
	agset(node, "color", "blue");
}

void setInitial(Agnode_t *node)
{
	agset(node, "shape", "square");
}

void addNodesRec(Agraph_t *g, struct State *s, int maxSize, char *word)
{
	int size = strlen(word);
	Node fake;
	Node *n;
	char *name = NULL;
	char *pCont;
	Agnode_t *gnode;


	if (size >= maxSize)
		return;

	fake.q = s;
	
	pCont = contsChars;
	fake.word = word;
	while (*pCont != '\0')
	{
		word[size] = *pCont;
		word[size+1] = '\0';

		fake.owner = 0;

		if (name == NULL)
			name = malloc(Node_nameSize(&fake));

		Node_name(name, &fake);
		gnode = agnode(g, name, TRUE);
		if (s->isAccepting)
			setAccepting(gnode);
		n = (Node *)(agbindrec(gnode, "Node", sizeof *n, FALSE));
		n->q = s;
		n->word = strdup(fake.word);
		n->owner = fake.owner;

		fake.owner = 1;
		Node_name(name, &fake);
		gnode = agnode(g, name, TRUE);
		if (s->isAccepting)
			setAccepting(gnode);
		n = (Node *)(agbindrec(gnode, "Node", sizeof *n, FALSE));
		n->q = s;
		n->word = strdup(fake.word);
		n->owner = fake.owner;

		addNodesRec(g, s, maxSize, word);

		pCont++;
	}

	free(name);
}

void addNodes(Agraph_t *g, struct State *s, int maxSize)
{
	char *word = malloc(maxSize + 1);
	char *name = malloc(6 + strlen(s->name) + 1 + 1 + 1);
	Agnode_t *gnode;
	Node *n;

	sprintf(name, "(%s, -, %d)", s->name, 0);
	gnode = agnode(g, name, TRUE);
	if (s->isAccepting)
		setAccepting(gnode);
	n = (Node *)(agbindrec(gnode, "Node", sizeof *n, FALSE));
	n->q = s;
	n->word = "";
	n->owner = 0;

	sprintf(name, "(%s, -, %d)", s->name, 1);
	gnode = agnode(g, name, TRUE);
	if (s->isAccepting)
		setAccepting(gnode);
	if (s->isInitial)
		setInitial(gnode);
	n = (Node *)(agbindrec(gnode, "Node", sizeof *n, FALSE));
	n->q = s;
	n->word = "";
	n->owner = 1;

	word[0] = '\0';

	free(name);

	addNodesRec(g, s, maxSize, word);

	free(word);
}

void addEdges(Agraph_t *g, int maxWordSize)
{
	Agnode_t *gn, *gdest;
	Node *n;
	Agedge_t *e;

	for (gn = agfstnode(g) ; gn != NULL ; gn = agnxtnode(g, gn))
	{
		n = (Node *)(aggetrec(gn, "Node", FALSE));
		/* Emit a controllable event */
		if (n->owner == 0 && n->word[0] != '\0')
		{
			struct State *dest = n->q->contSuccs[(unsigned char)n->word[0]];
			char *name;
			name = malloc(nodeNameSizeFromState(dest, n->word + 1, 0));
			if (name == NULL)
			{
				perror("malloc name");
				exit(EXIT_FAILURE);
			}
			nodeNameFromState(name, dest, n->word + 1, 0);
			gdest = agnode(g, name, FALSE);
			if (gdest == NULL)
			{
				fprintf(stderr, "ERROR: cannot find node with name %s.\n", name);
				exit(EXIT_FAILURE);
			}
			e = agedge(g, gn, gdest, "emit", TRUE);
			agset(e, "color", "green");
			free(name);
		}
		/* Stop emitting (1 plays after) */
		if (n->owner == 0)
		{
			/* Same name but replacing 0 with 1 */
			char *name = malloc(Node_nameSize(n));
			if (name == NULL)
			{
				perror("malloc name");
				exit(EXIT_FAILURE);
			}
			nodeNameFromState(name, n->q, n->word, 1);
			gdest = agnode(g, name, FALSE);
			if (gdest == NULL)
			{
				fprintf(stderr, "ERROR: cannot find node with name %s.\n", name);
				exit(EXIT_FAILURE);
			}
			e = agedge(g, gn, gdest, "stop", TRUE);
			agset(e, "color", "lightblue");
			free(name);
		}
		/* Add a controllable event */
		if (n->owner == 1 && strlen(n->word) < maxWordSize)
		{
			char *pcont = contsChars;
			int size = strlen(n->word);
			char *word = malloc(size + 2);
			char *name;

			if (word == NULL)
			{
				perror("malloc word");
				exit(EXIT_FAILURE);
			}
			strcpy(word, n->word);
			word[size] = *pcont;
			word[size+1] = '\0';
			name = malloc(nodeNameSizeFromState(n->q, word, 0));
			if (name == NULL)
			{
				perror("malloc name");
				exit(EXIT_FAILURE);
			}
			while (*pcont != '\0')
			{
				word[size] = *pcont;
				word[size+1] = '\0';
				nodeNameFromState(name, n->q, word, 0);
				gdest = agnode(g, name, FALSE);
				if (gdest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find node with name %s.\n", 
							name);
					exit(EXIT_FAILURE);
				}
				e = agedge(g, gn, gdest, "cont", TRUE);
				agset(e, "color", "orange");
				pcont++;
			}
			free(name);
			free(word);
		}
		/* Receive an uncontrollable event */
		if (n->owner == 1)
		{
			char *puncont = uncontsChars;
			char *name;

			while (*puncont != '\0')
			{
				struct State *dest = n->q->uncontsSuccs[(unsigned char)*puncont];
				name = malloc(nodeNameSizeFromState(dest, n->word, 0));
				if (name == NULL)
				{
					perror("malloc name");
					exit(EXIT_FAILURE);
				}
				nodeNameFromState(name, dest, n->word, 0);
				gdest = agnode(g, name, FALSE);
				if (gdest == NULL)
				{
					fprintf(stderr, "ERROR: cannot find node with name %s.\n", 
							name);
					exit(EXIT_FAILURE);
				}
				e = agedge(g, gn, gdest, "uncont", TRUE);
				agset(e, "color", "red");
				free(name);
				puncont++;
			}
		}
				
	}
}

void createStates(const struct List *pStates, const struct List *edges)
{
	struct ListIterator *it;
	int i;

	states = list_new();

	for (it = listIterator_first(pStates) ; listIterator_hasNext(it) ; it = 
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

		list_add(states, s);
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
		from = list_search(states, &id, cmpStateId);
		id = parserState_getId(pto);
		to = list_search(states, &id, cmpStateId);

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
			el = list_search(contsTable, &id, cmpSymId);
			if (el == NULL)
			{
				fprintf(stderr, "ERROR: cannot find symbol of id %d.\n", id);
				exit(EXIT_FAILURE);
			}
			from->contSuccs[(unsigned char)el->c] = to;
		}
		else
		{
			el = list_search(uncontsTable, &id, cmpSymId);
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

void createGraphFromAutomaton(Agraph_t *g, const char *filename, int maxWordSize)
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

	createChars(pconts, &contsTable, &contsChars);
	createChars(punconts, &uncontsTable, &uncontsChars);
	createStates(pstates, pedges);

	for (it = listIterator_first(states) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		struct State *s = listIterator_val(it);
		addNodes(g, s, maxWordSize);
	}
	listIterator_release(it);

	addEdges(g, maxWordSize);
}


int main(int argc, char *argv[])
{
	Agraph_t *g;
	GVC_t *gvc;
	char *filename;
	int maxBufferSize;

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

	g = agopen("G", Agdirected, NULL);
	agattr(g, AGNODE, "color", "black");
	agattr(g, AGNODE, "shape", "ellipse");
	agattr(g, AGEDGE, "color", "black");

	createGraphFromAutomaton(g, filename, maxBufferSize);
	
	gvc = gvContext();
	gvLayout(gvc, g, "dot");
	gvRender(gvc, g, "png", stdout);
	gvFreeLayout(gvc, g);

	agclose(g);
	gvFreeContext(gvc);

	return EXIT_SUCCESS;
}

