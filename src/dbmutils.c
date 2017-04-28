#include "dbmutils.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <dbm/constraints.h>
#include <dbm/dbm.h>

#include <list.h>
#include <fifo.h>

#include "clock.h"

extern struct Clock **clocks;

struct Dbmw
{
	raw_t *dbm;
	cindex_t dim;
};

struct Dbmw *dbmw_new(cindex_t dim)
{
	struct Dbmw *ret = malloc(sizeof *ret);
	
	if (ret == NULL)
	{
		perror("malloc dbmw");
		exit(EXIT_FAILURE);
	}

	ret->dim = dim;
	ret->dbm = malloc(dim * dim * sizeof *(ret->dbm));
	if (ret->dbm == NULL)
	{
		perror("malloc dbm");
		exit(EXIT_FAILURE);
	}

	dbm_init(ret->dbm, ret->dim);

	return ret;
}

struct Dbmw *dbmw_newcp(const struct Dbmw *src)
{
	struct Dbmw *new = dbmw_new(src->dim);
	dbm_copy(new->dbm, src->dbm, new->dim);

	return new;
}

struct Dbmw *dbmw_copy(struct Dbmw *dst, const struct Dbmw *src)
{
	dbm_copy(dst->dbm, src->dbm, dst->dim);

	return dst;
}


struct Dbmw *dbmw_zero(struct Dbmw *d)
{
	dbm_zero(d->dbm, d->dim);

	return d;
}

struct Dbmw *dbmw_init(struct Dbmw *d)
{
	dbm_init(d->dbm, d->dim);

	return d;
}

struct Dbmw *dbmw_up(struct Dbmw *d)
{
	dbm_up(d->dbm, d->dim);
	return d;
}

struct Dbmw *dbmw_down(struct Dbmw *d)
{
	dbm_down(d->dbm, d->dim);
	return d;
}

int dbmw_intersection(struct Dbmw *d1, const struct Dbmw *d2)
{
	return dbm_intersection(d1->dbm, d2->dbm, d1->dim);
}

int dbmw_isSubsetEq(const struct Dbmw *d1, const struct Dbmw *d2)
{
	return dbm_isSubsetEq(d1->dbm, d2->dbm, d1->dim);
}

int dbmw_isSupersetEq(const struct Dbmw *d1, const struct Dbmw *d2)
{
	return dbm_isSupersetEq(d1->dbm, d2->dbm, d1->dim);
}

int dbmw_intersects(const struct Dbmw *dbm1, const struct Dbmw *dbm2)
{
	struct Dbmw *cp = dbmw_newcp(dbm1);
	int ret = dbmw_intersection(cp, dbm2);

	dbmw_free(cp);

	return ret;
}

int dbmw_areEqual(const struct Dbmw *dbm1, const struct Dbmw *dbm2)
{
	return (dbm1->dim == dbm2->dim && dbm_areEqual(dbm1->dbm, dbm2->dbm, 
				dbm1->dim));
}

int dbmw_isEmpty(const struct Dbmw *dbm)
{
	return dbm_isEmpty(dbm->dbm, dbm->dim);
}

int dbmw_containsZero(const struct Dbmw *dbm)
{
	return dbm_hasZero(dbm->dbm, dbm->dim);
}

int dbmw_isPointIncluded(const struct Dbmw *dbm, const int32_t *val)
{
	return dbm_isPointIncluded(val, dbm->dbm, dbm->dim);
}

struct Dbmw *dbmw_reset(struct Dbmw *d, struct Clock *c)
{
	dbm_updateValue(d->dbm, d->dim, clock_getIndex(c), 0);

	return d;
}

struct Dbmw *dbmw_constrainClock(struct Dbmw *d, struct Clock *c, int32_t val)
{
	dbm_constrainClock(d->dbm, d->dim, clock_getIndex(c), val);

	return d;
}

struct Dbmw *dbmw_constrain(struct Dbmw *d, struct Clock *c1, struct Clock *c2, 
		int32_t bound, int strict)
{
	dbm_constrain1(d->dbm, d->dim, (c1 == NULL) ? 0 : clock_getIndex(c1), (c2 == 
				NULL) ? 0 : clock_getIndex(c2), dbm_boundbool2raw(bound, 
					strict));

	return d;
}

struct Dbmw *dbmw_freeClock(struct Dbmw *dbm, const struct Clock *c)
{
	dbm_freeClock(dbm->dbm, dbm->dim, clock_getIndex(c));
	return dbm;
}

struct Dbmw *dbmw_updateIncrementAll(struct Dbmw *d, unsigned int delay)
{
	unsigned int i;

