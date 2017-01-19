#ifndef CLOCK_H
#define CLOCK_H

struct Clock;

#include <dbm/dbm.h>

struct Clock *clock_new(unsigned int, cindex_t, const char *);
unsigned int clock_getId(const struct Clock *);
cindex_t clock_getIndex(const struct Clock *);
const char *clock_getName(const struct Clock *);
void clock_free(struct Clock *);

#endif

