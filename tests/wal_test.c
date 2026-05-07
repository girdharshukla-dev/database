#include "wal.h"
#include "slice.h"
#include "memtable.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static struct slice_type make_slice(const char *str) {
  struct slice_type slice = {.data = (uint8_t *)str, .length = strlen(str)};
  return slice;
}

int main(void) {
  const char *wal_path = "test.wal";
  unlink(wal_path);

  struct wal_type *wal = wal_open(wal_path);
  if (!wal) {
    fprintf(stderr, "Failed to open wal\n");
    return 1;
  }
  
  struct slice_type key1 = make_slice("name");
  struct slice_type val1 = make_slice("girdhar");
  
  struct slice_type key2 = make_slice("name2");
  struct slice_type val2 = make_slice("shukla");
  
  if(wal_append(wal, &key1, &val1) != 0){
    fprintf(stderr, "Failed to insert key1, val1\n");
    return 1;
  }
  
  if(wal_append(wal, &key2, &val2) != 0){
    fprintf(stderr, "Failed to insert key2, val1\n");
    return 1;
  }
  
  if(wal_sync(wal) != 0){
    fprintf(stderr, "Failed to sync wal\n");
    return 1;
  }
  
  wal_close(wal);
  printf("Wal append test case passed\n");
  
  wal = wal_open(wal_path);
  
  if (!wal) {
    fprintf(stderr, "Failed to open wal\n");
    return 1;
  }
  
  struct memtable_type *mt = memtable_create();
  if(!mt){
    fprintf(stderr, "Failed to create memtable\n");
    return 1;
  }

  int res = wal_replay(wal, mt);

  if(res != 0){
    fprintf(stderr, "Wal replay Failed\n");
    return 1;
  }

  printf("Wal replay test passed\n");

  struct slice_type lookup_key = make_slice("name");
  struct slice_type lookup_result;
  res = memtable_get(mt, &lookup_key, &lookup_result);
  if(res != 0){
    fprintf(stderr, "Lookup failed\n");
    return 1;
  }

  printf("Lookup result: %.*s\n", (int)lookup_result.length, lookup_result.data);
  
  wal_close(wal);
  unlink(wal_path);
  return 0;
  
}
