#ifndef DB_H
#define DB_H

#include "slice.h"

struct db_type;

struct db_type *db_open(const char *path);
int db_put(struct db_type *db, const struct slice_type *key, const struct slice_type *value);
int db_get(struct db_type *db, const struct slice_type *key, struct slice_type *value);

void db_close(struct db_type *db);

#endif
