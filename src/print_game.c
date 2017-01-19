#include <stdio.h>
#include <stdlib.h>

#include <getopt.h>
#include <string.h>
#include <sys/time.h>

/* For debugging */
#include "dbmutils.h"

#include <gvc.h>
#include <list.h>

#include "game_graph.h"



struct VizNode
{
	Agrec_t h;
	struct Node *n;
};

struct VizZone
{
	Agrec_t h;
	struct Zone *z;
};

extern struct Clock **clocks;

static char *nodeName(const struct Node *n)
{
	int size = 9; /* = strlen("(, , , 0)") */
	char *name;
	const char *constraints;

	constraints = node_getConstraints(n);

	size += strlen(constraints);
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

	if (node_word(n)[0] == '\0')
		sprintf(name, "(%s, %s, -, %d)", node_stateLabel(n), constraints, 
				node_owner(n));
	else
		sprintf(name, "(%s, %s, %s, %d)", node_stateLabel(n), constraints, 
				node_word(n), node_owner(n));

	return name;
}

static char *edgeName(const struct Edge *e, int i)
{
	int n = i;
	static const char *typeName[5];
	static int typeSize[5];
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
		typeName[TIMELPSD] = "timelpsd";
		for (i = 0 ;  i < 5 ; i++)
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
	
	name = malloc(size + 2);
	if (name == NULL)
	{
		perror("malloc name");
		exit(EXIT_FAILURE);
	}

	sprintf(name, "%s%d", typeName[e->type], i);

	return name;
}

static char *zoneName(const struct Node *n)
{
	int size = 4; /* = strlen("(, )") */
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

	return name;
}

static char *zoneEdgeName(const struct ZoneEdge *e, int i)
{
	int n = i;
	static const char *typeName[5];
	static int typeSize[5];
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
		typeName[TIMELPSD] = "timelpsd";
		for (i = 0 ;  i < 5 ; i++)
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
	
	name = malloc(size + 2);
	if (name == NULL)
	{
		perror("malloc name");
		exit(EXIT_FAILURE);
	}

	sprintf(name, "%s%d", typeName[e->type], i);

	return name;
}

void drawGraph(const struct Graph *g, FILE *outFile)
{
	Agraph_t *gviz;
	GVC_t *gvc;
	Agnode_t *gnode, *gdest;
	Agedge_t *gedge;
	struct ListIterator *it;
	struct Node *n;
	struct VizNode *nviz;
	int i;
	char *colors[5];
	char *name;

	colors[EMIT] = "green";
	colors[STOPEMIT] = "blue";
	colors[UNCONTRCVD] = "red";
	colors[CONTRCVD] = "orange";
	colors[TIMELPSD] = "purple";

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

void drawZoneGraph(const struct ZoneGraph *zg, FILE *outFile)
{
	Agraph_t *gviz;
	GVC_t *gvc;
	Agnode_t *gnode, *gdest;
	Agedge_t *gedge;
	struct ListIterator *it;
	struct Zone *z;
	struct VizZone *zviz;
	int i;
	char *colors[5];
	char *name;

	colors[EMIT] = "green";
	colors[STOPEMIT] = "blue";
	colors[UNCONTRCVD] = "red";
	colors[CONTRCVD] = "orange";
	colors[TIMELPSD] = "purple";

	gviz = agopen("G", Agdirected, NULL);
	agattr(gviz, AGRAPH, "overlap", "false");
	//agattr(gviz, AGRAPH, "ratio", "fill");
	agattr(gviz, AGNODE, "color", "black");
	agattr(gviz, AGNODE, "shape", "ellipse");
	agattr(gviz, AGNODE, "peripheries", "1");
	agattr(gviz, AGEDGE, "color", "black");

	for (it = listIterator_first(zoneGraph_getZones(zg)) ; 
			listIterator_hasNext(it) ; it = listIterator_next(it))
	{
		z = listIterator_val(it);
		name = zone_getName(z);
		gnode = agnode(gviz, name, TRUE);
		free(name);
		zviz = agbindrec(gnode, "Node", sizeof *zviz, FALSE);
		zviz->z = z;
		zone_setData(z, gnode);
	}
	listIterator_release(it);

	for (gnode = agfstnode(gviz) ; gnode != NULL ; gnode = agnxtnode(gviz, gnode))
	{
		zviz = (struct VizZone *)aggetrec(gnode, "Node", FALSE);
		if (zviz == NULL)
		{
			fprintf(stderr, "ERROR: no zone associated to graphviz node %s\n", 
					agnameof(gnode));
			exit(EXIT_FAILURE);
		}
		z = zviz->z;
		for (it = listIterator_first(zone_getEdges(z)), i = 0 ; 
				listIterator_hasNext(it) ; it = listIterator_next(it), i++)
		{
			struct ZoneEdge *e = listIterator_val(it);
			const struct Zone *dest = e->succ;
			Agnode_t *gdest = zone_getData(dest);
			name = zoneEdgeName(e, i);
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

