#include "db.h"
#include "memtable.h"
#include "wal.h"
#include "config.h"
#include "sstable.h"
#include "manifest.h"

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
  size_t immutable_count;

  struct manifest_type manifest;

  char db_path[MAX_PATH_LENGTH];
};

static int db_flush_immutable_mt(struct db_type *db);


// each wal maps to a memtable, after flushing correpsonding wals will be deleted
// the wals are contagious from manifest.oldest_wal_id to next_wal_id
// oldest memtable at the start of the immutable_mt and newest towards
// the end of the array, same ordering of sstable as in memtable, oldest ...->
// newest wal naming -> path/wal/wal_%zu.log, sstable naming -> path/sstable/sst_%zu.sstable
// manifest path as path/manifest.txt
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
  mkdir(db->db_path, 0755);

  char wal_dir[MAX_PATH_LENGTH];
  snprintf(wal_dir, sizeof(wal_dir), "%s/wal", db->db_path);
  mkdir(wal_dir, 0755);
  
  char sstable_dir[MAX_PATH_LENGTH];
  snprintf(sstable_dir, sizeof(sstable_dir), "%s/sstable", db->db_path);
  mkdir(sstable_dir, 0755);


  if(manifest_load(db->db_path, &db->manifest) == -1){
    if(manifest_init(db->db_path) == -1){
      fprintf(stderr, "Error in manifest_init in db_open\n");
      abort();
    }
    if(manifest_load(db->db_path, &db->manifest) == -1){
      fprintf(stderr, "Error in manifest_load in db_open\n");
      abort();
    }
  }

  db->immutable_count = 0;

  for(size_t wal_id = db->manifest.oldest_wal_id; wal_id < db->manifest.next_wal_id; wal_id++){
    
    struct memtable_type *mt = memtable_create();
    if(mt == NULL){
      fprintf(stderr, "Error in memtable_create in db_open\n");
      abort();
    }

    char wal_path[MAX_PATH_LENGTH];
    snprintf(wal_path, sizeof(wal_path), "%s/wal/wal_%zu.log", db->db_path, wal_id);
    struct wal_type *temp_wal = wal_open(wal_path);
    if(temp_wal == NULL){
      fprintf(stderr, "Error in wal_open in db_open\n");
      abort();
    }

    if(wal_replay(temp_wal, mt) == -1){
      fprintf(stderr, "Error in wal_replay in db_open\n");
      abort();
    }

    wal_close(temp_wal);

    db->immutable_mt[db->immutable_count++] = mt;

    if(db->immutable_count >= MAX_IMMUTABLE_MEMTABLE_COUNT){
      if(db_flush_immutable_mt(db) == -1){
        fprintf(stderr, "Error in db_flush_memtable in db_open\n");
        abort();
      }
    }
  }

  char active_wal_path[MAX_PATH_LENGTH];
  snprintf(active_wal_path, sizeof(active_wal_path), "%s/wal/wal_%zu.log", db->db_path, db->manifest.next_wal_id);

  db->active_wal = wal_open(active_wal_path);
  if(db->active_wal == NULL){
    fprintf(stderr, "Error in wal_open for active wal in db_open\n");
    abort();
  }

  db->manifest.next_wal_id++;

  if(manifest_store(db->db_path, &db->manifest) == -1){
    fprintf(stderr, "Error in manifest_store in db_open\n");
    abort();
  }

  db->active_mt = memtable_create();
  if(db->active_mt == NULL){
    fprintf(stderr, "Error in memtable_create for active mt in db_open\n");
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
             db->manifest.next_wal_id++);

    db->active_wal = wal_open(wal_path);
    if (db->active_wal == NULL) {
      fprintf(stderr, "errror in wal_open in db_put\n");
      abort();
    }

    db->manifest.next_wal_id++;
    if(manifest_store(db->db_path, &db->manifest) == -1){
      fprintf(stderr, "Error in manifest_store in db_put\n");
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

  for(int i = (int)db->manifest.live_sstable_count - 1; i >= 0; i--){
    char sstable_path[MAX_PATH_LENGTH];
    snprintf(sstable_path, sizeof(sstable_path), "%s/sstable/sst_%zu.sstable", db->db_path, db->manifest.live_sst[i]);
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


static int db_flush_immutable_mt(struct db_type *db) {
  char sstable_path[MAX_PATH_LENGTH];
  snprintf(sstable_path, sizeof(sstable_path), "%s/sstable/sst_%zu",
           db->db_path, db->manifest.next_sstable_id);

  if(sstable_flush(db->immutable_mt, db->immutable_count, sstable_path) == -1){
    return -1;
  }

  size_t previous_old_wal_id = db->manifest.oldest_wal_id;

  db->manifest.live_sst[db->manifest.live_sstable_count++] = db->manifest.next_sstable_id;
  db->manifest.next_sstable_id++;
  db->manifest.oldest_wal_id += db->immutable_count;

  if(manifest_store(db->db_path, &db->manifest) == -1){
    return -1;
  }

  for(size_t wal_id = previous_old_wal_id; wal_id < db->manifest.oldest_wal_id; wal_id++){
    char wal_path[MAX_PATH_LENGTH];
    snprintf(wal_path, sizeof(wal_path), "%s/wal/wal_%zu.log", db->db_path, wal_id);

    unlink(wal_path);
  }

  for(size_t i = 0; i < db->immutable_count; i++){
    memtable_destroy(db->immutable_mt[i]);
  }

  db->immutable_count = 0;
  
  return 0;

}


