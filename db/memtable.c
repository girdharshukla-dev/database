#include "memtable.h"
#include "skiplist.h"
#include "arena.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>


struct memtable_type {
  uint8_t *arena_buffer;
  struct arena_type arena;
  struct skiplist_type *sl;
};

struct memtable_type *memtable_create(void) {
  struct memtable_type *mt = malloc(sizeof(struct memtable_type));
  if (mt == NULL) {
    return NULL;
  }

  mt->arena_buffer = malloc(MEMTABLE_ARENA_SIZE);
  if(mt->arena_buffer == NULL){
    free(mt);
    return NULL;
  }

  arena_init(&mt->arena, mt->arena_buffer, MEMTABLE_ARENA_SIZE);

  mt->sl = skiplist_create(&mt->arena);
  if(mt->sl == NULL){
    memtable_destroy(mt);
    return NULL;
  }

  return mt;
}

void memtable_destroy(struct memtable_type *mt) {
  skiplist_destroy(mt->sl);
  free(mt->arena_buffer);
  free(mt);
}

int memtable_put(struct memtable_type *mt, const struct slice_type *key,
                 const struct slice_type *value) {

  uint8_t *buffer = arena_alloc(&mt->arena, key->length + value->length);
  if (buffer == NULL) {
    return -1;
  }
  memcpy(buffer, key->data, key->length);
  memcpy(buffer + key->length, value->data, value->length);

  struct slice_type k_copy = {.data = buffer, .length = key->length};
  struct slice_type v_copy = {.data = buffer + key->length,
                              .length = value->length};

  return skiplist_insert(mt->sl, &k_copy, &v_copy);
}

int memtable_get(struct memtable_type *mt, const struct slice_type *key,
                 struct slice_type *value) {
  int res = skiplist_get(mt->sl, key, value);
  if (res != 0 || value->length == 0)
    return -1;
  return 0;
}

int memtable_delete(struct memtable_type *mt, const struct slice_type *key){
  const struct slice_type value = {
    .data = NULL,
    .length = 0
  };
  
  return memtable_put(mt, key, &value);
}

struct skiplist_type *mt_skiplist(struct memtable_type *mt){
  return mt->sl;
}

