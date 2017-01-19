#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <gvc.h>
#include <list.h>

#include "game_graph.h"



struct VizNode
{
	Agrec_t h;
	struct Node *n;
};

static char *nodeName(const struct Node *n)
{
	int size = 7; /* = strlen("(, , 0)") */
	char *name;

	size += strlen(node_stateLabel(n));
	size += strlen(node_word(n));
	
	name = malloc(size + 1);
	if (name == NULL)
	{
		perror("malloc name");
		exit(EXIT_FAILURE);
	}

	sprintf(name, "(%s, %s, %d)", node_stateLabel(n), node_word(n), 
			node_owner(n));

	return name;
}

static char *edgeName(const struct Edge *e, int i)
{
	int n = i;
	static const char *typeName[4];
	static int typeSize[4];
	static int first = 1;
	int size = 0;
	char *name;

	if (first)
	{
		first = 0;
		typeName[EMIT] = "emit";
		typeName[STOPEMIT] = "stop";
		typeName[CONTRCVD] = "cont";
		typeName[UNCONTRCVD] = "uncont";
		for (i = 0 ;  i < 4 ; i++)
		{
			typeSize[i] = strlen(typeName[i]);
		}
	}

	size = typeSize[e->type];

	while (n > 0)
	{
		size++;
		n /= 10;
	}
	
	name = malloc(size + 1);
	if (name == NULL)
	{
		perror("malloc name");
		exit(EXIT_FAILURE);
	}

	sprintf(name, "%s%d", typeName[e->type], i);

	return name;
}

void drawGraph(struct Graph *g)
{
	Agraph_t *gviz;
	GVC_t *gvc;
	Agnode_t *gnode, *gdest;
	Agedge_t *gedge;
	struct ListIterator *it;
	struct Node *n;
	struct VizNode *nviz;
	int i;
	char *colors[4];
	char *name;

	colors[EMIT] = "green";
	colors[STOPEMIT] = "blue";
	colors[UNCONTRCVD] = "red";
	colors[CONTRCVD] = "orange";

	gviz = agopen("G", Agdirected, NULL);
	agattr(gviz, AGNODE, "color", "black");
	agattr(gviz, AGNODE, "shape", "ellipse");
	agattr(gviz, AGNODE, "peripheries", "1");
	agattr(gviz, AGEDGE, "color", "black");

	for (it = listIterator_first(graph_nodes(g)) ; listIterator_hasNext(it) ; it 
			= listIterator_next(it))
	{
		n = listIterator_val(it);
		name = nodeName(n);
		gnode = agnode(gviz, name, TRUE);
		free(name);
		if (node_isAccepting(n))
			agset(gnode, "peripheries", "2");
		if (node_isInitial(n))
			agset(gnode, "shape", "square");
		if (node_isWinning(n))
			agset(gnode, "color", "green");
		if (node_owner(n) == 0 && node_strat(n) == EMIT)
			agset(gnode, "color", "blue");
		nviz = agbindrec(gnode, "Node", sizeof *nviz, FALSE);
		nviz->n = n;
		node_setData(n, gnode);
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
		for (it = listIterator_first(node_edges(n)), i = 0 ; 
				listIterator_hasNext(it) ; it = listIterator_next(it), i++)
		{
			struct Edge *e = listIterator_val(it);
			struct Node *dest = e->succ;
			Agnode_t *gdest = node_getData(dest);
			name = edgeName(e, i);
			gedge = agedge(gviz, gnode, gdest, name, TRUE);
			free(name);
			agset(gedge, "color", colors[e->type]);
		}
		listIterator_release(it);
	}
	gvc = gvContext();
	gvLayout(gvc, gviz, "dot");
	gvRender(gvc, gviz, "png", stdout);
	gvFreeLayout(gvc, gviz);

	agclose(gviz);
	gvFreeContext(gvc);
}

int main(int argc, char *argv[])
{
	struct Graph *g;
	char *filename;
	int maxBufferSize;
	struct ListIterator *it;

	if (argc < 2)
	{
		fprintf(stderr, "Usage : %s <filename>\n", argv[0]);
		fprintf(stderr, "Where <filename> is an automaton file\n");
		exit(EXIT_FAILURE);
	}

	filename = argv[1];

	g = graph_newFromAutomaton(filename);
	drawGraph(g);

	graph_free(g);

	return EXIT_SUCCESS;
}

