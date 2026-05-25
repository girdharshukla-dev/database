#include "db.h"
#include "memtable.h"
#include "wal.h"
#include "config.h"
#include "sstable.h"

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

struct db_type {
  struct wal_type *active_wal;
  struct memtable_type *active_mt;

  struct memtable_type *immutable_mt[MAX_IMMUTABLE_MEMTABLE_COUNT];
  size_t sstable_ids[MAX_SSTABLE_COUNT];

  size_t immutable_count;
  size_t next_wal_id;

  size_t sstable_count;
  size_t next_sst_id;

  char db_path[MAX_PATH_LENGTH];
};

static int scan_sstables(struct db_type *db);

// each wal maps to a memtable, during flushing correpsonding wals will be
// deleted at a time there will be only MAX_IMMUTABLE_MEMTABLE_COUNT number of
// wals only oldest memtable at the start of the immutable_mt and newest towards
// the end of the array same ordering of sstable as in memtable, oldest ...->
// newest wal naming -> path/wal/wal_%zu.log, sstable naming ->
// path/sstable/sst_%zu.sstable
struct db_type *db_open(const char *path) {

  if (strlen(path) >= MAX_PATH_LENGTH) {
    fprintf(stderr, "db path too long\n");
    return NULL;
  }

  struct db_type *db = malloc(sizeof(struct db_type));

  if (db == NULL) {
    fprintf(stderr, "Error in allocating memory to db in db_open\n");
    return NULL;
  }

  strncpy(db->db_path, path, MAX_PATH_LENGTH - 1);
  db->db_path[MAX_PATH_LENGTH - 1] = '\0';

  char wal_dir[MAX_PATH_LENGTH];
  snprintf(wal_dir, sizeof(wal_dir), "%s/wal", db->db_path);
  mkdir(wal_dir, 0755);

  size_t max_id = 0;

  DIR *dir = opendir(wal_dir);
  if (dir == NULL) {
    fprintf(stderr, "opendir failed in db_open\n");
    free(db);
    return NULL;
  }

  struct dirent *entry;
  size_t wal_count = 0;
  size_t wal_ids[MAX_IMMUTABLE_MEMTABLE_COUNT];

  while ((entry = readdir(dir)) != NULL) {
    size_t id;
    if (sscanf(entry->d_name, "wal_%zu.log", &id) == 1) {
      if (id > max_id) {
        max_id = id;
      }
      wal_ids[wal_count++] = id;
    }
  }

  closedir(dir);

  for (size_t i = 0; i < wal_count; i++) {
    size_t min = i;
    for (int j = i + 1; j < wal_count; j++) {
      if (wal_ids[j] < wal_ids[min])
        min = j;
    }
    size_t temp = wal_ids[min];
    wal_ids[min] = wal_ids[i];
    wal_ids[i] = temp;
  }

  for (size_t i = 0; i < wal_count; i++) {

    struct memtable_type *mt = memtable_create();
    if (mt == NULL) {
      fprintf(stderr, "Error in wal_replay in memtable_create in db_open\n");
      abort();
    }
    char wal_path[MAX_PATH_LENGTH];
    snprintf(wal_path, sizeof(wal_path), "%s/wal/wal_%zu.log", db->db_path,
             wal_ids[i]);

    struct wal_type *temp_wal = wal_open(wal_path);
    if (temp_wal == NULL) {
      fprintf(stderr, "Error in temp_wal in db_open\n");
      abort();
    }

    if (wal_replay(temp_wal, mt) == -1) {
      fprintf(stderr, "Error in wal_replay in db_open\n");
      abort();
    }

    wal_close(temp_wal);

    db->immutable_mt[i] = mt;
  }

  db->immutable_count = wal_count;
  db->next_wal_id = max_id + 1;
  char active_wal_path[MAX_PATH_LENGTH];
  snprintf(active_wal_path, sizeof(active_wal_path), "%s/wal/wal_%zu.log",
           db->db_path, db->next_wal_id++);

  db->active_wal = wal_open(active_wal_path);
  if (db->active_wal == NULL) {
    fprintf(stderr, "error in wal_open in db_open\n");
    abort();
  }

  db->active_mt = memtable_create();
  if (db->active_mt == NULL) {
    fprintf(stderr, "error in memtable_create in db_open\n");
    abort();
  }

  if (scan_sstables(db) == -1) {
    fprintf(stderr, "Error in scan_sstables in db_open\n");
    abort();
  }

  return db;
}

