#include "memtable.h"
#include "skiplist.h"
#include "io_functions.h"
#include "config.h"

#include <stddef.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

static void sstable_flush_iters_destroy(struct skiplist_iter *sl_iters[],
                                        size_t count) {
  for (size_t i = 0; i < count; i++) {
    skiplist_iter_destroy(sl_iters[i]);
  }
}

// first flush to to a .tmp file, when whole flushing succeeds, rename it to
// .sstable sstable_path passed as something like /dir/sst_21
// format is still [key_length][value_length][key_data][value_data]
// the immutable memtables are flushed together to the sstable as a k-way merge
int sstable_flush(struct memtable_type *immutable_mt[], size_t count,
                  const char *sstable_path) {

  char sstable_temp_path[MAX_PATH_LENGTH];
  snprintf(sstable_temp_path, sizeof(sstable_temp_path), "%s.tmp",
           sstable_path);

  int sstable_fd = open(sstable_temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (sstable_fd < 0) {
    return -1;
  }

  struct skiplist_iter *sl_iters[count];

  for (size_t i = 0; i < count; i++) {
    sl_iters[i] = skiplist_iter_create(mt_skiplist(immutable_mt[i]));
    if (sl_iters[i] == NULL) {
      for (size_t j = 0; j < i; j++) {
        skiplist_iter_destroy(sl_iters[j]);
      }
      close(sstable_fd);
      return -1;
    }
  }

  while (1) {
    ssize_t smallest = -1;

    for (size_t i = 0; i < count; i++) {
      if (!skiplist_iter_valid(sl_iters[i])) {
        continue;
      }

      if (smallest == -1) {
        smallest = i;
      }

      if (slice_cmp(skiplist_iter_key(sl_iters[i]),
                    skiplist_iter_key(sl_iters[smallest])) < 0) {
        smallest = i;
      }
    }

    if (smallest == -1) {
      break;
    }

    ssize_t winner = smallest;

    for (size_t i = smallest + 1; i < count; i++) {
      if (!skiplist_iter_valid(sl_iters[i])) {
        continue;
      }
      if (slice_cmp(skiplist_iter_key(sl_iters[i]),
                    skiplist_iter_key(sl_iters[smallest])) == 0) {
        winner = i;
      }
    }

    const struct slice_type *key = skiplist_iter_key(sl_iters[winner]);
    const struct slice_type *value = skiplist_iter_value(sl_iters[winner]);

    if (attempt_full_write(sstable_fd, &key->length, sizeof(key->length)) !=
        sizeof(key->length)) {
      sstable_flush_iters_destroy(sl_iters, count);
      close(sstable_fd);
      return -1;
    }

    if (attempt_full_write(sstable_fd, &value->length, sizeof(value->length)) !=
        sizeof(value->length)) {
      sstable_flush_iters_destroy(sl_iters, count);
      close(sstable_fd);
      return -1;
    }

    if (attempt_full_write(sstable_fd, key->data, key->length) != key->length) {
      sstable_flush_iters_destroy(sl_iters, count);
      close(sstable_fd);
      return -1;
    }

    if (attempt_full_write(sstable_fd, value->data, value->length) !=
            value->length) {
      sstable_flush_iters_destroy(sl_iters, count);
      close(sstable_fd);
      return -1;
    }

    for (size_t i = smallest; i <= winner; i++) {
      if (!skiplist_iter_valid(sl_iters[i])) {
        continue;
      }
      if (slice_cmp(skiplist_iter_key(sl_iters[i]),
                    skiplist_iter_key(sl_iters[smallest])) == 0) {
        skiplist_iter_next(sl_iters[i]);
      }
    }
  }

  sstable_flush_iters_destroy(sl_iters, count);

  if (fdatasync(sstable_fd) == -1) {
    close(sstable_fd);
    unlink(sstable_temp_path);
    return -1;
  } else {
    close(sstable_fd);
    char sstable_final_path[MAX_PATH_LENGTH];
    snprintf(sstable_final_path, sizeof(sstable_final_path), "%s.sstable",
             sstable_path);
    if (rename(sstable_temp_path, sstable_final_path) == -1) {
      unlink(sstable_temp_path);
      return -1;
    }

    char dir[MAX_PATH_LENGTH];
    char *last_slash = strrchr(sstable_path, '/');

    size_t length = last_slash - sstable_path;

    memcpy(dir, sstable_path, length);
    dir[length] = '\0';
    int dirfd = open(dir, O_RDONLY);
    if (dirfd < 0) {
      return -1;
    }
    if (fsync(dirfd) != 0) {
      close(dirfd);
      return -1;
    }
    close(dirfd);
  }

  return 0;
}

int sstable_get(const char *sstable_path, const struct slice_type *target_key,
                struct slice_type *out) {
  int sstable_fd = open(sstable_path, O_RDONLY);
  if (sstable_fd < 0) {
    return -1;
  }

  uint32_t key_length;
  uint32_t value_length;

  uint32_t lengths[2];
  while (1) {
    if (attempt_full_read(sstable_fd, lengths, 2 * sizeof(uint32_t)) !=
        2 * sizeof(uint32_t)) {
      close(sstable_fd);
      return -1;
    }

    key_length = lengths[0];
    value_length = lengths[1];

    if (target_key->length != key_length) {
      if(lseek(sstable_fd, key_length + value_length, SEEK_CUR) == (off_t) - 1){
        close(sstable_fd);
        fprintf(stderr, "Error in lseek in sstable_get\n");
        return -1;
      }
      continue;
    }

    uint8_t *buffer = malloc(key_length + value_length);
    if(buffer == NULL){
      fprintf(stderr, "Error in buffer malloc in sstable_get\n");
      close(sstable_fd);
      return -1;
    }

    if(attempt_full_read(sstable_fd, buffer, key_length + value_length) != key_length + value_length){
      close(sstable_fd);
      free(buffer);
      return -1;
    }
    
    if(memcmp(target_key->data, buffer, target_key->length) == 0){
      if(value_length == 0){  // tombstone
        close(sstable_fd);
        free(buffer);
        return -2;
      }
      memcpy(out->data, buffer + target_key->length, value_length);
      out->length = value_length;
      close(sstable_fd);
      free(buffer);
      return 0;
    }

    free(buffer);
  }
}


