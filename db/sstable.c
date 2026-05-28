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

// first flush to to a .tmp file, when whole flushing succeeds, rename it to
// .sstable sstable_path passed as something like /dir/sst_21
// sstable_path are mostly passed around as /dir/sst_21 without the .sstable
// unless they dont need to be modified intermediately format is still
// [key_length][value_length][key_data][value_data]
int sstable_flush(struct memtable_type *mt, const char *sstable_path) {

  char sstable_temp_path[MAX_PATH_LENGTH];
  snprintf(sstable_temp_path, sizeof(sstable_temp_path), "%s.tmp",
           sstable_path);

  int sstable_fd = open(sstable_temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (sstable_fd < 0) {
    return -1;
  }

  struct skiplist_iter *sl_iter = skiplist_iter_create(mt_skiplist(mt));
  if (sl_iter == NULL) {
    fprintf(stderr, "Error in sl_iter creation in sstable_put\n");
    return -1;
  }

  while (skiplist_iter_valid(sl_iter)) {
    const struct slice_type *key = skiplist_iter_key(sl_iter);
    const struct slice_type *value = skiplist_iter_value(sl_iter);

    if (attempt_full_write(sstable_fd, &key->length, sizeof(key->length)) !=
        sizeof(key->length)) {
      skiplist_iter_destroy(sl_iter);
      close(sstable_fd);
      return -1;
    }

    if (attempt_full_write(sstable_fd, &value->length, sizeof(value->length)) !=
        sizeof(value->length)) {
      skiplist_iter_destroy(sl_iter);
      close(sstable_fd);
      return -1;
    }

    if (attempt_full_write(sstable_fd, key->data, key->length) != key->length) {
      skiplist_iter_destroy(sl_iter);
      close(sstable_fd);
      return -1;
    }

    if (attempt_full_write(sstable_fd, value->data, value->length) !=
        value->length) {
      skiplist_iter_destroy(sl_iter);
      close(sstable_fd);
      return -1;
    }

    skiplist_iter_next(sl_iter);
  }

  skiplist_iter_destroy(sl_iter);

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
      if (lseek(sstable_fd, key_length + value_length, SEEK_CUR) == (off_t)-1) {
        close(sstable_fd);
        fprintf(stderr, "Error in lseek in sstable_get\n");
        return -1;
      }
      continue;
    }

    uint8_t *buffer = malloc(key_length + value_length);
    if (buffer == NULL) {
      fprintf(stderr, "Error in buffer malloc in sstable_get\n");
      close(sstable_fd);
      return -1;
    }

    if (attempt_full_read(sstable_fd, buffer, key_length + value_length) !=
        key_length + value_length) {
      close(sstable_fd);
      free(buffer);
      return -1;
    }

    if (memcmp(target_key->data, buffer, target_key->length) == 0) {
      if (value_length == 0) { // tombstone
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

struct sstable_iter {
  int fd;

  struct slice_type key;
  struct slice_type value;

  int valid;
};

int sstable_iter_next(struct sstable_iter *sst_iter);

struct sstable_iter *sstable_iter_init(const char *path) {
  struct sstable_iter *sst_iter = malloc(sizeof(sst_iter));
  if (sst_iter == NULL) {
    fprintf(stderr, "Error in sst_iter malloc in iter_init\n");
    return NULL;
  }

  sst_iter->fd = open(path, O_RDONLY);
  if (sst_iter->fd < 0) {
    fprintf(stderr, "error in fd open in sstable_iter_init\n");
    return -1;
  }

  sst_iter->valid = 1;
  if (sstable_iter_next(sst_iter) == -1) {
    close(sst_iter->fd);
    free(sst_iter);
    return NULL;
  }

  return sst_iter;
}

int sstable_iter_next(struct sstable_iter *sst_iter) {
  free(sst_iter->key.data);
  free(sst_iter->value.data);

  uint32_t key_length, value_length;

  ssize_t n = attempt_full_read(sst_iter->fd, &key_length, sizeof(key_length));
  if (n == -1 || n != sizeof(key_length)) {
    fprintf(stderr, "Error in reading key_length in sstable_iter_next\n");
    return -1;
  }
  if (n == 0) {
    sst_iter->valid = 0;
    return 0;
  }

  n = attempt_full_read(sst_iter->fd, &value_length, sizeof(value_length));
  if (n == -1 || n != sizeof(value_length)) {
    fprintf(stderr, "Error in reading value_length in sstable_iter_next\n");
    return -1;
  }

  sst_iter->key.length = key_length;
  sst_iter->key.data = malloc(key_length);

  sst_iter->value.length = value_length;
  sst_iter->value.data = malloc(value_length);

  if (attempt_full_read(sst_iter->fd, sst_iter->key.data, key_length) !=
      sizeof(key_length)) {
    fprintf(stderr, "Error in reading key_data in sst_iter_next\n");
    return -1;
  }
  if (attempt_full_read(sst_iter->fd, sst_iter->value.data, value_length) !=
      sizeof(value_length)) {
    fprintf(stderr, "Error in reading value_data in sst_iter_next\n");
    return -1;
  }

  return 0;
}

void sstable_iter_destroy(struct sstable_iter *sst_iter) {
  free(sst_iter->key.data);
  free(sst_iter->value.data);
  close(sst_iter->fd);
  free(sst_iter);
}

static inline void sstable_iters_destroy(struct sstable_iter *sst_iters[],
                                         size_t count) {
  for (size_t i = 0; i < count; i++) {
    sstable_iter_destroy(sst_iters[i]);
  }
}

int sstable_compact(const char *sstable_paths[], size_t count,
                    const char *output_path) {
  char temp_sstable_path[MAX_PATH_LENGTH];

  snprintf(temp_sstable_path, sizeof(temp_sstable_path), "%s.tmp", output_path);

  int temp_fd = open(temp_sstable_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (temp_fd < 0) {
    fprintf(stderr, "Error in fd open in sstable_compact\n");
    return -1;
  }

  struct sstable_iter *sst_iters[count];
  for (size_t i = 0; i < count; i++) {
    sst_iters[i] = sstable_iter_init(sstable_paths[i]);
    if (sst_iters[i] == NULL) {
      for (int j = 0; j < i; j++) {
        sstable_iter_destroy(sst_iters[j]);
      }
      close(temp_fd);
      unlink(temp_sstable_path);
      return -1;
    }
  }

  while (1) {
    ssize_t smallest = -1;

    for (size_t i = 0; i < count; i++) {
      if (sst_iters[i]->valid) {
        continue;
      }
      if (smallest == -1) {
        smallest = i;
        continue;
      }

      if (slice_cmp(&sst_iters[i]->key, &sst_iters[smallest]->key) < 0) {
        smallest = i;
      }
    }

    if (smallest == -1) {
      break;
    }

    ssize_t winner = smallest;

    for (size_t i = smallest + 1; i < count; i++) {
      if (!sst_iters[i]->valid)
        continue;

      if (slice_cmp(&sst_iters[i]->key, &sst_iters[i]->value) == 0) {
        winner = i;
      }
    }

    struct slice_type key = sst_iters[winner]->key;
    struct slice_type value = sst_iters[winner]->value;

    if (attempt_full_write(temp_fd, &key.length, sizeof(key.length)) !=
        sizeof(key.length)) {
      sstable_iters_destroy(sst_iters, count);
      close(temp_fd);
      unlink(temp_sstable_path);
      return -1;
    }
    if (attempt_full_write(temp_fd, &value.length, sizeof(value.length)) !=
        sizeof(value.length)) {
      sstable_iters_destroy(sst_iters, count);
      close(temp_fd);
      unlink(temp_sstable_path);
      return -1;
    }

    if (attempt_full_write(temp_fd, key.data, key.length) != key.length) {
      sstable_iters_destroy(sst_iters, count);
      close(temp_fd);
      unlink(temp_sstable_path);
      return -1;
    }
    if (attempt_full_write(temp_fd, value.data, value.length) != value.length) {
      sstable_iters_destroy(sst_iters, count);
      close(temp_fd);
      unlink(temp_sstable_path);
      return -1;
    }

    for (size_t i = smallest; i <= winner; i++) {
      if (!sst_iters[i]->valid) {
        continue;
      }

      if (slice_cmp(&sst_iters[i]->key, &key) == 0) {
        if (sstable_iter_next(sst_iters[i]) == -1) {
          sstable_iters_destroy(sst_iters, count);
          close(temp_fd);
          unlink(temp_sstable_path);
          return -1;
        }
      }
    }
  }

  sstable_iters_destroy(sst_iters, count);

  if (fdatasync(temp_fd) == -1) {
    close(temp_fd);
    unlink(temp_sstable_path);
    return -1;
  }

  close(temp_fd);

  char final_path[MAX_PATH_LENGTH];
  snprintf(final_path, sizeof(final_path), "%s.sstable", output_path);

  if(rename(temp_sstable_path, final_path) == -1){
    unlink(temp_sstable_path);
    return -1;
  }

  char dir[MAX_PATH_LENGTH];
  char *slash = strrchr(output_path, '/');

  size_t length = slash - output_path;
  memcpy(dir, output_path, length);
  dir[length] = '\0';

  int dir_fd = open(dir, O_RDONLY);
  if(dir_fd < 0){
    return -1;
  }

  if(fsync(dir_fd) == -1){
    close(dir_fd);
    return -1;
  }

  close(dir_fd);
  return 0;
}


