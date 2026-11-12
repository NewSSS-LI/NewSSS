#!/bin/bash
# One-time host setup: packages, Filebench 1.5-alpha3 and RocksDB db_bench v8.11.4
# built under $TOOLS_DIR, the mount point, and the sudo rules the scripts need.
# Tested on Ubuntu 22.04 (fio 3.28). Usage: setup_host.sh
set -e
. "$(dirname "$0")/common.sh"

echo "== packages"
sudo apt-get update -q
sudo apt-get install -y -q build-essential git wget fio flex bison libtool automake autoconf \
	libgflags-dev python3

mkdir -p "$TOOLS_DIR" "$RESULTS_DIR"
cd "$TOOLS_DIR"

echo "== filebench 1.5-alpha3"
if [ ! -x "$FILEBENCH" ]; then
	wget -q https://github.com/filebench/filebench/releases/download/1.5-alpha3/filebench-1.5-alpha3.tar.gz
	tar xzf filebench-1.5-alpha3.tar.gz
	( cd filebench-1.5-alpha3 && ./configure -q --prefix="$TOOLS_DIR/filebench" && make -j"$(nproc)" > make.log 2>&1 && make install > install.log 2>&1 )
fi
ls -l "$FILEBENCH"

echo "== rocksdb db_bench v8.11.4"
if [ ! -x "$DB_BENCH" ]; then
	git clone -q --depth 1 --branch v8.11.4 https://github.com/facebook/rocksdb.git
	( cd rocksdb && DEBUG_LEVEL=0 make -j"$(nproc)" db_bench > make.log 2>&1 )
fi
"$DB_BENCH" --version | head -1

sudo mkdir -p "$MNT"

echo
echo "== sudo rules"
echo "The scripts call these with 'sudo -n' (no password prompt). Add them with"
echo "'sudo visudo -f /etc/sudoers.d/newsss-bench' (replace nvme0n1 with the board's"
echo "device from check_board.sh if it differs):"
U=$(id -un)
cat <<RULES
$U ALL=(root) NOPASSWD: /usr/bin/fio, /usr/bin/tee /sys/bus/pci/rescan, /usr/bin/tee /sys/bus/pci/devices/*/remove, /usr/bin/tee /proc/sys/vm/drop_caches, /usr/bin/tee /proc/sys/kernel/randomize_va_space, /usr/sbin/mkfs.ext4 -F /dev/nvme0n1, /usr/bin/mount /dev/nvme0n1 $MNT, /usr/bin/umount $MNT, /usr/bin/chown $U $MNT
RULES
echo
echo "Done. Check with: $(dirname "$0")/check_board.sh"
