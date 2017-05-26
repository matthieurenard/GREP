#include <stdio.h>
#include <stdlib.h>

#include <getopt.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "game_graph.h"
#include "print_game.h"

#define BUFFER_SIZE		256


struct Args
{
	FILE *drawFile;
	FILE *drawZoneFile;
	FILE *logFile;
	char *automatonFile;
	unsigned int pollingTime;
	enum EnforcerMode mode;
};


void print_usage(FILE *out, char *progName)
{
	fprintf(out, "Usage : %s [options] <filename>\n", progName);
	fprintf(out, "where <filename> is an automaton file.\n");
	fprintf(out, "List of possible options:\n"
			"-d, --drawgraph=FILE    print the game graph in FILE\n"
			"-l, --log-file=FILE     use FILE as log file\n"
			"-p, --polling=TIME      update the enforcer every TIME ms\n"
			"-f, --fast              use fast mode\n"
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
	args->mode = ENFORCERMODE_DEFAULT;

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
		{"fast", no_argument, NULL, 'f'},
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "d:z:l:p:f", longOptions, &optionIndex)) != 
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
			break;

			case 'f':
				args->mode = ENFORCERMODE_FAST;
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

	args->automatonFile = argv[optind];

	return 0;
}

int main(int argc, char *argv[])
{
	struct Args args;
	struct Graph *g;
	struct Enforcer *e;
	char buffer[BUFFER_SIZE];
	struct timeval currentTime, previousTime, nextTime;
	fd_set rfds;
	int i, quit = 0, pending = 0;
	unsigned int nextDelay = 1;

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

	e = enforcer_new(g, args.logFile, args.mode);

	if (gettimeofday(&previousTime, NULL) == -1)
	{
		perror("gettimeofday");
		exit(EXIT_FAILURE);
	}

	while (!quit)
	{
		int ready = 0;
		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);

		if (nextDelay != 0)
		{
			if (gettimeofday(&currentTime, NULL) == -1)
			{
				perror("gettimeofday");
				exit(EXIT_FAILURE);
			}

			if (nextDelay > timeval_delay(&currentTime, &previousTime))
				nextDelay -= timeval_delay(&currentTime, &previousTime);
			else
				nextDelay = 0;

			nextTime.tv_sec = nextDelay / 1000;
			nextTime.tv_usec = (nextDelay % 1000) * 1000;

			fprintf(stderr, " nextDelay = %u\n", nextDelay);
			fprintf(stderr, "Waiting %ld seconds...\n", nextTime.tv_sec * 1000 + 
					nextTime.tv_usec / 1000);
			ready = select(1, &rfds, NULL, NULL, &nextTime);
			gettimeofday(&nextTime, NULL);
			fprintf(stderr, "Waited %u ms\n", timeval_delay(&nextTime, &currentTime));
		}
		else
			ready = select(1, &rfds, NULL, NULL, NULL);
		/* There is some new event */
		if (ready >= 1)
		{
			int n;
			if (FD_ISSET(STDIN_FILENO, &rfds))
			{
				n = read(STDIN_FILENO, buffer + pending, BUFFER_SIZE - 1 - 
						pending);
				if (n == 0)
					quit = 1;
				else if (n == -1)
				{
					perror("read");
					exit(EXIT_FAILURE);
				}
				else
				{
					char *sym = buffer;
					n += pending;
					buffer[n] = '\0';
					pending = 0;
					do
					{
						while (*sym == ' ' || *sym == '\n')
							sym++;
						i = 0;
						while (sym[i] != '\0' && sym[i] != ' ' && sym[i] != 
								'\n')
							i++;

						if (sym[i] == '\0')
						{
							strcpy(buffer, sym);
							pending = i;
						}
						else
						{
							unsigned int delay;
							struct Event event;
							if (gettimeofday(&currentTime, NULL) == -1)
							{
								perror("gettimeofday");
								exit(EXIT_FAILURE);
							}
							delay = timeval_delay(&currentTime, &previousTime);
							nextDelay = enforcer_delay(e, delay);
							sym[i] = '\0';
							event.label = sym;
							nextDelay = enforcer_eventRcvd(e, &event);
							while (enforcer_getStrat(e) == STRAT_EMIT)
								nextDelay = enforcer_emit(e);
							timeval_cp(&previousTime, &currentTime);
						}
						sym += ++i;
					} while (sym <= buffer + n);
				}
			}
		}
		else
		{
			int delay;
			if (gettimeofday(&currentTime, NULL) == -1)
			{
				perror("gettimeofday");
				exit(EXIT_FAILURE);
			}
		   	delay = timeval_delay(&currentTime, &previousTime);
			nextDelay = enforcer_delay(e, delay);
			while (enforcer_getStrat(e) == STRAT_EMIT)
				nextDelay = enforcer_emit(e);
			timeval_cp(&previousTime, &currentTime);
		}
	}


	enforcer_free(e);
	graph_free(g);

	if (args.logFile != stderr)
		fclose(args.logFile);

	return EXIT_SUCCESS;
}

