#ifndef WAL_H
#define WAL_H

#include "slice.h"

struct wal_type {
  int fd;
  uint64_t id;
  char path[256];
};

struct wal_type *wal_open(const char *path);
void wal_close(struct wal_type *wal);

int wal_append(struct wal_type *wal, const struct slice_type *key, const struct slice_type *value);
int wal_sync(struct wal_type *wal);

struct memtable_type;
int wal_replay(struct wal_type *wal, struct memtable_type *mt);

#endif
