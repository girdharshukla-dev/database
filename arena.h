#ifndef ARENA_H
#define ARENA_H

#include <stdint.h>

struct arena{
  uint8_t *base;
  uint64_t current_position;
  uint64_t capacity;
};

struct arena *arena_create(uint64_t capacity);
void arena_destroy(struct arena *arena);
void *arena_alloc(struct arena *a, uint64_t size);
void arena_reset(struct arena *a);


#endif