#!/bin/bash
# FTL implementation check, part b (no GC). Run on a freshly loaded firmware (empty SSD),
# not after part a: a full SSD would need GC.
#   randwrite: 4K, QD32, 6 jobs over the whole SSD, IO amount per job below
# A 4K write is programmed as a whole 16KB page, so the flash takes about 4x the host
# writes; the amount keeps that (plus the FTL's translation-page writes) under the
# ~36.5GB of flash in 32GB mode, so no GC runs. It differs per FTL on purpose: the
# point is to check each implementation without GC, not a same-condition comparison.
# Override with RANDWRITE_IO_SIZE=<size per job>.
. "$(dirname "$0")/../../learnedftl-paper/host/common.sh"
case "$FTL" in
	greedyftl)  DEF=1280m ;;	# 7.5GB total, ~30GB on flash
	dftl|tpftl) DEF=512m  ;;	# 3GB total: translation-page writes stay below translation GC
	learnedftl) DEF=256m  ;;	# 1.5GB total: same for its translation lines
	*)          DEF=256m  ;;	# unknown FTL: smallest; check that no GC ran
esac
SIZE=${RANDWRITE_IO_SIZE:-$DEF}
find_board
new_run fio_b
echo "randwrite_io_size_per_job=$SIZE" >> "$OUT/meta.txt"
note "fio_b start ($FTL on $DEV, $SIZE per job)"
RND="--bs=4k --ioengine=libaio --iodepth=32 --numjobs=6"
fio_phase randwrite --rw=randwrite $RND --size=100% --io_size=$SIZE --norandommap --randrepeat=0
note "ALL DONE"