	for (i = 1 ; i < d->dim ; i++)
		dbm_updateIncrement(d->dbm, d->dim, i, delay);

	return d;
}

/** Assume that from is a point (i.e. valuation of the clocks, reading the first 
 * line (or column) gives the values of the clocks), and that dst is reachable 
 * from from (i.e. up(from)/\dst is not empty) */
unsigned int dbmw_nextPoint(struct Dbmw *dst, const struct Dbmw *from)
{
	unsigned int max = 0;
	int i;

	if (from->dim == 0)
		return 0;

	for (i = 1 ; i < from->dim ; i++)
	{
		/* Both values are negatives, since it is 0 - x <= -bound to signal that 
		 * x >= bound */
		int d = dbm_raw2bound(from->dbm[i]) - dbm_raw2bound(dst->dbm[i]);
		if (dbm_rawIsStrict(from->dbm[i]))
			d++;
		if (d < 0)
		{
			/* this should not happen if from is a point and dst intersects 
			 * up(from) */
			fprintf(stderr, "ERROR: computing next delay on wrong DBMs");
			max = 0;
			break;
		}
		if (d > max)
			max = d;
	}
	dbmw_updateIncrementAll(dst, max);

	return max;
}

/* Compute the distance of a valuation v to a zone, i.e. the smallest delay d 
 * such that v + d is in the zone .
 * Requires that up(v) /\ zone is not empty (thus d exists) */
unsigned int dbmw_distance(const int32_t *val, const struct Dbmw *to)
{
	int i;
	int max = 0;

	for (i = 1 ; i < to->dim ; i++)
	{
		/* take the opposite of to[i], since it is <= -c to say x >= c */
		int d = -dbm_raw2bound(to->dbm[i]) - val[i];
		if (dbm_rawIsStrict(to->dbm[i]))
			d++;
		if (val[i] > dbm_raw2bound(to->dbm[i * to->dim]))
		{
			fprintf(stderr, "ERROR: %d > %d\n", val[i], 
					dbm_raw2bound(to->dbm[i] * to->dim));
			return -1;
		}
		max = (d > max) ? d : max;
	}

	return (unsigned int)max;
}

/* Reduce the number of elements that are bounded, i.e. give a DBM that 
 * represents the same zone, but may be not closed, and has a minimal number of 
 * bounds */
static void dbmw_reduce(struct Dbmw *z)
{
	struct Dbmw *tmp = dbmw_newcp(z);
	struct Dbmw *res = dbmw_newcp(z);
	int i, j, n;

	n = z->dim;

	for (i = 0 ; i < n ; i++)
	{
		for (j = 0 ; j < n ; j++)
		{
			tmp->dbm[i * n + j] = dbm_LS_INFINITY;
			dbm_close(tmp->dbm, n);
			if (dbmw_areEqual(tmp, z))
				res->dbm[i * n + j] = dbm_LS_INFINITY;
			else
				dbmw_copy(tmp, res);
		}
	}		

	dbmw_copy(z, res);

	dbmw_free(tmp);
	dbmw_free(res);
}

#if 0
static struct Dbmw *dbmw_merge(const struct Dbmw *z1, const struct Dbmw *z2)
{
	int mergeable = 1;
	int m, n;
}
#endif

/**
 * Compute the subtraction of two zones. Since zones need to be convex, 
 * complementing a zone may not give a zone. Thus, the result of this 
 * subtraction is a partition of the subtraction into multiple zones.
 * @param Z, Z' zones
 * @ret a list of zones whose union is Z \ Z'
 */
struct List *dbmw_subtract(const struct Dbmw *Z0, const struct Dbmw *Z1)
{
	struct Dbmw *Ztmp = NULL, *Zinter = NULL, *Zremain;
	int i, j, n;
	struct List *ret = list_new();

	if (Z0->dim != Z1->dim)
	{
		fprintf(stderr, "ERROR: trying to subtract two DBMs of different " 
				"dimensions.\n");
		exit(EXIT_FAILURE);
	}

	Zinter = dbmw_newcp(Z0);
	dbmw_intersection(Zinter, Z1);
	dbmw_reduce(Zinter);
	Zremain = dbmw_newcp(Z0);

	n = Z0->dim;
	Ztmp = dbmw_new(n);
	
