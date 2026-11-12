#!/bin/bash
# The whole comparison for one firmware, in the order the results depend on:
# paper FIO procedure (also the preconditioning) -> Filebench -> RocksDB.
# Start it right after loading the firmware (empty SSD). Takes 10 to 15 hours.
# Usage: suite.sh
H="$(dirname "$0")"
echo "$(date "+%F %T") suite start"
"$H/paper_fio.sh" || { echo "$(date "+%F %T") SUITE FAILED in paper_fio"; exit 1; }
"$H/fb_bench.sh"  || { echo "$(date "+%F %T") SUITE FAILED in fb_bench"; exit 1; }
"$H/rdb_bench.sh" || { echo "$(date "+%F %T") SUITE FAILED in rdb_bench"; exit 1; }
echo "$(date "+%F %T") SUITE DONE"
