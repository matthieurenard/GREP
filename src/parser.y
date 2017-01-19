%code
{
	#include <stdlib.h>
	#include <stdio.h>
	#include <string.h>

	#include "parser.h"

	#include "list.h"

	#define NODEATTR_ACCEPTING		0x1
	#define NODEATTR_INITIAL		0x2


	static char charSet[256];
	static struct ParserAutomaton *a;

	static int cmpStateName(void *, void *);

	static int cmpClockName(void *, void *);

	static int cmpSymLabel(void *, void *);

	static unsigned int nextId = 0;
	static unsigned int symId = 0;
	static unsigned int clockId = 0;

	static void parserState_free(void *);
	static void parserEdge_free(void *);
	static void parserClockConstraint_free(void *);
	static void parserClock_free(void *);
	static void parserSymbol_free(void *);

	/* Avoid compiler warnings */
	int yylex();
	int yyerror(const char *);
}

%code requires
{
	enum ParserRelation {LT, LEQ, EQ, GEQ, GT, NONE};

	struct ParserState
	{
		int id;
		char *name;
		int attrs;
	};

	struct ParserClock
	{
		unsigned int id;
		char *name;
	};

	struct ParserClockConstraint
	{
		struct ParserClock *clock;
		int bound;
		enum ParserRelation rel;
	};

	struct ParserEdge
	{
		struct ParserState *from;
		struct ParserState *to;
		struct List *constraints;
		struct List *resets;
		struct ParserSymbol *sym;
	};

	struct ParserSymbol
	{
		unsigned int id;
		int isCont;
		char *label;
	};

	struct ParserAutomaton
	{
		int nbStates;
		int nbEdges;
		int nbConts;
		int nbUnconts;
		int nbAcceptings;
		int nbInitials;
		int nbClocks;
		struct List *states;
		struct List *edges;
		struct List *conts;
		struct List *unconts;
		struct List *clocks;
	};
}

%code provides
{
	struct ParserState;
	struct ParserEdge;
	struct ParserAutomaton;
	struct ParserClockConstraint;
	struct ParserClock;
	struct ParserSymbol;

	/** parse.y **/
	/** ParserState */
	int parserState_isAccepting(const struct ParserState *);
	int parserState_isInitial(const struct ParserState *);
	const char *parserState_getName(const struct ParserState *);
	unsigned int parserState_getId(const struct ParserState *);

	/** ParserEdge **/
	const struct ParserState *parserEdge_getFrom(const struct ParserEdge *);
	const struct ParserState *parserEdge_getTo(const struct ParserEdge *);
	const struct ParserSymbol *parserEdge_getSym(const struct ParserEdge *);
	const struct List *parserEdge_getConstraints(const struct ParserEdge *);
	const struct List *parserEdge_getResets(const struct ParserEdge *);

	/** ParserClock **/
	unsigned int parserClock_getId(const struct ParserClock *);
	const char *parserClock_getName(const struct ParserClock *);

	/** ParserClockConstraint **/
	const struct ParserClock *parserClockConstraint_getClock(
		const struct ParserClockConstraint *);
	int parserClockConstraint_getBound(const struct ParserClockConstraint *);
	enum ParserRelation parserClockConstraint_getRel(
		const struct ParserClockConstraint *);

	/** ParserSymbol **/
	unsigned int parserSymbol_getId(const struct ParserSymbol *);
	int parserSymbol_isCont(const struct ParserSymbol *);
	const char *parserSymbol_getLabel(const struct ParserSymbol *);

	/** automaton **/
	int parser_getNbStates();
	int parser_getNbEdges();
	int parser_getNbConts();
	int parser_getNbUnconts();
	int parser_getNbAcceptings();
	int parser_getNbInitials();
	int parser_getNbClocks();
	const struct List *parser_getStates();
	const struct List *parser_getEdges();
	const struct List *parser_getConts();
	const struct List *parser_getUnconts();
	const struct List *parser_getClocks();
	void parser_cleanup();

	/** scanner.l **/
	int parseFile(const char *);
}

%define parse.error verbose

%union
{
	char c;
	char *s;
	int n;
	struct ParserState *q;
	struct ParserSymbol *sym;
	struct ParserClock *clock;
	struct ParserClockConstraint *constraint;
	struct ParserEdge *e;
	struct List *l;
	enum ParserRelation rel;
}

%token AUTOMATON CONT UNCONT NODE CLOCKS EDGES 
%token ACCEPTING INITIALTOK
%token ID INTEGER EDGE 
%token LEQTOK GEQTOK 

