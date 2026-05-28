#!/bin/sh

set -e

RELEASE_DIR="build/release/"

mkdir -p "$RELEASE_DIR"

gcc -shared -fPIC -flto \
    db/db.c \
    db/wal.c \
    db/memtable.c \
    db/io_functions.c \
    db/skiplist.c \
    db/sstable.c \
    -I./db -O3 -o "$RELEASE_DIR/libkv.so" -lz

cp db/db.h db/slice.h "$RELEASE_DIR"

echo "Build generated in $RELEASE_DIR"

