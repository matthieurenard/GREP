#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

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
	const char *constraints;

	constraints = node_getConstraints(n);

	size += strlen(node_stateLabel(n));
	size += strlen(node_word(n));
	size += strlen(constraints);

	if (node_word(n)[0] == '\0')
		size++;
	
	name = malloc(size + 1);
	if (name == NULL)
	{
		perror("malloc name");
		exit(EXIT_FAILURE);
	}

	sprintf(name, "(%s, %s)", node_stateLabel(n), constraints);

	/*
	if (node_word(n)[0] == '\0')
		sprintf(name, "(%s, -, %d)", node_stateLabel(n), node_owner(n));
	else
		sprintf(name, "(%s, %s, %d)", node_stateLabel(n), node_word(n), 
				node_owner(n));
	*/

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

void drawGraph(struct Graph *g, FILE *outFile)
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
	agattr(gviz, AGRAPH, "overlap", "false");
	//agattr(gviz, AGRAPH, "ratio", "fill");
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
		{
			agset(gnode, "shape", "square");
			//agattr(gviz, AGRAPH, "root", agnameof(gnode));
		}
		if (node_isWinning(n))
			agset(gnode, "color", "green");
		if (node_owner(n) == 0 && node_strat(n) == STRAT_EMIT)
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
	gvRender(gvc, gviz, "png", outFile);
	gvFreeLayout(gvc, gviz);

	agclose(gviz);
	gvFreeContext(gvc);
}

void print_usage(FILE *out, char *progName)
{
	fprintf(out, "Usage : %s [options] <filename>\n", progName);
	fprintf(out, "where <filename> is an automaton file.\n");
	fprintf(out, "List of possible options:\n"
			"-d, --drawgraph=FILE    print the game graph in FILE\n"
			"-l, --log-file=FILE     use FILE as log file\n"
		   );
}

int main(int argc, char *argv[])
{
	struct Graph *g;
	char *filename;
	int maxBufferSize;
	struct ListIterator *it;
	struct Enforcer *e;
	char buffer[256];
	char c;
	int i, optionIndex;
	FILE *logFile = stderr, *drawFile = NULL;

	struct option longOptions[] = 
	{
		{"draw-graph", required_argument, NULL, 'd'},
		{"log-file", required_argument, NULL, 'l'},
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "d:l:", longOptions, &optionIndex)) != -1)
	{
		if (c == 0)
			c = longOptions[optionIndex].val;
		switch (c)
		{

			case 'd':
				drawFile = fopen(optarg, "w");
				if (drawFile == NULL)
				{
					perror("fopen");
					fprintf(stderr, "Impossible to open %s, graph will not be " 
							"drawn.\n", optarg);
				}
			break;

			case 'l':
				logFile = fopen(optarg, "a");
				if (logFile == NULL)
				{
					perror("fopen");
					fprintf(stderr, "Impossible to open %s, log will be "
							"redirected to stderr.\n", optarg);
				}
			break;

			case '?':
			break;

			default:
				fprintf(stderr, "getopt_long error.\n");
			break;
		}
	}

	if (optind >= argc)
	{
		print_usage(stderr, argv[0]);
		exit(EXIT_FAILURE);
	}

	filename = argv[optind];

	g = graph_newFromAutomaton(filename);
	if (drawFile != NULL)
		drawGraph(g, drawFile);

	e = enforcer_new(g, logFile);

	i = 0;
	while ((c = getchar()) != EOF)
	{
		if (c == ' ' || c == '\n')
		{
			if (i != 0)
			{
				struct Event event;
				buffer[i] = '\0';
				event.label = buffer;
				enforcer_eventRcvd(e, &event);
				while (enforcer_getStrat(e) == STRAT_EMIT)
					enforcer_emit(e);
			}
			i = 0;
		}
		else
			buffer[i++] = c;
	}


	enforcer_free(e);
	graph_free(g);

	return EXIT_SUCCESS;
}

