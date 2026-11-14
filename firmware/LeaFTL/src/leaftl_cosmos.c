#include <assert.h>
#include <string.h>
#include "leaftl_gc.h"
#include "memory_map.h"
#include "leaftl_cosmos.h"
#include "leaftl_oob.h"
#include "leaftl_stats.h"
typedef char LeaFtlGcScratchFits[(USER_PAGES_PER_BLOCK<=LEAFTL_FLUSH_SORT_CAPACITY)?1:-1];
typedef char LeaFtlUserSpareFits[(LEAFTL_OOB_BYTES(LEAFTL_MAX_GAMMA)<=BYTES_PER_SPARE_REGION_OF_SLICE)?1:-1];
static LEAFTL_STATS stats;
void LeaFtlStatsReset(void) { memset(&stats,0,sizeof(stats)); }
const LEAFTL_STATS *LeaFtlStatsGet(void) { return &stats; }
void LeaFtlStatsHost(unsigned int write,unsigned int bytes)
{ if(write) stats.host_write_bytes+=bytes;else stats.host_read_bytes+=bytes; }
static LEAFTL_MAPPING *mapping;
static LEAFTL_WRITEBACK_WORK *work;
static unsigned int program_tag=REQ_SLOT_TAG_NONE;
static int program_result;
static unsigned int expected_code;
static LEAFTL_MAPPING_STATUS last_status=LEAFTL_MAPPING_OK;
static LEAFTL_WRITEBACK_WORK *gc_work;
LEAFTL_MAPPING_STATUS LeaFtlCosmosLastStatus(void) { return last_status; }
int LeaFtlCosmosActive(void) { return mapping!=0; }
int LeaFtlStatsMemory(LEAFTL_STATS_MEMORY *out)
{
    if(!mapping || !out) return 0;
    out->groups=mapping->index->group_count;out->levels=mapping->index->level_count;
    out->segments=mapping->index->node_count;out->cache_bytes=LeaFtlCacheBytes(mapping);
    out->arena_bytes=(unsigned int)LEAFTL_RAM_BYTES(mapping->logical_count);
    out->segment_bytes=sizeof(LEAFTL_SEGMENT);out->node_bytes=sizeof(LEAFTL_INDEX_NODE);
    out->directory_bytes=LEAFTL_GROUP_COUNT(mapping->logical_count)*sizeof(LEAFTL_FLASH_DIRECTORY_ENTRY);
    out->legacy_forward_bytes=sizeof(LOGICAL_SLICE_MAP);out->legacy_reverse_bytes=sizeof(VIRTUAL_SLICE_MAP);
    return 1;
}
static int InitMetadataCache(unsigned int logical_count);
int LeaFtlCosmosInit(unsigned int logical_count)
{
    LEAFTL_MAPPING_STATUS status;
    if(!LEAFTL_FIRMWARE_WRITEBACK) return 1;
    status=LeaFtlMemoryInit((void *)LEAFTL_MEMORY_BASE_ADDR,
        (size_t)LEAFTL_RAM_BYTES(logical_count),logical_count,&mapping);
    if(status!=LEAFTL_MAPPING_OK) { mapping=0; return 0; }
    work=(LEAFTL_WRITEBACK_WORK *)(LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_WRITEBACK_OFFSET);
    work->busy=0;
    gc_work=(LEAFTL_WRITEBACK_WORK *)(LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_GC_WORK_OFFSET);
    gc_work->busy=0;
    if(!InitMetadataCache(logical_count)) { mapping=0; return 0; }
    LeaFtlStatsReset();
    return 1;
}
static void Drain(void *ctx) { (void)ctx; SyncAllLowLevelReqDone(); }
static int Inspect(void *ctx,unsigned int id,unsigned int *lpn)
{
    (void)ctx;
    if(dataBufMapPtr->dataBuf[id].dirty!=DATA_BUF_DIRTY ||
        dataBufMapPtr->dataBuf[id].blockingReqTail!=REQ_SLOT_TAG_NONE) return 0;
    *lpn=dataBufMapPtr->dataBuf[id].logicalSliceAddr; return 1;
}
static unsigned int Reserve(void *ctx,unsigned int count,unsigned int *first)
{ (void)ctx; return LeaFtlReserveRun(count,first); }
static unsigned int RunLpn(void *context,unsigned int ppn)
{
    const LEAFTL_WRITEBACK_WORK *w=(const LEAFTL_WRITEBACK_WORK *)context;
    unsigned int offset;
    if(!w || !w->busy || ppn<w->run_first_ppn) return LEAFTL_LPN_NONE;
    offset=ppn-w->run_first_ppn;
    if(offset>=w->run_count || w->run_start+offset>=LEAFTL_FLUSH_SORT_CAPACITY) return LEAFTL_LPN_NONE;
    return w->entries[w->run_start+offset].lpn;
}
static int RunNand(unsigned int code,unsigned int format,unsigned int id,unsigned int lpn,unsigned int ppn)
{
    unsigned int tag;
    if(program_tag!=REQ_SLOT_TAG_NONE || ppn>=USER_PAGES_PER_SSD) return 0;
    tag=GetFromFreeReqQ();
    reqPoolPtr->reqPool[tag].reqType=REQ_TYPE_NAND;
    reqPoolPtr->reqPool[tag].reqCode=code;
    reqPoolPtr->reqPool[tag].nvmeCmdSlotTag=0; /* internal NAND, no NVMe completion */
    reqPoolPtr->reqPool[tag].logicalSliceAddr=lpn;
    reqPoolPtr->reqPool[tag].reqOpt.dataBufFormat=format;
    reqPoolPtr->reqPool[tag].reqOpt.nandAddr=REQ_OPT_NAND_ADDR_VSA;
    reqPoolPtr->reqPool[tag].reqOpt.nandEcc=REQ_OPT_NAND_ECC_ON;
    reqPoolPtr->reqPool[tag].reqOpt.nandEccWarning=REQ_OPT_NAND_ECC_WARNING_OFF;
    reqPoolPtr->reqPool[tag].reqOpt.rowAddrDependencyCheck=REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
    reqPoolPtr->reqPool[tag].reqOpt.blockSpace=REQ_OPT_BLOCK_SPACE_MAIN;
    if(format==REQ_OPT_DATA_BUF_ADDR) reqPoolPtr->reqPool[tag].dataBufInfo.addr=id;
    else reqPoolPtr->reqPool[tag].dataBufInfo.entry=id;
    reqPoolPtr->reqPool[tag].nandInfo.virtualSliceAddr=LeaFtlPpnToVsa(ppn);
    if(code==REQ_CODE_WRITE) {
        unsigned char *spare=(unsigned char *)(uintptr_t)GenerateSpareDataBufAddr(tag);
        if(format==REQ_OPT_DATA_BUF_ENTRY || format==REQ_OPT_DATA_BUF_TEMP_ENTRY) {
            LEAFTL_WRITEBACK_WORK *w=format==REQ_OPT_DATA_BUF_ENTRY ? work : gc_work;
            if(RunLpn(w,ppn)!=lpn || LeaFtlOobBuild(spare,BYTES_PER_SPARE_REGION_OF_SLICE,
                ppn,USER_PAGES_PER_BLOCK,LEAFTL_GAMMA,RunLpn,w)!=LEAFTL_OOB_OK) {
                PutToFreeReqQ(tag);return 0;
            }
        } else {
            /* Metadata is not user data. Do not leak prior scratch spare. */
            memset(spare,0xff,BYTES_PER_SPARE_REGION_OF_SLICE);
        }
    }
    if(code==REQ_CODE_ERASE) {
        unsigned int vsa=LeaFtlPpnToVsa(ppn);
        unsigned int die=Vsa2VdieTranslation(vsa),block=Vsa2VblockTranslation(vsa);
        reqPoolPtr->reqPool[tag].nandInfo.programmedPageCnt=
            rowAddrDependencyTablePtr->block[Vdie2PchTranslation(die)][Vdie2PwayTranslation(die)][block].permittedProgPage;
    }
    program_tag=tag; expected_code=code; program_result=0;
    reqPoolPtr->reqPool[tag].prevBlockingReq=REQ_SLOT_TAG_NONE;
    reqPoolPtr->reqPool[tag].nextBlockingReq=REQ_SLOT_TAG_NONE;
    if(format==REQ_OPT_DATA_BUF_ENTRY) UpdateDataBufEntryInfoBlockingReq(id,tag);
    else if(format==REQ_OPT_DATA_BUF_TEMP_ENTRY) UpdateTempDataBufEntryInfoBlockingReq(id,tag);
    SelectLowLevelReqQ(tag); SyncAllLowLevelReqDone();
    if(program_tag!=REQ_SLOT_TAG_NONE) { program_tag=REQ_SLOT_TAG_NONE; return 0; }
    if(!program_result) stats.io_failures++;
    else if(code==REQ_CODE_ERASE) stats.erases++;
    else if(code==REQ_CODE_WRITE) {
        if(format==REQ_OPT_DATA_BUF_ADDR) stats.metadata_programs++;
        else {stats.data_programs++;if(format==REQ_OPT_DATA_BUF_TEMP_ENTRY) stats.gc_programs++;}
    } else if(code==REQ_CODE_READ) {
        if(lpn==LSA_NONE) stats.metadata_reads++;else stats.data_reads++;
    }
    return program_result;
}
void LeaFtlCosmosNandDone(unsigned int tag,unsigned int status)
{
    if(tag!=program_tag) return;
    if(reqPoolPtr->reqPool[tag].reqCode!=expected_code &&
       !(expected_code==REQ_CODE_READ && reqPoolPtr->reqPool[tag].reqCode==REQ_CODE_READ_TRANSFER)) return;
    program_result=(status==REQ_STATUS_DONE ||
        (expected_code==REQ_CODE_READ && status==REQ_STATUS_WARNING));
    if(status!=REQ_STATUS_DONE) {
        unsigned int vsa=reqPoolPtr->reqPool[tag].nandInfo.virtualSliceAddr;
        virtualBlockMapPtr->block[Vsa2VdieTranslation(vsa)][Vsa2VblockTranslation(vsa)].bad=1;
    }
    program_tag=REQ_SLOT_TAG_NONE;
}
static unsigned char *ReadSpare(unsigned int format,unsigned int id)
{
    uintptr_t address;
    if(format==REQ_OPT_DATA_BUF_ADDR) address=(uintptr_t)id+BYTES_PER_DATA_REGION_OF_SLICE;
    else address=(format==REQ_OPT_DATA_BUF_ENTRY ? SPARE_DATA_BUFFER_BASE_ADDR : TEMPORARY_SPARE_DATA_BUFFER_BASE_ADDR)+
        id*BYTES_PER_SPARE_REGION_OF_SLICE;
    return (unsigned char *)address;
}
static int ReadableDataPage(unsigned int ppn)
{
    unsigned int vsa,die,block,page;
    if(ppn>=USER_PAGES_PER_SSD) return 0;
    vsa=LeaFtlPpnToVsa(ppn);die=Vsa2VdieTranslation(vsa);
    block=Vsa2VblockTranslation(vsa);page=Vsa2VpageTranslation(vsa);
    /* Never queue a read of an unprogrammed tail: row dependency would wait
     * forever. Quarantined blocks may still contain readable older data. */
    return !virtualBlockMapPtr->block[die][block].free &&
        page<virtualBlockMapPtr->block[die][block].currentPage &&
        page<rowAddrDependencyTablePtr->block[Vdie2PchTranslation(die)][Vdie2PwayTranslation(die)][block].permittedProgPage;
}
/* Called before any host DMA. The second read must verify ITS OWN LPN,
 * not merely contain the requested LPN somewhere in its neighbor list. */
