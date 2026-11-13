#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory_map.h"
#define CHECK(c) do { if(!(c)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);} } while(0)
static LEAFTL_INDEX small_index;
static LEAFTL_INDEX_GROUP small_groups[1];
static LEAFTL_INDEX_LEVEL small_levels[1];
static LEAFTL_INDEX_NODE small_nodes[1];
static LEAFTL_MAPPING small_mapping;
static LEAFTL_TRAINING_POINT small_points[8];
static LEAFTL_SEGMENT_INPUT small_segments[8];

int main(void)
{
    uint64_t offsets[]={LEAFTL_RAM_CONTEXT_OFFSET,LEAFTL_RAM_INDEX_OFFSET,LEAFTL_RAM_GROUPS_OFFSET,
        LEAFTL_RAM_LEVELS_OFFSET,LEAFTL_RAM_NODES_OFFSET,LEAFTL_RAM_WORKSPACE_OFFSET,
        LEAFTL_RAM_POINTS_OFFSET,LEAFTL_RAM_INPUTS_OFFSET,LEAFTL_RAM_WRITEBACK_OFFSET,
        LEAFTL_RAM_DIRECTORY_OFFSET};
    size_t bytes=(size_t)LEAFTL_RESERVED_RAM_BYTES;
    void *allocation=malloc(bytes+63U);
    unsigned char *arena;
    LEAFTL_MAPPING *mapping=0,*invalid_result=0;
    LEAFTL_TRAINING_POINT points[3]={{0U,100U},{2U,102U},{4U,104U}};
    unsigned int i,token=0U,other=999U,vsa=999U;
    CHECK(allocation!=0);
    arena=(unsigned char *)(((uintptr_t)allocation+63U)&~(uintptr_t)63U);
    for(i=0U;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        CHECK((offsets[i]&63U)==0U);
        if(i!=0U) CHECK(offsets[i]>offsets[i-1U]);
    }
    CHECK(LEAFTL_RESERVED_RAM_BYTES<=LEAFTL_METADATA_RAM_BUDGET_BYTES);
    CHECK(LEAFTL_MEMORY_BASE_ADDR>=LEGACY_FTL_END_EXCLUSIVE);
    CHECK(LEAFTL_MEMORY_END_EXCLUSIVE<=0x40000000ULL);
    CHECK(LeaFtlMemoryInit(arena+1U,bytes,512U,&invalid_result)==LEAFTL_MAPPING_INVALID);
    CHECK(LeaFtlMemoryInit(arena,1U,512U,&invalid_result)==LEAFTL_MAPPING_INVALID);
    CHECK(LeaFtlMemoryInit(arena,bytes,0U,&invalid_result)==LEAFTL_MAPPING_INVALID);
    CHECK(LeaFtlMemoryInit(arena,bytes,UINT_MAX,&invalid_result)==LEAFTL_MAPPING_INVALID);
    CHECK(invalid_result==0);
    CHECK(LeaFtlMemoryInit(arena,bytes,512U,&mapping)==LEAFTL_MAPPING_OK);
    CHECK((unsigned char *)mapping==arena+LEAFTL_RAM_CONTEXT_OFFSET);
    CHECK((unsigned char *)mapping->workspace==arena+LEAFTL_RAM_WORKSPACE_OFFSET);
    CHECK(LeaFtlMappingLookup(mapping,0U,&vsa)==LEAFTL_MAPPING_MISS && vsa==999U);
    CHECK(LeaFtlMappingPrepare(mapping,points,3U,&token)==LEAFTL_MAPPING_OK);
    CHECK(LeaFtlMappingLookup(mapping,0U,&vsa)==LEAFTL_MAPPING_MISS && vsa==999U);
    CHECK(LeaFtlMappingPrepare(mapping,points,3U,&other)==LEAFTL_MAPPING_BUSY && other==999U);
    CHECK(LeaFtlMappingCompact(mapping,0U,0)==LEAFTL_MAPPING_BUSY);
    CHECK(LeaFtlMappingComplete(mapping,token+1U,1)==LEAFTL_MAPPING_STALE);
    CHECK(LeaFtlMappingComplete(mapping,token,0)==LEAFTL_MAPPING_PROGRAM_FAILED);
    CHECK(LeaFtlMappingLookup(mapping,0U,&vsa)==LEAFTL_MAPPING_MISS);
    CHECK(LeaFtlMappingPrepare(mapping,points,3U,&token)==LEAFTL_MAPPING_OK);
    CHECK(LeaFtlMappingComplete(mapping,token,1)==LEAFTL_MAPPING_OK);
    CHECK(LeaFtlMappingLookup(mapping,0U,&vsa)==LEAFTL_MAPPING_OK && vsa==100U);
    CHECK(LeaFtlMappingLookup(mapping,1U,&vsa)==LEAFTL_MAPPING_MISS && vsa==100U);
    CHECK(LeaFtlMappingComplete(mapping,token,1)==LEAFTL_MAPPING_STALE);
    points[0].ppn=200U;
    CHECK(LeaFtlMappingPrepare(mapping,points,1U,&token)==LEAFTL_MAPPING_OK);
    CHECK(LeaFtlMappingLookup(mapping,0U,&vsa)==LEAFTL_MAPPING_OK && vsa==100U);
    points[0].ppn=99999U;
    CHECK(LeaFtlMappingComplete(mapping,token,1)==LEAFTL_MAPPING_OK);
    CHECK(LeaFtlMappingLookup(mapping,0U,&vsa)==LEAFTL_MAPPING_OK && vsa==200U);
    points[0].lpn=2U;points[0].ppn=300U;
    CHECK(LeaFtlMappingPrepare(mapping,points,1U,&token)==LEAFTL_MAPPING_OK);
    CHECK(LeaFtlMappingAbort(mapping,token)==LEAFTL_MAPPING_OK);
    CHECK(LeaFtlMappingLookup(mapping,2U,&vsa)==LEAFTL_MAPPING_OK && vsa==102U);
    CHECK(LeaFtlMappingComplete(mapping,token,1)==LEAFTL_MAPPING_STALE);
    points[0].lpn=512U;
    CHECK(LeaFtlMappingPrepare(mapping,points,1U,&other)==LEAFTL_MAPPING_INVALID);
    points[0].lpn=255U;points[1].lpn=256U;
    CHECK(LeaFtlMappingPrepare(mapping,points,2U,&other)==LEAFTL_MAPPING_INVALID);
    points[0].lpn=0U;points[1].lpn=0U;
    CHECK(LeaFtlMappingPrepare(mapping,points,2U,&other)==LEAFTL_MAPPING_INVALID);
    CHECK(LeaFtlMappingLookup(mapping,512U,&vsa)==LEAFTL_MAPPING_INVALID);
    CHECK(LeaFtlMappingCompact(mapping,0U,0)==LEAFTL_MAPPING_OK);
    CHECK(LeaFtlMappingLookup(mapping,0U,&vsa)==LEAFTL_MAPPING_OK && vsa==200U);
    CHECK(LeaFtlIndexInit(&small_index,small_groups,1U,small_levels,1U,small_nodes,1U)==LEAFTL_INDEX_OK);
    CHECK(LeaFtlMappingInit(&small_mapping,&small_index,mapping->workspace,
        small_points,small_segments,8U,512U)==LEAFTL_MAPPING_OK);
    points[0].lpn=0U;points[0].ppn=10U;
    CHECK(LeaFtlMappingPrepare(&small_mapping,points,1U,&token)==LEAFTL_MAPPING_OK);
    CHECK(LeaFtlMappingComplete(&small_mapping,token,1)==LEAFTL_MAPPING_OK);
    points[0].lpn=1U;points[0].ppn=11U;other=999U;
    CHECK(LeaFtlMappingPrepare(&small_mapping,points,1U,&other)==LEAFTL_MAPPING_NO_SPACE && other==999U);
    CHECK(small_mapping.pending_token==0U);
    CHECK(LeaFtlMappingLookup(&small_mapping,0U,&vsa)==LEAFTL_MAPPING_OK && vsa==10U);
    small_mapping.next_token=UINT_MAX;
    CHECK(LeaFtlMappingPrepare(&small_mapping,points,1U,&other)==LEAFTL_MAPPING_TOKEN_EXHAUSTED);
    printf("PASS: mapping prepare/publish/failure/abort/busy/stale/capacity; arena alignment and size guards\n");
    printf("SYNTHETIC BSP channels=%u dies=%u logical-map=%u reverse-map=%u bytes\n",
        (unsigned)USER_CHANNELS,(unsigned)USER_DIES,(unsigned)sizeof(LOGICAL_SLICE_MAP),(unsigned)sizeof(VIRTUAL_SLICE_MAP));
    printf("Legacy end=0x%llx LeaFTL base=0x%llx end-exclusive=0x%llx arena=%llu bytes\n",
        (unsigned long long)LEGACY_FTL_END_EXCLUSIVE,(unsigned long long)LEAFTL_MEMORY_BASE_ADDR,
        (unsigned long long)LEAFTL_MEMORY_END_EXCLUSIVE,(unsigned long long)LEAFTL_RESERVED_RAM_BYTES);
    printf("context=%u index=%u workspace=%u training=%u trained=%u directory=%llu bytes\n",
        (unsigned)sizeof(LEAFTL_MAPPING),(unsigned)sizeof(LEAFTL_INDEX),(unsigned)sizeof(LEAFTL_INDEX_WORKSPACE),
        (unsigned)(LEAFTL_MAX_TRAINING_POINTS*sizeof(LEAFTL_TRAINING_POINT)),
        (unsigned)(LEAFTL_MAX_TRAINING_POINTS*sizeof(LEAFTL_SEGMENT_INPUT)),
        (unsigned long long)LEAFTL_RAM_DIRECTORY_BYTES(SLICES_PER_SSD));
    free(allocation);return 0;
}
