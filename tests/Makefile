wal_test:
	gcc -DDEBUG -I../db \
	wal_test.c \
	../db/wal.c \
	../db/memtable.c \
	../db/io_functions.c \
	../db/skiplist.c \
	-o ../build/wal_test \
	-lz

db_test:
	gcc -DDEBUG -I../db \
	db_test.c \
	../db/db.c \
	../db/wal.c \
	../db/memtable.c \
	../db/io_functions.c \
	../db/skiplist.c \
	../db/sstable.c \
	-o ../build/db_test \
	-lz

db_replay_temp_test:
	gcc -DDEBUG -I../db \
	db_replay_temp_test.c \
	../db/db.c \
	../db/wal.c \
	../db/memtable.c \
	../db/io_functions.c \
	../db/skiplist.c \
	../db/sstable.c \
	-o ../build/db_replay_temp_test \
	-lz

db_test_profile:
	gcc -DDEBUG -I../db \
	db_test.c \
	../db/db.c \
	../db/wal.c \
	../db/memtable.c \
	../db/io_functions.c \
	../db/skiplist.c \
	../db/sstable.c \
	-g -o ../build/db_test_profile \
	-lz && cd ../build/ && valgrind --tool=callgrind ./db_test_profile

db_debug_build:
	gcc -DDEBUG -shared -fPIC -flto \
    ../db/db.c \
    ../db/wal.c \
    ../db/memtable.c \
    ../db/io_functions.c \
    ../db/skiplist.c \
    ../db/sstable.c \
    -I../db -O3 -o "../build/libkv.so" -lz

benchmark:
	gcc -flto \
	benchmark.c \
    ../db/db.c \
    ../db/wal.c \
    ../db/memtable.c \
    ../db/io_functions.c \
    ../db/skiplist.c \
    ../db/sstable.c \
    -I../db -O3 -o "../build/benchmark" -lz


clean:
	rm -rf ../build/*