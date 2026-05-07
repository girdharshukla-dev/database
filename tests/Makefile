make test:
	gcc -I../db \
	wal_test.c \
	../db/wal.c \
	../db/memtable.c \
	../db/io_functions.c \
	../db/skiplist.c \
	-o ../build/wal_test \
	-lz