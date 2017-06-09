#ifndef FIFO_H
#define FIFO_H

struct Fifo;

struct Fifo *fifo_empty();
struct Fifo *fifo_enqueue(struct Fifo *, void *);
void *fifo_dequeue(struct Fifo *);
int fifo_isEmpty(const struct Fifo *);
int fifo_free(struct Fifo *);


#endif 

