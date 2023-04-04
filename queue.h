#ifndef __QUEUE__
#define __QUEUE__

#define QUEUE_SIZE 1000
struct Queue;

extern struct Queue* newQueue(int capacity);

extern int enqueue(struct Queue *q, char *value);

extern char* dequeue(struct Queue *q);

extern void freeQueue(struct Queue *q);

extern int get_sizeQueue(struct Queue *q);
#endif