int db_put(struct db_type *db, const struct slice_type *key,
           const struct slice_type *value) {

  // the memtable insertion is done first to avoid appending to wrong wal incase
  // memtable max size is reached or anyother invariant where memtable insertion
  // fails and leading to append to a wrong wal since reverting an wal_append is
  // not allowed, also success is only returned after wal_append succeeds
  // ensuring synchronized memtable and wal

  if (memtable_put(db->active_mt, key, value) == -1) {
    if (db->immutable_count == MAX_IMMUTABLE_MEMTABLE_COUNT) {
      if (db_flush_immutable_mt(db) == -1) {
        return -1;
      }
    }

    db->immutable_mt[db->immutable_count++] = db->active_mt;
    wal_close(db->active_wal);

    db->active_mt = memtable_create();
    if (db->active_mt == NULL) {
      fprintf(stderr, "error in memtable_create in db_put\n");
      abort();
    }

    char wal_path[MAX_PATH_LENGTH];
    snprintf(wal_path, sizeof(wal_path), "%s/wal/wal_%zu.log", db->db_path,
             db->next_wal_id++);

    db->active_wal = wal_open(wal_path);
    if (db->active_wal == NULL) {
      fprintf(stderr, "errror in wal_open in db_put\n");
      abort();
    }

    if (memtable_put(db->active_mt, key, value) == -1) {
      fprintf(stderr, "Error in memtable_put in db_put\n");
      return -1;
    }
  }

  if (wal_append(db->active_wal, key, value) == -1) {
    fprintf(stderr, "Error in wal_append in db_put\n");
    return memtable_delete(db->active_mt, key);
  }

  if (wal_sync(db->active_wal) == -1) {
    fprintf(stderr, "Error in wal_sync in db_put\n");
    abort();
  }

  return 0;
}

int db_get(struct db_type *db, const struct slice_type *key,
           struct slice_type *value) {
  if (memtable_get(db->active_mt, key, value) == 0) {
    return 0;
  }

  for (int i = (int)db->immutable_count - 1; i >= 0; i--) {
    if (memtable_get(db->immutable_mt[i], key, value) == 0) {
      return 0;
    }
  }

  for(int i = db->sstable_count - 1; i >= 0; i--){
    char sstable_path[MAX_PATH_LENGTH];
    snprintf(sstable_path, "%s/sstable/sst_%zu.sstable", db->db_path, db->sstable_ids[i]);
    int resp = sstable_get(sstable_path, key, value);
    if(resp == 0){
      return 0;
    }else if(resp == -2){
      //tombstone found
      return -1;
    }
  }

  return -1;
}

void db_close(struct db_type *db) {
  wal_close(db->active_wal);
  memtable_destroy(db->active_mt);

  for (size_t i = 0; i < db->immutable_count; i++) {
    memtable_destroy(db->immutable_mt[i]);
  }

  free(db);
}

static int scan_sstables(struct db_type *db) {
  char sstable_dir[MAX_PATH_LENGTH];
  snprintf(sstable_dir, sizeof(sstable_dir), "%s/sstable", db->db_path);

  int mkdir_resp = mkdir(sstable_dir, 0755);
  if (mkdir_resp == 0) {
    db->next_sst_id = 0;
    db->sstable_count = 0;
    return 0;
  } else if (mkdir_resp == -1 && errno == EEXIST) {

    DIR *dir = opendir(sstable_dir);
    if (dir == NULL) {
      return -1;
    }

    struct dirent *entry;
    size_t sstable_count = 0;
    while ((entry = readdir(dir)) != NULL) {
      size_t id;
      if (sscanf(entry->d_name, "sst_%zu.sstable", &id) == 1) {
        db->sstable_ids[sstable_count++] = id;
      }
    }

    db->sstable_count = sstable_count;

    for (size_t i = 0; i < sstable_count; i++) {
      size_t min = i;
      for (size_t j = i + 1; j < sstable_count; j++) {
        if (db->sstable_ids[j] < db->sstable_ids[min]) {
          min = j;
        }
      }
      size_t temp = db->sstable_ids[i];
      db->sstable_ids[i] = db->sstable_ids[min];
      db->sstable_ids[min] = temp;
    }
    if (sstable_count == 0) {
      db->next_sst_id = 0;
    } else {
      db->next_sst_id = db->sstable_ids[sstable_count - 1] + 1;
    }

    closedir(dir);
  } else {
    return -1;
  }

  return 0;
}

static int db_flush_immutable_mt(struct db_type *db) {
  char sstable_path[MAX_PATH_LENGTH];
  snprintf(sstable_path, sizeof(sstable_path), "%s/sstable/sst_%zu.sstable",
           db->db_path, db->next_sst_id);

  if(sstable_flush(db->immutable_mt, db->immutable_count, sstable_path) == -1){
    return -1;
  }

  db->sstable_ids[db->sstable_count++] = db->next_sst_id++;

  for(size_t i = 0; i < db->immutable_count; i++){
    char wal_path[MAX_PATH_LENGTH];
    // deleting the wals in reverse order ... next_wal_id - 1 is the active_wal
    snprintf(wal_path, sizeof(wal_path), "%s/wal/wal_%zu.log", db->db_path, db->next_wal_id - 2 - i);

    if(unlink(wal_path) == -1){
      fprintf(stderr, "Error in deleting wal file in db_flush_immutable_mt\n");
      return -1;
    }

    memtable_destroy(db->immutable_mt[db->next_wal_id - 2 - i]);
  }

  db->immutable_count = 0;
  
  return 0;

}