static int ReadCorrected(unsigned int format,unsigned int id,unsigned int lpn,
    unsigned int predicted,unsigned int *actual)
{
    unsigned int corrected,verified;
    unsigned char *spare=ReadSpare(format,id);
    if(lpn>=mapping->logical_count || !ReadableDataPage(predicted) ||
       !RunNand(REQ_CODE_READ,format,id,lpn,predicted)) return 0;
    if(LeaFtlOobFind(spare,BYTES_PER_SPARE_REGION_OF_SLICE,predicted,USER_PAGES_PER_BLOCK,
        LEAFTL_GAMMA,lpn,&corrected)!=LEAFTL_OOB_OK || !ReadableDataPage(corrected)) return 0;
    if(corrected!=predicted) {
        stats.corrections++;
        if(!RunNand(REQ_CODE_READ,format,id,lpn,corrected)) return 0;
        if(LeaFtlOobFind(spare,BYTES_PER_SPARE_REGION_OF_SLICE,corrected,USER_PAGES_PER_BLOCK,
            LEAFTL_GAMMA,lpn,&verified)!=LEAFTL_OOB_OK || verified!=corrected) return 0;
    }
    *actual=corrected;return 1;
}
static LEAFTL_MAPPING_STATUS LookupExact(void *context,unsigned int lpn,unsigned int *actual)
{
    unsigned int predicted;LEAFTL_MAPPING_STATUS status;
    (void)context;
    status=LeaFtlMappingLookup(mapping,lpn,&predicted);
    if(status!=LEAFTL_MAPPING_OK) return status;
    if(predicted>=USER_PAGES_PER_SSD) return LEAFTL_MAPPING_IO_ERROR;
#if LEAFTL_GAMMA > 0U
    /* Dedicated borrowed DMA scratch: never overwrite dirty host data or the
     * GC temporary page while resolving old mappings / valid-page checks. */
    if(!ReadCorrected(REQ_OPT_DATA_BUF_ADDR,LEAFTL_RESOLVE_PAGE_ADDR,lpn,predicted,actual))
        return LEAFTL_MAPPING_IO_ERROR;
#else
    *actual=predicted;
#endif
    return LEAFTL_MAPPING_OK;
}
static int Program(void *ctx,unsigned int id,unsigned int lpn,unsigned int ppn)
{ (void)ctx; return RunNand(REQ_CODE_WRITE,REQ_OPT_DATA_BUF_ENTRY,id,lpn,ppn); }
static void Publish(void *ctx,unsigned int id,unsigned int lpn,unsigned int ppn,unsigned int old)
{
    (void)ctx; LeaFtlPublishPage(lpn,ppn,old);
    dataBufMapPtr->dataBuf[id].dirty=DATA_BUF_CLEAN;
}
static void Abandon(void *ctx) { (void)ctx; LeaFtlAbandonRun(); }
int LeaFtlCosmosFlush(void)
{
    LEAFTL_WRITEBACK_IO io;
    unsigned int committed;
    if(!mapping) return 0;
    io.context=0; io.buffer_count=AVAILABLE_DATA_BUFFER_ENTRY_COUNT;
    io.drain=Drain; io.inspect=Inspect; io.reserve=Reserve; io.program=Program;
    io.publish=Publish; io.abandon=Abandon;
    io.lookup=LookupExact;
    last_status=LeaFtlWritebackFlush(mapping,work,&io,&committed);
    return last_status==LEAFTL_MAPPING_OK;
}
int LeaFtlCosmosSync(void)
{
    stats.syncs++;
    if(!mapping || !LeaFtlCosmosFlush()) {stats.sync_failures++;return 0;}
    last_status=LeaFtlCacheFlush(mapping);
    if(last_status!=LEAFTL_MAPPING_OK) {stats.sync_failures++;return 0;}
    SyncAllLowLevelReqDone();return 1;
}
unsigned int LeaFtlCosmosRead(unsigned int lpn)
{
    unsigned int ppn;
    if(LookupExact(0,lpn,&ppn)!=LEAFTL_MAPPING_OK) return VSA_FAIL;
    return LeaFtlPpnToVsa(ppn);
}