%type <s> ID
%type <n> INTEGER
/* Alphabet */
%type <s> letter
%type <l> optletterlist letterlist
/* Nodes */
%type <q> defnode node
%type <l> optlistnodes listnodes
%type <n> nodeattrs listnodeattr nodeattr 
/* Clocks */
%type <clock> clock
%type <l> optlistclocknames listclocknames
/* Edges */
%type <e> defedge
%type <sym> symbol
%type <l> listclocks listconstraints
%type <l> optlistedges listedges
/* Order relation */
%type <rel> rel
/* Constraints */
%type <constraint> constraint

%right '\n' ';'
%%

file: 			automaton
;

automaton: 		defautomaton automatonAttrs endautomaton
;

defautomaton:	AUTOMATON optends '{' optends
				{
					int i;

					for (i = 0 ; i < 256 ; i++)
					{
						charSet[i] = i;
					}
					a = malloc(sizeof *a);
					if (a == NULL)
					{
						perror("malloc a");
						exit(EXIT_FAILURE);
					}

					a->nbAcceptings = 0;
					a->nbInitials = 0;
					a->nbClocks = 0;
				}
;

automatonAttrs:	defalph optends defnodes optends defclocks optends defedges 
			  		optends
;

defalph: defcont term optterms defuncont term
		|defuncont term optterms defcont term
;

defcont: CONT optends '{' optletterlist '}'
			{
				int i = 0;
				struct ListIterator *it;
				a->conts = $4;
				for (it = listIterator_first(a->conts) ; 
					listIterator_hasNext(it) ; it = listIterator_next(it))
				{
					struct ParserSymbol *sym = listIterator_val(it);
					sym ->isCont = 1;
					i++;
				}
				listIterator_release(it);

				a->nbConts = i;
			}
;

defuncont: UNCONT optends '{' optletterlist '}'
			{
				int i = 0;
				struct ListIterator *it;
				a->unconts = $4;
				for (it = listIterator_first(a->unconts) ; 
					listIterator_hasNext(it) ; it = listIterator_next(it))
				{
					struct ParserSymbol *sym = listIterator_val(it);
					sym->isCont = 0;
					i++;
				}
				listIterator_release(it);

				a->nbUnconts = i;
			}
				
;

optletterlist:	optends							{$$ = list_new();}
			 	| optends letterlist optends	{$$ = $2;}
;

letterlist:		letter
				{
					struct ParserSymbol *sym;
					sym = malloc(sizeof *sym);
					if (sym == NULL)
					{
						perror("malloc sym");
						exit(EXIT_FAILURE);
					}
					sym->id = symId++;
					sym->isCont = -1;
					sym->label = $1;
					$$ = list_new();
					list_add($$, sym);
				}
			|	letterlist sep letter
				{
					struct ParserSymbol *sym;
					sym = malloc(sizeof *sym);
					if (sym == NULL)
					{
						perror("malloc sym");
						exit(EXIT_FAILURE);
					}
					sym->id = symId++;
					sym->isCont = -1;
					sym->label = $3;
					list_add($1, sym);
					$$ = $1;
				}
;

letter:		ID	{$$ = $1;}
;

sep: 	  ',' optends
   		| ';' optends
;

optsep: optends
	  	| ',' optends
	 	| ';' optends
;

defnodes: 	NODE optends '{' optlistnodes '}'
			{
				a->states = $4; 
				a->nbStates = list_size(a->states);
			}
;

optlistnodes: 	optends						{$$ = list_new();}
				| optends listnodes optsep	{$$ = $2;}

listnodes:	defnode					{$$ = list_new(); list_add($$, $1);}
			| listnodes sep defnode	{list_add($1, $3); $$ = $1;}
;

defnode:	node nodeattrs {$1->attrs = $2; $$ = $1;}
;

node: 	ID
		{
			struct ParserState *s = malloc(sizeof *s);
			if (s == NULL)
			{
				perror("malloc s");
				exit(EXIT_FAILURE);
			}

			s->name = $1;
			s->id = nextId++;
			s->attrs = 0;
			$$ = s;
		}
;

nodeattrs:	{$$ = 0;}
			| '[' listnodeattr ']' {$$ = $2;}
;

listnodeattr: 								{$$ = 0;}
			| listnodeattr sep nodeattr		{$$ |= $3;}
			| listnodeattr nodeattr			{$$ |= $2;}
;

nodeattr: ACCEPTING 	{$$ = NODEATTR_ACCEPTING; a->nbAcceptings++;}
		| INITIALTOK	{$$ = NODEATTR_INITIAL; a->nbInitials++;}
;

defclocks: 	CLOCKS optends '{' optlistclocknames '}'
		 	{
				a->clocks = $4;
				a->nbClocks = list_size(a->clocks);
			}
;

optlistclocknames:	optends							{$$ = list_new();}
				 	| optends listclocknames optsep	{$$ = $2;}