	for (i = 0 ; i < n ; i++)
	{
		for (j = 0 ; j < n ; j++)
		{
			constraint_t constraint, negConstraint;

			if (i == j)
				continue;

			dbmw_init(Ztmp);

			constraint = dbm_constraint2(i, j, dbm_raw2bound(Zinter->dbm[i * n + 
						j]), dbm_rawIsStrict(Zinter->dbm[i * n + j]));
			negConstraint = dbm_negConstraint(constraint);

			if (dbm_constrainC(Ztmp->dbm, Ztmp->dim, negConstraint) &&
					dbmw_intersection(Ztmp, Zremain))
			{
				list_append(ret, Ztmp);
				Ztmp = dbmw_new(n);
				if (dbm_constrainC(Ztmp->dbm, Ztmp->dim, constraint))
					dbmw_intersection(Zremain, Ztmp);
			}
		}
	}

	dbmw_free(Ztmp);

	return ret;
}

/**
 * Compute Z |_| Z', i.e. {Z /\ Z'} U (Z \ Z') U (Z' \ Z)
 * Like dbmw_partition, but only for two zones. It is used in dbmw_partition.
 * @param Z, Z' two zones
 * @ret list of zones, representing the partition of Z U Z' such that for all 
 * element Zi of the partition, Zi C Z or Zi /\ Z = 0, and the same goes with 
 * Z'.
 */
struct List *dbmw_partition2(const struct Dbmw *Z0, const struct Dbmw *Z1)
{
	struct Dbmw *dbm = dbmw_newcp(Z0);
	struct List *ret = list_new();
	struct List *l;
	struct ListIterator *it;

	if (dbmw_intersection(dbm, Z1))
		list_append(ret, dbm);
	else
	{
		dbmw_free(dbm);
		list_append(ret, dbmw_newcp(Z0));
		list_append(ret, dbmw_newcp(Z1));
		return ret;
	}

	l = dbmw_subtract(Z0, Z1);
	for (it = listIterator_first(l) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		dbm = listIterator_val(it);
		list_append(ret, dbm);
	}
	listIterator_release(it);
	list_free(l, NULL);

	l = dbmw_subtract(Z1, Z0);
	for (it = listIterator_first(l) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		dbm = listIterator_val(it);
		list_append(ret, dbm);
	}
	listIterator_release(it);

	list_free(l, NULL);

	return ret;
}

/**
 * Compute a partition (Z'i) of a given set of zones (Zi) such that for all i, 
 * j, Z'i C Zj or Z'i /\ Zj = 0.
 * @param list of zones (Zi)
 * @ret list of zones (Z'i)
 */
struct List *dbmw_partition(const struct List *zones)
{
	struct ListIterator *it;
	int i, changed;
	struct Fifo *wait = fifo_empty();
	struct List *ret = list_new();

	for (it = listIterator_first(zones), i = 0 ; listIterator_hasNext(it) ; it = 
			listIterator_next(it), i++)
	{
		struct Dbmw *dbm = listIterator_val(it);
		if (i == 0)
			list_append(ret, dbmw_newcp(dbm));
		else
			fifo_enqueue(wait, dbmw_newcp(dbm));
	}
	listIterator_release(it);

	while (!fifo_isEmpty(wait))
	{
		struct Dbmw *Z = fifo_dequeue(wait);
		struct Dbmw *Zi;

		//fprintf(stderr, "Z = %s\n", dbmw_sprint(Z, clocks));

		changed = 0;

		for (it = listIterator_first(ret) ; !changed && listIterator_hasNext(it) 
				; it = listIterator_next(it))
		{
			Zi = listIterator_val(it);
			if (dbmw_intersects(Z, Zi))
				changed = 1;
		}
		listIterator_release(it);

		if (changed && dbmw_areEqual(Z, Zi))
			continue;

		if (!changed && list_search(ret, Z, (int (*)(const void *, const void 
							*))dbmw_areEqual) == NULL)
			list_append(ret, Z);
		else
		{
			struct List *ZUZi = dbmw_partition2(Z, Zi);
			for (it = listIterator_first(ZUZi) ; listIterator_hasNext(it) ; it = 
					listIterator_next(it))
			{
				struct Dbmw *dbm = listIterator_val(it);
				fifo_enqueue(wait, dbm);
			}
			listIterator_release(it);
			list_free(ZUZi, NULL);
			
			list_remove(ret, Zi);
		}
	}

	fifo_free(wait);

	return ret;
}


/**
 * Compute Z |^| Z', i.e. the set of x in Z such that there exists d in R such 
 * that x + d is in Z' and for all d' <= d, x + d' in Z U Z'.
 * This is (?) equivalent to computing down(up(Z) /\ Z') /\ Z.
 * NOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO
 * This does not work (there can be some "hole" between the zones).
 * Relax all constraints and consider down(Z /\ Z') ??
 * @param Z, Z' zones
 * @ret zone down(up(Z) /\ Z') /\ Z
 */
