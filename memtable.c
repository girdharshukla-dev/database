#include "memtable.h"
#include "skiplist.h"

#include <stdlib.h>

struct memtable_type {
  struct skiplist_type *sl;
};

struct memtable_type *memtable_create(void) {
  struct memtable_type *mt = malloc(sizeof(struct memtable_type));
  mt->sl = skiplist_create();
  return mt;
}

void memtable_destroy(struct memtable_type *mt) {
  skiplist_destroy(mt->sl);
  free(mt);
}

int memtable_put(struct memtable_type *mt, const struct slice_type *key,
                 const struct slice_type *value) {
  uint8_t *k = malloc(key->length);
  memcpy(k, key->data, key->length);

  uint8_t *v = malloc(value->length);
  memcpy(v, value->data, value->length);

  struct slice_type k_copy = {.data = k, .length = key->length};
  struct slice_type v_copy = {.data = v, .length = value->length};

  return skiplist_insert(mt->sl, key, value);
}

int memtable_get(struct memtable_type *mt, const struct slice_type *key,
                 const struct slice_type *value) {
  int res = skiplist_get(mt->sl, key, value);
  if (res != 0)
    return -1;
  if (value->length == 0)
    return -1;
  return 0;
}
