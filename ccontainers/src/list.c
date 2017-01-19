#include "list.h"

#include <stdlib.h>
#include <stdio.h>

struct Element
{
	void *data;
	struct Element *next;
};

struct List
{
	struct Element *head;
	struct Element *tail;
	unsigned int size;
};

struct ListIterator
{
	struct Element *next;
};


struct List *list_new()
{
	struct List *l = malloc(sizeof *l);

	if (l == NULL)
	{
		perror("malloc list_new");
		exit(EXIT_FAILURE);
	}

	return list_init(l);
}

void list_free(struct List *l, void (*clean)(void *))
{
	struct Element *e, *next;

	if (l == NULL)
		return;

	e = l->head;
	while (e != NULL)
	{
		next = e->next;
		if (clean != NULL)
			clean(e->data);
		free(e);
		e = next;
	}

	free(l);
}

struct List *list_init(struct List *l)
{
	l->head = NULL;
	l->tail = NULL;
	l->size = 0;
	return l;
}

struct List *list_add(struct List *l, void *data)
{
	struct Element *e = malloc(sizeof *e);

	if (e == NULL)
	{
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	if (l->head == NULL)
		l->head = e;
	else
		l->tail->next = e;

	l->tail = e;

	e->next = NULL;
	e->data = data;

	l->size++;

	return l;
}

struct List *list_remove(struct List *l, void *data)
{
	struct Element *e, *prev;

	if (list_isEmpty(l))
		return l;

	e = l->head;
	prev = e;
	if (e->data == data)
	{
		e = e->next;
		free(prev);
		l->head = e;
		if (e == NULL)
			l->tail = e;

		l->size--;

		return l;
	}

	for ( ; e != NULL ; prev = e, e = e->next)
	{
		if (e->data == data)
		{
			prev->next = e->next;
			if (l->tail == e)
				l->tail = prev;
			free(e);
			l->size--;
		}
	}

	return l;
}

int list_isEmpty(const struct List *l)
{
	return l->head == NULL;
}

unsigned int list_size(const struct List *l)
{
	return l->size;
}

void *list_search(const struct List *l, void *val, int (*eq)(void *, void *))
{
	struct Element *e;

	if (list_isEmpty(l))
		return NULL;

	for (e = l->head ; e != NULL ; e = e->next)
	{
		if (eq(val, e->data))
			return e->data;
	}

	return NULL;
}

void list_cleanup(struct List *l, void (*release)(void *))
{
	struct Element *e, *p;

	if (l == NULL)
		return;

	e = l->head;
	for (p = e ; e != NULL ; p = e)
	{
		e = e->next;
		release(p->data);
		free(p);
	}
	l->head = NULL;
	l->tail = NULL;
	l->size = 0;
}

struct ListIterator *listIterator_first(const struct List *l)
{
	struct ListIterator *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	ret->next = l->head;

	return ret;
}

int listIterator_hasNext(const struct ListIterator *it)
{
	return (it->next != NULL);
}

int listIterator_isLast(const struct ListIterator *it)
{
	return (it->next != NULL && it->next->next == NULL);
}

struct ListIterator *listIterator_next(struct ListIterator *it)
{
	it->next = it->next->next;
	return it;
}

void *listIterator_val(const struct ListIterator *it)
{
	return it->next->data;
}

void listIterator_release(struct ListIterator *it)
{
	free(it);
}

