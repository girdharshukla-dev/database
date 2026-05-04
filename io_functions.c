#include "io_functions.h"

#include <unistd.h>
#include <stdint.h>
#include <errno.h>

ssize_t attempt_full_write(int fd, const void *buf, size_t count) {
  size_t written = 0;
  uint8_t *ptr = (uint8_t *)buf;
  while (written < count) {
    ssize_t n = write(fd, ptr + written, count - written);
    if (n > 0) {
      written += n;
    } else if (n == -1 && errno == EINTR) {
      continue;
    } else {
      return -1;
    }
  }
  return written;
}

ssize_t attempt_full_read(int fd, void *buf, size_t count) {
  size_t readn = 0;
  uint8_t *ptr = (uint8_t *)buf;
  while (read < count) {
    ssize_t n = read(fd, ptr + readn, count - readn);
    if (n > 0) {
      readn += n;
    } else if (n == 0) {
      return readn;
    } else if (n == -1 && errno == EINTR) {
      continue;
    } else {
      return -1;
    }
  }

  return readn;
}
