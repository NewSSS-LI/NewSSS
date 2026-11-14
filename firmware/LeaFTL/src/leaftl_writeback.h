#ifndef LEAFTL_WRITEBACK_H_
#define LEAFTL_WRITEBACK_H_
#include "leaftl_mapping.h"
/* Synchronous exclusive owner: drain DMA before collecting; do not relabel or
 * modify selected entries until Flush returns. Payload never moves. */
typedef struct { unsigned int id,lpn,old_ppn; } LEAFTL_FLUSH_ENTRY;
typedef struct {
    LEAFTL_FLUSH_ENTRY entries[LEAFTL_FLUSH_SORT_CAPACITY];
    LEAFTL_TRAINING_POINT points[LEAFTL_FLUSH_SORT_CAPACITY];
    unsigned int busy;
    /* Whole allocated run, including future groups/pages, for OOB neighbors. */
    unsigned int run_first_ppn,run_start,run_count;
} LEAFTL_WRITEBACK_WORK;
typedef struct {
    void *context;
    unsigned int buffer_count;
    void (*drain)(void *);
    /* 1 dirty and ready; 0 skip. Buffer cache must contain unique LPNs. */
    int (*inspect)(void *,unsigned int,unsigned int *);
    /* Reserve up to requested consecutive usable pages in ONE die/block.
     * Returns actual count, zero if exhausted. PPN is die-major, not VSA. */
    unsigned int (*reserve)(void *,unsigned int,unsigned int *);
    /* Must return only after NAND program completion; true iff successful. */
    int (*program)(void *,unsigned int,unsigned int,unsigned int);
    void (*publish)(void *,unsigned int,unsigned int,unsigned int,unsigned int);
    /* Retire unused reservation after prepare/program failure; never reuse it. */
    void (*abandon)(void *);
    /* Optional exact-address resolver: gamma>0 must verify predicted PPA
     * through OOB before old-page invalidation. NULL retains pure-model API. */
    LEAFTL_MAPPING_STATUS (*lookup)(void *,unsigned int lpn,unsigned int *actual_ppn);
} LEAFTL_WRITEBACK_IO;
LEAFTL_MAPPING_STATUS LeaFtlWritebackFlush(LEAFTL_MAPPING *,LEAFTL_WRITEBACK_WORK *,
    const LEAFTL_WRITEBACK_IO *,unsigned int *committed);
#endif
