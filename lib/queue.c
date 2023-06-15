#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "queue.h"

struct Node {
    char *value;
    struct Node *next;
};

struct Queue {
    int size;
    int max_size;
    struct Node *head;
    struct Node *tail;
};

struct Queue* newQueue(int capacity)
{
    struct Queue *q;
    q = malloc(sizeof(struct Queue));
    if (q == NULL) {
        return q;
    }

    q->size = 0;
    q->max_size = capacity;
    q->head = NULL;
    q->tail = NULL;

    return q;
}

int enqueue(struct Queue *q, char *value)
{
    if ((q->size + 1) > q->max_size) // kiem tra kich thuoc con trong hay ko
    {
        return q->size;
    }

    struct Node *node = malloc(sizeof(struct Node));
    node->value = malloc(strlen(value)+1);
    if (node == NULL)  // kiem tra viec khoi tao node co thanh cong hay ko
    {
        return q->size;
    }
    // node->value = value;
    strcpy(node->value,value);
    node->next = NULL;

    if (q->head == NULL) // neu head trong
    {
        q->head = node;
        q->tail = node;
        q->size = 1;
        return q->size;
    }

    q->tail->next = node;
    q->tail = node;
    q->size += 1;
    return q->size;
}

char* dequeue(struct Queue *q)
{
    if (q->size == 0) 
    {
        return NULL;
    }
    struct Node *node = q->head;
    q->head = q->head->next;
    q->size -= 1;
    char *value = malloc(strlen(node->value) + 1);
    strcpy(value, node->value);
    free(node->value);
    free(node);
    return value;
}

void freeQueue(struct Queue *q)
{
    if (q == NULL) {
        return;
    }

    while (q->head != NULL) 
    {
        struct Node *tmp = q->head;
        q->head = q->head->next;
        if (tmp->value != NULL) {
            free(tmp->value);
        }
        free(tmp);
    }

    if (q->tail != NULL) {
        free(q->tail);
    }
    free (q);
}

int get_sizeQueue(struct Queue *q)
{
    return q->size;
}