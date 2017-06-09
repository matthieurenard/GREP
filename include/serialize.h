#ifndef SERIALIZE_H
#define SERIALIZE_H

#include <stdio.h>
#include <stdint.h>

/* Helper functions to save/load (with serialization) */
void save_uint64(FILE *, uint64_t);
uint64_t load_uint64(FILE *);
void save_string(FILE *, const char *);
char *load_string(FILE *);

#endif

