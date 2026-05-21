#define _POSIX_C_SOURCE 199309L

#include "wal.h"
#include "slice.h"
#include "memtable.h"
#include "io_functions.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>
#include <string.h>

struct wal_type *wal_open(const char *path) {
  struct wal_type *wal = malloc(sizeof(struct wal_type));
  if (!wal)
    return NULL;

  snprintf(wal->path, sizeof(wal->path), "%s", path);

  wal->fd = open(path, O_CREAT | O_RDWR | O_APPEND, 0644);
  if (wal->fd < 0) {
    free(wal);
    return NULL;
  }

  return wal;
}

void wal_close(struct wal_type *wal) {
  close(wal->fd);
  free(wal);
}

int wal_append(struct wal_type *wal, const struct slice_type *key,
               const struct slice_type *value) {
  uint32_t key_length = key->length;
  uint32_t value_length = value->length;
  uint32_t crc;

  // the wal record format is [key_length][val_length][key][val][crc]
  int buffer_size = sizeof(key_length) + sizeof(value_length) + key_length +
                    value_length + sizeof(crc);

  uint8_t *buffer = malloc(buffer_size);
  if (!buffer)
    return -1;

  uint8_t *temp = buffer;
  memcpy(temp, &key_length, sizeof(key_length));
  temp += sizeof(key_length);
  memcpy(temp, &value_length, sizeof(value_length));
  temp += sizeof(value_length);
  memcpy(temp, key->data, key_length);
  temp += key_length;
  memcpy(temp, value->data, value_length);
  temp += value_length;

  crc = crc32(0, buffer, buffer_size - sizeof(crc));

  memcpy(temp, &crc, sizeof(crc));

  ssize_t res = attempt_full_write(wal->fd, buffer, buffer_size);
  if (res == -1 || res != buffer_size)
    return -1;
  return 0;
}

int wal_sync(struct wal_type *wal) { return fdatasync(wal->fd); }

int wal_replay(struct wal_type *wal, struct memtable_type *mt) {
  lseek(wal->fd, 0, SEEK_SET);
  while (1) {
    uint32_t key_length, value_length;
    // if (attempt_full_read(wal->fd, &key_length, sizeof(key_length)) !=
    //     sizeof(key_length))
    //   return -1;
    // if (attempt_full_read(wal->fd, &value_length, sizeof(value_length)) !=
    //     sizeof(value_length))
    //   return -1;

    ssize_t n = attempt_full_read(wal->fd, &key_length, sizeof(key_length));
    if (n == 0) { // EOF
      break;
    }
    if (n != sizeof(key_length))
      return -1;

    n = attempt_full_read(wal->fd, &value_length, sizeof(value_length));
    if (n == 0) { // EOF
      break;
    }
    if (n != sizeof(value_length))
      return -1;

    int buffer_size =
        sizeof(key_length) + sizeof(value_length) + key_length + value_length;
    uint8_t *buffer = malloc(buffer_size);
    memcpy(buffer, &key_length, sizeof(key_length));
    memcpy(buffer + sizeof(key_length), &value_length, sizeof(value_length));

    if (attempt_full_read(
            wal->fd, buffer + sizeof(key_length) + sizeof(value_length),
            key_length + value_length) != key_length + value_length) {
      free(buffer);
      return -1;
    }

    uint32_t crc = crc32(0, buffer, buffer_size);
    uint32_t stored_crc;
    if (attempt_full_read(wal->fd, &stored_crc, sizeof(stored_crc)) !=
        sizeof(stored_crc)) {
      free(buffer);
      return -1;
    }
    if (crc != stored_crc) {
      free(buffer);
      return -1;
    }

    uint8_t *ptr = buffer;

    ptr += sizeof(key_length) + sizeof(value_length);

    const struct slice_type key = {.data = ptr, .length = key_length};

    const struct slice_type value = {.data = ptr + key_length,
                                     .length = value_length};

    if (memtable_put(mt, &key, &value) == -1) {
      free(buffer);
      return -1;
    }

    free(buffer);
  }
  return 0;
}
