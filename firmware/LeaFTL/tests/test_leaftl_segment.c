#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "leaftl_segment.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static unsigned int batches=0U,crb_models=0U,exact_models=0U,seed=0x13579bdfU;
static unsigned int Random(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
static unsigned int Verify(const LEAFTL_TRAINING_POINT *points,unsigned int count,unsigned int group)
{
    LEAFTL_SEGMENT_INPUT outputs[256],before[256];
    LEAFTL_CRB crb;
    unsigned int map[256],n=0U,x,j;
    LeaFtlCrbInit(&crb);
    for(x=0U;x<256U;++x) map[x]=LEAFTL_PPN_NONE;
    for(x=0U;x<count;++x) map[LEAFTL_GROUP_OFFSET(points[x].lpn)]=points[x].ppn;
    CHECK(LeaFtlSegmentTrain(points,count,outputs,256U,&n)==LEAFTL_SEGMENT_OK);
    CHECK(n<=count);
    memcpy(before,outputs,n*sizeof(outputs[0]));
    for(j=0U;j<n;++j) {
        CHECK(LeaFtlSegmentInputValidate(&outputs[j]));
        if(LeaFtlSegmentIsCrb(&outputs[j].model)) {
            CHECK(LeaFtlCrbPut(&crb,&outputs[j].members)==LEAFTL_CRB_OK); ++crb_models;
        } else ++exact_models;
    }
    CHECK(LeaFtlCrbValidate(&crb));
    for(x=0U;x<256U;++x) {
        unsigned int matches=0U,lpn=group*256U+x;
        for(j=0U;j<n;++j) {
            unsigned int ppn=0x12345678U;
            LEAFTL_SEGMENT_STATUS status=LeaFtlSegmentPredict(&outputs[j].model,&crb,group,lpn,&ppn);
            if(LeaFtlSegmentContains(&outputs[j].model,&crb,group,lpn)) {
                CHECK(status==LEAFTL_SEGMENT_OK && map[x]!=LEAFTL_PPN_NONE && ppn==map[x]);
                CHECK(ceil(LeaFtlFloat16Decode(outputs[j].model.slope_bits & 0xfffeU)*(double)x+
                    outputs[j].model.intercept)==(double)ppn);
                ++matches;
            } else {
                CHECK(status==(lpn==LEAFTL_LPN_NONE ? LEAFTL_SEGMENT_INVALID : LEAFTL_SEGMENT_MISS));
                CHECK(ppn==0x12345678U);
            }
        }
        CHECK(matches==(map[x]!=LEAFTL_PPN_NONE ? 1U : 0U));
    }
    CHECK(memcmp(before,outputs,n*sizeof(outputs[0]))==0); ++batches; return n;
}
static void Rules(void)
{
    LEAFTL_TRAINING_POINT points[256]; LEAFTL_SEGMENT_INPUT models[256];
    LEAFTL_CRB crb; unsigned int n,i,ppn=999U;
    CHECK(Verify(0,0U,0U)==0U);
    for(i=0U;i<256U;++i) { points[i].lpn=i; points[i].ppn=1000U+i; }
    CHECK(Verify(points,256U,0U)==1U);
    CHECK(LeaFtlSegmentTrain(points,256U,models,256U,&n)==LEAFTL_SEGMENT_OK);
    CHECK(!LeaFtlSegmentIsCrb(&models[0].model));
    for(i=0U;i<128U;++i) { points[i].lpn=i*2U; points[i].ppn=1000U+i; }
    CHECK(Verify(points,128U,0U)==1U);
    CHECK(LeaFtlSegmentTrain(points,128U,models,256U,&n)==LEAFTL_SEGMENT_OK);
    CHECK(!LeaFtlSegmentIsCrb(&models[0].model));
    CHECK(!LeaFtlSegmentContains(&models[0].model,0,0U,1U)); /* ceil(1/K)=2 */
    points[0].lpn=0U; points[1].lpn=1U; points[2].lpn=4U; points[3].lpn=5U;
    for(i=0U;i<4U;++i) points[i].ppn=100U+points[i].lpn;
    CHECK(Verify(points,4U,0U)==1U);
    CHECK(LeaFtlSegmentTrain(points,4U,models,256U,&n)==LEAFTL_SEGMENT_OK);
    CHECK(LeaFtlSegmentIsCrb(&models[0].model)); /* No splitting at stride changes. */
    CHECK(LeaFtlSegmentPredict(&models[0].model,0,0U,0U,&ppn)==LEAFTL_SEGMENT_INVALID && ppn==999U);
    LeaFtlCrbInit(&crb); CHECK(LeaFtlCrbPut(&crb,&models[0].members)==LEAFTL_CRB_OK);
    CHECK(LeaFtlSegmentPredict(&models[0].model,&crb,0U,2U,&ppn)==LEAFTL_SEGMENT_MISS && ppn==999U);
    /* Trimmed singleton must retain nonzero K and unchanged integer I. */
    models[0].model.slope_bits=LeaFtlFloat16Encode(1.0); models[0].model.intercept=100U;
    models[0].model.first_offset=5U; models[0].model.length=0U;
    CHECK(LeaFtlSegmentPredict(&models[0].model,0,0U,5U,&ppn)==LEAFTL_SEGMENT_OK && ppn==105U);
    for(i=0U;i<255U;++i) { points[i].lpn=0xffffff00U+i; points[i].ppn=0xfffffffeU-i; }
    CHECK(Verify(points,255U,0x00ffffffU)==1U);
    for(i=0U;i<256U;++i) { points[i].lpn=i; points[i].ppn=20000001U+i; }
    CHECK(Verify(points,256U,0U)==1U);
    for(i=0U;i<256U;++i) points[i].ppn=0xfffffffeU-i;
    CHECK(Verify(points,256U,0U)==1U); /* Negative slope -> CRB, exact address. */
}
static void Errors(void)
{
    LEAFTL_TRAINING_POINT p[3]={{0U,0U},{1U,100000U},{2U,200000U}};
    LEAFTL_SEGMENT_INPUT out[3],before[3]; unsigned int count,required;
    memset(out,0xa5,sizeof(out)); memcpy(before,out,sizeof(out));
    CHECK(LeaFtlSegmentTrain(p,3U,0,0U,&required)==LEAFTL_SEGMENT_NO_SPACE && required==3U);
    CHECK(LeaFtlSegmentTrain(p,3U,out,2U,&count)==LEAFTL_SEGMENT_NO_SPACE && count==3U);
    CHECK(memcmp(out,before,sizeof(out))==0);
    CHECK(LeaFtlSegmentTrain(0,1U,out,3U,&count)==LEAFTL_SEGMENT_INVALID && count==0U);
    CHECK(LeaFtlSegmentTrain(p,257U,out,3U,&count)==LEAFTL_SEGMENT_INVALID);
    CHECK(LeaFtlSegmentTrain(p,1U,0,1U,&count)==LEAFTL_SEGMENT_INVALID);
    CHECK(LeaFtlSegmentTrain(p,1U,out,3U,0)==LEAFTL_SEGMENT_INVALID);
    p[1].lpn=0U; CHECK(LeaFtlSegmentTrain(p,2U,out,3U,&count)==LEAFTL_SEGMENT_INVALID);
    p[0].lpn=255U; p[1].lpn=256U;
    CHECK(LeaFtlSegmentTrain(p,2U,out,3U,&count)==LEAFTL_SEGMENT_INVALID);
    p[0].lpn=LEAFTL_LPN_NONE;
    CHECK(LeaFtlSegmentTrain(p,1U,out,3U,&count)==LEAFTL_SEGMENT_INVALID);
    p[0].lpn=0U; p[0].ppn=LEAFTL_PPN_NONE;
    CHECK(LeaFtlSegmentTrain(p,1U,out,3U,&count)==LEAFTL_SEGMENT_INVALID);
    CHECK(memcmp(out,before,sizeof(out))==0);
}
static void Packed8(void)
{
    LEAFTL_SEGMENT model={0},decoded,before;
    LEAFTL_TRAINING_POINT points[3]={{100U,0U},{101U,1U},{102U,2U}};
    LEAFTL_SEGMENT_INPUT inputs[256];
    unsigned char bytes[8],golden[8]={5U,10U,0U,0x38U,4U,3U,2U,1U};
    unsigned int count,i;
    CHECK(sizeof(model)==8U && offsetof(LEAFTL_SEGMENT,intercept)==4U);
    model.first_offset=5U;model.length=10U;model.slope_bits=0x3800U;model.intercept=0x01020304U;
    CHECK(LeaFtlSegmentEncode8(&model,bytes)==LEAFTL_SEGMENT_OK && memcmp(bytes,golden,8U)==0);
    CHECK(LeaFtlSegmentDecode8(bytes,&decoded)==LEAFTL_SEGMENT_OK && memcmp(&model,&decoded,8U)==0);
    before=decoded;bytes[1]=251U;
    CHECK(LeaFtlSegmentDecode8(bytes,&decoded)==LEAFTL_SEGMENT_INVALID && memcmp(&before,&decoded,8U)==0);
    /* A negative/fractional I cannot be smuggled into unsigned 4-byte I.
     * Split instead, preserving all 32-bit addresses with singleton I.
     */
    CHECK(LeaFtlSegmentTrain(points,3U,inputs,256U,&count)==LEAFTL_SEGMENT_OK && count==3U);
    CHECK(Verify(points,3U,0U)==3U);
    for(i=0U;i<count;++i) {
        CHECK(inputs[i].model.length==0U);
        CHECK(LeaFtlSegmentEncode8(&inputs[i].model,bytes)==LEAFTL_SEGMENT_OK);
        CHECK(LeaFtlSegmentDecode8(bytes,&decoded)==LEAFTL_SEGMENT_OK);
    }
    points[0].lpn=255U;points[0].ppn=0xfffffffeU;
    CHECK(Verify(points,1U,0U)==1U);
    CHECK(LeaFtlSegmentTrain(points,1U,inputs,256U,&count)==LEAFTL_SEGMENT_OK);
    CHECK(inputs[0].model.intercept==0xfffffffeU);
    CHECK(LeaFtlSegmentEncode8(&inputs[0].model,bytes)==LEAFTL_SEGMENT_OK);
    CHECK(LeaFtlSegmentDecode8(bytes,&decoded)==LEAFTL_SEGMENT_OK && decoded.intercept==0xfffffffeU);
    model.first_offset=0U;model.length=255U;model.slope_bits=0x3c00U;model.intercept=100U;
    CHECK(LeaFtlSegmentEncode8(&model,bytes)==LEAFTL_SEGMENT_OK);
    CHECK(LeaFtlSegmentDecode8(bytes,&decoded)==LEAFTL_SEGMENT_OK && LEAFTL_SEGMENT_LAST(&decoded)==255U);
}

int main(int argc,char **argv)
{
    LEAFTL_TRAINING_POINT points[256]; unsigned int count,e,i,b;
    Rules(); Errors(); Packed8();
    if(argc>1 && strcmp(argv[1],"--targeted")==0) {
        printf("PASS: %u core segment batches x 256 offsets; 8-byte field/golden/roundtrip/bounds checks\n",batches);
        return 0;
    }
    for(count=1U;count<=5U;++count) for(e=0U;e<(1U<<(2U*count));++e) {
        for(i=0U;i<count;++i) { points[i].lpn=256U+i*3U; points[i].ppn=(e>>(2U*i))&3U; }
        (void)Verify(points,count,1U);
    }
    for(b=0U;b<4000U;++b) {
        unsigned int group=Random()%0x01000000U; count=0U;
        for(i=0U;i<256U;++i) if((Random()&3U)!=0U && group*256U+i!=LEAFTL_LPN_NONE) {
            unsigned int ppn=Random(); points[count].lpn=group*256U+i;
            points[count++].ppn=ppn==LEAFTL_PPN_NONE ? 0U : ppn;
        }
        (void)Verify(points,count,group);
    }
    for(b=0U;b<1000U;++b) {
        unsigned int numerator=Random()%65536U,den=1U+Random()%255U,step=1U+Random()%16U;
        count=0U;
        for(i=0U;i<256U;i+=step) {
            unsigned int product=numerator*i,q=product/den;
            points[count].lpn=1024U+i;
            points[count++].ppn=(b&1U) ? 20000000U-q : 100U+q+(product%den!=0U);
        }
        (void)Verify(points,count,4U);
    }
    CHECK(crb_models>0U && exact_models>0U);
    printf("PASS: %u segment oracle batches, all 256 offsets; exact=%u CRB=%u models\n",batches,exact_models,crb_models);
    printf("Runtime segment=%u CRB=%u input=%u bytes\n",(unsigned)sizeof(LEAFTL_SEGMENT),
        (unsigned)sizeof(LEAFTL_CRB),(unsigned)sizeof(LEAFTL_SEGMENT_INPUT));
    return 0;
}
