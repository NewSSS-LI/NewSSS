/* Windows host integration: actual firmware request/FTL C modules; only
 * host DMA and NAND hardware completion are simulated. No board download. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory_map.h"
#include "leaftl_cosmos.h"
#include "leaftl_oob.h"
#include "leaftl_stats.h"
#include "nvme/nvme_io_cmd.h"
#include "nvme/host_lld.h"
__declspec(dllimport) void *__stdcall VirtualAlloc(void *,size_t,unsigned long,unsigned long);
#define CHECK(c) do { if(!(c)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);} } while(0)
#define PAGE_BYTES BYTES_PER_DATA_REGION_OF_SLICE
#define FLASH_SLOTS 4096U
typedef struct { unsigned int vsa;unsigned char *data;unsigned char spare[256]; } FLASH_PAGE;
static FLASH_PAGE flash_pages[FLASH_SLOTS];
static unsigned char host[PAGE_BYTES*4],expected[PAGE_BYTES*4];
static unsigned int completions,last_status,tx_count,rx_count,nand_reads,nand_writes,data_erases,meta_erases;
static unsigned int fail_code=UINT_MAX,fail_format=UINT_MAX,fail_vsa=UINT_MAX;
static int corrupt_temp_oob;
HOST_DMA_STATUS g_hostDmaStatus;
HOST_DMA_ASSIST_STATUS g_hostDmaAssistStatus;
unsigned int BYTES_PER_SPARE_REGION_OF_NAND_ROW,EXTENDED_BLOCKS_PER_LUN;
unsigned int storageCapacity_L=4096;
static int expect_barrier;
int inbyte(void) { CHECK(0);return 0; } /* Interactive hardware boot is not simulated. */
static int MetadataBlock(unsigned int die,unsigned int block)
{
    unsigned int i;for(i=0;i<DFTL_TRANS_BLOCKS_PER_DIE;i++)
        if(dftlTransDieMapPtr->die[die].blockList[i]==block) return 1;
    return 0;
}
static FLASH_PAGE *Flash(unsigned int vsa,int create)
{
    unsigned int i;FLASH_PAGE *empty=0;
    for(i=0;i<FLASH_SLOTS;i++) {
        if(flash_pages[i].data && flash_pages[i].vsa==vsa) return &flash_pages[i];
        if(!flash_pages[i].data && !empty) empty=&flash_pages[i];
    }
    if(!create) return 0;
    CHECK(empty);empty->vsa=vsa;empty->data=(unsigned char *)malloc(PAGE_BYTES);CHECK(empty->data);return empty;
}
unsigned int GenerateDataBufAddr(unsigned int tag)
{
    SSD_REQ_FORMAT *r=&reqPoolPtr->reqPool[tag];unsigned int address;
    if(r->reqOpt.dataBufFormat==REQ_OPT_DATA_BUF_ADDR) return r->dataBufInfo.addr;
    address=(r->reqOpt.dataBufFormat==REQ_OPT_DATA_BUF_TEMP_ENTRY ? TEMPORARY_DATA_BUFFER_BASE_ADDR : DATA_BUFFER_BASE_ADDR)+r->dataBufInfo.entry*PAGE_BYTES;
    if(r->reqType==REQ_TYPE_NVME_DMA) address+=r->nvmeDmaInfo.nvmeBlockOffset*BYTES_PER_NVME_BLOCK;
    return address;
}
unsigned int GenerateSpareDataBufAddr(unsigned int tag)
{
    SSD_REQ_FORMAT *r=&reqPoolPtr->reqPool[tag];
    if(r->reqOpt.dataBufFormat==REQ_OPT_DATA_BUF_ADDR) return r->dataBufInfo.addr+PAGE_BYTES;
    return (r->reqOpt.dataBufFormat==REQ_OPT_DATA_BUF_TEMP_ENTRY ? TEMPORARY_SPARE_DATA_BUFFER_BASE_ADDR : SPARE_DATA_BUFFER_BASE_ADDR)+r->dataBufInfo.entry*BYTES_PER_SPARE_REGION_OF_SLICE;
}
void set_auto_nvme_cpl(unsigned int tag,unsigned int specific,unsigned int status)
{ (void)tag;(void)specific;
    if(expect_barrier && tag==(unsigned int)(expect_barrier-1) && status==0) {
        unsigned int i;LEAFTL_MAPPING *m=(LEAFTL_MAPPING *)(uintptr_t)LEAFTL_MAPPING_ADDR;
        for(i=0;i<AVAILABLE_DATA_BUFFER_ENTRY_COUNT;i++) CHECK(dataBufMapPtr->dataBuf[i].dirty==DATA_BUF_CLEAN);
        for(i=0;i<m->index->group_count;i++) CHECK(!(m->index->groups[i].cache_state & LEAFTL_CACHE_DIRTY));
        CHECK(LeaFtlStatsGet()->syncs && !blockedReqCnt && !notCompletedNandReqCnt);
    }
    completions++;last_status=status;
}
void set_auto_rx_dma(unsigned int tag,unsigned int offset,unsigned int addr,unsigned int auto_completion)
{
    (void)tag;CHECK(!auto_completion);CHECK((offset+1)*BYTES_PER_NVME_BLOCK<=sizeof(host));
    memcpy((void *)(uintptr_t)addr,host+offset*BYTES_PER_NVME_BLOCK,BYTES_PER_NVME_BLOCK);
    rx_count++;g_hostDmaStatus.fifoTail.autoDmaRx++;
}
void set_auto_tx_dma(unsigned int tag,unsigned int offset,unsigned int addr,unsigned int auto_completion)
{
    (void)tag;CHECK(!auto_completion);CHECK((offset+1)*BYTES_PER_NVME_BLOCK<=sizeof(host));
    memcpy(host+offset*BYTES_PER_NVME_BLOCK,(void *)(uintptr_t)addr,BYTES_PER_NVME_BLOCK);
    tx_count++;g_hostDmaStatus.fifoTail.autoDmaTx++;
}
unsigned int check_auto_rx_dma_partial_done(unsigned int a,unsigned int b) { (void)a;(void)b;return 1; }
unsigned int check_auto_tx_dma_partial_done(unsigned int a,unsigned int b) { (void)a;(void)b;return 1; }
void SchedulingNandReq(void)
{
    unsigned int ch,way;
    for(ch=0;ch<USER_CHANNELS;ch++) for(way=0;way<USER_WAYS;way++) {
        unsigned int tag=nandReqQ[ch][way].headReq,status=REQ_STATUS_DONE;
        SSD_REQ_FORMAT *r;unsigned int vsa,code;
        if(tag==REQ_SLOT_TAG_NONE) {ReleaseBlockedByRowAddrDepReq(ch,way);continue;}
        r=&reqPoolPtr->reqPool[tag];vsa=r->nandInfo.virtualSliceAddr;code=r->reqCode;
        if(code==fail_code && r->reqOpt.dataBufFormat==fail_format && (fail_vsa==UINT_MAX || vsa==fail_vsa)) {
            status=REQ_STATUS_FAIL;fail_code=UINT_MAX;
        } else if(code==REQ_CODE_WRITE) {
            FLASH_PAGE *p;unsigned int ppn;
            CHECK(!Flash(vsa,0));p=Flash(vsa,1);memcpy(p->data,(void *)(uintptr_t)GenerateDataBufAddr(tag),PAGE_BYTES);
            memcpy(p->spare,(void *)(uintptr_t)GenerateSpareDataBufAddr(tag),sizeof(p->spare));
            if(r->reqOpt.dataBufFormat!=REQ_OPT_DATA_BUF_ADDR) {
                CHECK(LeaFtlOobFind(p->spare,sizeof(p->spare),LeaFtlVsaToPpn(vsa),USER_PAGES_PER_BLOCK,
                    LEAFTL_GAMMA,r->logicalSliceAddr,&ppn)==LEAFTL_OOB_OK && ppn==LeaFtlVsaToPpn(vsa));
                CHECK(p->spare[0]==0xff && p->spare[1]==0xff && p->spare[2]==0xff && p->spare[3]==0xff);
            } else {for(ppn=0;ppn<sizeof(p->spare);ppn++) CHECK(p->spare[ppn]==0xff);}
            nand_writes++;
        } else if(code==REQ_CODE_READ) {
            FLASH_PAGE *p=Flash(vsa,0);CHECK(p);memcpy((void *)(uintptr_t)GenerateDataBufAddr(tag),p->data,PAGE_BYTES);memcpy((void *)(uintptr_t)GenerateSpareDataBufAddr(tag),p->spare,sizeof(p->spare));nand_reads++;
            if(corrupt_temp_oob && r->reqOpt.dataBufFormat==REQ_OPT_DATA_BUF_TEMP_ENTRY) {
                memset((unsigned char *)(uintptr_t)GenerateSpareDataBufAddr(tag)+4U+4U*LEAFTL_GAMMA,0xff,4);
                corrupt_temp_oob=0;
            }
        } else if(code==REQ_CODE_ERASE) {
            unsigned int i,die=Vsa2VdieTranslation(vsa),block=Vsa2VblockTranslation(vsa);
            for(i=0;i<FLASH_SLOTS;i++) if(flash_pages[i].data && Vsa2VdieTranslation(flash_pages[i].vsa)==die && Vsa2VblockTranslation(flash_pages[i].vsa)==block) {
                free(flash_pages[i].data);flash_pages[i].data=0;
            }
            if(MetadataBlock(die,block)) meta_erases++;else data_erases++;
        } else CHECK(0);
        if(code==REQ_CODE_READ) r->reqCode=REQ_CODE_READ_TRANSFER;
        GetFromNandReqQ(ch,way,status,r->reqCode);
        ReleaseBlockedByRowAddrDepReq(ch,way);
    }
}
void SyncAllLowLevelReqDone(void)
{
    unsigned int spins=0;
    while(nvmeDmaReqQ.headReq!=REQ_SLOT_TAG_NONE || notCompletedNandReqCnt || blockedReqCnt) {
        CHECK(++spins<10000);CheckDoneNvmeDmaReq();SchedulingNandReq();
    }
}
void SyncAvailFreeReq(void) { SyncAllLowLevelReqDone(); }
void SyncReleaseEraseReq(unsigned int ch,unsigned int way,unsigned int block)
{ (void)ch;(void)way;(void)block;SyncAllLowLevelReqDone(); }
static void Setup(void)
{
    unsigned int i,die,b;
    LEAFTL_MAPPING *m;
    for(i=0;i<FLASH_SLOTS;i++) {free(flash_pages[i].data);flash_pages[i].data=0;}
    memset((void *)(uintptr_t)DATA_BUFFER_BASE_ADDR,0,0x10000000U);
    InitReqPool();InitDependencyTable();InitDataBuf();InitGcVictimMap();
    virtualSliceMapPtr=(P_VIRTUAL_SLICE_MAP)(uintptr_t)VIRTUAL_SLICE_MAP_ADDR;
    virtualBlockMapPtr=(P_VIRTUAL_BLOCK_MAP)(uintptr_t)VIRTUAL_BLOCK_MAP_ADDR;
    virtualDieMapPtr=(P_VIRTUAL_DIE_MAP)(uintptr_t)VIRTUAL_DIE_MAP_ADDR;
    phyBlockMapPtr=(P_PHY_BLOCK_MAP)(uintptr_t)PHY_BLOCK_MAP_ADDR;
    dftlTransDieMapPtr=(P_DFTL_TRANS_DIE_MAP)(uintptr_t)DFTL_TRANS_DIE_MAP_ADDR;
    memset(virtualSliceMapPtr,0xff,sizeof(*virtualSliceMapPtr));
    for(die=0;die<USER_DIES;die++) {
        virtualDieMapPtr->die[die].headFreeBlock=BLOCK_NONE;virtualDieMapPtr->die[die].tailFreeBlock=BLOCK_NONE;
        for(b=0;b<USER_BLOCKS_PER_DIE;b++) {
            virtualBlockMapPtr->block[die][b].bad=1;
            virtualBlockMapPtr->block[die][b].prevBlock=BLOCK_NONE;virtualBlockMapPtr->block[die][b].nextBlock=BLOCK_NONE;
        }
        for(b=0;b<16;b++) {virtualBlockMapPtr->block[die][b].bad=0;virtualBlockMapPtr->block[die][b].free=1;PutToFbList(die,b);}
        virtualDieMapPtr->die[die].currentBlock=GetFromFbList(die,GET_FREE_BLOCK_NORMAL);
        for(i=0;i<DFTL_TRANS_BLOCKS_PER_DIE;i++) dftlTransDieMapPtr->die[die].blockList[i]=GetFromFbList(die,GET_FREE_BLOCK_GC);
    }
    sliceAllocationTargetDie=0;CHECK(LeaFtlCosmosInit(1024) && LeaFtlCosmosActive());
    m=(LEAFTL_MAPPING *)(uintptr_t)LEAFTL_MAPPING_ADDR;
    CHECK(LeaFtlIndexInit(m->index,m->index->groups,2,m->index->levels,512,m->index->nodes,2048)==0);
    /* Tiny metadata pool/physical prefix forces GC without large tests. */
    ((LEAFTL_METADATA *)(uintptr_t)LEAFTL_METADATA_ADDR)->count=3;
    ((LEAFTL_METADATA *)(uintptr_t)LEAFTL_METADATA_ADDR)->pages=4;
    completions=last_status=tx_count=rx_count=nand_reads=nand_writes=data_erases=meta_erases=0;
    corrupt_temp_oob=0;
}
static void Command(unsigned int lba,unsigned int blocks,unsigned int code)
{
    unsigned int before=completions;
    ReqTransNvmeToSlice(0,lba,blocks-1,code);ReqTransSliceToLowLevel();
    CHECK(completions==before+1 && last_status==0 && sliceReqQ.headReq==REQ_SLOT_TAG_NONE);
}
static void Pattern(unsigned int lpn,unsigned int version)
{ unsigned int i;for(i=0;i<PAGE_BYTES;i++) host[i]=(unsigned char)(lpn*7+version*37+i*13); }
static void TestApproximateReads(void)
{
    static const unsigned int sparse[7]={32,33,34,38,76,78,115};
    unsigned int i,miss=UINT_MAX,observed=0,first=USER_PAGES_PER_DIE;
    LEAFTL_MAPPING *m;
    Setup();sliceAllocationTargetDie=1;
    for(i=0;i<7;i++) {Pattern(sparse[i],9);Command(sparse[i]*4,4,IO_NVM_WRITE);}
    CHECK(LeaFtlCosmosFlush());InitDataBuf();
    m=(LEAFTL_MAPPING *)(uintptr_t)LEAFTL_MAPPING_ADDR;
    for(i=0;i<7;i++) {
        unsigned int predicted,before=nand_reads;
        CHECK(LeaFtlMappingLookup(m,sparse[i],&predicted)==0);
        Pattern(sparse[i],9);memcpy(expected,host,PAGE_BYTES);Command(sparse[i]*4,4,IO_NVM_READ);
        CHECK(!memcmp(expected,host,PAGE_BYTES));
        CHECK(nand_reads-before==(predicted==first+i ? 1U : 2U));
        if(predicted!=first+i) {observed++;miss=i;}
    }
    if(LEAFTL_GAMMA) CHECK(observed>0);
    if(LEAFTL_GAMMA>1U) {
        unsigned int candidate;
        for(candidate=0;candidate<7;candidate++) {
            unsigned int predicted,slot,j,before=nand_reads,tx=tx_count;
            FLASH_PAGE *p;unsigned char saved[256];
            CHECK(LeaFtlMappingLookup(m,sparse[candidate],&predicted)==0);
            if(predicted==first+candidate || first+7U-predicted>LEAFTL_GAMMA) continue;
            p=Flash(LeaFtlPpnToVsa(predicted),0);CHECK(p);memcpy(saved,p->spare,sizeof(saved));
            for(slot=0;slot<2U*LEAFTL_GAMMA+1U;slot++) {
                unsigned char *e=p->spare+4U+4U*slot;
                unsigned int lpn=(unsigned int)e[0]|((unsigned int)e[1]<<8)|((unsigned int)e[2]<<16)|((unsigned int)e[3]<<24);
                if(lpn==sparse[candidate]) memset(e,0xff,4);
            }
            slot=first+7U-predicted+LEAFTL_GAMMA;
            for(j=0;j<4;j++) p->spare[4U+4U*slot+j]=(unsigned char)(sparse[candidate]>>(8U*j));
            InitDataBuf();ReqTransNvmeToSlice(0,sparse[candidate]*4,3,IO_NVM_READ);ReqTransSliceToLowLevel();
            CHECK(last_status!=0 && tx_count==tx && nand_reads==before+1 && !blockedReqCnt);
            memcpy(p->spare,saved,sizeof(saved));break;
        }
        CHECK(candidate<7); /* exercised an OOB pointer into unprogrammed tail */
    }
    if(miss!=UINT_MAX) {
        unsigned int predicted,before=tx_count,cpl=completions,j;
        FLASH_PAGE *p;unsigned char saved_spare[256];
        CHECK(LeaFtlMappingLookup(m,sparse[miss],&predicted)==0);
        p=Flash(LeaFtlPpnToVsa(predicted),0);CHECK(p);memcpy(saved_spare,p->spare,sizeof(saved_spare));
        /* Predicted-page OOB no longer names requested LPN: fail, never DMA. */
        for(j=0;j<2U*LEAFTL_GAMMA+1U;j++) {
            unsigned char *entry=p->spare+4U+4U*j;
            unsigned int lpn=(unsigned int)entry[0]|((unsigned int)entry[1]<<8)|((unsigned int)entry[2]<<16)|((unsigned int)entry[3]<<24);
            if(lpn==sparse[miss]) memset(entry,0xff,4);
        }
        InitDataBuf();ReqTransNvmeToSlice(0,sparse[miss]*4,3,IO_NVM_READ);ReqTransSliceToLowLevel();
        CHECK(last_status!=0 && tx_count==before && completions==cpl+1);memcpy(p->spare,saved_spare,sizeof(saved_spare));
        {
            FLASH_PAGE *correct=Flash(LeaFtlPpnToVsa(first+miss),0);
            unsigned char saved_correct[256];unsigned int byte;
            CHECK(correct);memcpy(saved_correct,correct->spare,sizeof(saved_correct));
            /* A neighbor match on the second page is NOT an own-LPN match. */
            for(byte=0;byte<4;byte++) {
                correct->spare[4U+4U*LEAFTL_GAMMA+byte]=(unsigned char)(777U>>(8U*byte));
                correct->spare[4U+4U*(LEAFTL_GAMMA+1U)+byte]=(unsigned char)(sparse[miss]>>(8U*byte));
            }
            InitDataBuf();ReqTransNvmeToSlice(0,sparse[miss]*4,3,IO_NVM_READ);ReqTransSliceToLowLevel();
            CHECK(last_status!=0 && tx_count==before);
            memcpy(correct->spare,saved_correct,sizeof(saved_correct));
        }
        /* First read succeeds, corrected page fails: also no DMA. */
        fail_code=REQ_CODE_READ;fail_format=REQ_OPT_DATA_BUF_ENTRY;fail_vsa=LeaFtlPpnToVsa(first+miss);
        InitDataBuf();ReqTransNvmeToSlice(0,sparse[miss]*4,3,IO_NVM_READ);ReqTransSliceToLowLevel();
        CHECK(last_status!=0 && tx_count==before);
        /* Failed old-PPA correction must preserve mapping and dirty data. */
        {
            unsigned int byte,old_predicted,writes_before=nand_writes;
            CHECK(LeaFtlMappingLookup(m,sparse[miss],&old_predicted)==0);
            for(j=0;j<2U*LEAFTL_GAMMA+1U;j++) {
                unsigned char *entry=p->spare+4U+4U*j;
                unsigned int lpn=(unsigned int)entry[0]|((unsigned int)entry[1]<<8)|((unsigned int)entry[2]<<16)|((unsigned int)entry[3]<<24);
                if(lpn==sparse[miss]) for(byte=0;byte<4;byte++) entry[byte]=0xff;
            }
            Pattern(sparse[miss],11);Command(sparse[miss]*4,4,IO_NVM_WRITE);
            CHECK(!LeaFtlCosmosFlush() && nand_writes==writes_before);
            CHECK(LeaFtlMappingLookup(m,sparse[miss],&predicted)==0 && predicted==old_predicted);
            memcpy(p->spare,saved_spare,sizeof(saved_spare));CHECK(LeaFtlCosmosFlush());
        }
    }
    /* Partial write after a natural misprediction must preserve the other
     * 12KiB of the corrected page, not the initially misread neighbor. */
    {
        unsigned int candidate=0,predicted,before;
        if(LEAFTL_GAMMA) {
            for(candidate=0;candidate<7;candidate++) {
                CHECK(LeaFtlMappingLookup(m,sparse[candidate],&predicted)==0);
                if(candidate!=miss && predicted!=first+candidate) break;
            }
            CHECK(candidate<7);
        }
        Pattern(sparse[candidate],9);memcpy(expected,host,PAGE_BYTES);
        memset(expected+4096,0xa3,4096);InitDataBuf();memset(host,0xa3,4096);before=nand_reads;
        Command(sparse[candidate]*4+1,1,IO_NVM_WRITE);
        CHECK(nand_reads-before==(LEAFTL_GAMMA ? 2U : 1U));
        CHECK(LeaFtlCosmosFlush());InitDataBuf();Command(sparse[candidate]*4,4,IO_NVM_READ);
        CHECK(!memcmp(expected,host,PAGE_BYTES));
    }
    /* Fresh source: overwrite one true PPA and GC the remaining approximate
     * mappings. Exact resolver must not invalidate the predicted neighbor. */
    Setup();sliceAllocationTargetDie=1;
    for(i=0;i<7;i++) {Pattern(sparse[i],9);Command(sparse[i]*4,4,IO_NVM_WRITE);}
    CHECK(LeaFtlCosmosFlush());
    virtualDieMapPtr->die[1].currentBlock=GetFromFbList(1,GET_FREE_BLOCK_NORMAL);
    CHECK(virtualDieMapPtr->die[1].currentBlock!=BLOCK_FAIL);sliceAllocationTargetDie=1;
    Pattern(sparse[2],10);Command(sparse[2]*4,4,IO_NVM_WRITE);CHECK(LeaFtlCosmosFlush());
    CHECK(virtualSliceMapPtr->virtualSlice[LeaFtlPpnToVsa(first+2)].logicalSliceAddr==LSA_NONE);
    for(i=0;i<7;i++) if(i!=2) CHECK(virtualSliceMapPtr->virtualSlice[LeaFtlPpnToVsa(first+i)].logicalSliceAddr==sparse[i]);
    {
        FLASH_PAGE *p=Flash(LeaFtlPpnToVsa(first),0);
        unsigned char saved[256];unsigned int free_before=virtualDieMapPtr->die[1].freeBlockCnt,writes=nand_writes;
        CHECK(p);memcpy(saved,p->spare,sizeof(saved));memset(p->spare+4U+4U*LEAFTL_GAMMA,0xff,4);
        CHECK(!LeaFtlCollectGarbage(1) && !data_erases && nand_writes==writes);
        CHECK(virtualDieMapPtr->die[1].freeBlockCnt==free_before);memcpy(p->spare,saved,sizeof(saved));
        corrupt_temp_oob=1;
        CHECK(!LeaFtlCollectGarbage(1) && !data_erases && nand_writes==writes && !corrupt_temp_oob);
        CHECK(virtualDieMapPtr->die[1].freeBlockCnt==free_before); /* no empty-target leak */
    }
    CHECK(LeaFtlCollectGarbage(1) && data_erases==1);InitDataBuf();
    for(i=0;i<7;i++) {
        Pattern(sparse[i],i==2 ? 10U : 9U);memcpy(expected,host,PAGE_BYTES);
        Command(sparse[i]*4,4,IO_NVM_READ);CHECK(!memcmp(host,expected,PAGE_BYTES));
    }
    printf("PASS gamma=%u: %u real mispredictions corrected with exactly one extra read; bad OOB/corrected-read failure suppress DMA; exact old-PPA invalidation and approximate valid-page GC\n",LEAFTL_GAMMA,observed);
}
static void TestBlockBoundaryCacheMiss(void)
{
    static const unsigned int lpns[7]={250,251,254,255,256,260,300};
    unsigned int i;LEAFTL_MAPPING *m;
    Setup();sliceAllocationTargetDie=1;
    /* Real NAND prefix programs put the next sorted run at pages 252..255. */
    for(i=0;i<252;i++) {Pattern(512+i,5);Command((512+i)*4,4,IO_NVM_WRITE);}
    CHECK(LeaFtlCosmosFlush());
    CHECK(virtualBlockMapPtr->block[1][0].currentPage==252);
    for(i=0;i<7;i++) {Pattern(lpns[i],6);Command(lpns[i]*4,4,IO_NVM_WRITE);}
    sliceAllocationTargetDie=1;CHECK(LeaFtlCosmosFlush());
    m=(LEAFTL_MAPPING *)(uintptr_t)LEAFTL_MAPPING_ADDR;CHECK(LeaFtlCacheFlush(m)==0);
    /* Evict all clean resident groups, forcing Flash reload/CRB decode. */
    while(m->index->group_count) CHECK(LeaFtlIndexRemoveGroup(m->index,m->index->groups[0].group_id)==0);
    InitDataBuf();
    for(i=0;i<7;i++) {
        Pattern(lpns[i],6);memcpy(expected,host,PAGE_BYTES);memset(host,0,PAGE_BYTES);
        Command(lpns[i]*4,4,IO_NVM_READ);CHECK(!memcmp(expected,host,PAGE_BYTES));
    }
    Pattern(512,5);memcpy(expected,host,PAGE_BYTES);Command(512*4,4,IO_NVM_READ);CHECK(!memcmp(expected,host,PAGE_BYTES));
    CHECK(!blockedReqCnt && !notCompletedNandReqCnt);
    printf("PASS gamma=%u: block-tail split, LPN group boundary 255/256, partial-run OOB padding and Flash cache miss\n",LEAFTL_GAMMA);
}
static void FlushCommand(void)
{
    NVME_COMMAND c;unsigned int before=completions;
    unsigned int expected_completions=(sliceReqQ.headReq==REQ_SLOT_TAG_NONE) ? 1U : 2U;
    memset(&c,0,sizeof(c));c.cmdDword[0]=IO_NVM_FLUSH;c.cmdSlotTag=1;
    expect_barrier=2;handle_nvme_io_cmd(&c);expect_barrier=0;
    CHECK(completions==before+expected_completions);
}
static void TestFlushFuaStats(void)
{
    NVME_COMMAND c;LEAFTL_STATS_MEMORY mem;unsigned int before;
    const LEAFTL_STATS *st;
    Setup();memset(host,0x61,PAGE_BYTES*2);
    /* FLUSH must process earlier queued slices before writing back. */
    ReqTransNvmeToSlice(0,0,7,IO_NVM_WRITE);FlushCommand();
    st=LeaFtlStatsGet();CHECK(!last_status && st->data_programs==2 && st->metadata_programs>0);
    CHECK(st->host_write_bytes==2*PAGE_BYTES && st->syncs==1);
    before=nand_writes;FlushCommand();CHECK(!last_status && nand_writes==before);
    /* Actual NVMe DW12.FUA decoder, multi-slice completion ordering. */
    memset(&c,0,sizeof(c));c.cmdDword[0]=IO_NVM_WRITE;c.cmdDword[10]=8;
    c.cmdDword[12]=7U | (1U<<30);
    before=completions;handle_nvme_io_cmd(&c);expect_barrier=1;
    ReqTransSliceToLowLevel();expect_barrier=0;
    CHECK(completions==before+1 && !last_status && st->data_programs==4);
    /* Program failure: report failure, retain dirty data, retry FLUSH. */
    c.cmdDword[10]=16;fail_code=REQ_CODE_WRITE;fail_format=REQ_OPT_DATA_BUF_ENTRY;fail_vsa=UINT_MAX;
    before=completions;handle_nvme_io_cmd(&c);ReqTransSliceToLowLevel();
    CHECK(last_status && completions==before+1 && st->sync_failures==1);
    FlushCommand();CHECK(!last_status);
    /* Mapping write failure: no false success; dirty mapping retries. */
    Command(32,4,IO_NVM_WRITE);
    fail_code=REQ_CODE_WRITE;fail_format=REQ_OPT_DATA_BUF_ADDR;fail_vsa=UINT_MAX;
    FlushCommand();CHECK(last_status && st->sync_failures==2);
    FlushCommand();CHECK(!last_status && st->io_failures==2);
    CHECK(LeaFtlStatsMemory(&mem) && mem.segment_bytes==8 && mem.node_bytes==20);
    CHECK(mem.directory_bytes==32 && mem.legacy_forward_bytes && mem.legacy_reverse_bytes);
    CHECK(st->data_programs+st->metadata_programs==nand_writes);
    CHECK(st->data_reads+st->metadata_reads==nand_reads);
    CHECK(sizeof(*st)==96);
    printf("PASS gamma=%u: NVMe FLUSH/FUA order, data/metadata failure + retry, exact I/O counters, 96-byte stats, honest memory accounting (no restart persistence)\n",LEAFTL_GAMMA);
}
int main(void)
{
    unsigned int i,before,old;LEAFTL_MAPPING *m;
    CHECK(LEAFTL_FIRMWARE_WRITEBACK==1 && LEAFTL_GAMMA<=LEAFTL_MAX_GAMMA);
    CHECK(VirtualAlloc((void *)(uintptr_t)DATA_BUFFER_BASE_ADDR,0x10000000U,0x3000U,4U)==(void *)(uintptr_t)DATA_BUFFER_BASE_ADDR);
    TestFlushFuaStats();
    TestApproximateReads();
    TestBlockBoundaryCacheMiss();
    Setup();
    for(i=0;i<PAGE_BYTES*2;i++) host[i]=(unsigned char)(i*3+7);
    memcpy(expected,host,PAGE_BYTES*2);Command(0,8,IO_NVM_WRITE);
    CHECK(LeaFtlCosmosFlush());InitDataBuf();memset(host,0,sizeof(host));Command(0,8,IO_NVM_READ);CHECK(!memcmp(host,expected,PAGE_BYTES*2));
    memset(host,0xa6,8192);memset(expected+4096,0xa6,8192);InitDataBuf();Command(1,2,IO_NVM_WRITE);
    CHECK(LeaFtlCosmosFlush());InitDataBuf();Command(0,8,IO_NVM_READ);CHECK(!memcmp(host,expected,PAGE_BYTES*2));
    InitDataBuf();memset(host,0x5a,4096);Command(41,1,IO_NVM_WRITE);
    CHECK(LeaFtlCosmosFlush());InitDataBuf();Command(40,4,IO_NVM_READ);
    for(i=0;i<PAGE_BYTES;i++) CHECK(host[i]==(i>=4096 && i<8192 ? 0x5a : 0));
    Setup();
    /* One more host page than cache slots must exercise automatic dirty
     * eviction through request_transform, without an explicit flush first. */
    for(i=0;i<257;i++) {Pattern(i,3);Command(i*4,4,IO_NVM_WRITE);}
    CHECK(nand_writes>=256 && LeaFtlCosmosFlush());InitDataBuf();
    Pattern(0,3);memcpy(expected,host,PAGE_BYTES);Command(0,4,IO_NVM_READ);CHECK(!memcmp(expected,host,PAGE_BYTES));
    Pattern(256,3);memcpy(expected,host,PAGE_BYTES);Command(1024,4,IO_NVM_READ);CHECK(!memcmp(expected,host,PAGE_BYTES));
    Setup();
    for(i=0;i<256;i++) {Pattern(i,1);Command(i*4,4,IO_NVM_WRITE);}
    sliceAllocationTargetDie=0;CHECK(LeaFtlCosmosFlush());
    for(i=0;i<128;i++) {Pattern(i,2);Command(i*4,4,IO_NVM_WRITE);}
    sliceAllocationTargetDie=0;CHECK(LeaFtlCosmosFlush());
    CHECK(LeaFtlCollectGarbage(0) && data_erases>0);InitDataBuf();
    for(i=0;i<256;i++) {Pattern(i,i<128 ? 2U : 1U);memcpy(expected,host,PAGE_BYTES);memset(host,0,PAGE_BYTES);Command(i*4,4,IO_NVM_READ);CHECK(!memcmp(expected,host,PAGE_BYTES));}
    m=(LEAFTL_MAPPING *)(uintptr_t)LEAFTL_MAPPING_ADDR;
    for(i=0;i<24;i++) {unsigned int lpn=(i%4)*256;Pattern(lpn,i+3);Command(lpn*4,4,IO_NVM_WRITE);CHECK(LeaFtlCosmosFlush());CHECK(LeaFtlCacheFlush(m)==0);}
    CHECK(meta_erases>0);
    CHECK(LeaFtlCosmosFlush());InitDataBuf();old=AddrTransRead(0);before=tx_count;
    fail_code=REQ_CODE_READ;fail_format=REQ_OPT_DATA_BUF_ENTRY;fail_vsa=old;
    i=completions;ReqTransNvmeToSlice(0,0,7,IO_NVM_READ);ReqTransSliceToLowLevel();
    CHECK(last_status!=0 && tx_count==before && completions==i+1);
    Pattern(0,77);Command(0,4,IO_NVM_WRITE);fail_code=REQ_CODE_WRITE;fail_format=REQ_OPT_DATA_BUF_ENTRY;fail_vsa=UINT_MAX;
    CHECK(!LeaFtlCosmosFlush() && AddrTransRead(0)==old);
    CHECK(LeaFtlCosmosFlush());memcpy(expected,host,PAGE_BYTES);InitDataBuf();Command(0,4,IO_NVM_READ);CHECK(!memcmp(host,expected,PAGE_BYTES));
    CHECK(!notCompletedNandReqCnt && !blockedReqCnt && freeReqQ.reqCnt==AVAILABLE_OUNTSTANDING_REQ_COUNT);
    printf("PASS: actual request transform/allocation, manual NVMe completion, DMA, full/partial/unmapped RMW, NAND readback, data GC, cache/metadata GC, read-error DMA suppression, program failure/retry; reads=%u writes=%u data-erases=%u meta-erases=%u\n",nand_reads,nand_writes,data_erases,meta_erases);
    CHECK(LeaFtlStatsGet()->data_programs+LeaFtlStatsGet()->metadata_programs==nand_writes);
    CHECK(LeaFtlStatsGet()->data_reads+LeaFtlStatsGet()->metadata_reads==nand_reads);
    CHECK(LeaFtlStatsGet()->gc_programs==128);
    return 0;
}
