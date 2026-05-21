#include "skiplist.h"
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define SKIPLIST_MAX_LEVEL_COUNT 16

struct sl_node {
  struct slice_type key;
  struct slice_type value;
  struct sl_node **next;
};

struct skiplist_type {
  struct arena_type *arena;
  struct sl_node *head;
  int max_level;
};

int slice_cmp(const struct slice_type *a, const struct slice_type *b) {
  uint64_t min = a->length < b->length ? a->length : b->length;
  int r = memcmp(a->data, b->data, min);
  if (r != 0)
    return r;

  if (a->length < b->length)
    return -1;
  else if (a->length > b->length)
    return 1;
  return 0;
}

static int random_level() {
  int level = 0;
  while ((rand() & 1) && level < SKIPLIST_MAX_LEVEL_COUNT - 1) {
    level++;
  }
  return level;
}

static struct sl_node *create_node(struct slice_type key,
                                   struct slice_type value, int level,
                                   struct arena_type *arena) {
  struct sl_node *n = arena_alloc(arena, sizeof(struct sl_node));
  if (n == NULL) {
    fprintf(stderr, "Error in allocating memory to sl_node\n");
    return NULL;
  }
  
  n->key = key;
  n->value = value;
  
  // in previous versions this used arena, but now since the levels are randomized ... the chosen
  // levles during replay might be greater that previous during original inserts thus exceeding 
  // the memtable size and breaking the assume 1 wal <-> 1 memtable
  n->next = malloc((level + 1) * sizeof(struct sl_node *));
  
  if (n->next == NULL) {
    fprintf(stderr, "Error in allocating memory to sl_node->next\n");
    return NULL;
  }

  for (size_t i = 0; i <= level; i++) {
    n->next[i] = NULL;
  }
  return n;
}

struct skiplist_type *skiplist_create(struct arena_type *arena) {
  struct skiplist_type *sl = arena_alloc(arena, sizeof(struct skiplist_type));
  if (sl == NULL) {
    fprintf(stderr, "Error in allocating memory to skiplist\n");
    return NULL;
  }
  sl->arena = arena;
  struct slice_type dummy_key = {.data = NULL, .length = 0};
  struct slice_type dummy_value = {.data = NULL, .length = 0};
  sl->head =
      create_node(dummy_key, dummy_value, SKIPLIST_MAX_LEVEL_COUNT - 1, arena);
  if (sl->head == NULL) {
    return NULL;
  }
  sl->max_level = SKIPLIST_MAX_LEVEL_COUNT - 1;
  return sl;
}

static void find_less_than_or_equal_to(struct skiplist_type *sl,
                                       const struct slice_type *key,
                                       struct sl_node *pred[]) {
  struct sl_node *cur = sl->head;
  for (int i = sl->max_level; i >= 0; i--) {
    while (cur->next[i] != NULL && slice_cmp(&cur->next[i]->key, key) < 0)
      cur = cur->next[i];
    pred[i] = cur;
  }
}

int skiplist_insert(struct skiplist_type *sl, const struct slice_type *key,
                    const struct slice_type *value) {
  struct sl_node *preds[SKIPLIST_MAX_LEVEL_COUNT];
  find_less_than_or_equal_to(sl, key, preds);

  if (preds[0]->next[0] != NULL &&
      slice_cmp(&preds[0]->next[0]->key, key) == 0) {
    preds[0]->next[0]->value = *value;
    return 0;
  }

  int new_level = random_level(); // this is the level index not level count
  struct sl_node *n = create_node(*key, *value, new_level, sl->arena);
  if(n == NULL){
    return -1;
  }

  for (int i = 0; i <= new_level; i++) {
    n->next[i] = preds[i]->next[i];
    preds[i]->next[i] = n;
  }
  return 0;
}

int skiplist_get(struct skiplist_type *sl, const struct slice_type *key,
                 struct slice_type *value) {
  struct sl_node *cur = sl->head;
  for (int i = SKIPLIST_MAX_LEVEL_COUNT - 1; i >= 0; i--) {
    while (cur->next[i] != NULL && slice_cmp(&cur->next[i]->key, key) < 0)
      cur = cur->next[i];
    if (cur->next[i] != NULL && slice_cmp(&cur->next[i]->key, key) == 0) {
      *value = cur->next[i]->value;
      return 0;
    }
  }
  return -1;
}

void skiplist_destroy(struct skiplist_type *sl){
  struct sl_node *cur = sl->head;
  while(cur != NULL){
    struct sl_node *next = cur->next[0];
    free(cur->next);
    cur = next;
  }
}