struct Dbmw *dbmw_upTo(const struct Dbmw *Z0, const struct Dbmw *Z1)
{
	struct Dbmw *z0 = dbmw_newcp(Z0), *z1 = dbmw_newcp(Z1);
	struct Dbmw *ret = NULL;
	struct ListIterator *it;

	dbmw_up(z0);
	dbmw_down(z1);

	if (dbmw_intersection(z0, z1))
	{
		struct List *l = dbmw_subtract(z0, Z1);
		int ok = 1;
		for (it = listIterator_first(l) ; listIterator_hasNext(it) ; it = 
				listIterator_next(it))
		{
			struct Dbmw *z = listIterator_val(it);

			if (!dbmw_isSubsetEq(z, Z0))
				ok = 0;
		}
		listIterator_release(it);

		if (ok)
		{
			ret = dbmw_newcp(Z0);
			if (!dbmw_intersection(ret, z1))
			{
				dbmw_free(ret);
				ret = NULL;
			}
		}

		list_free(l, (void (*)(void *))dbmw_free);
	}

	dbmw_free(z0);
	dbmw_free(z1);
	
	return ret;
}

void dbmw_print(FILE *f, const struct Dbmw *d, struct Clock **const clocks)
{
	int i, j;
	int first = 1;

	if (dbm_isEqualToInit(d->dbm, d->dim))
	{
		fprintf(f, "T");
		return;
	}

	fprintf(f, "{");

	for (i = 1 ; i < d->dim ; i++)
	{
		raw_t constraint;
		int32_t bound;

		constraint = d->dbm[i]; // d[0,i]
		bound = dbm_raw2bound(constraint);

		if (bound != 0)
		{
			if (!first)
				fprintf(f, ",");
			else
				first = 0;

			fprintf(f, "%s ", clock_getName(clocks[i]));
			fprintf(f, ">%s ", dbm_rawIsWeak(constraint) ? "=" : "");
			fprintf(f, "%d", -bound);
		}
		constraint = d->dbm[i * d->dim]; // d[i,0]
		bound = dbm_raw2bound(constraint);

		if (bound != dbm_INFINITY)
		{
			if (!first)
				fprintf(f, ",");
			else
				first = 0;

			fprintf(f, "%s ", clock_getName(clocks[i]));
			fprintf(f, "<%s ", dbm_rawIsWeak(constraint) ? "=" : "");
			fprintf(f, "%d", bound);
		}

		for (j = i + 1 ; j < d->dim ; j++)
		{
			constraint = d->dbm[i * d->dim + j]; // d[i,j]
			bound = dbm_raw2bound(constraint);

			if (bound != dbm_INFINITY)
			{
				int li = (bound < 0) ? j : i;
				int ri = (bound < 0) ? i : j;
				if (!first)
					fprintf(f, ",");
				else
					first = 0;

				fprintf(f, "%s - %s ", clock_getName(clocks[li]), 
						clock_getName(clocks[ri]));
				fprintf(f, "%s%s ", (bound < 0) ? ">" : "<", 
						dbm_rawIsWeak(constraint) ? "=" : "");
				fprintf(f, "%d", (bound < 0) ? -bound : bound);
			}

			constraint = d->dbm[j * d->dim + i]; // d[j,i]
			bound = dbm_raw2bound(constraint);

			if (bound != dbm_INFINITY)
			{
				int li = (bound < 0) ? i : j;
				int ri = (bound < 0) ? j : i;
				if (!first)
					fprintf(f, ",");
				else
					first = 0;

				fprintf(f, "%s - %s ", clock_getName(clocks[li]), 
						clock_getName(clocks[ri]));
				fprintf(f, "%s%s ", (bound < 0) ? ">" : "<", 
						dbm_rawIsWeak(constraint) ? "=" : "");
				fprintf(f, "%d", (bound < 0) ? -bound : bound);
			}
		}
	}
	fprintf(f, "}");
}