int LeaFtlCosmosLoad(unsigned int id,unsigned int lpn)
{
    unsigned int ppn;
    LEAFTL_MAPPING_STATUS status;
    if(!mapping || id>=AVAILABLE_DATA_BUFFER_ENTRY_COUNT) return 0;
    SyncAllLowLevelReqDone();
    status=LeaFtlMappingLookup(mapping,lpn,&ppn);
    if(status==LEAFTL_MAPPING_MISS) {
        memset((void *)(uintptr_t)(DATA_BUFFER_BASE_ADDR+id*BYTES_PER_DATA_REGION_OF_SLICE),
            0,BYTES_PER_DATA_REGION_OF_SLICE);
        return 1;
    }
    if(status!=LEAFTL_MAPPING_OK || ppn>=USER_PAGES_PER_SSD) return 0;
    return ReadCorrected(REQ_OPT_DATA_BUF_ENTRY,id,lpn,ppn,&ppn);
}
int LeaFtlCosmosErase(unsigned int die,unsigned int block)
{
    return RunNand(REQ_CODE_ERASE,REQ_OPT_DATA_BUF_NONE,0,LSA_NONE,
        die*USER_PAGES_PER_DIE+block*USER_PAGES_PER_BLOCK);
}
typedef struct { unsigned int die,block; } COSMOS_GC;
static unsigned int GcReverse(void *v,unsigned int ppn)
{ (void)v; return virtualSliceMapPtr->virtualSlice[LeaFtlPpnToVsa(ppn)].logicalSliceAddr; }
static unsigned int GcReserve(void *v,unsigned int n,unsigned int *first)
{ COSMOS_GC *c=(COSMOS_GC *)v; return LeaFtlReserveGcRun(c->die,n,first); }
static int GcRead(void *v,unsigned int ppn)
{
    COSMOS_GC *c=(COSMOS_GC *)v;
    unsigned int lpn=GcReverse(v,ppn),actual;
    return lpn!=LSA_NONE && ReadCorrected(REQ_OPT_DATA_BUF_TEMP_ENTRY,c->die,lpn,ppn,&actual) && actual==ppn;
}
static int GcProgram(void *v,unsigned int lpn,unsigned int ppn)
{
    COSMOS_GC *c=(COSMOS_GC *)v;
    return RunNand(REQ_CODE_WRITE,REQ_OPT_DATA_BUF_TEMP_ENTRY,c->die,lpn,ppn);
}
static void GcPublish(void *v,unsigned int lpn,unsigned int ppn,unsigned int old)
{ (void)v; LeaFtlPublishPage(lpn,ppn,old); }
static void GcAbandon(void *v) { (void)v;LeaFtlAbandonGcRun(); }
static int GcErase(void *v)
{
    COSMOS_GC *c=(COSMOS_GC *)v;
    if(!LeaFtlCosmosErase(c->die,c->block)) return 0;
    LeaFtlFreeErasedBlock(c->die,c->block); return 1;
}
int LeaFtlCosmosGc(unsigned int die)
{
    COSMOS_GC ctx;
    LEAFTL_GC_IO io;
    unsigned int block,best=BLOCK_NONE,score=0,moved;
    if(!mapping || die>=USER_DIES || mapping->pending_token || gc_work->busy) return 0;
    /* Do not unlink a victim before relocation: publication updates its list. */
    for(block=0;block<USER_BLOCKS_PER_DIE;block++) {
        unsigned int gain,i;
        int metadata=0;
        /* Reserved translation blocks are never data-GC victims. */
        if(dftlTransDieMapPtr) for(i=0;i<DFTL_TRANS_BLOCKS_PER_DIE;i++)
            if(dftlTransDieMapPtr->die[die].blockList[i]==block) metadata=1;
        if(metadata) continue;
        if(virtualBlockMapPtr->block[die][block].free ||
           virtualBlockMapPtr->block[die][block].bad ||
           virtualBlockMapPtr->block[die][block].currentPage==0U ||
           block==virtualDieMapPtr->die[die].currentBlock) continue;
        gain=virtualBlockMapPtr->block[die][block].invalidSliceCnt+
            USER_PAGES_PER_BLOCK-virtualBlockMapPtr->block[die][block].currentPage;
        if(gain>score) { score=gain; best=block; }
    }
    if(best==BLOCK_NONE) return 0;
    ctx.die=die; ctx.block=best;
    io.context=&ctx; io.first_ppn=die*USER_PAGES_PER_DIE+best*USER_PAGES_PER_BLOCK;
    io.page_count=USER_PAGES_PER_BLOCK; io.reverse=GcReverse; io.drain=Drain;
    io.reserve=GcReserve; io.read=GcRead; io.program=GcProgram;
    io.publish=GcPublish; io.abandon=GcAbandon; io.erase=GcErase;
    io.lookup=LookupExact;
    return LeaFtlGcRelocate(mapping,gc_work,&io,&moved)==LEAFTL_MAPPING_OK;
}

