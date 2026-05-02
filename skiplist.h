#ifndef SKIPLIST_H
#define SKIPLIST_H

#include "slice.h"

struct skiplist_type;
struct skiplist_type *skiplist_create();
void skiplist_destroy(struct skiplist_type *sl);

int skiplist_insert(struct skiplist_type *sl, const struct slice_type *key, const struct slice_type *value);
int skiplist_get(struct skiplist_type *sl, const struct slice_type *key, struct slice_type *value);


#endif