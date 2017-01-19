#include "dbmutils.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <dbm/constraints.h>
#include <dbm/dbm.h>

#include <list.h>
#include <fifo.h>

#include "clock.h"


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

struct Dbmw *dbmw_newcp(struct Dbmw *src)
{
	struct Dbmw *new = dbmw_new(src->dim);
	dbm_copy(new->dbm, src->dbm, new->dim);

	return new;
}

struct Dbmw *dbmw_copy(struct Dbmw *dst, struct Dbmw *src)
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

int dbmw_isSubseteq(const struct Dbmw *d1, const struct Dbmw *d2)
{
	return dbm_isSubsetEq(d1->dbm, d2->dbm, d1->dim);
}

int dbmw_intersects(const struct Dbmw *dbm1, const struct Dbmw *dbm2)
{
	struct Dbmw *cp = dbmw_newcp(dbm1);
	int ret = dbmw_intersection(cp, dbm2);

	dbmw_free(cp);

	return ret;
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
		int32_t bound, raw_t strict)
{
	dbm_constrain1(d->dbm, d->dim, (c1 == NULL) ? 0 : clock_getIndex(c1), (c2 == 
				NULL) ? 0 : clock_getIndex(c2), dbm_boundbool2raw(bound, 
					strict));

	return d;
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

/**
 * Compute the subtraction of two zones. Since zones need to be convex, 
 * complementing a zone may not give a zone. Thus, the result of this 
 * subtraction is a partition of the subtraction into multiple zones.
 * @param Z, Z' zones
 * @ret a list of zones whose union is Z \ Z'
 */
struct List *dbmw_subtract(const struct Dbmw *Z0, const struct Dbmw *Z1)
{
	struct Dbmw *Ztmp, *Zprev = NULL;
	int i, j, n;
	struct List *ret = list_new();

	if (Z0->dim != Z1->dim)
	{
		fprintf(stderr, "ERROR: trying to subtract two DBMs of different " 
				"dimensions.\n");
		exit(EXIT_FAILURE);
	}

	n = Z0->dim;
	Ztmp = dbmw_new(n);
	
	for (i = 0 ; i < n ; i++)
	{
		for (j = 0 ; j < n ; j++)
		{
			constraint_t constraint;
			if (i == j)
				continue;
			dbmw_init(Ztmp);

			constraint = dbm_constraint2(i, j, dbm_raw2bound(Z1->dbm[i][j]), 
					dbm_rawIsStrict(Z1->dbm[i][j]));
			constraint = dbm_negConstraint(constraint);
			dbm_constrainC(Ztmp->dbm, Ztmp->dim, constraint);
			dbmw_intersection(Ztmp, Z0);

			if (Zprev != NULL && dbm_isValid()) /* TODO */
			{
			}
		}
	}
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

	list_add(ret, dbmw_intersection(dbm, Z1));
	l = dbmw_subtract(Z0, Z1);
	for (it = listIterator_first(l) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		dbm = listIterator_val(it);
		list_add(ret, dbm);
	}
	listIterator_release(it);
	list_free(l, NULL);

	l = dbmw_subtract(Z1, Z0);
	for (it = listIterator_first(l) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		dbm = listIterator_val(it);
		list_add(ret, dbm);
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
struct List *dbmw_partition(const struct List *Zi)
{
	struct ListIterator *it;
	int i, changed;
	struct Fifo *wait = fifo_empty();
	struct List *ret = list_new();

	for (it = listIterator_first(Zi), i = 0 ; listIterator_hasNext(it) ; it = 
			listIterator_next(it), i++)
	{
		struct Dbmw *dbm = listIterator_val(it);
		if (i == 0)
			list_add(ret, dbmw_newcp(dbm));
		else
			fifo_enqueue(wait, dbmw_newcp(dbm));
	}
	listIterator_release(it);

	while (!fifo_isEmpty(wait))
	{
		struct Dbmw *Z = fifo_dequeue(wait);
		struct Dbmw *Zi;

		changed = 0;

		for (it = listIterator_first(ret) ; !changed && listIterator_hasNext(it) 
				; it = listIterator_next(it))
		{
			Zi = listIterator_val(it);
			if (dbmw_intersects(Z, Zi))
				changed = 1;
		}
		listIterator_release(it);

		if (!changed)
			list_add(ret, Z);
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

void dbmw_print(FILE *f, const struct Dbmw *d, struct Clock **const clocks)
{
	int i, j;
	int first = 1;

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
				if (!first)
					fprintf(f, ",");
				else
					first = 0;

				fprintf(f, "%s - %s ", clock_getName(clocks[j]), clock_getName(clocks[i]));
				fprintf(f, ">%s ", dbm_rawIsWeak(constraint) ? "=" : "");
				fprintf(f, "%d", -bound);
			}

			constraint = d->dbm[j * d->dim + i]; // d[j,i]
			bound = dbm_raw2bound(constraint);

			if (bound != dbm_INFINITY)
			{
				if (!first)
					fprintf(f, ",");
				else
					first = 0;

				fprintf(f, "%s - %s ", clock_getName(clocks[j]), clock_getName(clocks[i]));
				fprintf(f, "<%s ", dbm_rawIsWeak(constraint) ? "=" : "");
				fprintf(f, "%d", bound);
			}
		}
	}
	fprintf(f, "}");
}
		
void dbmw_free(struct Dbmw *d)
{
	free(d->dbm);
	free(d);
}

