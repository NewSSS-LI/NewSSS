#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "leaftl_memory.h"
#define CHECK(c) do { if(!(c)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);} } while(0)
static unsigned char disk[6][LEAFTL_FLASH_PAGE_BYTES],written[6];
static unsigned char stage[LEAFTL_FLASH_PAGE_BYTES],read_page[LEAFTL_FLASH_PAGE_BYTES];
static unsigned char gc_page[LEAFTL_FLASH_PAGE_BYTES],protected_record[LEAFTL_FLASH_MAX_BYTES];
static LEAFTL_METADATA pool;static LEAFTL_METADATA_BLOCK blocks[3];
static LEAFTL_CACHE cache;static LEAFTL_FLASH flash;
static unsigned int reads,programs,erases;static int fail_program,fail_read;
static int Program(void *v,unsigned int vsa,const unsigned char *page)
{ (void)v;CHECK(vsa<6 && !written[vsa]);if(fail_program) return 0;memcpy(disk[vsa],page,sizeof(stage));written[vsa]=1;programs++;return 1; }
static int Read(void *v,unsigned int vsa,unsigned char *page)
{ (void)v;CHECK(vsa<6 && written[vsa]);reads++;if(fail_read) return 0;memcpy(page,disk[vsa],sizeof(stage));return 1; }
static int Erase(void *v,unsigned int base)
{ (void)v;CHECK(base<6 && base%2==0);written[base]=written[base+1]=0;erases++;return 1; }
static LEAFTL_MAPPING_STATUS Write(LEAFTL_MAPPING *m,unsigned int lpn,unsigned int ppn)
{
    unsigned int token;LEAFTL_TRAINING_POINT point;LEAFTL_MAPPING_STATUS s;
    point.lpn=lpn;point.ppn=ppn;s=LeaFtlMappingPrepare(m,&point,1,&token);
    return s==0 ? LeaFtlMappingComplete(m,token,1) : s;
}
static void Expect(LEAFTL_MAPPING *m,unsigned int lpn,unsigned int ppn)
{ unsigned int value=UINT_MAX;LEAFTL_MAPPING_STATUS s=LeaFtlMappingLookup(m,lpn,&value);if(s || value!=ppn) fprintf(stderr,"lpn=%u expected=%u actual=%u status=%d reads=%u programs=%u erases=%u\n",lpn,ppn,value,s,reads,programs,erases);CHECK(s==0 && value==ppn); }
int main(void)
{
    size_t bytes=(size_t)LEAFTL_RAM_BYTES(1024);void *raw=malloc(bytes+63);unsigned char *arena;
    LEAFTL_MAPPING *m;LEAFTL_FLASH_DIRECTORY_ENTRY *directory,old;
    LEAFTL_METADATA_IO raw_io={0,Program,Read,Erase};LEAFTL_FLASH_IO io={&pool,LeaFtlMetadataAppend,LeaFtlMetadataRead};
    unsigned int before,i,value,token;LEAFTL_TRAINING_POINT point={1,101};
    CHECK(raw);arena=(unsigned char *)(((uintptr_t)raw+63U)&~(uintptr_t)63U);
    CHECK(LeaFtlMemoryInit(arena,bytes,1024,&m)==0);
    CHECK(LeaFtlIndexInit(m->index,m->index->groups,2,m->index->levels,512,m->index->nodes,2048)==0);
    directory=(LEAFTL_FLASH_DIRECTORY_ENTRY *)(arena+LEAFTL_RAM_DIRECTORY_OFFSET);
    blocks[0].base=0;blocks[1].base=2;blocks[2].base=4;
    CHECK(LeaFtlMetadataInit(&pool,blocks,3,2,1,directory,4,gc_page,&raw_io));
    CHECK(LeaFtlFlashInit(&flash,directory,1024,stage,sizeof(stage),&io)==0);
    CHECK(LeaFtlCacheInit(&cache,m,&flash,100000,read_page,protected_record)==0);
    CHECK(Write(m,0,100)==0 && Write(m,2,102)==0 && Write(m,256,200)==0);
    Expect(m,0,100);CHECK(!reads && !programs);
    CHECK(Write(m,512,300)==0 && programs==1 && !reads);
    CHECK(directory[0].vsa!=UINT_MAX && directory[0].vsa==directory[1].vsa);
    before=reads;CHECK(Write(m,2,999)==0 && reads==before); /* lazy write miss */
    Expect(m,2,999);CHECK(reads==before); /* RAM hit does not fetch old group */
    Expect(m,0,100);CHECK(reads>before);Expect(m,2,999); /* fetch+merge retains both */
    CHECK(LeaFtlMappingLookup(m,1,&value)==LEAFTL_MAPPING_MISS);
    CHECK(LeaFtlMappingPrepare(m,&point,1,&token)==0);
    CHECK(LeaFtlCacheFlush(m)==LEAFTL_MAPPING_BUSY);
    CHECK(LeaFtlMappingComplete(m,token,0)==LEAFTL_MAPPING_PROGRAM_FAILED);
    CHECK(LeaFtlCacheFlush(m)==0);
    /* Dirty partial group eviction must merge Flash too, without a read miss. */
    CHECK(LeaFtlIndexRemoveGroup(m->index,0)==0);
    before=reads;CHECK(Write(m,2,1001)==0 && reads==before);
    CHECK(LeaFtlCacheFlush(m)==0 && reads>before);
    CHECK(LeaFtlIndexRemoveGroup(m->index,0)==0);Expect(m,0,100);Expect(m,2,1001);
    /* Repeated tiny metadata updates force GC with shared packed-page refs. */
    for(i=0;i<10;i++) {CHECK(Write(m,2,1100+i)==0);CHECK(LeaFtlCacheFlush(m)==0);}
    CHECK(erases>0);Expect(m,0,100);Expect(m,2,1109);Expect(m,256,200);
    {
        LEAFTL_TRAINING_POINT sparse[3]={{768,400},{770,401},{773,402}};
        CHECK(LeaFtlMappingPrepare(m,sparse,3,&token)==0 && LeaFtlMappingComplete(m,token,1)==0);
        CHECK(LeaFtlCacheFlush(m)==0 && LeaFtlIndexRemoveGroup(m->index,3)==0);
        before=reads;CHECK(Write(m,770,999)==0 && reads==before);
        Expect(m,768,400);Expect(m,770,999);Expect(m,773,402);
    }
    CHECK(Write(m,2,1200)==0);old=directory[0];fail_program=1;
    CHECK(LeaFtlCacheFlush(m)!=0);fail_program=0;
    CHECK(LeaFtlMappingLookup(m,2,&value)==0 && value==1200);
    /* A failed append never publishes its failed physical page. GC may move
     * older snapshots first, so compare old logical content, not raw VSA. */
    CHECK(directory[0].vsa!=UINT_MAX && old.length==directory[0].length);
    puts("PASS: lazy write miss, RAM-hit no read, deferred merge/newest wins/holes, dirty eviction merge, pending pin, packed metadata, metadata GC, failed-write RAM retention");
    printf("reads=%u programs=%u erases=%u cache-bytes=%u\n",reads,programs,erases,LeaFtlCacheBytes(m));
    free(raw);return 0;
}
