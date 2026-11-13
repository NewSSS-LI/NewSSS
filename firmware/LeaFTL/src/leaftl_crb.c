#include <string.h>
#include "leaftl_crb.h"

typedef char CrbFitsBudget[sizeof(LEAFTL_CRB) <= LEAFTL_MAX_CRB_BYTES_PER_GROUP ? 1 : -1];

static int Bit(const unsigned char *bits, unsigned int x)
{ return (bits[x / 8U] & (1U << (x % 8U))) != 0U; }
static void SetBit(unsigned char *bits, unsigned int x, int value)
{
    unsigned char mask = (unsigned char)(1U << (x % 8U));
    if (value) bits[x / 8U] |= mask;
    else bits[x / 8U] &= (unsigned char)~mask;
}
void LeaFtlMembersClear(LEAFTL_MEMBERS *members)
{ memset(members, 0, sizeof(*members)); }
void LeaFtlMembersAdd(LEAFTL_MEMBERS *members, unsigned int offset)
{ if (offset < LEAFTL_GROUP_SIZE) SetBit(members->bits, offset, 1); }
int LeaFtlMembersHas(const LEAFTL_MEMBERS *members, unsigned int offset)
{ return offset < LEAFTL_GROUP_SIZE && Bit(members->bits, offset); }
unsigned int LeaFtlMembersCount(const LEAFTL_MEMBERS *members)
{
    unsigned int x, count=0U;
    for (x=0U; x<LEAFTL_GROUP_SIZE; ++x) if (Bit(members->bits,x)) ++count;
    return count;
}
void LeaFtlCrbInit(LEAFTL_CRB *crb) { memset(crb,0,sizeof(*crb)); }

int LeaFtlCrbValidate(const LEAFTL_CRB *crb)
{
    LEAFTL_MEMBERS seen;
    unsigned int i, records=0U, prior_key=0U;
    if (crb==0 || crb->count>LEAFTL_GROUP_SIZE || crb->records>crb->count) return 0;
    if (crb->count!=0U && !Bit(crb->starts,0U)) return 0;
    LeaFtlMembersClear(&seen);
    for(i=0U;i<crb->count;++i) {
        unsigned int x=crb->offsets[i];
        if (Bit(seen.bits,x)) return 0;
        SetBit(seen.bits,x,1);
        if(Bit(crb->starts,i)) {
            if(records!=0U && x<=prior_key) return 0;
            prior_key=x; ++records;
        } else if(x<=crb->offsets[i-1U]) return 0;
    }
    return records==crb->records;
}

/* Select a record head from the delimiter bitmap. Binary search is over
 * sorted SLPA keys, not over the globally nearly-sorted offset payload.
 */
