#ifndef SET_H
#define SET_H

struct Set;

struct Set *set_empty(int (*)(const void *, const void *));
struct Set *set_copy(const struct Set *, void *(*)(void *));
int set_add(struct Set *, void *);
/* remove all elements el such that pred(ref, el) holds */
unsigned int set_remove(struct Set *, int (*pred)(const void *, const void *), 
		const void *ref);
void set_cleanup(struct Set *, void (*)(void *));
int set_in(const struct Set *, const void *);
int set_isEmpty(const struct Set *);
struct Set *set_reset(struct Set *);
int set_allSatisfy(const struct Set *, int (*pred)(void *));
void set_free(struct Set *);

#endif

