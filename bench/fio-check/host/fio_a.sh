#!/bin/bash
# FTL implementation check, part a (no GC). Run on a freshly loaded firmware (empty SSD).
#   seqwrite: 128K, QD4, 1 job, 32GB (whole SSD; also the fill the reads need)
#   seqread : 128K, QD4, 4 jobs, 8GB each (each job reads its quarter)
#   randread: 4K, QD32, 6 jobs, 5GB each over the whole SSD
# libaio, direct I/O, every phase ends by IO amount (no time_based/runtime).
# Uses the settings and helpers of ../../learnedftl-paper/host (config.sh, common.sh).
. "$(dirname "$0")/../../learnedftl-paper/host/common.sh"
find_board
new_run fio_a
note "fio_a start ($FTL on $DEV)"
SEQ="--bs=128k --ioengine=libaio --iodepth=4"
RND="--bs=4k --ioengine=libaio --iodepth=32 --numjobs=6"
fio_phase seqwrite --rw=write    $SEQ --numjobs=1 --size=100%
fio_phase seqread  --rw=read     $SEQ --numjobs=4 --size=8g --offset_increment=8g
fio_phase randread --rw=randread $RND --size=100% --io_size=5g --norandommap --randrepeat=0
note "ALL DONE"
