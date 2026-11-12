#!/bin/bash
# Filebench, paper Table I: fileserver, webserver, varmail, 600 s each.
# Each workload gets a fresh ext4 (the firmware has no TRIM, so the SSD keeps the
# state the previous step left). Run after paper_fio.sh, without reloading firmware.
# Usage: fb_bench.sh
. "$(dirname "$0")/common.sh"
find_board
new_run fb
note "fb_bench start ($FTL on $DEV)"
echo 0 | sudo -n /usr/bin/tee /proc/sys/kernel/randomize_va_space > /dev/null	# filebench needs it
#     workload   files   mean size threads
for spec in "fileserver 225000 131072 50" "webserver 825000 16384 64" "varmail 475000 16384 64"; do
	set -- $spec
	fresh_fs "$1"
	sed -e "s|^set \$dir=.*|set \$dir=$MNT|" -e "s|^set \$nfiles=.*|set \$nfiles=$2|" \
	    -e "s|mean:[0-9]*;|mean:$3;|" -e "s|^set \$nthreads=.*|set \$nthreads=$4|" \
	    -e "s|^run .*|run 600|" "$FB_WORKLOADS/$1.f" > "$OUT/$1.f"
	note "START filebench $1"
	$LIMIT "$FILEBENCH" -f "$OUT/$1.f" > "$OUT/filebench_$1.txt" 2>&1
	note "END   filebench $1 rc=$?"
	grep -E "IO Summary|rror" "$OUT/filebench_$1.txt" | tail -3 | tee -a "$LOG"
	df -h "$MNT" | tail -1 | tee -a "$LOG"
	sleep 20
done
mountpoint -q "$MNT" && sudo -n /usr/bin/umount "$MNT"
note "ALL DONE"
