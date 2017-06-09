#ifndef CCONTAINERS_TREE_H
#define CCONTAINERS_TREE_H

struct Tree;


struct Tree *tree_new(void *);
void tree_free(struct Tree *, void (*cleanData)(void *));

void *tree_getData(const struct Tree *);
struct Tree *tree_setData(struct Tree *, void *);
struct Tree *tree_getParent(const struct Tree *);
unsigned int tree_getNbSons(const struct Tree *);
struct Tree *tree_getSon(const struct Tree *, unsigned int);
struct Tree *tree_addSon(struct Tree *, struct Tree *);
struct Tree *tree_removeSon(struct Tree *, unsigned int);
int tree_isRoot(const struct Tree *);

#endif

