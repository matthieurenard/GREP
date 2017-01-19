#include "set.h"

#include <stdlib.h>
#include <stdio.h>

#define MIN_SET_SIZE	8
#define MAX_ADDED_SIZE	256

struct Set
{
	void **tab;
	unsigned int allocSize;
	unsigned int size;
	int (*cmp)(const void *, const void *);
};

static int eqPtr(const void *ptr1, const void *ptr2)
{
	return (ptr1 == ptr2);
}

struct Set *set_empty(int (*cmp)(const void *, const void *))
{
	struct Set *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc set");
		exit(EXIT_FAILURE);
	}

	ret->tab = malloc(MIN_SET_SIZE * sizeof *(ret->tab));
	if (ret->tab == NULL)
	{
		perror("malloc set tab");
		exit(EXIT_FAILURE);
	}

	ret-> allocSize = MIN_SET_SIZE;
	ret->size = 0;
	ret->cmp = (cmp == NULL) ? eqPtr : cmp;

	return ret;
}

struct Set *set_newCopy(const struct Set *src, void * (*cp)(void *))
{
	struct Set *ret = set_empty(src->cmp);

	return set_copy(ret, src, cp);
}

struct Set *set_copy(struct Set *dst, const struct Set *src, void * (*cp)(void *))
{
	int i;

	if (dst->allocSize < src->allocSize)
	{

		dst->tab = realloc(dst->tab, src->allocSize * sizeof *dst->tab);
		if (dst->tab == NULL)
		{
			perror("realloc set tab");
			exit(EXIT_FAILURE);
		}

		dst->allocSize = src->allocSize;
	}

	dst->size = src->size;

	if (cp == NULL)
	{
		for (i = 0 ; i < src->size ; i++)
		{
			dst->tab[i] = src->tab[i];
		}
	}
	else
	{
		for (i = 0 ; i < dst->size ; i++)
		{
			dst->tab[i] = cp(src->tab[i]);
		}
	}

	return dst;
}


int set_add(struct Set *s, void *data)
{
	if (set_in(s, data))
		return 0;

	if (s->size >= s->allocSize)
	{
		s->allocSize = (s->allocSize * 2 > MAX_ADDED_SIZE) ? s->allocSize + 
			MAX_ADDED_SIZE : s->allocSize * 2;
		s->tab = realloc(s->tab, s->allocSize * sizeof *s->tab);
		if (s->tab == NULL)
		{
			perror("realloc set tab");
			exit(EXIT_FAILURE);
		}
	}
	s->tab[s->size++] = data;

	return 1;
}

unsigned int set_remove(struct Set *s, int (*pred)(const void *, const void *), 
		const void *ref)
{
	int i, j;

	for (i = 0, j = 0 ; i < s->size ; i++, j++)
	{
		s->tab[j] = s->tab[i];
		if (pred(ref, s->tab[i]))
			j--;
	}

	s->size -= (i - j);

	return (i - j);
}

void set_cleanup(struct Set *s, void (*clean)(void *))
{
	int i;

	for (i = 0 ; i < s->size ; i++)
	{
		clean(s->tab[i]);
	}

	set_reset(s);
}

int set_in(const struct Set *s, const void *data)
{
	int i;

	for (i = 0 ; i < s->size ; i++)
	{
		if (s->cmp(data, s->tab[i]))
			return 1;
	}

	return 0;
}

int set_isEmpty(const struct Set *s)
{
	return (s->size == 0);
}

struct Set *set_reset(struct Set *s)
{
	s->size = 0;

	return s;
}

void set_applyToAll(const struct Set *s, void (*f)(void *, void *), void *ref)
{
	int i;

	for (i = 0 ; i < s->size ; i++)
	{
		f(ref, s->tab[i]);
	}
}

int set_allSatisfy(const struct Set *s, int (*pred)(void *))
{
	int i;

	for (i = 0 ; i < s->size ; i++)
	{
		if (!pred(s->tab[i]))
			return 0;
	}

	return 1;
}

void set_free(struct Set *s)
{
	free(s->tab);
	free(s);
}

	

