#ifndef SLICE_H
#define SLICE_H

#include <stdint.h>

struct slice_type{
    uint8_t *data;
    uint64_t length;
};

#endif