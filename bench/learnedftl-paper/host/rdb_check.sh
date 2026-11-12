#!/bin/bash
# Checks the RocksDB DB that rdb_bench.sh left on the SSD: does the SSD return the
# data RocksDB wrote? db_bench readseq stops without any message when a read fails
# (it only checks iter->Valid()), so wrong data can look like a short readseq.
# verifychecksum reads every block of every SST file and fails on the first
# checksum mismatch; readseq then has to read all 10M keys.
# Run right after rdb_bench.sh, without reloading firmware (a reload empties the SSD).
# It runs after all measurements and changes none of them. Takes 10 to 20 minutes.
# Usage: rdb_check.sh
. "$(dirname "$0")/common.sh"
find_board
new_run rdbcheck
note "rdb_check start ($FTL on $DEV)"
mountpoint -q "$MNT" && sudo -n /usr/bin/umount "$MNT"
sudo -n /usr/bin/mount "$DEV" "$MNT" || { note "mount failed"; exit 1; }
[ -d "$MNT/rocksdb" ] || { note "no DB at $MNT/rocksdb: run rdb_bench.sh first"; sudo -n /usr/bin/umount "$MNT"; exit 1; }
du -sh "$MNT/rocksdb" | tee -a "$LOG"
sync; echo 3 | sudo -n /usr/bin/tee /proc/sys/vm/drop_caches > /dev/null
# Same DB options as rdb_bench.sh. Auto compaction is off so the check writes as
# little as possible.
COMMON="--db=$MNT/rocksdb --num=10000000 --key_size=16 --value_size=1000 --compression_type=none --threads=1 --use_direct_reads=true --use_direct_io_for_flush_and_compaction=true --use_existing_db=1 --disable_auto_compactions=true"
fail=0
for b in verifychecksum readseq; do
	note "START db_bench $b"
	$LIMIT "$DB_BENCH" $COMMON --benchmarks=$b > "$OUT/dbbench_$b.txt" 2>&1
	rc=$?
	note "END   db_bench $b rc=$rc"
	grep -E "VerifyChecksum\(\) failed|Corruption|^readseq " "$OUT/dbbench_$b.txt" | head -3 | tee -a "$LOG"
	[ $rc -eq 0 ] || fail=1
done
ops=$(sed -nE 's/^readseq .* ([0-9]+) operations;.*/\1/p' "$OUT/dbbench_readseq.txt")
note "readseq operations: ${ops:-none} (expected 10000000)"
[ "$ops" = 10000000 ] || fail=1
sudo -n /usr/bin/umount "$MNT"
if [ $fail -eq 0 ]; then note "CHECK PASSED"; else note "CHECK FAILED"; fi
note "ALL DONE"
exit $fail
