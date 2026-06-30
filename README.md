# Database

### LSM-Tree based Key-Value storage engine

The storage engine is implemented as a LSM-Tree architecture using a WAL, an in-memory arena backed memtable 
as a skiplist, immutable SSTables and compaction.

[Architecture](#architecture)
- [DB initialization](#db-initialization)
- [Write path](#write-path)
- [Read path](#read-path)
- [Recovery](#recovery)

[Components](#components)
- [WAL](#wal-write-ahead-log)
- [Memtable](#memtable)
- [SSTable](#sstable)
- [Manifest](#manifesttxt)

[Build instructions and examples](#build-instructions)<br>
[Work in progress](#work-in-progress-)<br>
[Benchmarks](#benchmarks)

---

## Architecture

### DB initialization
1. When the user does a `db_open`, the engines tries to make the required directories(eg. if "./testdb" passed as path)
and others like `./testdb/wal` and `./testdb/sstable` if they don't exist.
2. Then it checks if `manifest.txt` exists in the directory. If yes, then it loads it else it creates one.
The `manifest.txt` contains information related to the state of the database such as the number of sstables, 
live sstables and the count of sstables. (For more info about manifest.txt look into the components section).
3. Then it performs a recovery of the previous database(by replaying the wal's) state if the `flush.log` and `active.log`
(active.wal is created after step 4).
(Refer the Recovery section for details)
4. It creates a active memtable in memory for the incoming writes.

### Write path
1. The engine first tries to do a insertion into the active memtable. Then it appends to the wal. Correctness is still handled. 
> `IMPORTANT NOTE` : This is different from the conventional way of first appending to the wal and then doing a memtable_insert, 
> since in this engine a single wal is expected to correspond to 1 memtable, so appending to an active 
> wal means it will definitely belong to the active memtables, though this might not be the case if the 
> active memtable has reached its maximum threshold size. 
> Correctness is still maintained through the fact that db_put only return success only after both memtable insert and the 
> wal append are completed (wal append done after a memtable flush if memtable max size reached).
2. If the memtable insert fails because of the active memtable reaching its threshold size, 
it freezes the active wal to `flush.log` and tries to flush it.
This is done to enable crash recovery if process dies during the flush. The active memtable is then flushed to a sstable.
3. After the flush, a new active memtable and an `active.log` created and the insert is tried again.

### Read path
1. The reads first hit the active memtable, if found then the result is returned else they go to the sstable lookup.
2. If sstable looked up, the sstable contains a sparse index at the end of the file to avoid 
naive linear scan of the whole sstable. (Details here [SSTable](#sstable))

### Recovery
Recovery is based on 1 hard rule -> a single .log file(wal) corresponds to exactly 1 memtable.
1. The recovery happens at the time of db_open, the engine checks for any `flush.log`.
If a `flush.log` exists it means the process failed during the flushing of memtable in the previous run.
The engine then tries to replay the `flush.log` and flush it again. 
2. Only after the flushing of `flush.log` succeeds, it first builds a active memtable in memory that will be used for subsequent inserts
and replays the `active.log` if it exists, into this active memtables.


## Components

### WAL (Write Ahead Log)
- The writes to the storage engines are appended to the wal to enable crash recovery.
- The format of the records appended to wal are as :
[key_length][value_length][key][value][crc]
- A crc is appended along with the data, to discover any corruption in wal.
- If the crc check fails while replay on a record, the whole wal after it is assumed to be corrupted 
and wal_replay is assumed to be a failure.
- In the current state of the project, the engine does a wal_sync on every wal append.
- When a memtable is to be flushed, the active wal `active.log` is renamed to `flush.log`.
- Then this `flush.log` is flushed. This is done to provide an explicit state that a flushing was interrupted in previous run.
- The `flush.log` currently not plays a crucial role with the current single threaded architecture, 
but will be helpful later when it will be directly flushed to a sstable without unnecessarily building 
a memtable first and then flushing it.

### Memtable
- The memtable is implemented as a skiplist.
- It is backed by a arena with the max size as defined in the `config.h` as `MEMTABLE_ARENA_SIZE`.
- Once the memtable reaches this max size, it is flushed to a sstable.

### SSTable

- The sstable are the persistent files of the db produced when a memtable is flushed to disk.
- The max number of sstables that can exist at an instant is defined by the `MAX_SSTABLE_COUNT` in `config.h`.
- When the count of sstables exceed the the `MAX_SSTABLE_COUNT`, `MAX_SSTABLES_COMPACTED`(also defined in `config.h`) 
number of oldest sstables are merged into one.
- The sstable file is structred as below:
```

[ header ] -> contains sparse index offset and number of sparse index entries
[ Block 1 ] -> multiple records of format
             [key_length][value_length][key_data][value_data]
[ Block 2 ]
[ Block 3 ]
...
...
...       -> each block has a target size of BLOCK_SIZE but it is not a
             strict threshold for a record that itself is greater than the BLOCK_SIZE
...       -> if the last record overflows the current block ... a new block is started and this   
             last record is moved to that new block
          -> if a record arrives that itself is bigger than the BLOCK_SIZE, a big_block of size
             relevant to the record is created and this record occupies this whole big_block
[ sparse index ] -> contains the first key of each block and its offset
               -> records as [key_length][key_data][offset]
[ EOF ]

```
- For the sstable loopkup during reads, first the sparse index is read and offset is set to the 
correct block which might contain the key
- This avoids naive linear scanning of whole sstable thus reducing read amplification.

### Manifest (manifest.txt)

- The `manifest.txt` file in the directory of the database is the file that helps in rebuilding the database 
structure at the time of db initialization.
- It contains information, like live sstables, next sstable id to be used and sstable count.
- This helps in avoiding deriving the state of db from the file system.
- When the sstables are merged, the merging is only said to be successful only if the generated sstable is registered in the manifest.


## Build instructions

> The engine depends on just `zlib` as the only third party dependency. (`zlib` is used for crc32 computation)

For the release build, just run the following command :
```bash
bash build.sh
```
This will output a libkv.so, slice.h and a db.h in the `build/release` folder.

To use it in your application code, you can just include the `db.h` and `slice.h` in your code and 
link against the `libkv.so` produced.
The data type to use for key and values is given in the slice.h.
```c
struct slice_type{
    uint8_t *data;
    uint32_t length;
};
```
The application is expected to pass a backing buffer for the data field. 
The engine expects a buffer for the data field and does a memcpy to the data field, it is on the user that the size of the buffer is big enough.

At this current stage the only apis available are `db_put` and `db_get`. `db_delete` will be added soon, 
but the semantics have already been decided and the engine identifies a `slice_type value` of `length = 0` as 
tombstone and treats it as a deleted key-value pair.

See the full example at examples/example.c.

An example code snippet is given as :
```c
struct person{
    char name[6];
    int age;
};

struct db_type *db = db_open("./example_db");
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

db_put(db, &key, &value);

struct person person_out;
struct slice_type out = {
    .data = (uint8_t *)&person_out,
    .length = sizeof(person_out),
};

db_get(db, &key, &out);
db_close(db);
```

Check out the full example at examples/example.c .
The application code can be compiled as :
```bash
gcc example.c -I. -L. -lkv -Wl,-rpath=. -O2 -o example
```
This expects the libkv.so, db.h and slice.h somewhere in the same directory as example.c .


## Benchmarks
To run benchmarks, run the following commands:
```bash
cd tests
make benchmark
cd ../build
./benchmark
```

## Work in progress :
- Optimising the sstable lookup by adding bloom filters.


