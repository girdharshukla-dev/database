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

  struct manifest_type manifest;

  char db_path[MAX_PATH_LENGTH];
};

static int db_flush_memtable(struct db_type *db);
static int db_compact_sstables(struct db_type *db);

// newest wal naming -> path/wal/wal_%zu.log, sstable naming ->
// path/sstable/sst_%zu.sstable manifest path as path/manifest.txt there will be
// 2 log files, 1 wal.log and 2 flush.log ... flush.log is waht is to be flushed
// to sstable and wal.log is the active wal
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

  if (manifest_load(db->db_path, &db->manifest) == -1) {
    if (manifest_init(db->db_path) == -1) {
      fprintf(stderr, "Error in manifest_init in db_open\n");
      abort();
    }
    if (manifest_load(db->db_path, &db->manifest) == -1) {
      fprintf(stderr, "Error in manifest_load in db_open\n");
      abort();
    }
  }

  char flush_wal_path[MAX_PATH_LENGTH];
  snprintf(flush_wal_path, sizeof(flush_wal_path), "%s/wal/flush.log",
           db->db_path);

  if (access(flush_wal_path, F_OK) == 0) {
    db->active_wal = NULL;
    db->active_mt = memtable_create();

    if (db->active_mt == NULL) {
      fprintf(stderr, "Error memtable_create in db_open\n");
    }
    struct wal_type *flush_wal = wal_open(flush_wal_path);
    if (flush_wal == NULL) {
      fprintf(stderr, "Error in flush wal_open in db_open\n");
      abort();
    }

    if (wal_replay(flush_wal, db->active_mt) == -1) {
      fprintf(stderr, "Error in wal_replay for flush wal in db_open\n");
      abort();
    }
    wal_close(flush_wal);

    if (db_flush_memtable(db) == -1) {
      fprintf(stderr, "Error in db_flush_memtable in db_open\n");
      abort();
    }

    memtable_destroy(db->active_mt);
  }

  db->active_mt = memtable_create();
  if (db->active_mt == NULL) {
    fprintf(stderr, "Error memtable_create in db_open\n");
  }

  char active_wal_path[MAX_PATH_LENGTH];
  snprintf(active_wal_path, sizeof(active_wal_path), "%s/wal/active.log",
           db->db_path);

  if (access(active_wal_path, F_OK) == 0) {
    struct wal_type *temp_wal = wal_open(active_wal_path);
    if (temp_wal == NULL) {
      fprintf(stderr, "Error in wal_open in db_open for active.log\n");
      abort();
    }

    if (wal_replay(temp_wal, db->active_mt) == -1) {
      fprintf(stderr, "Error in wal_replay for active.log\n");
      abort();
    }

    wal_close(temp_wal);
  }

  db->active_wal = wal_open(active_wal_path);
  if (db->active_wal == NULL) {
    fprintf(stderr, "Error in wal_open for active.log\n");
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
    if (db_flush_memtable(db) == -1) {
      return -1;
    }

    memtable_destroy(db->active_mt);

    db->active_mt = memtable_create();
    if (db->active_mt == NULL) {
      fprintf(stderr, "Error in memtable_create in db_put\n");
      return -1;
    }

    char active_wal_path[MAX_PATH_LENGTH];
    snprintf(active_wal_path, sizeof(active_wal_path), "%s/wal/active.log",
             db->db_path);
    unlink(active_wal_path);
    db->active_wal = wal_open(active_wal_path);
    if (db->active_wal == NULL) {
      fprintf(stderr, "Error in wal_open in db_put\n");
      return -1;
    }

    if (memtable_put(db->active_mt, key, value) == -1) {
      fprintf(stderr, "memtable_put error in db_put\n");
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

  for (int i = db->manifest.live_sstable_count - 1; i >= 0; i--) {
    char sstable_path[MAX_PATH_LENGTH];
    snprintf(sstable_path, sizeof(sstable_path), "%s/sstable/sst_%zu.sstable",
             db->db_path, db->manifest.live_sst[i]);

    if (sstable_get(sstable_path, key, value) == 0) {
      return 0;
    }
  }

  return -1;
}

static int db_flush_memtable(struct db_type *db) {
  char active_wal_path[MAX_PATH_LENGTH];
  char flush_wal_path[MAX_PATH_LENGTH];
  char sstable_path[MAX_PATH_LENGTH];

  snprintf(active_wal_path, sizeof(active_wal_path), "%s/wal/active.log",
           db->db_path);
  snprintf(flush_wal_path, sizeof(flush_wal_path), "%s/wal/flush.log",
           db->db_path);
  snprintf(sstable_path, sizeof(sstable_path), "%s/sstable/sst_%zu",
           db->db_path, db->manifest.next_sstable_id);

  if (db->active_wal != NULL) {
    wal_close(db->active_wal);
    if (rename(active_wal_path, flush_wal_path) == -1) {
      fprintf(stderr, "Error in rename in db_flush_memtable\n");
      return -1;
    }
  }

  if (sstable_flush(db->active_mt, sstable_path) == -1) {
    fprintf(stderr, "error in sstable_flush in db_flush_memtable\n");
    return -1;
  }

  db->manifest.live_sst[db->manifest.live_sstable_count++] =
      db->manifest.next_sstable_id++;
  if (manifest_store(db->db_path, &db->manifest) == -1) {
    fprintf(stderr, "error in manifest_store in db_flush_memtable\n");
    return -1;
  }

  if (unlink(flush_wal_path) == -1) {
    fprintf(stderr, "Error in unlink flush wal in db_flush_memtable\n");
    return -1;
  }

  if (db->manifest.live_sstable_count >= MAX_SSTABLE_COUNT) {
    if (db_compact_sstables(db) == -1) {
      fprintf(stderr, "Error in db_compact_sstable in db_flush_memetable\n");
      return -1;
    }
  }

  return 0;
}

void db_close(struct db_type *db) {
  wal_close(db->active_wal);
  memtable_destroy(db->active_mt);

  free(db);
}

static int db_compact_sstables(struct db_type *db) {
  char sstable_paths_storage[MAX_SSTABLES_COMPACTED][MAX_PATH_LENGTH];
  const char *sstable_paths[MAX_SSTABLES_COMPACTED];
  for (size_t i = 0; i < MAX_SSTABLES_COMPACTED; i++) {
    snprintf(sstable_paths_storage[i], sizeof(sstable_paths_storage[i]),
             "%s/sstable/sst_%zu.sstable", db->db_path,
             db->manifest.live_sst[i]);
    sstable_paths[i] = sstable_paths_storage[i];
  }


  int output_id = db->manifest.next_sstable_id++;
  char output_path[MAX_PATH_LENGTH];
  snprintf(output_path, sizeof(output_path), "%s/sstable/sst_%d",
           db->db_path, output_id);

  if (sstable_compact(sstable_paths, MAX_SSTABLES_COMPACTED, output_path) ==
      -1) {
    fprintf(stderr, "Errror in sstable_compact in db_compact_sstable\n");
    return -1;
  }

  db->manifest.live_sst[0] = output_id;
  memmove(db->manifest.live_sst + 1,
          db->manifest.live_sst + MAX_SSTABLES_COMPACTED,
          (db->manifest.live_sstable_count - MAX_SSTABLES_COMPACTED) *
              sizeof(uint64_t));

  db->manifest.live_sstable_count =
      db->manifest.live_sstable_count - MAX_SSTABLES_COMPACTED + 1;

  if (manifest_store(db->db_path, &db->manifest) == -1) {
    fprintf(stderr, "manifest_store failed in db_compact_sstable\n");
    return -1;
  }

  for(size_t i = 0; i < MAX_SSTABLES_COMPACTED; i++){
    if(unlink(sstable_paths[i]) == -1){
      fprintf(stderr, "[WARNING] unlink failed for sstable: ", sstable_paths[i]);
    }
  }

  return 0;
}
