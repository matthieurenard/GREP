#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>
#include "list.h"

struct ParserState;
struct ParserEdge;
struct ParserAutomaton;
struct ParserClockConstraint;
struct ParserClock;
struct ParserSymbol;
enum ParserRelation {LT, LEQ, EQ, GEQ, GT, NONE};

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

#endif
