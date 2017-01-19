%code
{
	#include <stdlib.h>
	#include <stdio.h>

	#include "parser_offline_input.h"

	static struct Fifo *fifo;
	static struct Fifo *read;
}

%code requires
{
	#include "fifo.h"

	struct ParserOfflineInputEvent
	{
		unsigned int date;
		char *name;
	};

	/* Avoid compiler warnings */
	int yylex();
	void yyerror(char *);
}

%code provides
{
	struct ParserOfflineInputEvent *parserOfflineInput_getNext();
	int  parserOfflineInput_hasNext();
	void parserOfflineInput_cleanup();

	unsigned int parserOfflineInputEvent_getDelay(const struct ParserOfflineInputEvent*);
	const char *parserOfflineInputEvent_getName(const struct ParserOfflineInputEvent*);

	/* In lex file */
	int parseOfflineInput(const char *filename);
}

/* Avoid name conflicts with other parsers linked with this one */
%define api.prefix {offlineInput}

%union
{
	unsigned int n;
	char *s;
	struct Fifo *fifo;
	struct ParserOfflineInputEvent *event;
}

%token INTEGER ID

%type<s> ID
%type<n> INTEGER
%type<fifo> events
%type<event> event

%%
input: events				{fifo = $1; read = fifo_empty();}
;

events:						{$$ = fifo_empty();}
	  | events event		{$$ = fifo_enqueue($1, $2);}
	  | events '.' event	{$$ = fifo_enqueue($1, $3);}
;

event: '(' INTEGER sep ID ')'
	 {
		struct ParserOfflineInputEvent *e = malloc(sizeof *e);
		if (e == NULL)
		{
			perror("malloc");
			exit(EXIT_FAILURE);
		}
		e->date = $2;
		e->name = $4;
		$$ = e;
	}
;

sep:
   | ','
;

%%

struct ParserOfflineInputEvent *parserOfflineInput_getNext()
{
	struct ParserOfflineInputEvent *e = fifo_dequeue(fifo);
	fifo_enqueue(read, e);
	return e;
}

int parserOfflineInput_hasNext()
{
	return !fifo_isEmpty(fifo);
}

unsigned int parserOfflineInputEvent_getDelay(const struct 
ParserOfflineInputEvent *e)
{
	return e->date;
}

const char *parserOfflineInputEvent_getName(const struct ParserOfflineInputEvent 
*e)
{
	return e->name;
}

void parserOfflineInput_cleanup()
{
	while (!fifo_isEmpty(fifo))
	{
		struct ParserOfflineInputEvent *e = fifo_dequeue(fifo);
		free(e->name);
		free(e);
	}
	fifo_free(fifo);

	while (!fifo_isEmpty(read))
	{
		struct ParserOfflineInputEvent *e = fifo_dequeue(read);
		free(e->name);
		free(e);
	}
	fifo_free(read);
}