char *dbmw_sprint(const struct Dbmw *d, struct Clock **const clocks)
{
	int i, j;
	int first = 1;
	char *s0;
	char *s;

	if (dbm_isEqualToInit(d->dbm, d->dim))
	{
		return strdup("T");
	}

	s0 = malloc(512);
	if (s0 == NULL)
	{
		perror("malloc dbmw_sprint:s0");
		exit(EXIT_FAILURE);
	}

	s = s0;
	for (i = 0 ; i < 512 ; i++)
		s[i] = '\0';

	s += sprintf(s, "{");

	for (i = 1 ; i < d->dim ; i++)
	{
		raw_t constraint;
		int32_t bound;

		constraint = d->dbm[i]; // d[0,i]
		bound = dbm_raw2bound(constraint);

		if (bound != 0)
		{
			if (!first)
				s += sprintf(s, ",");
			else
				first = 0;

			s += sprintf(s, "%s ", clock_getName(clocks[i]));
			s += sprintf(s, ">%s ", dbm_rawIsWeak(constraint) ? "=" : "");
			if (bound == -dbm_INFINITY)
				s += sprintf(s, "%s", "Inf");
			else
				s += sprintf(s, "%d", -bound);
		}
		constraint = d->dbm[i * d->dim]; // d[i,0]
		bound = dbm_raw2bound(constraint);

		if (bound != dbm_INFINITY)
		{
			if (!first)
				s += sprintf(s, ",");
			else
				first = 0;

			s += sprintf(s, "%s ", clock_getName(clocks[i]));
			s += sprintf(s, "<%s ", dbm_rawIsWeak(constraint) ? "=" : "");
			s += sprintf(s, "%d", bound);
		}

		for (j = i + 1 ; j < d->dim ; j++)
		{
			constraint = d->dbm[i * d->dim + j]; // d[i,j]
			bound = dbm_raw2bound(constraint);

			if (bound != dbm_INFINITY)
			{
				int li = (bound < 0) ? j : i;
				int ri = (bound < 0) ? i : j;
				if (!first)
					s += sprintf(s, ",");
				else
					first = 0;

				s += sprintf(s, "%s - %s ", clock_getName(clocks[li]), 
						clock_getName(clocks[ri]));
				s += sprintf(s, "%s%s ", (bound < 0) ? ">" : "<", 
						dbm_rawIsWeak(constraint) ? "=" : "");
				s += sprintf(s, "%d", (bound < 0) ? -bound : bound);
			}

			constraint = d->dbm[j * d->dim + i]; // d[j,i]
			bound = dbm_raw2bound(constraint);

			if (bound != dbm_INFINITY)
			{
				int li = (bound < 0) ? i : j;
				int ri = (bound < 0) ? j : i;
				if (!first)
					s += sprintf(s, ",");
				else
					first = 0;

				s += sprintf(s, "%s - %s ", clock_getName(clocks[li]), 
						clock_getName(clocks[ri]));
				s += sprintf(s, "%s%s ", (bound < 0) ? ">" : "<", 
						dbm_rawIsWeak(constraint) ? "=" : "");
				s += sprintf(s, "%d", (bound < 0) ? -bound : bound);
			}
		}
	}
	s += sprintf(s, "}");

	return s0;
}

static void save_uint32(FILE *f, uint32_t n)
{
	int i;

	for (i = 0 ; i < sizeof n ; i++)
	{
		fprintf(f, "%c", (char)(n >> (i * 8)) & 0xff);
	}
}

static uint32_t load_uint32(FILE *f)
{
	uint32_t n = 0;
	int i;

	for (i = 0 ; i < sizeof n ; i++)
	{
		uint32_t c = fgetc(f);
		if ((int32_t)c == EOF)
		{
			fprintf(stderr, "ERROR: EOF reached while parsing uint32\n");
			exit(EXIT_FAILURE);
		}
		c &= 0xff;
		n |= c << (i * 8);
	}

	return n;
}

void dbmw_save(const struct Dbmw *z, FILE *f)
{
	int i, j;

	save_uint32(f, (uint32_t)z->dim);
	for (i = 0 ; i < z->dim ; i++)
	{
		for (j = 0 ; j < z->dim ; j++)
		{
			save_uint32(f, (uint32_t)(dbm_rawIsStrict(z->dbm[i * z->dim + j]) != 
						0));
			save_uint32(f, (uint32_t)(dbm_raw2bound(z->dbm[i * z->dim + j])));
		}
	}
}

struct Dbmw *dbmw_load(FILE *f)
{
	int i, j;
	uint32_t dim, strict;
	int32_t bound;
	struct Dbmw *ret;

	dim = load_uint32(f);
	ret = dbmw_new(dim);
	
	for (i = 0 ; i < dim ; i++)
	{
		for (j = 0 ; j < dim ; j++)
		{
			strict = load_uint32(f);
			bound = (int32_t)load_uint32(f);
			ret->dbm[i * dim + j] = dbm_boundbool2raw(bound, strict);
		}
	}

	return ret;
}

void dbmw_free(struct Dbmw *d)
{
	free(d->dbm);
	free(d);
}

