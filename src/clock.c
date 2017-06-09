#include "clock.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <dbm/dbm.h>

struct Clock
{
	unsigned int id;
	cindex_t index;
	unsigned int val;
	char *name;
};

struct Clock *clock_new(unsigned int id, cindex_t index, const char *name)
{
	struct Clock *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc clock");
		exit(EXIT_FAILURE);
	}

	ret->id = id;
	ret->index = index;
	ret->val = 0;
	ret->name = strdup(name);

	return ret;
}

unsigned int clock_getId(const struct Clock *c)
{
	return c->id;
}

cindex_t clock_getIndex(const struct Clock *c)
{
	return c->index;
}

const char *clock_getName(const struct Clock *c)
{
	return c->name;
}

void clock_free(struct Clock *c)
{
	free(c->name);
	free(c);
}

