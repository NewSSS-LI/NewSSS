/* LeaFTL port configuration. No BSP dependency and no memory allocation.
 * Address and resource contracts: docs/LEAFTL_STEP1.md.
 */
#ifndef LEAFTL_CONFIG_H_
#define LEAFTL_CONFIG_H_

#include <limits.h>

/* The existing firmware uses 32-bit unsigned int addresses. */
#if CHAR_BIT != 8 || UINT_MAX != 0xffffffffU
#error "LeaFTL requires 8-bit bytes and 32-bit unsigned int"
#endif

/* Fixed by the group-offset format; this is not a tuning parameter. */
#define LEAFTL_GROUP_SIZE                  256U
#define LEAFTL_GROUP_SHIFT                 8U
#define LEAFTL_GROUP_OFFSET_MASK           255U
#define LEAFTL_LPN_NONE                    0xffffffffU
#define LEAFTL_PPN_NONE                    0xffffffffU
#define LEAFTL_GROUP_ID(lpn)               ((lpn) / LEAFTL_GROUP_SIZE)
#define LEAFTL_GROUP_OFFSET(lpn)           ((lpn) % LEAFTL_GROUP_SIZE)
/* Valid for counts up to UINT_MAX, without count + 255 overflow. */
#define LEAFTL_GROUP_COUNT(lpn_count) \
    (((lpn_count) / LEAFTL_GROUP_SIZE) + \
     (((lpn_count) % LEAFTL_GROUP_SIZE) != 0U))

/* Stage 7-8 writeback: die-major usable-page coordinate, NOT raw NAND row.
 * Convert through LeaFtlPpnToVsa before existing bad-block/SLC row mapping.
 * Legacy DFTL continues to store interleaved VSA. */
#define LEAFTL_PPN_MODE_VSA                1U
#define LEAFTL_PPN_MODE_DIE_MAJOR          2U
#define LEAFTL_PPN_MODE                    LEAFTL_PPN_MODE_DIE_MAJOR
#ifndef LEAFTL_GAMMA
#define LEAFTL_GAMMA                       0U
#endif
#define LEAFTL_MAX_GAMMA 16U
#if LEAFTL_GAMMA > LEAFTL_MAX_GAMMA
#error "Gamma exceeds the supported OOB neighborhood"
#endif

/* Provisional bounds, not allocated arrays or proven sizeof totals.
 * All future allocators must reject/compact/evict on exhaustion; never
 * silently discard a mapping. Retained older levels count toward limits.
 */
#ifndef LEAFTL_MAX_CACHED_GROUPS
#define LEAFTL_MAX_CACHED_GROUPS            256U
#endif
#ifndef LEAFTL_MAX_LEVELS_PER_GROUP
#define LEAFTL_MAX_LEVELS_PER_GROUP         256U
#endif
#ifndef LEAFTL_MAX_SEGMENTS_PER_GROUP
#define LEAFTL_MAX_SEGMENTS_PER_GROUP       1024U
#endif
#ifndef LEAFTL_MAX_RESIDENT_SEGMENTS
#define LEAFTL_MAX_RESIDENT_SEGMENTS        65536U
#endif
/* Worst-case scratch input for one logical group; larger flush batches
 * must be partitioned by group (and any required physical run boundary).
 */
#define LEAFTL_MAX_TRAINING_POINTS          LEAFTL_GROUP_SIZE

/* CRB: at most 256 unique offsets. Current nearly-sorted records use
 * boundary bits rather than ambiguous null-byte separators, including
 * offsets 0 and 255. This is a byte budget, NOT on-flash serialization.
 */
#define LEAFTL_MAX_CRB_LPNS_PER_GROUP       LEAFTL_GROUP_SIZE
#define LEAFTL_MAX_CRB_BYTES_PER_GROUP      (2U * LEAFTL_GROUP_SIZE)

/* Budget for additional LeaFTL metadata, including directory, resident
 * segments, CRB, bookkeeping and scratch. Existing data/reverse maps and
 * buffers must ALSO be reported in total FTL DRAM accounting (step 6).
 */
#ifndef LEAFTL_METADATA_RAM_BUDGET_BYTES
#define LEAFTL_METADATA_RAM_BUDGET_BYTES    (8U * 1024U * 1024U)
#endif
/* Paper-format target only; never use this as sizeof(runtime segment). */
#define LEAFTL_TARGET_SEGMENT_BYTES         8U

/* Actual resident pools and scratch reservations. Step 11's RAM directory
 * uses VSA u32 + page offset u16 + record length u16 (8 bytes per group).
 */
#ifndef LEAFTL_RESIDENT_LEVEL_CAPACITY
#define LEAFTL_RESIDENT_LEVEL_CAPACITY \
    (LEAFTL_MAX_CACHED_GROUPS * LEAFTL_MAX_LEVELS_PER_GROUP)
#endif
#ifndef LEAFTL_FLUSH_SORT_CAPACITY
#define LEAFTL_FLUSH_SORT_CAPACITY          LEAFTL_GROUP_SIZE
#endif
#define LEAFTL_DIRECTORY_ENTRY_RESERVED_BYTES 8U

#if LEAFTL_MAX_CACHED_GROUPS < 1U
#error "At least one resident group is required"
#endif
#if LEAFTL_MAX_LEVELS_PER_GROUP < 1U || LEAFTL_MAX_LEVELS_PER_GROUP > LEAFTL_GROUP_SIZE
#error "Level limit must be between 1 and 256"
#endif
#if LEAFTL_MAX_SEGMENTS_PER_GROUP < LEAFTL_GROUP_SIZE
#error "A group must accommodate 256 singleton segments"
#endif
#if LEAFTL_MAX_RESIDENT_SEGMENTS < LEAFTL_MAX_SEGMENTS_PER_GROUP
#error "Resident segment pool must accommodate one maximum-size group"
#endif
#if LEAFTL_METADATA_RAM_BUDGET_BYTES < 1U
#error "Metadata RAM budget must be positive"
#endif

#endif /* LEAFTL_CONFIG_H_ */