/* Metadata I/O uses the same completion tracker and reserved translation
 * blocks, but never calls legacy DFTL page format/translation GC. */
static int MetadataProgram(void *ctx,unsigned int vsa,const unsigned char *page)
{ (void)ctx;return RunNand(REQ_CODE_WRITE,REQ_OPT_DATA_BUF_ADDR,(unsigned int)(uintptr_t)page,LSA_NONE,LeaFtlVsaToPpn(vsa)); }
static int MetadataRead(void *ctx,unsigned int vsa,unsigned char *page)
{ (void)ctx;return RunNand(REQ_CODE_READ,REQ_OPT_DATA_BUF_ADDR,(unsigned int)(uintptr_t)page,LSA_NONE,LeaFtlVsaToPpn(vsa)); }
static int MetadataErase(void *ctx,unsigned int vsa)
{ (void)ctx;return RunNand(REQ_CODE_ERASE,REQ_OPT_DATA_BUF_NONE,0,LSA_NONE,LeaFtlVsaToPpn(vsa)); }
static int InitMetadataCache(unsigned int logical_count)
{
    LEAFTL_FLASH *flash=(LEAFTL_FLASH *)(uintptr_t)LEAFTL_FLASH_CONTEXT_ADDR;
    LEAFTL_CACHE *cache=(LEAFTL_CACHE *)(uintptr_t)LEAFTL_CACHE_ADDR;
    LEAFTL_METADATA *pool=(LEAFTL_METADATA *)(uintptr_t)LEAFTL_METADATA_ADDR;
    LEAFTL_METADATA_BLOCK *blocks=(LEAFTL_METADATA_BLOCK *)(uintptr_t)LEAFTL_METADATA_BLOCKS_ADDR;
    LEAFTL_FLASH_DIRECTORY_ENTRY *directory=(LEAFTL_FLASH_DIRECTORY_ENTRY *)(uintptr_t)LEAFTL_DIRECTORY_ADDR;
    LEAFTL_METADATA_IO raw;
    LEAFTL_FLASH_IO io;
    unsigned int die,i,count=0;
    for(die=0;die<USER_DIES;die++) for(i=0;i<DFTL_TRANS_BLOCKS_PER_DIE;i++) {
        unsigned int block=dftlTransDieMapPtr->die[die].blockList[i];
        blocks[count++].base=Vorg2VsaTranslation(die,block,0);
    }
    raw.context=0;raw.program=MetadataProgram;raw.read=MetadataRead;raw.erase=MetadataErase;
    if(!LeaFtlMetadataInit(pool,blocks,count,USER_PAGES_PER_BLOCK,USER_DIES,directory,
        LEAFTL_GROUP_COUNT(logical_count),(unsigned char *)(uintptr_t)LEAFTL_METADATA_GC_ADDR,&raw)) return 0;
    io.context=pool;io.append=LeaFtlMetadataAppend;io.read=LeaFtlMetadataRead;
    if(LeaFtlFlashInit(flash,directory,logical_count,(unsigned char *)(uintptr_t)LEAFTL_FLASH_PAGE_ADDR,
        LEAFTL_FLASH_PAGE_BYTES,&io)!=LEAFTL_FLASH_OK) return 0;
    return LeaFtlCacheInit(cache,mapping,flash,LEAFTL_MAPPING_CACHE_BYTES,
        (unsigned char *)(uintptr_t)LEAFTL_CACHE_READ_ADDR,
        (unsigned char *)(uintptr_t)LEAFTL_CACHE_PROTECTED_ADDR)==LEAFTL_MAPPING_OK;
}
