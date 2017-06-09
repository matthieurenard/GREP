#ifndef STACK_H
#define STACK_H

struct Stack;

struct Stack *stack_empty();
struct Stack *stack_push(struct Stack *, void *);
void *stack_pop(struct Stack *);
void *stack_top(const struct Stack *);
void stack_free(struct Stack *);
int stack_isEmpty(const struct Stack *);

#endif

