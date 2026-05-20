#include "db.h"
#include "memtable.h"
#include "wal.h"

#include <stdlib.h>
#include <stdio.h>

struct db_type {
  struct wal_type *wal;
  struct memtable_type *mt;
};

struct db_type *db_open(const char *path) {
  struct db_type *db = malloc(sizeof(struct db_type));
  
  if (db == NULL) {
    fprintf(stderr, "Error in allocating memory to db in db_open\n");
    return NULL;
  }
  db->wal = wal_open(path);
  if (db->wal == NULL) {
    fprintf(stderr, "Error in opening wal\n");
    free(db);
    return NULL;
  }

  db->mt = memtable_create();
  if (db->mt == NULL) {
    wal_close(db->wal);
    free(db);
    fprintf(stderr, "Error in memtable_create() \n");
    return NULL;
  }

  int wal_replay_res = wal_replay(db->wal, db->mt);
  if(wal_replay_res == -1){
    fprintf(stderr, "Error in wal_replay in db_open\n");
    db_close(db);
    return NULL;
  }

  return db;
}

int db_put(struct db_type *db, const struct slice_type *key,
           const struct slice_type *value) {
  if (wal_append(db->wal, key, value) == -1) {
    fprintf(stderr, "Error in appending to wal in db_put\n");
    return -1;
  }

  if (wal_sync(db->wal) == -1) {
    fprintf(stderr, "Error in wal_sync in db_put\n");
    return -1;
  }

  if (memtable_put(db->mt, key, value) == -1) {
    fprintf(stderr, "Error in insertion to memtable in db_put\n");
    // freeze it
    return -1;
  }
  return 0;
}

int db_get(struct db_type *db, const struct slice_type *key,
           struct slice_type *value) {
  return memtable_get(db->mt, key, value);
}

void db_close(struct db_type *db) {
  wal_close(db->wal);
  memtable_destroy(db->mt);
  free(db);
}
