## Ideas

- immutable memtables
- wal rotation , with one wal corresponding to 1 memtable 
- sstable to be compacted size-tiered and not just the last 2 but more 
- memtable not to be flushed into the sstable as plain records but blocks(maybe 4kb pages), adding offsets/indexes for faster reads
- compaction should be done in the background .... compact sstable first into a temporary file 
- allow multiple clients .... serialize to a single writer
