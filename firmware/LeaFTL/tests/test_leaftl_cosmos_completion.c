/* Compile the real adapter in this TU so its private synchronous tracker can
 * be driven without a hardware address-map initialization. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "leaftl_cosmos.c"
__declspec(dllimport) void *__stdcall VirtualAlloc(void *,size_t,unsigned long,unsigned long);
#define CHECK(c) do { if(!(c)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);} } while(0)
P_DFTL_TRANS_DIE_MAP dftlTransDieMapPtr;
P_REQ_POOL reqPoolPtr;
P_DATA_BUF_MAP dataBufMapPtr;
P_TEMPORARY_DATA_BUF_MAP tempDataBufMapPtr;
P_VIRTUAL_BLOCK_MAP virtualBlockMapPtr;
P_VIRTUAL_SLICE_MAP virtualSliceMapPtr;
P_VIRTUAL_DIE_MAP virtualDieMapPtr;
P_ROW_ADDR_DEPENDENCY_TABLE rowAddrDependencyTablePtr;
static unsigned int status_to_report=REQ_STATUS_DONE,submissions,publications;
unsigned int GetFromFreeReqQ(void) { return 0; }
void PutToFreeReqQ(unsigned int tag) { (void)tag; }
unsigned int GenerateSpareDataBufAddr(unsigned int tag) { (void)tag;return SPARE_DATA_BUFFER_BASE_ADDR; }
void UpdateDataBufEntryInfoBlockingReq(unsigned int id,unsigned int tag)
{ (void)id;(void)tag; }
void UpdateTempDataBufEntryInfoBlockingReq(unsigned int id,unsigned int tag)
{ (void)id;(void)tag; }
unsigned int LeaFtlPpnToVsa(unsigned int ppn) { return ppn*USER_DIES; }
unsigned int LeaFtlVsaToPpn(unsigned int vsa) { return vsa/USER_DIES; }
void SelectLowLevelReqQ(unsigned int tag)
{ CHECK(tag==0); submissions++; }
void SyncAllLowLevelReqDone(void)
{
    if(program_tag==REQ_SLOT_TAG_NONE) return;
    LeaFtlCosmosNandDone(1,REQ_STATUS_DONE); CHECK(program_tag==0);
    if(expected_code==REQ_CODE_READ) {
        unsigned char *spare=ReadSpare(reqPoolPtr->reqPool[0].reqOpt.dataBufFormat,reqPoolPtr->reqPool[0].dataBufInfo.entry);
        unsigned int i,lpn=reqPoolPtr->reqPool[0].logicalSliceAddr;
        memset(spare,0xff,BYTES_PER_SPARE_REGION_OF_SLICE);
        for(i=0;i<4;i++) spare[LEAFTL_OOB_RESERVED_BYTES+4U*LEAFTL_GAMMA+i]=(unsigned char)(lpn>>(8U*i));
        reqPoolPtr->reqPool[0].reqCode=REQ_CODE_READ_TRANSFER;
    }
    LeaFtlCosmosNandDone(0,status_to_report); CHECK(program_tag==REQ_SLOT_TAG_NONE);
    LeaFtlCosmosNandDone(0,REQ_STATUS_FAIL); CHECK(program_tag==REQ_SLOT_TAG_NONE);
}
unsigned int LeaFtlReserveRun(unsigned int n,unsigned int *first)
{ *first=500;return n; }
void LeaFtlAbandonRun(void) {}
void LeaFtlAbandonGcRun(void) {}
void LeaFtlPublishPage(unsigned int lpn,unsigned int ppn,unsigned int old)
{ unsigned int actual;(void)old;CHECK(LeaFtlMappingLookup(mapping,lpn,&actual)==0 && actual==ppn);publications++; }
unsigned int LeaFtlReserveGcRun(unsigned int die,unsigned int n,unsigned int *first)
{ (void)die;*first=500;return n; }
void LeaFtlFreeErasedBlock(unsigned int die,unsigned int block) { (void)die;(void)block; }
int main(void)
{
    size_t bytes=(size_t)LEAFTL_RAM_BYTES(512);
    void *raw=malloc(bytes+63),*arena;
    unsigned int token,ppn,count;
    LEAFTL_TRAINING_POINT point={0,100};
    CHECK(VirtualAlloc((void *)(uintptr_t)DATA_BUFFER_BASE_ADDR,0x1000000U,0x3000U,4U)==(void *)(uintptr_t)DATA_BUFFER_BASE_ADDR);
    CHECK(raw);arena=(void *)(((uintptr_t)raw+63U)&~(uintptr_t)63U);
    CHECK(LeaFtlMemoryInit(arena,bytes,512,&mapping)==0);
    work=(LEAFTL_WRITEBACK_WORK *)((unsigned char *)arena+LEAFTL_RAM_WRITEBACK_OFFSET);work->busy=0;
    reqPoolPtr=(P_REQ_POOL)calloc(1,sizeof(*reqPoolPtr));
    dataBufMapPtr=(P_DATA_BUF_MAP)calloc(1,sizeof(*dataBufMapPtr));
    virtualBlockMapPtr=(P_VIRTUAL_BLOCK_MAP)calloc(1,sizeof(*virtualBlockMapPtr));
    rowAddrDependencyTablePtr=(P_ROW_ADDR_DEPENDENCY_TABLE)calloc(1,sizeof(*rowAddrDependencyTablePtr));
    CHECK(reqPoolPtr && dataBufMapPtr && virtualBlockMapPtr);
    CHECK(rowAddrDependencyTablePtr);
    virtualBlockMapPtr->block[0][0].currentPage=256;
    rowAddrDependencyTablePtr->block[0][0][0].permittedProgPage=256;
    CHECK(LeaFtlMappingPrepare(mapping,&point,1,&token)==0);
    CHECK(LeaFtlMappingComplete(mapping,token,1)==0);
    CHECK(LeaFtlCosmosLoad(0,0)==1 && submissions==1);
    status_to_report=REQ_STATUS_FAIL;
    CHECK(LeaFtlCosmosLoad(0,0)==0 && virtualBlockMapPtr->block[0][0].bad);
    CHECK(LeaFtlMappingLookup(mapping,0,&ppn)==0 && ppn==100);
    status_to_report=REQ_STATUS_WARNING; CHECK(LeaFtlCosmosLoad(0,0)==1);
    dataBufMapPtr->dataBuf[0].logicalSliceAddr=0;
    dataBufMapPtr->dataBuf[0].dirty=DATA_BUF_DIRTY;
    dataBufMapPtr->dataBuf[0].blockingReqTail=REQ_SLOT_TAG_NONE;
    status_to_report=REQ_STATUS_FAIL;
    CHECK(!LeaFtlCosmosFlush() && LeaFtlCosmosLastStatus()==LEAFTL_MAPPING_PROGRAM_FAILED);
    CHECK(dataBufMapPtr->dataBuf[0].dirty && !publications && !mapping->pending_token);
    status_to_report=REQ_STATUS_DONE;
    CHECK(LeaFtlCosmosFlush() && !dataBufMapPtr->dataBuf[0].dirty && publications==1);
    count=submissions;point.ppn=USER_PAGES_PER_SSD;
    CHECK(LeaFtlMappingPrepare(mapping,&point,1,&token)==0 && LeaFtlMappingComplete(mapping,token,1)==0);
    CHECK(!LeaFtlCosmosLoad(0,0) && submissions==count);
    puts("PASS: real Cosmos adapter read/failure/warning, wrong/duplicate completion, failed write retention/retry, address bounds");
    free(reqPoolPtr);free(dataBufMapPtr);free(virtualBlockMapPtr);free(rowAddrDependencyTablePtr);free(raw);return 0;
}
