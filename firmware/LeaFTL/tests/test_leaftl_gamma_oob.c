#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "leaftl_flash.h"
#include "leaftl_oob.h"
#define CHECK(c) do { if(!(c)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);} } while(0)
static LEAFTL_INDEX index;
static LEAFTL_INDEX_GROUP groups[2];
static LEAFTL_INDEX_LEVEL levels[512];
static LEAFTL_INDEX_NODE nodes[2048];
static LEAFTL_INDEX_WORKSPACE work;
static LEAFTL_SEGMENT_INPUT segments[256];
static unsigned char spare[256],before[256],record[LEAFTL_FLASH_MAX_BYTES];
static const unsigned int lpns[7]={32,33,34,38,76,78,115};
static unsigned int RunLpn(void *v,unsigned int ppn)
{ (void)v;return ppn>=1000U && ppn<1007U ? lpns[ppn-1000U] : LEAFTL_LPN_NONE; }
static unsigned int Identity(void *v,unsigned int ppn) { (void)v;return ppn==LEAFTL_PPN_NONE ? LEAFTL_LPN_NONE : ppn; }
static unsigned int Unknown(void *v,unsigned int ppn) { (void)v;(void)ppn;return LEAFTL_LPN_NONE; }
static unsigned int CrossGroup(void *v,unsigned int ppn)
{ static const unsigned int lpn[4]={254,255,256,300};(void)v;return ppn>=100 && ppn<104 ? lpn[ppn-100] : LEAFTL_LPN_NONE; }
static void Put32(unsigned char *p,unsigned int x)
{ unsigned int i;for(i=0;i<4;i++) p[i]=(unsigned char)(x>>(8U*i)); }
static void Verify(unsigned int overwritten)
{
    unsigned int x,i;
    for(x=0;x<256;x++) {
        unsigned int expected=LEAFTL_PPN_NONE,predicted=1234567,actual=7654321;
        LEAFTL_INDEX_STATUS status;
        for(i=0;i<7;i++) if(x==lpns[i]) expected=1000+i;
        if(overwritten && x==32) expected=2000;
        status=LeaFtlIndexLookup(&index,x,&predicted);
        if(expected==LEAFTL_PPN_NONE) CHECK(status==LEAFTL_INDEX_MISS && predicted==1234567);
        else {
            int64_t error=(int64_t)predicted-expected;
            CHECK(status==0 && error>=-(int64_t)LEAFTL_GAMMA && error<=LEAFTL_GAMMA);
            if(expected!=2000) {
                CHECK(predicted>=1000 && predicted<1007); /* no unknown run tail */
                CHECK(LeaFtlOobBuild(spare,sizeof(spare),predicted,256,LEAFTL_GAMMA,RunLpn,0)==0);
                CHECK(LeaFtlOobFind(spare,sizeof(spare),predicted,256,LEAFTL_GAMMA,x,&actual)==0 && actual==expected);
            } else CHECK(predicted==2000);
        }
    }
}
int main(void)
{
    LEAFTL_TRAINING_POINT points[7];unsigned int i,count,errors=0,length,actual=1234;
    CHECK(sizeof(LEAFTL_SEGMENT)==8);
    for(i=0;i<7;i++) {points[i].lpn=lpns[i];points[i].ppn=1000+i;}
    CHECK(LeaFtlSegmentTrain(points,7,segments,256,&count)==0);
    CHECK(LeaFtlIndexInit(&index,groups,2,levels,512,nodes,2048)==0);
    CHECK(LeaFtlIndexInsert(&index,segments,count,&work)==0);Verify(0);
    for(i=0;i<7;i++) {
        unsigned int predicted;CHECK(LeaFtlIndexLookup(&index,lpns[i],&predicted)==0);
        if(predicted!=points[i].ppn) errors++;
    }
    if(LEAFTL_GAMMA) CHECK(errors>0);else CHECK(!errors);
    /* Exact overwrite of an approximate SLPA must rekey surviving CRB. */
    points[0].lpn=32;points[0].ppn=2000;
    CHECK(LeaFtlSegmentTrain(points,1,segments,256,&i)==0);
    CHECK(LeaFtlIndexInsert(&index,segments,i,&work)==0);Verify(1);
    CHECK(LeaFtlIndexCompactGroup(&index,0,&work,0)==0);Verify(1);
    CHECK(LeaFtlIndexExportGroup(&index,0,&work)==0);
    CHECK(LeaFtlFlashEncode(&work,0,record,sizeof(record),&length)==0);
    CHECK(LeaFtlIndexRemoveGroup(&index,0)==0);
    CHECK(LeaFtlFlashDecode(record,length,0,&work)==0 && LeaFtlIndexRestoreGroup(&index,0,&work)==0);Verify(1);
    if(LEAFTL_GAMMA) {
        points[0].lpn=0;points[0].ppn=0;points[1].lpn=1;points[1].ppn=100;
        CHECK(LeaFtlSegmentTrain(points,2,segments,256,&i)==0 && i==2);
        points[0].lpn=0;points[0].ppn=LEAFTL_PPN_NONE-1U;
        CHECK(LeaFtlSegmentTrain(points,1,segments,256,&i)==0 && i==1 && segments[0].model.intercept==LEAFTL_PPN_NONE-1U);
    }
    /* Full 256-byte USER spare, marker word and block-neighbor padding. */
    CHECK(LeaFtlOobBuild(spare,sizeof(spare),0,256,16,Identity,0)==0);
    CHECK(LeaFtlOobFind(spare,sizeof(spare),0,256,16,0,&actual)==0 && actual==0);
    CHECK(LeaFtlOobFind(spare,sizeof(spare),0,256,16,16,&actual)==0 && actual==16);
    for(i=0;i<4+16*4;i++) CHECK(spare[i]==0xff);
    for(i=LEAFTL_OOB_BYTES(16);i<sizeof(spare);i++) CHECK(spare[i]==0xff);
    CHECK(LeaFtlOobBuild(spare,sizeof(spare),255,256,16,Identity,0)==0);
    CHECK(LeaFtlOobFind(spare,sizeof(spare),255,256,16,255,&actual)==0 && actual==255);
    actual=1234;CHECK(LeaFtlOobFind(spare,sizeof(spare),255,256,16,256,&actual)==LEAFTL_OOB_MISS && actual==1234);
    CHECK(LeaFtlOobBuild(spare,sizeof(spare),256,256,16,Identity,0)==0);
    CHECK(LeaFtlOobFind(spare,sizeof(spare),256,256,16,255,&actual)==LEAFTL_OOB_MISS);
    Put32(spare+4,255);CHECK(LeaFtlOobFind(spare,sizeof(spare),256,256,16,256,&actual)==LEAFTL_OOB_INVALID);
    CHECK(LeaFtlOobBuild(spare,sizeof(spare),100,256,1,Identity,0)==0);
    Put32(spare+4,100);CHECK(LeaFtlOobFind(spare,sizeof(spare),100,256,1,100,&actual)==LEAFTL_OOB_INVALID);
    memcpy(before,spare,sizeof(spare));
    CHECK(LeaFtlOobBuild(spare,LEAFTL_OOB_BYTES(16)-1,100,256,16,Identity,0)==LEAFTL_OOB_INVALID && !memcmp(before,spare,sizeof(spare)));
    CHECK(LeaFtlOobBuild(spare,sizeof(spare),100,256,1,Unknown,0)==LEAFTL_OOB_INVALID && !memcmp(before,spare,sizeof(spare)));
    CHECK(LeaFtlOobBuild(spare,sizeof(spare),101,256,4,CrossGroup,0)==0);
    CHECK(LeaFtlOobFind(spare,sizeof(spare),101,256,4,256,&actual)==0 && actual==102);
    CHECK(LeaFtlOobFind(spare,sizeof(spare),101,256,4,300,&actual)==0 && actual==103);
    printf("PASS gamma=%u: segments=%u nonzero-errors=%u; bounded half model/CRB holes/overwrite/rekey/compaction/Flash roundtrip, OOB neighbor correction/0/255/block edges/ambiguity/size guards\n",LEAFTL_GAMMA,count,errors);
    return 0;
}
