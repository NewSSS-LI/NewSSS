#ifndef LEAFTL_INDEX_H_
#define LEAFTL_INDEX_H_
#include "leaftl_segment.h"
#define LEAFTL_INDEX_NONE 0xffffffffU
#define LEAFTL_INDEX_PLAN_CAPACITY (LEAFTL_MAX_SEGMENTS_PER_GROUP+LEAFTL_GROUP_SIZE)
typedef enum { LEAFTL_INDEX_OK=0,LEAFTL_INDEX_MISS=1,
               LEAFTL_INDEX_INVALID=-1,LEAFTL_INDEX_NO_SPACE=-2 } LEAFTL_INDEX_STATUS;
typedef struct {
    unsigned int group_id,first_level,level_count,segment_count;
    LEAFTL_CRB crb;
    unsigned int cache_state; /* dirty/complete flags + bounded LRU stamp */
} LEAFTL_INDEX_GROUP;
typedef struct { unsigned int first_segment,root_segment,segment_count,next; } LEAFTL_INDEX_LEVEL;
typedef struct { LEAFTL_SEGMENT model; unsigned int next,left,right; } LEAFTL_INDEX_NODE;
typedef struct {
    LEAFTL_INDEX_GROUP *groups;
    LEAFTL_INDEX_LEVEL *levels;
    LEAFTL_INDEX_NODE *nodes;
    unsigned int group_capacity,level_capacity,node_capacity;
    unsigned int group_count,level_count,node_count,free_level,free_node,initialized;
} LEAFTL_INDEX;
typedef struct { unsigned int level_count,segment_count; } LEAFTL_INDEX_GROUP_INFO;
typedef struct { LEAFTL_SEGMENT model; unsigned int next; } LEAFTL_INDEX_PLAN_NODE;
typedef struct { unsigned int first,next; } LEAFTL_INDEX_PLAN_LEVEL;
/* Caller-owned transaction scratch. No full PPN map or private persistent
 * membership mask. Allocate outside firmware stack; account at step 6.
 */
typedef struct {
    LEAFTL_INDEX_PLAN_NODE nodes[LEAFTL_INDEX_PLAN_CAPACITY];
    LEAFTL_INDEX_PLAN_LEVEL levels[LEAFTL_INDEX_PLAN_CAPACITY];
    unsigned int schedule[LEAFTL_INDEX_PLAN_CAPACITY];
    LEAFTL_CRB crb;
    unsigned int first,free_node,free_level,node_count,level_count;
} LEAFTL_INDEX_WORKSPACE;
typedef LEAFTL_INDEX_WORKSPACE LEAFTL_INDEX_COMPACTION_WORKSPACE;
typedef struct { unsigned int segments_before,segments_after,levels_before,levels_after; }
    LEAFTL_INDEX_COMPACTION_RESULT;

/* Caller-owned disjoint, live buffers; invalid init leaves buffers unchanged.
 * No heap/Flash I/O; reinitialization deliberately resets the RAM index.
 */
LEAFTL_INDEX_STATUS LeaFtlIndexInit(LEAFTL_INDEX *index,
    LEAFTL_INDEX_GROUP *groups,unsigned int group_capacity,
    LEAFTL_INDEX_LEVEL *levels,unsigned int level_capacity,
    LEAFTL_INDEX_NODE *nodes,unsigned int node_capacity);
/* Algorithm 1 seg_update at level 0, including Algorithm 2 seg_merge.
 * One group, range-sorted nonoverlapping inputs. Actual membership payloads
 * must match models. CRB duplicate removal/old SLPA renaming also occurs
 * when an EXACT new segment overwrites a CRB-backed old segment.
 * Error -> all persistent buffers unchanged; workspace may change.
 * Empty input succeeds. No implicit compaction. All caller buffers, inputs,
 * workspace and output parameters must be disjoint. Single-threaded only.
 */
LEAFTL_INDEX_STATUS LeaFtlIndexInsert(LEAFTL_INDEX *index,
    const LEAFTL_SEGMENT_INPUT *segments,unsigned int count,LEAFTL_INDEX_WORKSPACE *workspace);
/* Same validation/transaction planning as Insert, WITHOUT commit. Only
 * workspace changes. Caller must prevent mutations between preflight and
 * later Insert (the mapping layer enforces a single pending batch).
 */
LEAFTL_INDEX_STATUS LeaFtlIndexCanInsert(const LEAFTL_INDEX *index,
    const LEAFTL_SEGMENT_INPUT *segments,unsigned int count,LEAFTL_INDEX_WORKSPACE *workspace);
LEAFTL_INDEX_STATUS LeaFtlIndexLookup(const LEAFTL_INDEX *index,unsigned int lpn,unsigned int *ppn);
LEAFTL_INDEX_STATUS LeaFtlIndexGetGroupInfo(const LEAFTL_INDEX *index,unsigned int group_id,
    LEAFTL_INDEX_GROUP_INFO *info);
/* Flash snapshots preserve level order, CRB and coefficient bits exactly.
 * Restore validates/preflights before any persistent mutation; scratch may
 * change. Caller prevents mapping transactions/concurrent mutations. */
LEAFTL_INDEX_STATUS LeaFtlIndexExportGroup(const LEAFTL_INDEX *,unsigned int,
    LEAFTL_INDEX_WORKSPACE *);
LEAFTL_INDEX_STATUS LeaFtlIndexRestoreGroup(LEAFTL_INDEX *,unsigned int,
    LEAFTL_INDEX_WORKSPACE *);
int LeaFtlIndexValidateSnapshot(LEAFTL_INDEX_WORKSPACE *);
LEAFTL_INDEX_STATUS LeaFtlIndexRemoveGroup(LEAFTL_INDEX *,unsigned int);
/* Prepend RAM delta levels to a decoded older Flash snapshot, correcting CRB
 * ownership and merging overwritten members. Persistent index unchanged. */
LEAFTL_INDEX_STATUS LeaFtlIndexOverlayGroup(const LEAFTL_INDEX *,unsigned int,
    LEAFTL_INDEX_WORKSPACE *,LEAFTL_SEGMENT_INPUT *level_inputs);
/* Algorithm 1 upper-to-lower seg_update, not survivor repartitioning or
 * coefficient coalescing. All surviving segments keep K/I BIT-FOR-BIT,
 * including trimmed singletons. Exact interior obsolete members may remain
 * implicit under newer segments as in the paper; CRB memberships are unique.
 * One bounded pass over original levels, bottom-up, after cross-level
 * seg_merge. Newly created collision levels are not recursively revisited.
 * Repeated calls are safe; irreducible range overlap may retain levels.
 * Optional result written only on OK. MISS/INVALID/NO_SPACE preserve index
 * and result. Workspace may change. No automatic million-write scheduling,
 * gamma>0/OOB, Flash persistence, or concurrency is claimed here.
 */
LEAFTL_INDEX_STATUS LeaFtlIndexCompactGroup(LEAFTL_INDEX *index,unsigned int group_id,
    LEAFTL_INDEX_WORKSPACE *workspace,LEAFTL_INDEX_COMPACTION_RESULT *result);
#endif
