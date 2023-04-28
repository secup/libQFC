#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    void *buffer;
    void *buffer_end;
    size_t capacity;
    size_t count;
    size_t size;
    void *head;
    void *tail;
} ringBuffer_t;

void rb_init(ringBuffer_t *cb, size_t capacity, size_t size)
{
    cb->buffer = malloc(capacity * size);
    cb->buffer_end = (char *)cb->buffer + capacity * size;
    cb->capacity = capacity;
    cb->count = 0;
    cb->size = size;
    cb->head = cb->buffer;
    cb->tail = cb->buffer;
}

void rb_free(ringBuffer_t *cb)
{
    free(cb->buffer);
}

void rb_push(ringBuffer_t *cb, const void *item)
{
    
    memcpy(cb->head, item, cb->size);
    cb->head = (char*)cb->head + cb->size;
    if(cb->head == cb->buffer_end)
        cb->head = cb->buffer;
    cb->count++;
}

void rb_pop(ringBuffer_t *cb, void *item)
{
    
    memcpy(item, cb->tail, cb->size);
    cb->tail = (char*)cb->tail + cb->size;
    if(cb->tail == cb->buffer_end)
        cb->tail = cb->buffer;
    cb->count--;
}