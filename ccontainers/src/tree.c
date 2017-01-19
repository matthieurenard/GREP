#include "tree.h"

#include <stdlib.h>
#include <stdio.h>

#define MIN_ALLOCSIZE	2
#define MAX_ADDSIZE		256

struct Tree
{
	int allocSize;
	unsigned int size;
	void *data;
	struct Tree *parent;
	struct Tree **sons;
};

struct Tree *tree_new(void *data)
{
	int i;
	struct Tree *ret = malloc(sizeof *ret);

	if (ret == NULL)
	{
		perror("malloc tree");
		exit(EXIT_FAILURE);
	}
	ret->allocSize = MIN_ALLOCSIZE;
	ret->size = 0;
	ret->data = data;
	ret->parent = NULL;
	ret->sons = malloc(ret->allocSize * sizeof *(ret->sons));
	if (ret->sons == NULL)
	{
		perror("malloc ret->sons");
		exit(EXIT_FAILURE);
	}

	for (i = 0 ; i < ret->allocSize ; i++)
		ret->sons[i] = NULL;

	return ret;
}

void tree_free(struct Tree *t, void (*cleanData)(void *))
{
	free(t->sons);
	if (cleanData != NULL)
	{
		cleanData(t->data);
	}
	free(t);
}

struct Tree *tree_addSon(struct Tree *t, struct Tree *son)
{
	if (t->size >= t->allocSize)
	{
		int i;
		int addSize = (t->allocSize >= MAX_ADDSIZE) ? MAX_ADDSIZE : t->allocSize;
		t->allocSize += addSize;
		t->sons = realloc(t->sons, t->allocSize * sizeof *(t->sons));
		for (i = t->size ; i < t->allocSize ; i++)
			t->sons[i] = NULL;
	}

	t->sons[t->size++] = son;
	son->parent = t;

	return t;
}

struct Tree *tree_removeSon(struct Tree *t, unsigned int i)
{
	int j;
	struct Tree *ret;

	if (i >= 0 && i < t->size)
	{
		ret = t->sons[i];
		ret->parent = NULL;
		for (j = i ; j < t->size - 1 ; j++)
		{
			t->sons[j] = t->sons[j+1];
		}
		t->size--;
	}
	else
		ret = NULL;

	return ret;
}

void *tree_getData(const struct Tree *t)
{
	return t->data;
}

struct Tree *tree_setData(struct Tree *t, void *data)
{
	t->data = data;
	return t;
}

unsigned int tree_getNbSons(const struct Tree *t)
{
	return t->size;
}

struct Tree *tree_getSon(const struct Tree *t, unsigned int i)
{
	if (i >= 0 && i < t->size)
		return t->sons[i];

	fprintf(stderr, "ERROR: tree has less than %d sons\n", i + 1);
	return NULL;
}

int tree_isRoot(const struct Tree *t)
{
	return (t->parent == NULL);
}


