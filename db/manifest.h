#ifndef MANIFEST_H
#define MANIFEST_H

#include "config.h"
#include "io_functions.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

// -> SSTABLE_COUNT
// -> LIVE_SST=[0,1,3,4]
// -> NEXT_SSTABLE_ID
struct manifest_type {
  uint64_t live_sstable_count;
  uint64_t live_sst[MAX_SSTABLE_COUNT];
  uint64_t next_sstable_id;
};

static int manifest_store(const char *db_path, const struct manifest_type *manifest);

static int manifest_init(const char *db_path) {
  struct manifest_type manifest;
  memset(&manifest, 0, sizeof(manifest));
  return manifest_store(db_path, &manifest);
}

static int manifest_load(const char *db_path, struct manifest_type *out) {
  char manifest_path[MAX_PATH_LENGTH];
  snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.txt", db_path);

  int fd = open(manifest_path, O_RDONLY);
  if (fd < 0) {
    return -1;
  }

  if (attempt_full_read(fd, out, sizeof(*out)) != sizeof(*out)) {
    close(fd);
    return -1;
  }

  if (out->live_sstable_count >= MAX_SSTABLE_COUNT) {
    return -2; // compaction
  }

  return 0;
}

static int manifest_store(const char *db_path,
                          const struct manifest_type *manifest) {
  char temp_path[MAX_PATH_LENGTH];
  char final_path[MAX_PATH_LENGTH];

  snprintf(temp_path, sizeof(temp_path), "%s/manifest.tmp", db_path);
  snprintf(final_path, sizeof(final_path), "%s/manifest.txt", db_path);

  int fd = open(temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    return -1;
  }

  if (attempt_full_write(fd, manifest, sizeof(*manifest)) !=
      sizeof(*manifest)) {
    close(fd);
    unlink(temp_path);
    return -1;
  }

  if (fdatasync(fd) == -1) {
    close(fd);
    unlink(temp_path);
    return -1;
  }

  close(fd);

  if (rename(temp_path, final_path) == -1) {
    unlink(temp_path);
    return -1;
  }

  int dir_fd = open(db_path, O_RDONLY);
  if (dir_fd < 0) {
    return -1;
  }

  if (fsync(dir_fd) == -1) {
    close(dir_fd);
    return -1;
  }

  close(dir_fd);

  return 0;
}

#endif

