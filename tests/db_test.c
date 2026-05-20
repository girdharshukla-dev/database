#include "db.h"

#include <string.h>
#include <stdio.h>

int main(void) {
  const struct slice_type key = {.data = (uint8_t *)"name", .length = strlen("name")};
  const struct slice_type value = {.data = (uint8_t *)"girdhar", .length = strlen("girdhar")};
  
  struct db_type *db = db_open("wal.log");
  int res = db_put(db, &key, &value);
  if(res == -1){
    printf("db_put failed\n");
    return -1;
  }else{
    printf("db_put success\n");
  }
  
  struct slice_type val;
  res = db_get(db, &key, &val);
  if(res == -1){
    printf("db_get failed\n");
    return -1;
  }else{
    printf("db_get passes with val as: %.*s\n", val.length, val.data);
  }
  
  const struct slice_type new_value = {.data = (uint8_t *)"shukla", .length = strlen("shukla")};
  db_put(db, &key, &new_value);

  db_close(db);

  db = db_open("wal.log");
  memset(&val, 0, sizeof(val));
  res = db_get(db, &key, &val);
  if(res == -1){
    printf("db_get after wal_replay failed\n");
    return -1;
  }else{
    printf("db_get after wal_replay passes with val as: %.*s\n", val.length, val.data);
  }


  return 0;
}
