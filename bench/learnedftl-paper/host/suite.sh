#!/bin/bash
# The whole comparison for one firmware, in the order the results depend on:
# paper FIO procedure (also the preconditioning) -> Filebench -> RocksDB, then a
# data check of the RocksDB DB. Start it right after loading the firmware (empty
# SSD). Takes 10 to 15 hours.
# Usage: suite.sh
H="$(dirname "$0")"
echo "$(date "+%F %T") suite start"
"$H/paper_fio.sh" || { echo "$(date "+%F %T") SUITE FAILED in paper_fio"; exit 1; }
"$H/fb_bench.sh"  || { echo "$(date "+%F %T") SUITE FAILED in fb_bench"; exit 1; }
"$H/rdb_bench.sh" || { echo "$(date "+%F %T") SUITE FAILED in rdb_bench"; exit 1; }
# Data check of the DB rdb_bench.sh left. It measures nothing, so a failed check
# does not fail the suite, but the RocksDB results above should not be used then.
if "$H/rdb_check.sh"; then
	echo "$(date "+%F %T") RDB CHECK PASSED"
else
	echo "$(date "+%F %T") RDB CHECK FAILED: do not use this run's RocksDB results, report it"
fi
echo "$(date "+%F %T") SUITE DONE"
