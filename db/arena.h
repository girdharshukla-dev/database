#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

struct arena_type;

void arena_init(struct arena_type *arena, void *buffer, size_t buffer_length);
void *arena_alloc_align(struct arena_type *arena, size_t alignment, size_t size);
void *arena_alloc(struct arena_type *arena, size_t size);
void arena_reset(struct arena_type *arena);


#endif
