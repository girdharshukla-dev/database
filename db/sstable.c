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
#include <debug.h>

// first flush to to a .tmp file, when whole flushing succeeds, rename it to
// .sstable sstable_path passed as something like /dir/sst_21
// sstable_path are mostly passed around as /dir/sst_21 without the .sstable
// ... unless they dont need to be modified intermediately
// format of a sstable is
/*
 *  [header] -> contains sparse index offset and number of sparse index entries
 *  [Block 1] -> multiple records of format
 *               [key_length][value_length][key_data][value_data]
 *  [Block 2]
 *  [Block 3]
 *  ...
 *  ...
 *  ...       -> each block has a target size of BLOCK_SIZE but it is not a
 *               strict threshold
 *  ...       -> if the last record overflows ... let it overflow
 *  [sparse index] -> contains the first key of each block and its offset
 *                 -> records as [key_length][key_data][offset]
 */

#define BLOCK_SIZE 4096

struct sstable_header_type {
  uint64_t sparse_index_offset;
  size_t idx_entries_count;
};

struct sparse_idx_entry_type {
  struct slice_type key;
  uint64_t offset;
};

struct sparse_idx_array {
  struct sparse_idx_entry_type *entries;
  size_t count;
  size_t capacity;
};

static struct sparse_idx_array *idx_init(size_t size) {
  struct sparse_idx_array *idx = malloc(sizeof(*idx));
  if (idx == NULL) {
    DEBUG_LOG("Error in malloc in idx_init\n");
  }
  idx->entries = malloc(size * sizeof(struct sparse_idx_entry_type));
  idx->count = 0;
  idx->capacity = size;
  return idx;
};

static int idx_put(struct sparse_idx_entry_type element,
                   struct sparse_idx_array *idx) {
  if (idx->count >= idx->capacity) {
    if ((idx->entries = realloc(idx->entries,
                                idx->capacity * 2 * sizeof(*idx->entries))) ==
        NULL) {
      return -1;
    }
    idx->capacity *= 2;
  }
  idx->entries[idx->count++] = element;
  return 0;
}

struct sstable_writer_type {
  int fd;
  struct sparse_idx_array *idx_array;
  size_t used;
  uint8_t block[BLOCK_SIZE];
  struct slice_type first_key;
};

int sstable_kv_writer(struct sstable_writer_type *w,
                      const struct slice_type *key,
                      const struct slice_type *value) {
  if (w->used == 0) {
    memcpy(&w->first_key, key, sizeof(*key));
  }

  size_t record_size =
      sizeof(key->length) + sizeof(value->length) + key->length + value->length;

  if (w->used > 0 && w->used + record_size >= BLOCK_SIZE) {
    off_t block_offset = lseek(w->fd, 0, SEEK_CUR);

    int n = attempt_full_write(w->fd, w->block, w->used);

    if (n == -1 || n != w->used) {
      DEBUG_LOG("Error in flushing block");
      return -1;
    }

    struct sparse_idx_entry_type entry = {.key = w->first_key,
                                          .offset = block_offset};

    idx_put(entry, w->idx_array);

    w->used = 0;
    memset(&w->first_key, 0, sizeof(w->first_key));
    memset(w->block, 0, sizeof(w->block));

  } else if (w->used == 0 && record_size > BLOCK_SIZE) {

    uint8_t big_block[record_size];
    memcpy(big_block + w->used, &key->length, sizeof(key->length));
    w->used += sizeof(key->length);
    memcpy(big_block + w->used, &value->length, sizeof(value->length));
    w->used += sizeof(value->length);
    memcpy(big_block + w->used, key->data, key->length);
    w->used += key->length;
    memcpy(big_block + w->used, value->data, value->length);
    w->used += value->length;

    off_t block_offset = lseek(w->fd, 0, SEEK_CUR);
    int n = attempt_full_write(w->fd, big_block, w->used);

    if (n == -1 || n != w->used) {
      DEBUG_LOG("Error in flushing block\n");
      return -1;
    }

    struct sparse_idx_entry_type entry = {.key = w->first_key,
                                          .offset = block_offset};

    idx_put(entry, w->idx_array);

    w->used = 0;
    memset(&w->first_key, 0, sizeof(w->first_key));
    memset(w->block, 0, sizeof(w->block));

    return 0;
  }

  memcpy(w->block + w->used, &key->length, sizeof(key->length));
  w->used += sizeof(key->length);
  memcpy(w->block + w->used, &value->length, sizeof(value->length));
  w->used += sizeof(value->length);
  memcpy(w->block + w->used, key->data, key->length);
  w->used += key->length;
  memcpy(w->block + w->used, value->data, value->length);
  w->used += value->length;

  return 0;
}

