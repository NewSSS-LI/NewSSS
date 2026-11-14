#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "leaftl_memory.h"
#define CHECK(c) do { if(!(c)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);} } while(0)
static unsigned char disk[4][LEAFTL_FLASH_PAGE_BYTES],saved[LEAFTL_FLASH_PAGE_BYTES];
static unsigned char encoded[LEAFTL_FLASH_PAGE_BYTES],staging[LEAFTL_FLASH_PAGE_BYTES];
static unsigned int writes;static int fail_write,fail_read,fail_at=-1;
static LEAFTL_FLASH flash;
static LEAFTL_INDEX_WORKSPACE decoded;
static LEAFTL_INDEX tiny;
static LEAFTL_INDEX_GROUP tiny_groups[1];
static LEAFTL_INDEX_LEVEL tiny_levels[1];
static LEAFTL_INDEX_NODE tiny_nodes[1];
static int Append(void *v,const unsigned char *p,unsigned int n,unsigned int *vsa)
{ (void)v;CHECK(n==LEAFTL_FLASH_PAGE_BYTES);if(fail_write || (int)writes==fail_at) return 0;CHECK(writes<4);*vsa=writes;memcpy(disk[writes++],p,n);return 1; }
static int Read(void *v,unsigned int vsa,unsigned char *p,unsigned int n)
{ (void)v;if(fail_read) return 0;CHECK(vsa<writes);memcpy(p,disk[vsa],n);return 1; }
static void Insert(LEAFTL_MAPPING *m,unsigned int lpn,unsigned int ppn)
{
    LEAFTL_TRAINING_POINT point;unsigned int token;
    point.lpn=lpn;point.ppn=ppn;
    CHECK(LeaFtlMappingPrepare(m,&point,1,&token)==0 && LeaFtlMappingComplete(m,token,1)==0);
}
static void Clear(LEAFTL_MAPPING *m)
{
    LEAFTL_INDEX *i=m->index;
    CHECK(LeaFtlIndexInit(i,i->groups,i->group_capacity,i->levels,i->level_capacity,i->nodes,i->node_capacity)==0);
}
static unsigned int Get32(const unsigned char *p)
{ return (unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24); }
static unsigned int ReferenceCrc(const unsigned char *p,unsigned int n)
{
    unsigned int i,j;uint32_t crc=0xffffffffU;
    for(i=0;i<n;i++) {crc^=p[i];for(j=0;j<8;j++) crc=(crc&1U) ? (crc>>1)^0xedb88320U : crc>>1;}
    return crc^0xffffffffU;
}
int main(void)
{
    size_t bytes=(size_t)LEAFTL_RAM_BYTES(512);
    void *raw1=malloc(bytes+63),*raw2=malloc(bytes+63);unsigned char *arena1,*arena2;
    LEAFTL_MAPPING *m1,*m2;LEAFTL_FLASH_DIRECTORY_ENTRY *directory,old[2];
    LEAFTL_FLASH_IO io={0,Append,Read};LEAFTL_SEGMENT_INPUT input;
    LEAFTL_TRAINING_POINT point={0,100};
    unsigned int length,other,value,i,crb_at,ids[2]={0,1},stored,token;
    CHECK(raw1 && raw2);arena1=(unsigned char *)(((uintptr_t)raw1+63U)&~(uintptr_t)63U);
    arena2=(unsigned char *)(((uintptr_t)raw2+63U)&~(uintptr_t)63U);
    CHECK(LeaFtlMemoryInit(arena1,bytes,512,&m1)==0 && LeaFtlMemoryInit(arena2,bytes,512,&m2)==0);
    directory=(LEAFTL_FLASH_DIRECTORY_ENTRY *)(arena1+LEAFTL_RAM_DIRECTORY_OFFSET);
    CHECK(sizeof(*directory)==8 && directory[0].vsa==UINT_MAX && directory[1].length==0);
    CHECK(LeaFtlFlashInit(&flash,directory,512,staging,sizeof(staging),&io)==0);
    CHECK(LeaFtlFlashLoad(&flash,m2,0)==LEAFTL_FLASH_MISS);
    memset(&input,0,sizeof(input));input.model.first_offset=0;input.model.length=5;
    input.model.slope_bits=(uint16_t)((LeaFtlFloat16Encode(0.4)&0xfffeU)|1U);input.model.intercept=100;
    LeaFtlMembersAdd(&input.members,0);LeaFtlMembersAdd(&input.members,2);LeaFtlMembersAdd(&input.members,5);
    CHECK(LeaFtlIndexInsert(m1->index,&input,1,m1->workspace)==0);
    Insert(m1,2,200);Insert(m1,255,999);Insert(m1,256,800);
    CHECK(LeaFtlFlashStoreMany(&flash,m1,ids,2,&stored)==0 && stored==2 && writes==1);
    CHECK(directory[0].vsa==0 && directory[1].vsa==0 && directory[0].offset==0 && directory[1].offset==directory[0].length);
    length=directory[0].length;CHECK(length<LEAFTL_FLASH_MAX_BYTES);
    memcpy(saved,disk[0],sizeof(saved));
    CHECK(Get32(saved)==0 && LEAFTL_FLASH_HEADER_BYTES==12);
    CHECK(Get32(saved+LEAFTL_FLASH_PAGE_BYTES-4)==ReferenceCrc(saved,LEAFTL_FLASH_PAGE_BYTES-4));
    for(i=length+directory[1].length;i<LEAFTL_FLASH_PAGE_BYTES-4;i++) CHECK(saved[i]==0xff);
    CHECK(LeaFtlFlashDecode(saved,length,0,&decoded)==0 && decoded.crb.count==2 && decoded.level_count>=2);
    CHECK(LeaFtlFlashEncode(&decoded,0,encoded,sizeof(encoded),&other)==0 && other==length && !memcmp(saved,encoded,length));
    CHECK(LeaFtlFlashLoad(&flash,m2,0)==0 && LeaFtlFlashLoad(&flash,m2,1)==0);
    for(i=0;i<512;i++) {
        unsigned int a=0,b=0;LEAFTL_MAPPING_STATUS sa=LeaFtlMappingLookup(m1,i,&a),sb=LeaFtlMappingLookup(m2,i,&b);
        CHECK(sa==sb && (sa!=LEAFTL_MAPPING_OK || a==b));
    }
    CHECK(LeaFtlFlashLoad(&flash,m2,0)==LEAFTL_FLASH_BUSY);
    memcpy(old,directory,sizeof(old));Insert(m1,2,201);fail_write=1;
    CHECK(LeaFtlFlashStoreMany(&flash,m1,ids,2,&stored)==LEAFTL_FLASH_IO_ERROR && stored==0 && !memcmp(old,directory,sizeof(old)) && writes==1);
    fail_write=0;CHECK(LeaFtlFlashStore(&flash,m1,0)==0 && directory[0].vsa==1 && directory[1].vsa==0);
    Clear(m2);Insert(m2,256,801);fail_read=1;
    CHECK(LeaFtlFlashLoad(&flash,m2,0)==LEAFTL_FLASH_IO_ERROR);fail_read=0;
    disk[1][20]^=1;
    CHECK(LeaFtlFlashLoad(&flash,m2,0)==LEAFTL_FLASH_INVALID);
    CHECK(LeaFtlMappingLookup(m2,256,&value)==0 && value==801 && m2->index->group_count==1);
    disk[1][20]^=1;
    CHECK(LeaFtlFlashDecode(saved,length-1,0,&decoded)==LEAFTL_FLASH_INVALID);
    CHECK(LeaFtlFlashDecode(saved,length,1,&decoded)==LEAFTL_FLASH_INVALID);
    memcpy(encoded,saved,length);encoded[4]=encoded[5]=0;
    CHECK(LeaFtlFlashDecode(encoded,length,0,&decoded)==LEAFTL_FLASH_INVALID);
    memcpy(encoded,saved,length);crb_at=12U+2U*((unsigned int)saved[4]|((unsigned int)saved[5]<<8))+8U*((unsigned int)saved[6]|((unsigned int)saved[7]<<8));
    encoded[crb_at+1]=encoded[crb_at];
    CHECK(LeaFtlFlashDecode(encoded,length,0,&decoded)==LEAFTL_FLASH_INVALID);
    CHECK(LeaFtlFlashDecode(saved,length,0,&decoded)==0);
    CHECK(LeaFtlIndexInit(&tiny,tiny_groups,1,tiny_levels,1,tiny_nodes,1)==0);
    CHECK(LeaFtlIndexRestoreGroup(&tiny,0,&decoded)==LEAFTL_INDEX_NO_SPACE && !tiny.group_count && !tiny.node_count);
    decoded.nodes[decoded.levels[decoded.first].first].next=decoded.levels[decoded.first].first;
    CHECK(LeaFtlFlashEncode(&decoded,0,encoded,sizeof(encoded),&other)==LEAFTL_FLASH_INVALID);
    flash.busy=1;CHECK(LeaFtlFlashStore(&flash,m1,0)==LEAFTL_FLASH_BUSY);flash.busy=0;
    CHECK(LeaFtlMappingPrepare(m1,&point,1,&token)==0);
    CHECK(LeaFtlFlashStore(&flash,m1,0)==LEAFTL_FLASH_BUSY);
    CHECK(LeaFtlMappingAbort(m1,token)==0);
    CHECK(LeaFtlFlashLoad(&flash,m2,0)==0 && LeaFtlMappingLookup(m2,2,&value)==0 && value==201);
    directory[0].offset=LEAFTL_FLASH_PAGE_BYTES-1;Clear(m2);
    CHECK(LeaFtlFlashLoad(&flash,m2,0)==LEAFTL_FLASH_INVALID);
    /* Two bounded large snapshots force two pages; failed second page must
     * leave only the successful prefix published. No per-page update array. */
    Clear(m1);LeaFtlFlashDirectoryInit(directory,2);writes=0;memset(&decoded,0,sizeof(decoded));
    decoded.first=0;decoded.level_count=256;decoded.node_count=1024;
    for(i=0;i<256;i++) {
        unsigned int j;decoded.levels[i].first=i*4;decoded.levels[i].next=i<255 ? i+1 : UINT_MAX;
        for(j=0;j<4;j++) {
            unsigned int n=i*4+j;decoded.nodes[n].model.first_offset=(unsigned char)j;
            decoded.nodes[n].model.intercept=1000+n;decoded.nodes[n].next=j<3 ? n+1 : UINT_MAX;
        }
    }
    CHECK(LeaFtlIndexRestoreGroup(m1->index,0,&decoded)==0 && LeaFtlIndexRestoreGroup(m1->index,1,&decoded)==0);
    fail_at=1;CHECK(LeaFtlFlashStoreMany(&flash,m1,ids,2,&stored)==LEAFTL_FLASH_IO_ERROR && stored==1 && writes==1);
    CHECK(directory[0].vsa==0 && directory[1].vsa==UINT_MAX);fail_at=-1;
    puts("PASS: packed groups/offsets, 512 lookup roundtrip, CRC, malformed input, failed page atomicity, two-page partial commit, stale-load/busy/capacity guards");
    printf("header=%u directory=%u record=%u arena(512 LPN)=%u\n",LEAFTL_FLASH_HEADER_BYTES,(unsigned int)sizeof(*directory),length,(unsigned int)bytes);
    free(raw1);free(raw2);return 0;
}
