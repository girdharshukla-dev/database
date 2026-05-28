#ifndef SSTABLE_H
#define SSTABLE_H

#include "slice.h"
#include "memtable.h"
#include <stddef.h>


int sstable_flush(struct memtable_type *mt, const char *sstable_path);
int sstable_get(const char *sstable_path, const struct slice_type *key, struct slice_type *out);

struct sstable_iter;
struct sstable_iter *sstable_iter_init(const char *path);
int sstable_iter_next(struct sstable_iter *sst_iter);
void sstable_iter_destroy(struct sstable_iter *sst_iter);

int sstable_compact(const char *sstable_paths[], size_t count, const char *output_path);

#endif