int sstable_flush(struct memtable_type *mt, const char *sstable_path) {

  char sstable_temp_path[MAX_PATH_LENGTH];
  snprintf(sstable_temp_path, sizeof(sstable_temp_path), "%s.tmp",
           sstable_path);

  int sstable_fd = open(sstable_temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (sstable_fd < 0) {
    return -1;
  }

  struct sstable_header_type header = {0};
  attempt_full_write(sstable_fd, &header, sizeof(header));

  struct skiplist_iter *sl_iter = skiplist_iter_create(mt_skiplist(mt));
  if (sl_iter == NULL) {
    DEBUG_LOG("Error in sl_iter creation in sstable_put\n");
    return -1;
  }

  struct sstable_writer_type *w = malloc(sizeof(*w));
  w->fd = sstable_fd;
  w->idx_array = idx_init(64);
  w->used = 0;
  memset(&w->first_key, 0, sizeof(w->first_key));

  while (skiplist_iter_valid(sl_iter)) {
    const struct slice_type *key = skiplist_iter_key(sl_iter);
    const struct slice_type *value = skiplist_iter_value(sl_iter);

    if (sstable_kv_writer(w, key, value) == -1) {
      close(sstable_fd);
      skiplist_iter_destroy(sl_iter);
      unlink(sstable_temp_path);
      DEBUG_LOG("Error in sstable_kv_writer in sstable_flush\n");
      return -1;
    }

    skiplist_iter_next(sl_iter);
  }

  if (w->used > 0) {
    off_t block_offset = lseek(sstable_fd, 0, SEEK_CUR);
    int n = attempt_full_write(sstable_fd, w->block, w->used);

    if (n == -1 || n != w->used) {
      DEBUG_LOG("Error in flushing block");
      skiplist_iter_destroy(sl_iter);
      close(sstable_fd);
      unlink(sstable_temp_path);
      return -1;
    }

    struct sparse_idx_entry_type entry = {.key = w->first_key,
                                          .offset = block_offset};
    idx_put(entry, w->idx_array);

    w->used = 0;
    memset(&w->first_key, 0, sizeof(w->first_key));
    memset(w->block, 0, sizeof(w->block));
  }

  skiplist_iter_destroy(sl_iter);

  size_t idx_offset = lseek(sstable_fd, 0, SEEK_CUR);
  for (size_t i = 0; i < w->idx_array->count; i++) {
    struct sparse_idx_entry_type entry = w->idx_array->entries[i];
    struct slice_type entry_key = entry.key;
    uint64_t entry_offset = entry.offset;

    int n = attempt_full_write(sstable_fd, &entry_key.length,
                               sizeof(entry_key.length));
    if (n == -1) {
      close(sstable_fd);
      unlink(sstable_temp_path);
      return -1;
    }
    n = attempt_full_write(sstable_fd, entry_key.data, entry_key.length);
    if (n == -1) {
      close(sstable_fd);
      unlink(sstable_temp_path);
      return -1;
    }
    n = attempt_full_write(sstable_fd, &entry.offset, sizeof(entry.offset));
    if (n == -1) {
      close(sstable_fd);
      unlink(sstable_temp_path);
      return -1;
    }
  }

  header.sparse_index_offset = idx_offset;
  header.idx_entries_count = w->idx_array->count;
  lseek(sstable_fd, 0, SEEK_SET);
  if (attempt_full_write(sstable_fd, &header, sizeof(header)) !=
      sizeof(header)) {
    close(sstable_fd);
    unlink(sstable_temp_path);
    DEBUG_LOG("Error in writing header\n");
    return -1;
  }

  free(w->idx_array->entries);
  free(w->idx_array);
  free(w);

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

  struct sstable_header_type header;
  int n = attempt_full_read(sstable_fd, &header, sizeof(header));
  if (n == -1 || n != sizeof(header)) {
    close(sstable_fd);
    DEBUG_LOG("Error in reading header in sstable_get\n");
    return -1;
  }

  off_t sparse_idx_offset = header.sparse_index_offset;
  off_t start_offset = lseek(sstable_fd, 0, SEEK_CUR),
        end_offset = sparse_idx_offset, temp_offset;
  struct slice_type temp_key;

  lseek(sstable_fd, sparse_idx_offset, SEEK_SET);

  for (size_t i = 0; i < header.idx_entries_count; i++) {
    int n = attempt_full_read(sstable_fd, &temp_key.length,
                              sizeof(temp_key.length));
    if (n == -1 || n != sizeof(temp_key.length)) {
      DEBUG_LOG("Error in reading sparse index in sstable_get\n");
      close(sstable_fd);
      return -1;
    }

    temp_key.data = malloc(temp_key.length);

    n = attempt_full_read(sstable_fd, temp_key.data, temp_key.length);
    if (n == -1 || n != temp_key.length) {
      DEBUG_LOG("Error in reading sparse index in sstable_get\n");
      close(sstable_fd);
      return -1;
    }

    n = attempt_full_read(sstable_fd, &temp_offset, sizeof(temp_offset));
    if (slice_cmp(target_key, &temp_key) >= 0) {
      start_offset = temp_offset;
    } else {
      end_offset = temp_offset;
      free(temp_key.data);
      break;
    }
    free(temp_key.data);
  }

  lseek(sstable_fd, start_offset, SEEK_SET);

  uint32_t key_length;
  uint32_t value_length;
  uint32_t lengths[2];

  while (lseek(sstable_fd, 0, SEEK_CUR) < end_offset) {
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
        DEBUG_LOG("Error in lseek in sstable_get\n");
        return -1;
      }
      continue;
    }

    uint8_t *buffer = malloc(key_length + value_length);
    if (buffer == NULL) {
      DEBUG_LOG("Error in buffer malloc in sstable_get\n");
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

  close(sstable_fd);

  return -1;
}

struct sstable_iter {
  int fd;

  struct slice_type key;
  struct slice_type value;

  uint64_t sparse_index_offset;

  int valid;
};

int sstable_iter_next(struct sstable_iter *sst_iter);

struct sstable_iter *sstable_iter_init(const char *path) {
  struct sstable_iter *sst_iter = malloc(sizeof(*sst_iter));
  if (sst_iter == NULL) {
    DEBUG_LOG("Error in sst_iter malloc in iter_init\n");
    return NULL;
  }

  sst_iter->fd = open(path, O_RDONLY);
  if (sst_iter->fd < 0) {
    DEBUG_LOG("error in fd open in sstable_iter_init\n");
    return NULL;
  }

  sst_iter->key.data = NULL;
  sst_iter->value.data = NULL;
  sst_iter->valid = 1;

  struct sstable_header_type header;
  int n = attempt_full_read(sst_iter->fd, &header, sizeof(header));

  if (n == -1 || n != sizeof(header)) {
    DEBUG_LOG("Error in reading header in sstable_iter_init\n");
    return NULL;
  }

  sst_iter->sparse_index_offset = header.sparse_index_offset;
  lseek(sst_iter->fd, sizeof(header), SEEK_SET);

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

  sst_iter->key.data = NULL;
  sst_iter->value.data = NULL;

  uint32_t key_length, value_length;

  off_t curr_offset = lseek(sst_iter->fd, 0, SEEK_CUR);
  if (curr_offset >= sst_iter->sparse_index_offset) {
    sst_iter->valid = 0;
    return 0;
  }

  ssize_t n = attempt_full_read(sst_iter->fd, &key_length, sizeof(key_length));
  if (n == 0) {
    sst_iter->valid = 0;
    return 0;
  }
  if (n == -1 || n != sizeof(key_length)) {
    DEBUG_LOG("Error in reading key_length in sstable_iter_next\n");
    return -1;
  }

  n = attempt_full_read(sst_iter->fd, &value_length, sizeof(value_length));
  if (n == 0) {
    sst_iter->valid = 0;
    return 0;
  }
  if (n == -1 || n != sizeof(value_length)) {
    DEBUG_LOG("Error in reading value_length in sstable_iter_next\n");
    return -1;
  }

  sst_iter->key.length = key_length;
  sst_iter->key.data = malloc(key_length);

  sst_iter->value.length = value_length;
  sst_iter->value.data = malloc(value_length);

  if (attempt_full_read(sst_iter->fd, sst_iter->key.data, key_length) !=
      key_length) {
    DEBUG_LOG("Error in reading key_data in sst_iter_next\n");
    return -1;
  }
  if (attempt_full_read(sst_iter->fd, sst_iter->value.data, value_length) !=
      value_length) {
    DEBUG_LOG("Error in reading value_data in sst_iter_next\n");
    return -1;
  }

  return 0;
}

void sstable_iter_destroy(struct sstable_iter *sst_iter) {
  if (sst_iter->key.data != NULL && sst_iter->value.data != NULL) {
    free(sst_iter->key.data);
    free(sst_iter->value.data);
  }
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
    DEBUG_LOG("Error in fd open in sstable_compact\n");
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

  struct sstable_writer_type *sst_writer = malloc(sizeof(*sst_writer));
  sst_writer->used = 0;
  sst_writer->fd = temp_fd;
  sst_writer->idx_array = idx_init(64);
  memset(&sst_writer->first_key, 0, sizeof(sst_writer->first_key));
  memset(&sst_writer->block, 0, sizeof(sst_writer->block));

  struct sstable_header_type header = {0};
  int n = attempt_full_write(temp_fd, &header, sizeof(header));
  if (n == -1 || n != sizeof(header)) {
    unlink(temp_sstable_path);
    close(temp_fd);
    sstable_iters_destroy(sst_iters, count);
    DEBUG_LOG("Error in writing header to temp file in sstable_comapct\n");
    return -1;
  }

  while (1) {
    ssize_t smallest = -1;

    for (size_t i = 0; i < count; i++) {
      if (!sst_iters[i]->valid) {
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

      if (slice_cmp(&sst_iters[i]->key, &sst_iters[smallest]->key) == 0) {
        winner = i;
      }
    }

    struct slice_type key = sst_iters[winner]->key;
    struct slice_type value = sst_iters[winner]->value;

    if (sstable_kv_writer(sst_writer, &key, &value) == -1) {
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

  if (sst_writer->used > 0) {
    off_t block_offset = lseek(temp_fd, 0, SEEK_CUR);
    int n = attempt_full_write(temp_fd, sst_writer->block, sst_writer->used);

    if (n == -1 || n != sst_writer->used) {
      DEBUG_LOG("Error in flushing block");
      sstable_iters_destroy(sst_iters, count);
      close(temp_fd);
      unlink(temp_sstable_path);
      return -1;
    }

    struct sparse_idx_entry_type entry = {.key = sst_writer->first_key,
                                          .offset = block_offset};
    idx_put(entry, sst_writer->idx_array);

    sst_writer->used = 0;
    memset(&sst_writer->first_key, 0, sizeof(sst_writer->first_key));
    memset(sst_writer->block, 0, sizeof(sst_writer->block));
  }

  sstable_iters_destroy(sst_iters, count);

  size_t idx_offset = lseek(temp_fd, 0, SEEK_CUR);
  for (size_t i = 0; i < sst_writer->idx_array->count; i++) {
    struct sparse_idx_entry_type entry = sst_writer->idx_array->entries[i];
    struct slice_type entry_key = entry.key;
    uint64_t entry_offset = entry.offset;

    int n = attempt_full_write(temp_fd, &entry_key.length,
                               sizeof(entry_key.length));
    if (n == -1) {
      close(temp_fd);
      unlink(temp_sstable_path);
      return -1;
    }
    n = attempt_full_write(temp_fd, entry_key.data, entry_key.length);
    if (n == -1) {
      close(temp_fd);
      unlink(temp_sstable_path);
      return -1;
    }
    n = attempt_full_write(temp_fd, &entry.offset, sizeof(entry.offset));
    if (n == -1) {
      close(temp_fd);
      unlink(temp_sstable_path);
      return -1;
    }
  }

  header.sparse_index_offset = idx_offset;
  header.idx_entries_count = sst_writer->idx_array->count;
  lseek(temp_fd, 0, SEEK_SET);
  if (attempt_full_write(temp_fd, &header, sizeof(header)) != sizeof(header)) {
    close(temp_fd);
    unlink(temp_sstable_path);
    DEBUG_LOG("Error in writing header\n");
    return -1;
  }

  free(sst_writer->idx_array->entries);
  free(sst_writer->idx_array);
  free(sst_writer);

  if (fdatasync(temp_fd) == -1) {
    close(temp_fd);
    unlink(temp_sstable_path);
    return -1;
  }

  close(temp_fd);

  char final_path[MAX_PATH_LENGTH];
  snprintf(final_path, sizeof(final_path), "%s.sstable", output_path);

  if (rename(temp_sstable_path, final_path) == -1) {
    unlink(temp_sstable_path);
    return -1;
  }

  char dir[MAX_PATH_LENGTH];
  char *slash = strrchr(output_path, '/');

  size_t length = slash - output_path;
  memcpy(dir, output_path, length);
  dir[length] = '\0';

  int dir_fd = open(dir, O_RDONLY);
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
