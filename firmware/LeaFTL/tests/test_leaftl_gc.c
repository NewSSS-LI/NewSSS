#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "leaftl_memory.h"
#include "leaftl_gc.h"
#define CHECK(c) do { if(!(c)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);} } while(0)
static LEAFTL_MAPPING *map;
static LEAFTL_WRITEBACK_WORK work;
static unsigned int reverse_map[4],next_ppn,temp,reads,programs,erases,freed,abandons;
static int fail_read,fail_program,fail_erase,overlap;
static void *arena;
static size_t bytes;
static void Seed(unsigned int lpn,unsigned int ppn)
{
    LEAFTL_TRAINING_POINT point; unsigned int token;
    point.lpn=lpn; point.ppn=ppn;
    CHECK(LeaFtlMappingPrepare(map,&point,1,&token)==0);
    CHECK(LeaFtlMappingComplete(map,token,1)==0);
}
static void Reset(void)
{
    CHECK(LeaFtlMemoryInit(arena,bytes,512,&map)==0);
    memset(&work,0,sizeof(work));
    reverse_map[0]=2;reverse_map[1]=0;reverse_map[2]=300;reverse_map[3]=9;
    Seed(2,100);Seed(0,101);Seed(300,102);Seed(9,900); /* stale reverse */
    next_ppn=200;reads=programs=erases=freed=abandons=0;
    fail_read=fail_program=-1;fail_erase=overlap=0;
}
static void Drain(void *v) { (void)v; }
static unsigned int Reverse(void *v,unsigned int ppn)
{ (void)v; CHECK(ppn>=100 && ppn<104);return reverse_map[ppn-100]; }
static unsigned int Reserve(void *v,unsigned int n,unsigned int *first)
{ (void)v;*first=overlap ? 100U : next_ppn; next_ppn+=n;return n; }
static int Read(void *v,unsigned int ppn)
{ (void)v;temp=ppn;return (int)reads++!=fail_read; }
static int Program(void *v,unsigned int lpn,unsigned int ppn)
{
    unsigned int current; (void)v;
    CHECK(map->pending_token && temp>=100 && temp<104 && reverse_map[temp-100]==lpn);
    CHECK(LeaFtlMappingLookup(map,lpn,&current)==0 && current==temp && current!=ppn);
    return (int)programs++!=fail_program;
}
static void Publish(void *v,unsigned int lpn,unsigned int ppn,unsigned int old)
{
    unsigned int actual; (void)v;
    CHECK(LeaFtlMappingLookup(map,lpn,&actual)==0 && actual==ppn);
    CHECK(old>=100 && old<104); reverse_map[old-100]=LEAFTL_LPN_NONE;
}
static void Abandon(void *v) { (void)v;abandons++; }
static int Erase(void *v)
{
    unsigned int i,current; (void)v; erases++;
    for(i=0;i<4;i++) if(reverse_map[i]!=LEAFTL_LPN_NONE)
        CHECK(LeaFtlMappingLookup(map,reverse_map[i],&current)!=0 || current!=100+i);
    if(fail_erase) return 0;
    freed++;return 1;
}
int main(void)
{
    void *raw; unsigned int moved,current;
    LEAFTL_GC_IO io={0,100,4,Reverse,Drain,Reserve,Read,Program,Publish,Abandon,Erase};
    bytes=(size_t)LEAFTL_RAM_BYTES(512);raw=malloc(bytes+63);CHECK(raw);
    arena=(void *)(((uintptr_t)raw+63U)&~(uintptr_t)63U);
    Reset();CHECK(LeaFtlGcRelocate(map,&work,&io,&moved)==0 && moved==3);
    CHECK(reads==3 && programs==3 && erases==1 && freed==1);
    CHECK(LeaFtlMappingLookup(map,0,&current)==0 && current==200);
    CHECK(LeaFtlMappingLookup(map,2,&current)==0 && current==201);
    CHECK(LeaFtlMappingLookup(map,300,&current)==0 && current==202);
    CHECK(LeaFtlMappingLookup(map,9,&current)==0 && current==900);
    Reset();fail_read=0;
    CHECK(LeaFtlGcRelocate(map,&work,&io,&moved)==LEAFTL_MAPPING_PROGRAM_FAILED);
    CHECK(!programs && !erases && !freed && abandons==1);
    CHECK(LeaFtlMappingLookup(map,0,&current)==0 && current==101);
    Reset();fail_program=1;
    CHECK(LeaFtlGcRelocate(map,&work,&io,&moved)==LEAFTL_MAPPING_PROGRAM_FAILED);
    CHECK(!moved && !erases && !map->pending_token);
    CHECK(LeaFtlMappingLookup(map,2,&current)==0 && current==100);
    Reset();fail_read=2;
    CHECK(LeaFtlGcRelocate(map,&work,&io,&moved)==LEAFTL_MAPPING_PROGRAM_FAILED && moved==2);
    CHECK(!erases && LeaFtlMappingLookup(map,300,&current)==0 && current==102);
    fail_read=-1;
    CHECK(LeaFtlGcRelocate(map,&work,&io,&moved)==0 && moved==1 && freed==1);
    Reset();fail_erase=1;
    CHECK(LeaFtlGcRelocate(map,&work,&io,&moved)==LEAFTL_MAPPING_PROGRAM_FAILED && !freed);
    fail_erase=0;programs=0;
    CHECK(LeaFtlGcRelocate(map,&work,&io,&moved)==0 && !moved && !programs && freed==1);
    Reset();overlap=1;
    CHECK(LeaFtlGcRelocate(map,&work,&io,&moved)==LEAFTL_MAPPING_INVALID && !erases && !programs);
    puts("PASS: GC latest-map validity, sorted actual-PPN relearning, stale reverse, read/program failures, partial retry, erase-before-free, overlap rejection");
    free(raw);return 0;
}
