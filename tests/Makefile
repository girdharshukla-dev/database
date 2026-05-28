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


clean:
	rm -rf ../build/*