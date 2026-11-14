#ifndef LEAFTL_CACHE_H_
#define LEAFTL_CACHE_H_
#include "leaftl_flash.h"
#define LEAFTL_CACHE_DIRTY 0x80000000U
#define LEAFTL_CACHE_COMPLETE 0x40000000U
#define LEAFTL_CACHE_STAMP 0x3fffffffU
#ifndef LEAFTL_MAPPING_CACHE_BYTES
#define LEAFTL_MAPPING_CACHE_BYTES (1024U*1024U)
#endif
#define LEAFTL_CACHE_EVICT_BATCH 8U
typedef struct LEAFTL_CACHE {
    LEAFTL_FLASH *flash;
    unsigned char *read_page,*protected_record;
    unsigned int budget,clock,busy;
    unsigned int ids[LEAFTL_MAX_CACHED_GROUPS];
} LEAFTL_CACHE;
/* Borrowed disjoint buffers: read_page is NAND-page+spare DMA storage,
 * protected_record holds <= FLASH_MAX_BYTES while victims are persisted.
 * Whole operation is exclusive. No additional per-LPN state/heap. */
LEAFTL_MAPPING_STATUS LeaFtlCacheInit(LEAFTL_CACHE *,LEAFTL_MAPPING *,LEAFTL_FLASH *,
    unsigned int budget,unsigned char *read_page,unsigned char *protected_record);
LEAFTL_MAPPING_STATUS LeaFtlCacheEnsure(LEAFTL_MAPPING *,unsigned int group_id);
LEAFTL_MAPPING_STATUS LeaFtlCachePlanRoom(LEAFTL_MAPPING *,unsigned int group_id,
    unsigned int final_nodes,unsigned int final_levels);
void LeaFtlCacheDirty(LEAFTL_MAPPING *,unsigned int group_id);
void LeaFtlCacheHit(LEAFTL_MAPPING *,unsigned int group_id);
LEAFTL_MAPPING_STATUS LeaFtlCacheFlush(LEAFTL_MAPPING *);
unsigned int LeaFtlCacheBytes(const LEAFTL_MAPPING *);
#endif