static unsigned int Head(const LEAFTL_CRB *crb, unsigned int rank)
{
    unsigned int byte;
    for(byte=0U;byte<(crb->count+7U)/8U;++byte) {
        unsigned int bits=crb->starts[byte],count,bit;
        count=bits-((bits>>1)&0x55U);
        count=(count&0x33U)+((count>>2)&0x33U);
        count=(count+(count>>4))&0x0fU;
        if(rank>=count) { rank-=count; continue; }
        for(bit=0U;bit<8U;++bit) if((bits&(1U<<bit))!=0U) {
            if(rank==0U) return byte*8U+bit;
            --rank;
        }
    }
    return crb->count;
}
static unsigned int Find(const LEAFTL_CRB *crb,unsigned int key)
{
    unsigned int low=0U,high=crb->records;
    while(low<high) {
        unsigned int mid=low+(high-low)/2U;
        unsigned int pos=Head(crb,mid);
        if(pos==crb->count) return crb->count;
        if(crb->offsets[pos]<key) low=mid+1U; else high=mid;
    }
    if(low==crb->records) return crb->count;
    {
        unsigned int pos=Head(crb,low);
        return pos<crb->count && crb->offsets[pos]==key ? pos : crb->count;
    }
}
static unsigned int End(const LEAFTL_CRB *crb,unsigned int begin)
{
    unsigned int end=begin+1U;
    while(end<crb->count && !Bit(crb->starts,end)) ++end;
    return end;
}
int LeaFtlCrbContains(const LEAFTL_CRB *crb,unsigned int start,unsigned int offset)
{
    unsigned int low,high,end;
    if(crb==0 || crb->count>LEAFTL_GROUP_SIZE || crb->records>crb->count ||
       start>=LEAFTL_GROUP_SIZE || offset>=LEAFTL_GROUP_SIZE) return 0;
    low=Find(crb,start); if(low==crb->count) return 0;
    end=End(crb,low); high=end;
    while(low<high) {
        unsigned int mid=low+(high-low)/2U;
        if(crb->offsets[mid]<offset) low=mid+1U; else high=mid;
    }
    return low<end && crb->offsets[low]==offset;
}
LEAFTL_CRB_STATUS LeaFtlCrbGet(const LEAFTL_CRB *crb,unsigned int start,
                              LEAFTL_MEMBERS *members)
{
    unsigned int pos,end;
    if(crb==0 || members==0 || crb->count>LEAFTL_GROUP_SIZE ||
       crb->records>crb->count || start>=LEAFTL_GROUP_SIZE) return LEAFTL_CRB_INVALID;
    pos=Find(crb,start); if(pos==crb->count) return LEAFTL_CRB_MISS;
    end=End(crb,pos); LeaFtlMembersClear(members);
    for(;pos<end;++pos) LeaFtlMembersAdd(members,crb->offsets[pos]);
    return LEAFTL_CRB_OK;
}

static void InsertRecord(LEAFTL_CRB *crb,const unsigned char *values,unsigned int count)
{
    unsigned int pos=0U,i;
    while(pos<crb->count) {
        if(crb->offsets[pos]>values[0]) break;
        pos=End(crb,pos);
    }
    for(i=crb->count;i>pos;--i) {
        crb->offsets[i-1U+count]=crb->offsets[i-1U];
        SetBit(crb->starts,i-1U+count,Bit(crb->starts,i-1U));
    }
    for(i=0U;i<count;++i) {
        crb->offsets[pos+i]=values[i]; SetBit(crb->starts,pos+i,i==0U);
    }
    crb->count=(uint16_t)(crb->count+count); ++crb->records;
}
LEAFTL_CRB_STATUS LeaFtlCrbRemove(LEAFTL_CRB *crb,const LEAFTL_MEMBERS *members)
{
    LEAFTL_CRB result;
    unsigned char values[LEAFTL_GROUP_SIZE];
    unsigned int pos=0U;
    if(members==0 || !LeaFtlCrbValidate(crb)) return LEAFTL_CRB_INVALID;
    LeaFtlCrbInit(&result);
    while(pos<crb->count) {
        unsigned int end=End(crb,pos),count=0U;
        for(;pos<end;++pos) if(!LeaFtlMembersHas(members,crb->offsets[pos]))
            values[count++]=crb->offsets[pos];
        if(count!=0U) InsertRecord(&result,values,count);
    }
    *crb=result; return LEAFTL_CRB_OK;
}
LEAFTL_CRB_STATUS LeaFtlCrbPut(LEAFTL_CRB *crb,const LEAFTL_MEMBERS *members)
{
    LEAFTL_CRB result;
    unsigned char values[LEAFTL_GROUP_SIZE];
    unsigned int x,count=0U;
    if(members==0 || !LeaFtlCrbValidate(crb)) return LEAFTL_CRB_INVALID;
    for(x=0U;x<LEAFTL_GROUP_SIZE;++x)
        if(LeaFtlMembersHas(members,x)) values[count++]=(unsigned char)x;
    if(count==0U) return LEAFTL_CRB_INVALID;
    result=*crb;
    if(LeaFtlCrbRemove(&result,members)!=LEAFTL_CRB_OK) return LEAFTL_CRB_INVALID;
    InsertRecord(&result,values,count); *crb=result;
    return LEAFTL_CRB_OK;
}