listclocknames: ID
				{
					struct ParserClock *clock = malloc(sizeof *clock);

					if (clock == NULL)
					{
						perror("malloc clock");
						exit(EXIT_FAILURE);
					}

					clock->id = clockId++;
					clock->name = $1;
					$$ = list_new();
					list_add($$, clock);
				}
				| listclocknames sep ID
				{
					struct ParserClock *clock = malloc(sizeof *clock);

					if (clock == NULL)
					{
						perror("malloc clock");
						exit(EXIT_FAILURE);
					}

					clock->id = clockId++;
					clock->name = $3;
					a->nbClocks++;
					list_add($1, clock);
					$$ = $1;
				}
;

defedges:	EDGES optends '{' optlistedges '}'
			{
				a->edges = $4;
				a->nbEdges = list_size($4);
			}
;

optlistedges:	optends						{$$ = list_new();}
				| optends listedges optsep	{$$ = $2;}
;

listedges:	defedge					{$$ = list_new(); list_add($$, $1);}
			| listedges sep defedge	{$$ = list_add($1, $3);}
;

defedge: ID EDGE '{' symbol '}' '{' listclocks '}' '{' listconstraints '}' ID
			{
				struct ParserEdge *e = malloc(sizeof *e);
				if (e == NULL)
				{
					perror("malloc e");
					exit(EXIT_FAILURE);
				}

				e->from = list_search(a->states, $1, cmpStateName);
				if (e->from == NULL)
				{
					fprintf(stderr, "ERROR : no state named %s found.\n", $1);
					exit(EXIT_FAILURE);
				}

				e->to = list_search(a->states, $12, cmpStateName);
				if (e->to == NULL)
				{
					fprintf(stderr, "ERROR : no state named %s found.\n", $12);
					exit(EXIT_FAILURE);
				}

				e->sym = $4;
				e->resets = $7;
				e->constraints = $10;

				free($1);
				free($12);

				$$ = e;
			}
;

symbol:	ID 
		{
			struct ParserSymbol *sym;
			sym = list_search(a->conts, $1, cmpSymLabel);
			if (sym == NULL)
				sym = list_search(a->unconts, $1, cmpSymLabel);
			if (sym == NULL)
			{
				fprintf(stderr, "Unknown label %s\n", $1);
				exit(EXIT_FAILURE);
			}
			$$ = sym;
			free($1);
		}
;

listclocks:							{$$ = list_new();}
		  	| clock					{$$ = list_add(list_new(), $1);}
			| listclocks sep clock 	{$$ = list_add($1, $3);}
;

clock:	ID
		{
			struct ParserClock *clock = list_search(a->clocks, $1, cmpClockName);
			if (clock == NULL)
			{
				fprintf(stderr, "Unknown clock %s\n", $1);
				exit(EXIT_FAILURE);
			}
			$$ = clock;
			free($1);
		}
;

listconstraints: 							{$$ = list_new();}
				| constraint				{$$ = list_add(list_new(), $1);}
				| listconstraints sep constraint	{$$ = list_add($1, $3);}
;

constraint:	ID rel INTEGER
			{
				struct ParserClockConstraint *constraint = 
					malloc(sizeof *constraint);

				if (constraint == NULL)
				{
					perror("malloc constraint");
					exit(EXIT_FAILURE);
				}

				constraint->clock = list_search(a->clocks, $1, cmpClockName);
				if (constraint->clock == NULL)
				{
					fprintf(stderr, "Unknown clock %s\n", $1);
					exit(EXIT_FAILURE);
				}

				constraint->rel = $2;
				constraint->bound = $3;
				$$ = constraint;
				free($1);
			}
				
			| INTEGER rel ID
			{
				struct ParserClockConstraint *constraint = 
					malloc(sizeof *constraint);

				if (constraint == NULL)
				{
					perror("malloc constraint");
					exit(EXIT_FAILURE);
				}

				constraint->clock = list_search(a->clocks, $3, cmpClockName);
				if (constraint->clock == NULL)
				{
					fprintf(stderr, "Unknown clock %s\n", $3);
					exit(EXIT_FAILURE);
				}

				constraint->rel = $2;
				/* reverse the inequality to have the clock on the left */
				switch (constraint->rel)
				{
					case LT:
						constraint->rel = GT;
					break;
					case LEQ:
						constraint->rel = GEQ;
					break;
					case GEQ:
						constraint->rel = LEQ;
					break;
					case GT:
						constraint->rel = LT;
					break;
					default:
					break;
				}
				constraint->bound = $1;
				$$ = constraint;
				free($3);
			}
;

rel: 	'<' 		{$$ = LT;}
		| LEQTOK	{$$ = LEQ;}
		| '='		{$$ = EQ;}
		| GEQTOK	{$$ = GEQ;}
		| '>'		{$$ = GT;}
