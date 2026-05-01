#include "arena.h"

#include <stdlib.h>

struct arena *arena_create(uint64_t capacity) {
  struct arena *a = malloc(sizeof(struct arena));
  if(!a){return NULL;}
  a->base = malloc(capacity);
  if (!a->base) {
    free(a);
    return NULL;
  }
  a->current_position = 0;
  a->capacity = capacity;
  return a;
}

void arena_destroy(struct arena *a) {
  free(a->base);
  free(a);
}

void *arena_alloc(struct arena *a, uint64_t size) {
  uint64_t aligned_position = ((a->current_position + 7) / 8) * 8;
  if(aligned_position > a->capacity - size){
    return NULL;
  }
  void *ptr = a->base + aligned_position;
  a->current_position = aligned_position + size;
  return ptr;
}

void arena_reset(struct arena *a) { a->current_position = 0; }
