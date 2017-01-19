#include <stdio.h>
#include <stdlib.h>

#include <getopt.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "game_graph.h"
#include "print_game.h"
#include "parser_offline_input.h"

#define BUFFER_SIZE		256


struct Args
{
	FILE *drawFile;
	FILE *drawZoneFile;
	FILE *logFile;
	char *automatonFile;
	unsigned int pollingTime;
};

struct InputEvent
{
	unsigned int date;
	char *label;
};


void print_usage(FILE *out, char *progName)
{
	fprintf(out, "Usage : %s [options] <filename>\n", progName);
	fprintf(out, "where <filename> is an automaton file.\n");
	fprintf(out, "List of possible options:\n"
			"-d, --drawgraph=FILE    print the game graph in FILE\n"
			"-l, --log-file=FILE     use FILE as log file\n"
			"-p, --polling=TIME      update the enforcer every TIME ms\n"
		   );
}

void timeval_cp(struct timeval *dst, const struct timeval *src)
{
	dst->tv_sec = src->tv_sec;
	dst->tv_usec = src->tv_usec;
}

unsigned int timeval_ms(const struct timeval *tv)
{
	return tv->tv_sec * 1000 + tv->tv_usec / 1000;
}

int timeval_delay(const struct timeval *tv1, const struct timeval *tv2)
{
	return timeval_ms(tv1) - timeval_ms(tv2);
}

void initArgs(struct Args *args)
{
	args->drawFile = NULL;
	args->drawZoneFile = NULL;
	args->automatonFile = NULL;
	args->pollingTime = 0;

	args->logFile = stderr;
}

int parseArgs(int argc, char *argv[], struct Args *args)
{
	int optionIndex;
	char c;
	struct option longOptions[] = 
	{
		{"draw-graph", required_argument, NULL, 'd'},
		{"draw-zone-graph", required_argument, NULL, 'z'},
		{"log-file", required_argument, NULL, 'l'},
		{"polling", required_argument, NULL, 'p'},
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "d:z:l:", longOptions, &optionIndex)) != 
			-1)
	{
		if (c == 0)
			c = longOptions[optionIndex].val;
		switch (c)
		{

			case 'd':
				args->drawFile = fopen(optarg, "w");
				if (args->drawFile == NULL)
				{
					perror("fopen");
					fprintf(stderr, "Impossible to open %s, graph will not be " 
							"drawn.\n", optarg);
				}
			break;

			case 'z':
				args->drawZoneFile = fopen(optarg, "w");
				if (args->drawZoneFile == NULL)
				{
					perror("fopen");
					fprintf(stderr, "Impossible to open %s, zone graph will not" 
						" be drawn.\n", optarg);
				}
			break;	

			case 'l':
				args->logFile = fopen(optarg, "a");
				if (args->logFile == NULL)
				{
					perror("fopen");
					fprintf(stderr, "Impossible to open %s, log will be "
							"redirected to stderr.\n", optarg);
				}
			break;

			case 'p':
				args->pollingTime = atoi(optarg);

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

	args->automatonFile = argv[optind];

	return 0;
}

struct InputEvent *inputEvent_new(unsigned int date, const char *name)
{
	struct InputEvent *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc inputEvent_new:ret");
		exit(EXIT_FAILURE);
	}

	ret->date = date;
	ret->label = strdup(name);

	return ret;
}

void inputEvent_free(struct InputEvent *e)
{
	free(e->label);
	free(e);
}

static struct Fifo *readEvents()
{
	struct Fifo *ret = fifo_empty();

	parseOfflineInput(NULL);

	while (parserOfflineInput_hasNext())
	{
		struct ParserOfflineInputEvent *pe = parserOfflineInput_getNext();
		struct InputEvent *e = 
			inputEvent_new(parserOfflineInputEvent_getDelay(pe), 
					parserOfflineInputEvent_getName(pe));
		fifo_enqueue(ret, e);
	}

	parserOfflineInput_cleanup();

	return ret;
}

int main(int argc, char *argv[])
{
	struct Args args;
	struct Graph *g;
	struct Enforcer *e;
	fd_set rfds;
	int i, quit = 0, pending = 0;
	unsigned int nextDelay = 0, nextDate = 0, date = 0;
	struct Fifo *events;
	struct InputEvent *event = NULL;

	initArgs(&args);
	parseArgs(argc, argv, &args);

	g = graph_newFromAutomaton(args.automatonFile);
	if (args.drawFile != NULL)
	{
		drawGraph(g, args.drawFile);
		fclose(args.drawFile);
	}
	if (args.drawZoneFile != NULL)
	{
		drawZoneGraph(graph_getZoneGraph(g), args.drawZoneFile);
		fclose(args.drawZoneFile);
	}

	e = enforcer_new(g, args.logFile);

	events = readEvents();

	if (!fifo_isEmpty(events))
	{
		event = fifo_dequeue(events);
		nextDelay = event->date + 1;
		nextDate = event->date + 1;
	}
	else
		nextDelay = 0;
	while (event != NULL || nextDate > date)
	{
		struct Event enfEvent;

		/* Read next event */
		if (event != NULL && (event->date < nextDate || nextDelay == 0))
		{
			enforcer_delay(e, event->date - date);
			date = event->date;
			enfEvent.label = event->label;
			nextDelay = enforcer_eventRcvd(e, &enfEvent);
			inputEvent_free(event);
			if (!fifo_isEmpty(events))
				event = fifo_dequeue(events);
			else
				event = NULL;
			nextDate = date + nextDelay;
		}
		/* Change zone */
		else if (nextDelay > 0)
		{
			nextDelay = enforcer_delay(e, nextDate - date);
			date = nextDate;
			nextDate = date + nextDelay;
		}
		else
		{
			fprintf(stderr, "ERROR: not progressing (event = (%u, %s), date =" 
				" %u, nextDate = %u\n", event->date, event->label, date, 
				nextDate);
			exit(EXIT_FAILURE);
			nextDelay = 0;
		}

		while (enforcer_getStrat(e) == STRAT_EMIT)
		{
			nextDelay = enforcer_emit(e);
			nextDate = date + nextDelay;
		}
	}

	fprintf(stderr, "%u %u\n", date, nextDate);

	enforcer_free(e);
	graph_free(g);

	if (args.logFile != stderr)
		fclose(args.logFile);

	return EXIT_SUCCESS;
}

