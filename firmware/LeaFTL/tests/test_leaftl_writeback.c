#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "leaftl_memory.h"
#define CHECK(c) do { if(!(c)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);} } while(0)
static LEAFTL_WRITEBACK_WORK work;
static LEAFTL_MAPPING *mapping;
static unsigned int lpns[6]={300,2,255,0,256,99};
static unsigned int dirty[6]={1,1,1,1,1,0};
static unsigned int next_ppn=1000,programs,published,abandoned,drained,fail_at=UINT_MAX;
static void Drain(void *ctx) { (void)ctx; drained=1; }
static int Inspect(void *ctx,unsigned int id,unsigned int *lpn)
{ (void)ctx; CHECK(drained); *lpn=lpns[id]; return (int)dirty[id]; }
static unsigned int Reserve(void *ctx,unsigned int n,unsigned int *first)
{
    unsigned int i; (void)ctx;
    for(i=1;i<n;i++) CHECK(work.entries[i-1].lpn<work.entries[i].lpn);
    *first=next_ppn; next_ppn+=n; return n;
}
static int Program(void *ctx,unsigned int id,unsigned int lpn,unsigned int ppn)
{
    unsigned int old=UINT_MAX; (void)ctx;
    CHECK(dirty[id] && lpns[id]==lpn && mapping->pending_token);
    /* Program callback must still see the previously published map. */
    if(LeaFtlMappingLookup(mapping,lpn,&old)==LEAFTL_MAPPING_OK) CHECK(old!=ppn);
    return programs++!=fail_at;
}
static void Publish(void *ctx,unsigned int id,unsigned int lpn,unsigned int ppn,unsigned int old)
{
    unsigned int actual=UINT_MAX; (void)ctx; (void)old;
    CHECK(LeaFtlMappingLookup(mapping,lpn,&actual)==LEAFTL_MAPPING_OK && actual==ppn);
    CHECK(dirty[id]); dirty[id]=0; published++;
}
static void Abandon(void *ctx) { (void)ctx; abandoned++; }
int main(void)
{
    size_t bytes=(size_t)LEAFTL_RAM_BYTES(512U);
    void *raw=malloc(bytes+63U);
    void *arena=(void *)(((uintptr_t)raw+63U)&~(uintptr_t)63U);
    LEAFTL_WRITEBACK_IO io={0,6,Drain,Inspect,Reserve,Program,Publish,Abandon};
    unsigned int n,value;
    CHECK(raw && LeaFtlMemoryInit(arena,bytes,512,&mapping)==LEAFTL_MAPPING_OK);
    CHECK(LeaFtlWritebackFlush(mapping,&work,&io,&n)==LEAFTL_MAPPING_OK && n==5);
    CHECK(programs==5 && published==5 && !abandoned);
    CHECK(LeaFtlMappingLookup(mapping,0,&value)==0 && value==1000);
    CHECK(LeaFtlMappingLookup(mapping,2,&value)==0 && value==1001);
    CHECK(LeaFtlMappingLookup(mapping,255,&value)==0 && value==1002);
    CHECK(LeaFtlMappingLookup(mapping,256,&value)==0 && value==1003);
    CHECK(LeaFtlMappingLookup(mapping,300,&value)==0 && value==1004);
    /* Failure after one successful physical program in a same-group batch. */
    dirty[1]=dirty[3]=1; fail_at=programs+1;
    CHECK(LeaFtlWritebackFlush(mapping,&work,&io,&n)==LEAFTL_MAPPING_PROGRAM_FAILED);
    CHECK(n==0 && dirty[1] && dirty[3] && abandoned==1 && !mapping->pending_token);
    CHECK(LeaFtlMappingLookup(mapping,0,&value)==0 && value==1000);
    CHECK(LeaFtlMappingLookup(mapping,2,&value)==0 && value==1001);
    fail_at=UINT_MAX;
    CHECK(LeaFtlWritebackFlush(mapping,&work,&io,&n)==0 && n==2);
    CHECK(LeaFtlMappingLookup(mapping,0,&value)==0 && value==1007);
    CHECK(LeaFtlMappingLookup(mapping,2,&value)==0 && value==1008);
    work.busy=1; CHECK(LeaFtlWritebackFlush(mapping,&work,&io,&n)==LEAFTL_MAPPING_BUSY); work.busy=0;
    CHECK(LeaFtlWritebackFlush(mapping,&work,&io,&n)==0 && n==0);
    dirty[0]=dirty[1]=1; lpns[0]=lpns[1]; value=programs;
    CHECK(LeaFtlWritebackFlush(mapping,&work,&io,&n)==LEAFTL_MAPPING_INVALID && programs==value);
    printf("PASS: sorted IDs, actual consecutive PPN across groups, pending visibility, failure/retry, busy, empty, duplicate rejection\n");
    printf("writeback scratch=%u, arena=%u\n",(unsigned int)sizeof(work),(unsigned int)bytes);
    free(raw); return 0;
}
