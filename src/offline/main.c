#include <stdio.h>
#include <stdlib.h>

#include <getopt.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "game_graph.h"
#include "print_game.h"
#include "parser_offline_input.h"

#define BUFFER_SIZE		256
#define NTIMES			2048
#define CLOCK       	CLOCK_PROCESS_CPUTIME_ID


enum FileType {AUTOMATON_FILE, GRAPH_FILE, NONE_FILE};

struct Args
{
	FILE *drawFile;
	FILE *drawZoneFile;
	FILE *logFile;
	FILE *timeFile;
	char *saveFilename;
	enum FileType fileType;
	char *filename;
	enum EnforcerMode mode;
};

struct InputEvent
{
	unsigned int date;
	char *label;
};


void print_usage(FILE *out, char *progName)
{
	fprintf(out, "Usage : %s [options] (-a <automatonFile> | -g <graphFile>)\n", 
			progName);
	fprintf(out, "where <automatonFile> is an automaton file, or <graphFile> is" 
			" a graph file (see -s option).\n");
	fprintf(out, "List of possible options:\n"
			"-d, --drawgraph=FILE    print the game graph in FILE\n"
			"-l, --log-file=FILE     use FILE as log file\n"
			"-s, --save-graph=FILE   save the graph to FILE (use it with -g)\n"
			"-t, --time-file=FILE    save times to FILE\n"
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
	args->saveFilename = NULL;
	args->timeFile = NULL;
	args->fileType = NONE_FILE;
	args->filename = NULL;
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
		{"automaton-file", required_argument, NULL, 'a'},
		{"graph-file", required_argument, NULL, 'g'},
		{"save-graph", required_argument, NULL, 's'},
		{"time-file", required_argument, NULL, 't'},
		{"fast", no_argument, NULL, 'f'},
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "d:z:l:a:g:s:t:f", longOptions, &optionIndex)) != 
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
					fprintf(stderr, "Cannot open %s, graph will not be " 
							"drawn.\n", optarg);
				}
			break;

			case 'z':
				args->drawZoneFile = fopen(optarg, "w");
				if (args->drawZoneFile == NULL)
				{
					perror("fopen");
					fprintf(stderr, "Cannot open %s, zone graph will not" 
						" be drawn.\n", optarg);
				}
			break;	

			case 'l':
				args->logFile = fopen(optarg, "a");
				if (args->logFile == NULL)
				{
					perror("fopen");
					fprintf(stderr, "Cannot open %s, log will be "
							"redirected to stderr.\n", optarg);
				}
			break;

			case 'a':
			case 'g':
				if (c == 'a')
					args->fileType = AUTOMATON_FILE;
				else
					args->fileType = GRAPH_FILE;
				args->filename = optarg;
			break;

			case 's':
				args->saveFilename = optarg;
			break;

			case 't':
				args->timeFile = fopen(optarg, "a");
				if (args->timeFile == NULL)
				{
					perror("fopen");
					fprintf(stderr, "Cannot open %s, times will not be" 
							" saved.\n", optarg);
				}
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

	if (args->fileType == NONE_FILE)
	{
		print_usage(stderr, argv[0]);
		exit(EXIT_FAILURE);
	}

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
	unsigned int nextDelay = 0, nextDate = 0, date = 0;
	struct Fifo *events;
	struct InputEvent *event = NULL;
	long unsigned int time = 0;
	struct timespec precTime, currentTime;
	int isFirstEvent;

	initArgs(&args);
	parseArgs(argc, argv, &args);

	if (args.fileType == AUTOMATON_FILE)
		g = graph_newFromAutomaton(args.filename);
	else if (args.fileType == GRAPH_FILE)
		g = graph_load(args.filename);
	else
	{
		fprintf(stderr, "No graph given. Aborting...\n");
		return EXIT_FAILURE;
	}

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
	if (args.saveFilename != NULL)
	{
		graph_save(g, args.saveFilename);
	}

	e = enforcer_new(g, args.logFile, args.mode);

	events = readEvents();

	if (!fifo_isEmpty(events))
	{
		event = fifo_dequeue(events);
		nextDelay = event->date + 1;
		nextDate = event->date + 1;
	}
	else
		nextDelay = 0;
	time = 0;
	isFirstEvent = 1;
	while (event != NULL || nextDate > date)
	{
		struct Event enfEvent;
		long int secs, nsecs;

		/* Read next event */
		if (event != NULL && (event->date <= nextDate || nextDelay == 0))
		{
			if (args.timeFile != NULL)
			{
				if (!isFirstEvent)
				{
					fprintf(args.timeFile, "%lu ", time);
					time = 0;
				}
				else
					isFirstEvent = 0;
				if (clock_gettime(CLOCK, &precTime) == -1)
				{
					perror("clock_gettime prec delay");
					exit(EXIT_FAILURE);
				}
			}

			if (event->date != date)
				enforcer_delay(e, event->date - date);
			if (args.timeFile != NULL)
			{
				if (clock_gettime(CLOCK, &currentTime) == -1)
				{
					perror("clock_gettime current delay");
					exit(EXIT_FAILURE);
				}
				secs = currentTime.tv_sec - precTime.tv_sec - (precTime.tv_nsec 
						> currentTime.tv_nsec);
				nsecs = currentTime.tv_nsec + ((precTime.tv_nsec > 
							currentTime.tv_nsec) * 1000000000 - 
						precTime.tv_nsec);
				//fprintf(args.timeFile, "delay: %lu, ", secs * 1000000000 + 
				//nsecs);
				time += secs * 1000000000 + nsecs;
			}

			date = event->date;
			enfEvent.label = event->label;
			
			if (args.timeFile != NULL)
			{
				if (clock_gettime(CLOCK, &precTime) == -1)
				{
					perror("clock_gettime prec eventRcvd");
					exit(EXIT_FAILURE);
				}
			}
			
			nextDelay = enforcer_eventRcvd(e, &enfEvent);

			if (args.timeFile != NULL)
			{
				if (clock_gettime(CLOCK, &currentTime) == -1)
				{
					perror("clock_gettime current eventRcvd");
					exit(EXIT_FAILURE);
				}
				secs = currentTime.tv_sec - precTime.tv_sec - (precTime.tv_nsec 
						> currentTime.tv_nsec);
				nsecs = currentTime.tv_nsec + ((precTime.tv_nsec > 
							currentTime.tv_nsec) * 1000000000 - 
						precTime.tv_nsec);
				//fprintf(args.timeFile, "eventRcvd: %lu, ", secs * 1000000000 + nsecs);
				time += secs * 1000000000 + nsecs;
			}
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
			if (args.timeFile != NULL && clock_gettime(CLOCK, &precTime) == -1)
			{
				perror("clock_gettime prec 2");
				exit(EXIT_FAILURE);
			}

			nextDelay = enforcer_delay(e, nextDate - date);

			if (args.timeFile != NULL)
			{
				if (clock_gettime(CLOCK, &currentTime) == -1)
				{
					perror("clock_gettime current 2");
					exit(EXIT_FAILURE);
				}
				secs = currentTime.tv_sec - precTime.tv_sec - (precTime.tv_nsec > 
						currentTime.tv_nsec);
				nsecs = currentTime.tv_nsec + ((precTime.tv_nsec > 
							currentTime.tv_nsec) * 1000000000 - precTime.tv_nsec);
				//fprintf(args.timeFile, "delay: %lu, ", secs * 1000000000 + 
				//nsecs);
				time += secs * 1000000000 + nsecs;
			}

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

		if (args.timeFile != NULL && clock_gettime(CLOCK, &precTime) == -1)
		{
			perror("clock_gettime prec 2");
			exit(EXIT_FAILURE);
		}
		while (enforcer_getStrat(e) == STRAT_EMIT)
		{
			nextDelay = enforcer_emit(e);
			nextDate = date + nextDelay;
		}
		if (args.timeFile != NULL)
		{
			if (clock_gettime(CLOCK, &currentTime) == -1)
			{
				perror("clock_gettime current 2");
				exit(EXIT_FAILURE);
			}
			secs = currentTime.tv_sec - precTime.tv_sec - (precTime.tv_nsec > 
					currentTime.tv_nsec);
			nsecs = currentTime.tv_nsec + ((precTime.tv_nsec > 
						currentTime.tv_nsec) * 1000000000 - precTime.tv_nsec);
			//fprintf(args.timeFile, "getStrat: %lu, ", secs * 1000000000 + nsecs);

			time += secs * 1000000000 + nsecs;
			/*
			if (nTimes >= NTIMES)
			{
				int j;

				for (j = 0 ; j < nTimes ; j++)
				{
					fprintf(args.timeFile, "%lu\n", times[j]);
				}
				nTimes = 0;
			}
			*/
		}
	}

	/*
	if (args.timeFile != NULL)
	{
		int j;

		for (j = 0 ; j < nTimes ; j++)
		{
			fprintf(args.timeFile, "%lu\n", times[j]);
		}
	}
	*/

	enforcer_free(e);
	graph_free(g);
	fifo_free(events);

	if (args.logFile != stderr)
		fclose(args.logFile);
	if (args.timeFile != NULL)
	{
		fprintf(args.timeFile, "%lu\n", time);
		fclose(args.timeFile);
	}

	return EXIT_SUCCESS;
}

