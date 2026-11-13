#ifndef LEAFTL_MAPPING_H_
#define LEAFTL_MAPPING_H_
#include "leaftl_index.h"
typedef enum {
    LEAFTL_MAPPING_OK=0,LEAFTL_MAPPING_MISS=1,LEAFTL_MAPPING_INVALID=-1,
    LEAFTL_MAPPING_NO_SPACE=-2,LEAFTL_MAPPING_BUSY=-3,LEAFTL_MAPPING_STALE=-4,
    LEAFTL_MAPPING_PROGRAM_FAILED=-5,LEAFTL_MAPPING_TOKEN_EXHAUSTED=-6,
    LEAFTL_MAPPING_IO_ERROR=-7
} LEAFTL_MAPPING_STATUS;
typedef struct {
    struct LEAFTL_CACHE *cache;
    LEAFTL_INDEX *index;
    LEAFTL_INDEX_WORKSPACE *workspace;
    LEAFTL_TRAINING_POINT *pending_points;
    LEAFTL_SEGMENT_INPUT *pending_segments;
    unsigned int capacity,logical_count,next_token,pending_token;
    unsigned int pending_count,pending_segment_count,initialized;
} LEAFTL_MAPPING;

/* One exclusive owner per index; all arrays/work/context disjoint and live.
 * logical_count is the host-visible LPN count, NOT the physical geometry.
 * Buffers are caller-owned; no heap, Flash I/O or stack-sized arrays here.
 */
LEAFTL_MAPPING_STATUS LeaFtlMappingInit(LEAFTL_MAPPING *mapping,LEAFTL_INDEX *index,
    LEAFTL_INDEX_WORKSPACE *workspace,LEAFTL_TRAINING_POINT *points,
    LEAFTL_SEGMENT_INPUT *segments,unsigned int capacity,unsigned int logical_count);
/* Published model PPN only. Pending unprogrammed PPN is NEVER exposed here.
 * Reads of pending data must use the data buffer in later integration.
 * With gamma>0 this is a PREDICTION, not an exact physical identity. Device
 * reads and old-page/GC comparisons must verify/correct it through OOB.
 */
LEAFTL_MAPPING_STATUS LeaFtlMappingLookup(LEAFTL_MAPPING *mapping,
    unsigned int lpn,unsigned int *vsa);
/* Strictly sorted unique LPNs in one group, already allocated REAL model PPNs.
 * Train, validate quantization, preflight capacity, then mark pending.
 * token output written only on OK; no published map changes on prepare.
 */
LEAFTL_MAPPING_STATUS LeaFtlMappingPrepare(LEAFTL_MAPPING *mapping,
    const LEAFTL_TRAINING_POINT *points,unsigned int count,unsigned int *token);
/* Call only after ALL programs in the batch have completed successfully.
 * success=0 aborts the whole batch with PROGRAM_FAILED; published map stays.
 * A failed index commit retains pending state for explicit retry/abort.
 * It does not perform NAND rollback/invalidation or claim crash atomicity.
 */
LEAFTL_MAPPING_STATUS LeaFtlMappingComplete(LEAFTL_MAPPING *mapping,
    unsigned int token,int all_programs_succeeded);
LEAFTL_MAPPING_STATUS LeaFtlMappingAbort(LEAFTL_MAPPING *mapping,unsigned int token);
LEAFTL_MAPPING_STATUS LeaFtlMappingCompact(LEAFTL_MAPPING *mapping,unsigned int group_id,
    LEAFTL_INDEX_COMPACTION_RESULT *result);
#endif
