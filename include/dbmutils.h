#ifndef DBM_UTILS_H
#define DBM_UTILS_H

struct Dbmw;


#include <stdint.h>
#include <stdio.h>
#include <dbm/dbm.h>
#include "clock.h"

struct Dbmw *dbmw_new(cindex_t dim);
struct Dbmw *dbmw_newcp(const struct Dbmw *);
struct Dbmw *dbmw_copy(struct Dbmw *, const struct Dbmw *);

struct Dbmw *dbmw_zero(struct Dbmw *);
struct Dbmw *dbmw_up(struct Dbmw *);
struct Dbmw *dbmw_down(struct Dbmw *);
struct Dbmw *dbmw_freeClock(struct Dbmw *, const struct Clock *);

int dbmw_intersection(struct Dbmw *, const struct Dbmw *);
int dbmw_isSubsetEq(const struct Dbmw *, const struct Dbmw *);
int dbmw_isSupersetEq(const struct Dbmw *, const struct Dbmw *);
int dbmw_intersects(const struct Dbmw *, const struct Dbmw *);
int dbmw_areEqual(const struct Dbmw *, const struct Dbmw *);
int dbmw_isEmpty(const struct Dbmw *);
int dbmw_containsZero(const struct Dbmw *);
int dbmw_isPointIncluded(const int32_t *, const struct Dbmw *);

struct Dbmw *dbmw_reset(struct Dbmw *, struct Clock *);
struct Dbmw *dbmw_constrainClock(struct Dbmw *, struct Clock *, int32_t);
struct Dbmw *dbmw_constrain(struct Dbmw *, struct Clock *, struct Clock *, 
		int32_t, int);

struct Dbmw *dbmw_updateIncrementAll(struct Dbmw *, unsigned int);
unsigned int dbmw_nextPoint(struct Dbmw *, const struct Dbmw *);

struct List *dbmw_partition(const struct List *);
struct Dbmw *dbmw_upTo(const struct Dbmw *, const struct Dbmw *);

void dbmw_print(FILE *, const struct Dbmw *, struct Clock ** const);
char *dbmw_sprint(const struct Dbmw *, struct Clock ** const);

void dbmw_free(struct Dbmw *);

/* For debugging, should be static */
struct List *dbmw_subtract(const struct Dbmw *, const struct Dbmw *);

#endif