;

endautomaton: '}' optends
;

optends:
		| optends '\n'
;

optterms:
		| optterms '\n'
		| optterms ';'
;

term: 	  '\n'
		| ';'
;
%%

/** comparison function to use with list_search **/
static int cmpStateName(void *p1, void *p2)
{
	struct ParserState *s = p2;
	char *name = p1;

	return (strcmp(name, s->name) == 0);
}

static int cmpClockName(void *p1, void *p2)
{
	struct ParserClock *clock = p2;
	char *name = p1;

	return (strcmp(name, clock->name) == 0);
}

static int cmpSymLabel(void *p1, void *p2)
{
	struct ParserSymbol *sym = p2;
	char *label = p1;

	return (strcmp(label, sym->label) == 0);
}

/** ParserState **/
int parserState_isAccepting(const struct ParserState *s)
{
	return ((s->attrs & NODEATTR_ACCEPTING) != 0);
}

int parserState_isInitial(const struct ParserState *s)
{
	return ((s->attrs & NODEATTR_INITIAL) != 0);
}

const char *parserState_getName(const struct ParserState *s)
{
	return s->name;
}

unsigned int parserState_getId(const struct ParserState *s)
{
	return s->id;
}

void parserState_free(void *p)
{
	struct ParserState *ps = p;

	free(ps->name);
	free(ps);
}

/** ParserEdge **/
const struct ParserState *parserEdge_getFrom(const struct ParserEdge *e)
{
	return e->from;
}

const struct ParserState *parserEdge_getTo(const struct ParserEdge *e)
{
	return e->to;
}

const struct ParserSymbol *parserEdge_getSym(const struct ParserEdge *e)
{
	return e->sym;
}

/**
 * @return List<ParserClockConstraint>
 */
const struct List *parserEdge_getConstraints(const struct ParserEdge *e)
{
	return e->constraints;
}

/**
 * @return List<ParserClock>
 */
const struct List *parserEdge_getResets(const struct ParserEdge *e)
{
	return e->resets;
}

void parserEdge_free(void *p)
{
	struct ParserEdge *pe = p;

	list_free(pe->constraints, parserClockConstraint_free);
	list_free(pe->resets, NULL);
	free(pe);
}

/** ParserClock **/
unsigned int parserClock_getId(const struct ParserClock *clock)
{
	return clock->id;
}

const char *parserClock_getName(const struct ParserClock *clock)
{
	return clock->name;
}

void parserClock_free(void *p)
{
	struct ParserClock *pc = p;

	free(pc->name);
	free(p);
}

/** ParserClockConstraint **/
const struct ParserClock *parserClockConstraint_getClock(
	const struct ParserClockConstraint *constraint)
{
	return constraint->clock;
}

int parserClockConstraint_getBound(const struct ParserClockConstraint 
*constraint)
{
	return constraint->bound;
}

enum ParserRelation parserClockConstraint_getRel(
	const struct ParserClockConstraint *constraint)
{
	return constraint->rel;
}

void parserClockConstraint_free(void *p)
{
	struct ParserClockConstraint *pcc = p;

	free(pcc);
}

/** ParserSymbol **/
unsigned int parserSymbol_getId(const struct ParserSymbol *sym)
{
	return sym->id;
}

int parserSymbol_isCont(const struct ParserSymbol *sym)
{
	return sym->isCont;
}

const char *parserSymbol_getLabel(const struct ParserSymbol *sym)
{
	return sym->label;
}

void parserSymbol_free(void *p)
{
	struct ParserSymbol *ps = p;

	free(ps->label);
	free(ps);
}

/** Automaton **/
const struct List *parser_getStates()
{
	return a->states;
}

const struct List *parser_getEdges()
{
	return a->edges;
}

const struct List *parser_getConts()
{
	return a->conts;
}

const struct List *parser_getUnconts()
{
	return a->unconts;
}

const struct List *parser_getClocks()
{
	return a->clocks;
}

int parser_getNbStates()
{
	return a->nbStates;
}

int parser_getNbEdges()
{
	return a->nbEdges;
}

int parser_getNbConts()
{
	return a->nbConts;
}

int parser_getNbUnconts()
{
	return a->nbUnconts;
}

int parser_getNbAcceptings()
{
	return a->nbAcceptings;
}

int parser_getNbInitials()
{
	return a->nbInitials;
}

int parser_getNbClocks()
{
	return a->nbClocks;
}

void parser_cleanup()
{
	list_free(a->states, parserState_free);
	list_free(a->edges, parserEdge_free);
	list_free(a->conts, parserSymbol_free);
	list_free(a->unconts, parserSymbol_free);
	list_free(a->clocks, parserClock_free);
	free(a);
}

