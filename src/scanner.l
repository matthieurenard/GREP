%{
	#include <stdio.h>
	#include <string.h>

	#include "parser.h"

	static int line = 1;
	static int col = 1;
	static int precCol = 1;

	/* Avoid compiler warnings */
	int yylex_destroy();

	void yyerror(char *s)
	{
		fprintf(stderr, "%s line %d:%d\n", s, line, col);
	}

	int parseFile(const char *filename)
	{
		FILE *f = stdin;
		YY_BUFFER_STATE bs;
		int ret;

		if (filename != NULL)
		{
			f = fopen(filename, "r");
			if (f == NULL)
			{
				perror("fopen");
				exit(EXIT_FAILURE);
			}
		}
		//yydebug = 1;

		bs = yy_create_buffer(f, YY_BUF_SIZE);
		yypush_buffer_state(bs);
		ret = yyparse();
		if (f != NULL && f != stdin)
			fclose(f);
		yypop_buffer_state();

		yylex_destroy();

		return ret;
	}

%}

/* Avoid compiler warnings about unused variable/function */
%option noinput
%option nounput

%x CSTYLE_COMMENT
%x CPPSTYLE_COMMENT

NAME		([a-zA-Z_][a-zA-Z_0-9]*)
INTEGER		([0-9]*)

%%
"/*" 					{BEGIN(CSTYLE_COMMENT);}
<CSTYLE_COMMENT>"*/" 	{BEGIN(INITIAL);}
<CSTYLE_COMMENT>"\n"	{line++; col = 1;}
<CSTYLE_COMMENT>.		;

"//"					{BEGIN(CPPSTYLE_COMMENT);}
<CPPSTYLE_COMMENT>"\n"	{BEGIN(INITIAL); line++; col = 1; return '\n';}
<CPPSTYLE_COMMENT>.		;

"automaton" {precCol = col; col += strlen("automaton"); return AUTOMATON;}
"nodes"		{precCol = col; col += strlen("nodes"); return NODE;}
"accepting"	{precCol = col; col += strlen("accepting"); return ACCEPTING;}
"initial"	{precCol = col; col += strlen("initial"); return INITIALTOK;}
"cont"		{precCol = col; col += strlen("cont"); return CONT;}
"uncont"	{precCol = col; col += strlen("uncont"); return UNCONT;}
"clocks"	{precCol = col; col += strlen("clocks"); return CLOCKS;}
"edges"		{precCol = col; col += strlen("edges"); return EDGES;}
"["			{precCol = col; col += strlen("["); return '[';}
"]"			{precCol = col; col += strlen("]"); return ']';}
","			{precCol = col; col += strlen(","); return ',';}
";"			{precCol = col; col += strlen(";"); return ';';}
"{"			{precCol = col; col += strlen("{"); return '{';}
"}"			{precCol = col; col += strlen("}"); return '}';}
"<"			{precCol = col; col += strlen("<"); return '<';}
">"			{precCol = col; col += strlen(">"); return '>';}
"="			{precCol = col; col += strlen("="); return '=';}
"<="		{precCol = col; col += strlen("<="); return LEQTOK;}
">="		{precCol = col; col += strlen(">="); return GEQTOK;}
"->"		{precCol = col; col += strlen("->"); return EDGE;}
{NAME}		{yylval.s = strdup(yytext); precCol = col; col += strlen(yytext); 
				return ID;}
{INTEGER}	{yylval.n = atoi(yytext); precCol = col; col += strlen(yytext); 
				return INTEGER;}
[ \t\v\f]	{precCol = ++col;}
"\n"		{line++; precCol = col = 1; return '\n';}
.			{precCol = ++col;}
%%



