#ifndef MEMTABLE_H
#define MEMTABLE_H

#include "slice.h"

struct memtable_type;
struct memtable_type *memtable_create(void);

void memtable_destroy(struct memtable_type *mt);

int memtable_put(struct memtable_type *mt, const struct slice_type *key, const struct slice_type *value);

int memtable_get(struct memtable_type *mt, const struct slice_type *key, struct slice_type *value);

#endif