#include "db.h"

#include <string.h>
#include <stdio.h>

int main(void) {
  struct db_type *db = db_open("./testdb");

  if(db == NULL){
    fprintf(stderr, "Error in db_open\n");
    return -1;
  }

  struct slice_type key, value, out;

  char kbuf[64], vbuf[64];

  for(int i = 0; i < 200; i++){
    snprintf(kbuf, sizeof(kbuf), "key_%d", i);
    snprintf(vbuf, sizeof(vbuf), "value_%d", i);
    
    key.data = (uint8_t *)kbuf;
    key.length = strlen(kbuf);

    value.data = (uint8_t *)vbuf;
    value.length = strlen(vbuf);

    if(db_put(db, &key, &value) == -1){
      fprintf(stderr, "Error in db_put at: %d", i);
      return -1;
    }
  }

  for(int i = 150; i <= 170; i++){
    snprintf(kbuf, sizeof(kbuf), "key_%d", i);
    snprintf(vbuf, sizeof(vbuf), "updated_%d", i);
    
    key.data = (uint8_t *)kbuf;
    key.length = strlen(kbuf);

    value.data = (uint8_t *)vbuf;
    value.length = strlen(vbuf);

    if(db_put(db, &key, &value) == -1){
      fprintf(stderr, "Error in db_put while update at: %d", i);
      return -1;
    }
  }

  for(int i = 0; i < 200; i++){
    snprintf(kbuf, sizeof(kbuf), "key_%d", i);

    key.data = (uint8_t *)kbuf;
    key.length = strlen(kbuf);

    if(db_get(db, &key, &out) == -1){
      fprintf(stderr, "Error in db_get for: %d", i);
      return -1;
    }

    if(i >= 150 && i <= 170){
      snprintf(vbuf, sizeof(vbuf), "updated_%d", i);
    }else{
      snprintf(vbuf, sizeof(vbuf), "value_%d", i);
    }

    if(out.length != strlen(vbuf) || memcmp(out.data, vbuf, out.length) != 0){
      fprintf(stderr, "Error in key->value value in db_get\n");
      return -1;
    }
  }

  db_close(db);

  db = db_open("./testdb");
  if(db == NULL){
    fprintf(stderr, "Error in db_open for recovery\n");
    return -1;
  }

  for(int i = 0; i < 200; i++){
    snprintf(kbuf, sizeof(kbuf), "key_%d", i);

    key.data = (uint8_t *)kbuf;
    key.length = strlen(kbuf);

    if(db_get(db, &key, &out) == -1){
      fprintf(stderr, "Error in db_get for: %d", i);
      return -1;
    }

    if(i >= 150 && i <= 170){
      snprintf(vbuf, sizeof(vbuf), "updated_%d", i);
    }else{
      snprintf(vbuf, sizeof(vbuf), "value_%d", i);
    }

    if(out.length != strlen(vbuf) || memcmp(out.data, vbuf, out.length) != 0){
      fprintf(stderr, "Error in key->value value in db_get\n");
      return -1;
    }

    // printf("KEY: %.*s , VALUE: %.*s , (out length = %zu)\n", (int)key.length, (char *)key.data, (int)out.length, (char *)out.data, out.length);
  }

  db_close(db);

  printf("All tests passed\n");

  return 0;
}
