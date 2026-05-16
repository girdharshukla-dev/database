#include "arena.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#define ALIGNMENT 16

struct arena_type{
    uint8_t *buffer_base;
    size_t current_offset;
    size_t buffer_length;
};

void *arena_alloc_aligned(struct arena_type *arena, size_t size, size_t align){
    assert((align & (align - 1)) == 0);

    uintptr_t ptr = (uintptr_t)arena->buffer_base + (uintptr_t)arena->current_offset;
    uintptr_t remainder = ptr & (align - 1);
    if(remainder != 0){
        ptr += (align - remainder);
    }
    ptr -= (uintptr_t)arena->buffer_base;
    if(ptr + size <= arena->buffer_length){
        void *ret_ptr = &arena->buffer_base[ptr];
        arena->current_offset = ptr + size;
        memset(ret_ptr, 0, size);
        return ret_ptr;
    }
    return NULL;
}

void *arena_alloc(struct arena_type *arena, size_t size){
    return arena_alloc_aligned(arena, size, ALIGNMENT);
}

void arena_init(struct arena_type *arena, void *buffer, size_t buffer_length){
    arena->buffer_base = buffer;
    arena->current_offset = 0;
    arena->buffer_length = buffer_length;
}

void arena_reset(struct arena_type *arena){
    arena->current_offset = 0;
}

