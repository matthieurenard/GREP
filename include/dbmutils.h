#ifndef DBM_UTILS_H
#define DBM_UTILS_H

struct Dbmw;


#include <stdint.h>
#include <stdio.h>
#include "clock.h"

struct Dbmw *dbmw_new(cindex_t dim);
struct Dbmw *dbmw_newcp(struct Dbmw *);
struct Dbmw *dbmw_copy(struct Dbmw *, struct Dbmw *);

struct Dbmw *dbmw_zero(struct Dbmw *);
struct Dbmw *dbmw_up(struct Dbmw *);
struct Dbmw *dbmw_down(struct Dbmw *);

int dbmw_intersection(struct Dbmw *, const struct Dbmw *);
int dbmw_isSubseteq(const struct Dbmw *, const struct Dbmw *);
int dbmw_intersects(const struct Dbmw *, const struct Dbmw *);

struct Dbmw *dbmw_reset(struct Dbmw *, struct Clock *);
struct Dbmw *dbmw_constrainClock(struct Dbmw *, struct Clock *, int32_t);
struct Dbmw *dbmw_constrain(struct Dbmw *, struct Clock *, struct Clock *, 
		int32_t, int);

struct Dbmw *dbmw_updateIncrementAll(struct Dbmw *, unsigned int);
unsigned int dbmw_nextPoint(struct Dbmw *, const struct Dbmw *);

void dbmw_print(FILE *, const struct Dbmw *, struct Clock ** const);

void dbmw_free(struct Dbmw *);

#endif

