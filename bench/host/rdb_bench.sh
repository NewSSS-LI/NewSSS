#!/bin/bash
# RocksDB db_bench, paper Section IV-D: 1 thread, fillseq -> overwrite ->
# readrandom (600 s) -> readseq (600 s). 10M keys x (16B + 1000B) is about a third
# of 32GB; 20M ran out of space in overwrite (compaction keeps old and new SSTs).
# Run after fb_bench.sh, without reloading firmware.
# Usage: rdb_bench.sh
. "$(dirname "$0")/common.sh"
find_board
new_run rdb
fresh_fs rdb
note "rdb_bench start ($FTL on $DEV)"
COMMON="--db=$MNT/rocksdb --num=10000000 --key_size=16 --value_size=1000 --compression_type=none --threads=1 --use_direct_reads=true --use_direct_io_for_flush_and_compaction=true --histogram=1"
for b in fillseq overwrite readrandom readseq; do
	extra=""
	[ $b != fillseq ] && extra="--use_existing_db=1"
	{ [ $b = readrandom ] || [ $b = readseq ]; } && extra="$extra --duration=600"
	note "START db_bench $b"
	$LIMIT "$DB_BENCH" $COMMON --benchmarks=$b $extra > "$OUT/dbbench_$b.txt" 2>&1
	note "END   db_bench $b rc=$?"
	grep -E "^$b " "$OUT/dbbench_$b.txt" | head -1 | tee -a "$LOG"
	df -h "$MNT" | tail -1 | tee -a "$LOG"
	sleep 20
done
mountpoint -q "$MNT" && sudo -n /usr/bin/umount "$MNT"
note "ALL DONE"
