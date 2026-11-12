#!/bin/bash
# LearnedFTL paper FIO procedure (HPCA'24, Section IV-B). Also the preconditioning
# before Filebench and RocksDB: they start from the state this leaves behind.
#   512KB sequential write x6 -> 4KB seq read -> 512KB random write 192GB (6x)
#   -> 4KB random read -> 4KB random write -> 4KB sequential write
# 4KB phases: psync, 64 jobs. Run on a freshly loaded firmware (empty SSD).
# Usage: paper_fio.sh
. "$(dirname "$0")/common.sh"
find_board
new_run fio
note "paper_fio start ($FTL on $DEV)"
P4K="--bs=4k --ioengine=psync --numjobs=64 --time_based"
fio_phase seqwarm   --rw=write     --bs=512k --ioengine=libaio --iodepth=32 --size=100% --loops=6
fio_phase seqread   --rw=read      $P4K --size=512M --offset_increment=512M --runtime=300
fio_phase randwarm  --rw=randwrite --bs=512k --ioengine=libaio --iodepth=32 --size=100% --io_size=192G --norandommap --randrepeat=0
fio_phase randread  --rw=randread  $P4K --size=100% --runtime=300
fio_phase randwrite --rw=randwrite $P4K --size=100% --runtime=600 --norandommap --randrepeat=0
fio_phase seqwrite  --rw=write     $P4K --size=512M --offset_increment=512M --runtime=600
note "ALL DONE"
