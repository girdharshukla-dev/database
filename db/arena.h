#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>

struct arena_type{
    uint8_t *buffer_base;
    size_t current_offset;
    size_t buffer_length;
};

void arena_init(struct arena_type *arena, void *buffer, size_t buffer_length);
void *arena_alloc_aligned(struct arena_type *arena, size_t size, size_t alignment);
void *arena_alloc(struct arena_type *arena, size_t size);
void arena_reset(struct arena_type *arena);


#endif
