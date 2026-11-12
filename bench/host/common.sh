#!/bin/bash
# Shared helpers for the host-side benchmark scripts. Sourced, not run.
#
# Every script that touches the SSD calls find_board first: it looks for exactly
# one NVMe namespace whose model and size are the Cosmos+ board in 32GB mode and
# refuses to run otherwise, so a host disk is never written by mistake.

HOST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$HOST_DIR/../.." && pwd)"

if [ ! -f "$HOST_DIR/config.sh" ]; then
	echo "config.sh not found: copy $HOST_DIR/config.example.sh to config.sh and fill it in" >&2
	exit 1
fi
. "$HOST_DIR/config.sh"

: "${FTL:?set FTL in config.sh}"
: "${BOARD_ID:?set BOARD_ID in config.sh}"
: "${RESULTS_DIR:?set RESULTS_DIR in config.sh}"
: "${TOOLS_DIR:?set TOOLS_DIR in config.sh}"
MNT=${MNT:-/mnt/ssd}
EXPECT_MODEL=${EXPECT_MODEL:-Cosmos+ OpenSSD}
EXPECT_SIZE=${EXPECT_SIZE:-34359738368}
EXPECT_PCI=${EXPECT_PCI:-}
MEM_LIMIT=${MEM_LIMIT:-2G}

FILEBENCH="$TOOLS_DIR/filebench/bin/filebench"
FB_WORKLOADS="$TOOLS_DIR/filebench/share/filebench/workloads"
DB_BENCH="$TOOLS_DIR/rocksdb/db_bench"

export XDG_RUNTIME_DIR=/run/user/$(id -u)
# Tool output in English whatever the host language, so meta.txt and the parsers
# see the same text on every host (free prints "메모리:" instead of "Mem:" in ko_KR).
export LC_ALL=C
# Filebench and db_bench run in a 2GB memory cgroup so the page cache cannot
# hide the SSD (the paper's host has 128GB of DRAM, the SSD 32GB).
LIMIT="systemd-run --user --scope -q -p MemoryMax=$MEM_LIMIT -p MemorySwapMax=0"

LOG=/dev/null
note() { echo "$(date "+%F %T") $*" | tee -a "$LOG"; }

# Sets DEV (/dev/nvmeXn1) or exits.
find_board() {
	local b model size pci found=()
	for b in /sys/block/nvme*n1; do
		[ -e "$b" ] || continue
		model=$(sed "s/ *$//" "$b/device/model" 2>/dev/null)
		size=$(( $(cat "$b/size") * 512 ))
		pci=$(basename "$(readlink -f "$b/device/device")")
		if [ "$model" = "$EXPECT_MODEL" ] && [ "$size" = "$EXPECT_SIZE" ] && { [ -z "$EXPECT_PCI" ] || [ "$pci" = "$EXPECT_PCI" ]; }; then
			found+=("$(basename "$b")")
		fi
	done
	if [ ${#found[@]} -ne 1 ]; then
		note "BOARD CHECK FAILED: ${#found[@]} device(s) match model [$EXPECT_MODEL], size $EXPECT_SIZE${EXPECT_PCI:+, PCI $EXPECT_PCI} -> abort"
		exit 1
	fi
	DEV=/dev/${found[0]}
}

# Makes $OUT, the result directory of one run, and records what was measured.
new_run() {
	local label=$1
	OUT="$RESULTS_DIR/$(date +%Y%m%d_%H%M)_${FTL}_${BOARD_ID}_$label"
	mkdir -p "$OUT"
	LOG="$OUT/phases.log"
	{
		echo "ftl=$FTL"
		echo "board_id=$BOARD_ID"
		echo "run=$label"
		echo "started=$(date "+%F %T %z")"
		echo "firmware_sha256=${FW_SHA256:-unknown}"
		echo "repo_commit=$(git -C "$REPO_DIR" rev-parse --short HEAD 2>/dev/null)$(git -C "$REPO_DIR" diff --quiet 2>/dev/null || echo -dirty)"
		echo "device=$DEV"
		echo "kernel=$(uname -r)"
		echo "os=$(. /etc/os-release; echo "$PRETTY_NAME")"
		echo "cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')"
		echo "mem_gb=$(free -g | awk '/^Mem/ {print $2}')"
		echo "scheduler=$(cat /sys/block/$(basename "$DEV")/queue/scheduler)"
		echo "fio=$(fio --version)"
		echo "filebench=$([ -x "$FILEBENCH" ] && echo 1.5-alpha3 || echo missing)"
		echo "db_bench=$("$DB_BENCH" --version 2>/dev/null | head -1)"
		echo "mem_limit=$MEM_LIMIT"
	} > "$OUT/meta.txt"
}

# Fresh ext4 on the board, mounted at $MNT and owned by this user, page cache dropped.
fresh_fs() {
	find_board
	mountpoint -q "$MNT" && sudo -n /usr/bin/umount "$MNT"
	sudo -n /usr/sbin/mkfs.ext4 -F "$DEV" > "$OUT/mkfs_$1.log" 2>&1 || { note "mkfs failed"; exit 1; }
	sudo -n /usr/bin/mount "$DEV" "$MNT" || { note "mount failed"; exit 1; }
	sudo -n /usr/bin/chown "$(id -un)" "$MNT"
	sync; echo 3 | sudo -n /usr/bin/tee /proc/sys/vm/drop_caches > /dev/null
	sleep 20
}

# One fio phase on the raw device. Output: text + JSON in $OUT/<name>.txt.
fio_phase() {
	local name=$1; shift
	find_board
	note "START $name"
	sudo -n /usr/bin/fio --name="$name" --filename="$DEV" --direct=1 --group_reporting \
		--output-format=normal,json --output="$OUT/$name.txt" "$@"
	local rc=$?
	note "END   $name rc=$rc"
	grep -E "^ *(read|write):|err=" "$OUT/$name.txt" | head -4 | tee -a "$LOG"
	sleep 20	# firmware may print statistics after 2 s of idle
	[ $rc -eq 0 ] || { note "FAILED"; exit 1; }
}
