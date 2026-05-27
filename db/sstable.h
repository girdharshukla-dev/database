#ifndef SSTABLE_H
#define SSTABLE_H

#include "slice.h"
#include "memtable.h"
#include <stddef.h>

int sstable_flush(struct memtable_type *mt, const char *sstable_path);

int sstable_get(const char *sstable_path, const struct slice_type *key, struct slice_type *out);

#endif

