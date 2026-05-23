#ifndef SKIPLIST_H
#define SKIPLIST_H

#include "slice.h"

struct skiplist_type;
struct arena_type;

struct skiplist_type *skiplist_create(struct arena_type *arena);

int skiplist_insert(struct skiplist_type *sl, const struct slice_type *key, const struct slice_type *value);
int skiplist_get(struct skiplist_type *sl, const struct slice_type *key, struct slice_type *value);
void skiplist_destroy(struct skiplist_type *sl);
int slice_cmp(const struct slice_type *a, const struct slice_type *b);


struct skiplist_iter;

struct skiplist_iter *skiplist_iter_create(struct skiplist_type *sl);
int skiplist_iter_valid(struct skiplist_iter *sl_iter);
void skiplist_iter_next(struct skiplist_iter *sl_iter);
const struct slice_type *skiplist_iter_key(const struct skiplist_iter *sl_iter);
const struct slice_type *skiplist_iter_value(const struct skiplist_iter *sl_iter);
void skiplist_iter_destroy(struct skiplist_iter *sl_iter);


#endif
