#ifndef LEAFTL_STATS_H_
#define LEAFTL_STATS_H_
#include <stdint.h>
/* Single firmware thread; reset only at an idle experiment boundary.
 * No per-LPN counters, tracing buffers, timers or allocation. */
typedef struct {
    uint64_t host_read_bytes,host_write_bytes;
    uint64_t data_reads,data_programs,metadata_reads,metadata_programs;
    uint64_t gc_programs,erases,corrections,io_failures;
    uint64_t syncs,sync_failures;
} LEAFTL_STATS;
typedef struct {
    unsigned int groups,levels,segments,cache_bytes,arena_bytes;
    unsigned int segment_bytes,node_bytes,directory_bytes;
    unsigned int legacy_forward_bytes,legacy_reverse_bytes;
} LEAFTL_STATS_MEMORY;
void LeaFtlStatsReset(void);
const LEAFTL_STATS *LeaFtlStatsGet(void);
void LeaFtlStatsHost(unsigned int write,unsigned int bytes);
int LeaFtlStatsMemory(LEAFTL_STATS_MEMORY *);
/* WAF = (data_programs + metadata_programs) * NAND page bytes /
 * host_write_bytes. gc_programs is a SUBSET of data_programs. */
#endif
