#include "leaftl_memory.h"
#include <string.h>
LEAFTL_MAPPING_STATUS LeaFtlMemoryInit(void *arena,size_t bytes,
    unsigned int logical_count,LEAFTL_MAPPING **result)
{
    unsigned char *base=(unsigned char *)arena;
    uint64_t needed=LEAFTL_RAM_BYTES(logical_count);
    uintptr_t address=(uintptr_t)arena;
    LEAFTL_INDEX *index;
    LEAFTL_MAPPING *mapping;
    if(arena==0 || result==0 || logical_count==0U ||
       (address&(LEAFTL_RAM_ALIGNMENT-1U))!=0U || needed>(uint64_t)SIZE_MAX ||
       needed>(uint64_t)bytes || needed>LEAFTL_METADATA_RAM_BUDGET_BYTES ||
       address>UINTPTR_MAX-(uintptr_t)needed) return LEAFTL_MAPPING_INVALID;
    index=(LEAFTL_INDEX *)(base+LEAFTL_RAM_INDEX_OFFSET);
    mapping=(LEAFTL_MAPPING *)(base+LEAFTL_RAM_CONTEXT_OFFSET);
    if(LeaFtlIndexInit(index,(LEAFTL_INDEX_GROUP *)(base+LEAFTL_RAM_GROUPS_OFFSET),
        LEAFTL_MAX_CACHED_GROUPS,(LEAFTL_INDEX_LEVEL *)(base+LEAFTL_RAM_LEVELS_OFFSET),
        LEAFTL_RESIDENT_LEVEL_CAPACITY,(LEAFTL_INDEX_NODE *)(base+LEAFTL_RAM_NODES_OFFSET),
        LEAFTL_MAX_RESIDENT_SEGMENTS)!=LEAFTL_INDEX_OK) return LEAFTL_MAPPING_INVALID;
    if(LeaFtlMappingInit(mapping,index,(LEAFTL_INDEX_WORKSPACE *)(base+LEAFTL_RAM_WORKSPACE_OFFSET),
        (LEAFTL_TRAINING_POINT *)(base+LEAFTL_RAM_POINTS_OFFSET),
        (LEAFTL_SEGMENT_INPUT *)(base+LEAFTL_RAM_INPUTS_OFFSET),
        LEAFTL_MAX_TRAINING_POINTS,logical_count)!=LEAFTL_MAPPING_OK) return LEAFTL_MAPPING_INVALID;
    LeaFtlFlashDirectoryInit((LEAFTL_FLASH_DIRECTORY_ENTRY *)(base+LEAFTL_RAM_DIRECTORY_OFFSET),
        LEAFTL_GROUP_COUNT(logical_count));
    memset(base+LEAFTL_RAM_FLASH_CONTEXT_OFFSET,0,sizeof(LEAFTL_FLASH));
    *result=mapping; return LEAFTL_MAPPING_OK;
}
