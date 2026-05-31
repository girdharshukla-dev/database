#include "slice.h"
#include "db.h"

#include <stdio.h>
#include <string.h>

struct person{
    char name[6];
    int age;
};

int main(void){
    struct db_type *db = db_open("./example_db");
    if(!db){
        fprintf(stderr, "Error in opening db\n");
        return -1;
    }

    struct person person_in = {
        .name = "hello",
        .age = 20,
    };

    struct slice_type key = {
        .data = (uint8_t *)"user:1",
        .length = strlen("user:1")
    };

    struct slice_type value = {
        .data = (uint8_t *)&person_in,
        .length = sizeof(person_in)
    };

    if(db_put(db, &key, &value) != -1){
        printf("Inserted person_in\n");
    }else{
        printf("Inserting person_in failed\n");
        return -1;
    }

    struct person person_out;
    struct slice_type out = {
        .data = (uint8_t *)&person_out,
        .length = sizeof(person_out),
    };

    if(db_get(db, &key, &out) != -1){
        printf("Name: %s\n", person_out.name);
        printf("Age: %d\n", person_out.age);
    }else{
        printf("Value not found\n");
    }

    db_close(db);

    return 0;
}


// To compile this : gcc example.c -I. -L. -lkv -Wl,-rpath=. -O2 -o example
// This expects the libkv.so, db.h and slice.h somewhere in the same directory as example.c

