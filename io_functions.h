#ifndef IO_H
#define IO_H

#include <unistd.h>

ssize_t attempt_full_write(int fd, const void *buf, size_t count);
ssize_t attempt_full_read(int fd, void *buf, size_t count);



#endif
