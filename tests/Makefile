make wal_test:
	gcc -I../db \
	wal_test.c \
	../db/wal.c \
	../db/memtable.c \
	../db/io_functions.c \
	../db/skiplist.c \
	-o ../build/wal_test \
	-lz

make db_test:
	gcc -I../db \
	db_test.c \
	../db/db.c \
	../db/wal.c \
	../db/memtable.c \
	../db/io_functions.c \
	../db/skiplist.c \
	-o ../build/db_test \
	-lz

make clean:
	rm ../build/*