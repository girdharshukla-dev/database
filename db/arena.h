#ifndef ARENA_H
#define ARENA_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARENA_ALIGNMENT 16

struct arena_type {
  uint8_t *buffer_base;
  size_t current_offset;
  size_t buffer_length;
};

static inline void *arena_alloc_aligned(struct arena_type *arena, size_t size,
                                        size_t align) {
  if((align & (align - 1)) != 0){
    return NULL;
  }

  uintptr_t ptr =
      (uintptr_t)arena->buffer_base + (uintptr_t)arena->current_offset;
  uintptr_t remainder = ptr & (align - 1);
  if (remainder != 0) {
    ptr += (align - remainder);
  }
  ptr -= (uintptr_t)arena->buffer_base;
  if (ptr + size <= arena->buffer_length) {
    void *ret_ptr = &arena->buffer_base[ptr];
    arena->current_offset = ptr + size;
    memset(ret_ptr, 0, size);
    return ret_ptr;
  }
  return NULL;
}

static inline void *arena_alloc(struct arena_type *arena, size_t size) {
  return arena_alloc_aligned(arena, size, ARENA_ALIGNMENT);
}

static inline void arena_init(struct arena_type *arena, void *buffer,
                              size_t buffer_length) {
  arena->buffer_base = (uint8_t *)buffer;
  arena->current_offset = 0;
  arena->buffer_length = buffer_length;
}

static inline void arena_reset(struct arena_type *arena) {
  arena->current_offset = 0;
}

#endif
