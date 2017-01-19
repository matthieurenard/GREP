#include "stack.h"

#include <stdlib.h>
#include <stdio.h>

struct Element
{
	void *data;
	struct Element *prev;
};

struct Stack
{
	struct Element *head;
};

struct Stack *stack_empty()
{
	struct Stack *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc stack");
		exit(EXIT_FAILURE);
	}

	ret->head = NULL;

	return ret;
}

struct Stack *stack_push(struct Stack *s, void *data)
{
	struct Element *e = malloc(sizeof *e);

	if (e == NULL)
	{
		perror("malloc stack:element");
		exit(EXIT_FAILURE);
	}

	e->data = data;
	e->prev = s->head;
	s->head = e;

	return s;
}

void *stack_pop(struct Stack *s)
{
	void *data;
	struct Element *e;

	if (stack_isEmpty(s))
	{
		fprintf(stderr, "Error: cannot pop from empty stack\n");
		return NULL;
	}

	e = s->head;
	data = e->data;
	s->head = e->prev;

	free(e);

	return data;
}

void *stack_top(const struct Stack *s)
{
	if (stack_isEmpty(s))
	{
		fprintf(stderr, "Warning: asking top of empty stack\n");
		return NULL;
	}
	return s->head->data;
}

void stack_free(struct Stack *s)
{
	if (!stack_isEmpty(s))
	{
		fprintf(stderr, "Warning: freeing non-empty stack\n");
		exit(EXIT_FAILURE);
	}

	free(s);
}

int stack_isEmpty(const struct Stack *s)
{
	return (s->head == NULL);
}	

