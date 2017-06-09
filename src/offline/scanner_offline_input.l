%{
	#include <stdio.h>
	#include <stdlib.h>

	#include "parser_offline_input.h"

	static int line = 1;
	static int col = 1;

	/* Avoid compiler warning */
	int yylex_destroy();

	void offlineInputerror(char *s)
	{
		fprintf(stderr, "%s line %d:%d\n", s, line, col);
	}

	int parseOfflineInput(const char *filename)
	{
		FILE *f;
		YY_BUFFER_STATE bs;
		int ret;

		if (filename == NULL)
			f = stdin;
		else
		{
			f = fopen(filename, "r");
			if (f == NULL)
			{
				perror("fopen");
				exit(EXIT_FAILURE);
			}
		}
		
		bs = yy_create_buffer(f, YY_BUF_SIZE);
		yypush_buffer_state(bs);
		ret = offlineInputparse();
		if (f != NULL && f != stdin)
			fclose(f);
		yypop_buffer_state();

		yylex_destroy();

		return ret;
	}
%}

/* Avoid compiler warnings */
%option nounput
%option noinput
%option noyywrap

/* Avoid conflicts with other parser linked with this one */
%option prefix="offlineInput" outfile="lex.yy.c"

NAME	([a-zA-Z_][a-zA-Z_0-9]*)
INTEGER ([0-9]*)

%%
"("			{col++; return '(';}
")"			{col++; return ')';}
","			{col++; return ',';}
"."			{col++; return '.';}
{INTEGER}	{col += strlen(yytext); offlineInputlval.n = atoi(yytext); return
				INTEGER;}
{NAME}		{col += strlen(yytext); offlineInputlval.s = strdup(yytext); return 
				ID;}
[ \t\v\f]	{col++;}
"\n"		{line++; col = 1;}
.			{col++;}

%%

		

