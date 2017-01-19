#include "serialize.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdint.h>

#include <list.h>


 void save_uint64(FILE *f, uint64_t n)
{
	int i;

	for (i = 0 ; i < sizeof n ; i++)
	{
		fprintf(f, "%c", (char)(n >> (i * 8)) & 0xff);
	}
}

 uint64_t load_uint64(FILE *f)
{
	uint64_t n = 0;
	int i;

	for (i = 0 ; i < sizeof n ; i++)
	{
		int c = fgetc(f);
		uint64_t uc = (uint64_t)c;
		if (c == EOF)
		{
			fprintf(stderr, "ERROR: load_uint64: EOF reached while parsing"
					" number\n");
			exit(EXIT_FAILURE);
		}
		uc &= 0xff;
		n |= uc << (i * 8);
	}

	return n;
}

 void save_string(FILE *f, const char *s)
{
	fprintf(f, "%s", s);
	fprintf(f, "%c", '\0');
}

 char *load_string(FILE *f)
{
	int c, i;
	struct List *l;
	struct ListIterator *it;
	char *s, *ret;
	size_t size;

	l = list_new();
	
	i = 0;
	size = 0;
	while ((c = fgetc(f)) != EOF && c != '\0')
	{
		if (i == 0)
		{
			s = malloc(512);
			if (s == NULL)
			{
				perror("malloc load_string:s");
				exit(EXIT_FAILURE);
			}

			list_add(l, s);
		}
		s[i++] = (char)c;
		if (i >= 511)
		{
			s[511] = '\0';
			i = 0;
		}
		size++;
	}
	if (i != 0)
		s[i] = '\0';

	ret = malloc(size + 1);
	if (ret == NULL)
	{
		perror("malloc loadString:ret");
		exit(EXIT_FAILURE);
	}

	ret[0] = '\0';
	for (it = listIterator_first(l) ; listIterator_hasNext(it) ; it = 
			listIterator_next(it))
	{
		s = listIterator_val(it);
		strcat(ret, s);
	}
	listIterator_release(it);

	list_free(l, free);

	return ret;
}

