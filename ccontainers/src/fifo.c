#include "fifo.h"

#include <stdlib.h>
#include <stdio.h>

struct Element
{
	void *data;
	struct Element *next;
};

struct Fifo
{
	struct Element *head;
	struct Element *tail;
};

struct Fifo *fifo_empty()
{
	struct Fifo *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc fifo");
		exit(EXIT_FAILURE);
	}

	ret->head = NULL;
	ret->tail = NULL;

	return ret;
}

struct Fifo *fifo_enqueue(struct Fifo *f, void *data)
{
	struct Element *newEl = malloc(sizeof *newEl);

	if (newEl == NULL)
	{
		perror("malloc FIFO:newEl");
		exit(EXIT_FAILURE);
	}

	newEl->data = data;
	newEl->next = NULL;
	if (fifo_isEmpty(f))
		f->head = f->tail = newEl;
	else
	{
		f->tail->next = newEl;
		f->tail = newEl;
	}

	return f;
}

void *fifo_dequeue(struct Fifo *f)
{
	struct Element *el;
	void *data;

	if (fifo_isEmpty(f))
	{
		fprintf(stderr, "Cannot dequeue: empty FIFO\n");
		return NULL;
	}

	el = f->head;
	f->head = el->next;
	if (f->head == NULL)
		f->tail = NULL;

	data = el->data;
	free(el);

	return data;
}

int fifo_isEmpty(const struct Fifo *f)
{
	return (f->head == NULL);
}

int fifo_free(struct Fifo *f)
{
	if (!fifo_isEmpty(f))
	{
		fprintf(stderr, "Not freeing non-empty FIFO\n");
		return 0;
	}
	free(f);
	return 1;
}

