#include "leaftl_oob.h"
#include <stdint.h>
#include <string.h>
static void Put32(unsigned char *p,unsigned int x)
{ unsigned int i;for(i=0;i<4;i++) p[i]=(unsigned char)(x>>(8U*i)); }
static unsigned int Get32(const unsigned char *p)
{ return (unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24); }
static int Neighbor(unsigned int center,unsigned int pages,int delta,unsigned int *ppn)
{
    int64_t value=(int64_t)center+delta;
    if(value<0 || value>=LEAFTL_PPN_NONE || (uint64_t)value/pages!=center/pages) return 0;
    *ppn=(unsigned int)value;return 1;
}
LEAFTL_OOB_STATUS LeaFtlOobBuild(unsigned char *spare,unsigned int bytes,
    unsigned int center,unsigned int pages,unsigned int gamma,
    unsigned int (*getter)(void *,unsigned int),void *context)
{
    unsigned int i,own;
    if(!spare || !pages || center==LEAFTL_PPN_NONE || gamma>LEAFTL_MAX_GAMMA ||
       bytes<LEAFTL_OOB_BYTES(gamma) || !getter) return LEAFTL_OOB_INVALID;
    own=getter(context,center);if(own==LEAFTL_LPN_NONE) return LEAFTL_OOB_INVALID;
    memset(spare,0xff,bytes);
    for(i=0;i<2U*gamma+1U;i++) {
        unsigned int ppn,lpn=LEAFTL_LPN_NONE;
        if(Neighbor(center,pages,(int)i-(int)gamma,&ppn))
            lpn=i==gamma ? own : getter(context,ppn);
        Put32(spare+LEAFTL_OOB_RESERVED_BYTES+4U*i,lpn);
    }
    return LEAFTL_OOB_OK;
}
LEAFTL_OOB_STATUS LeaFtlOobFind(const unsigned char *spare,unsigned int bytes,
    unsigned int center,unsigned int pages,unsigned int gamma,unsigned int lpn,unsigned int *actual)
{
    unsigned int i,found=LEAFTL_PPN_NONE;
    if(!spare || !actual || !pages || center==LEAFTL_PPN_NONE || lpn==LEAFTL_LPN_NONE ||
       gamma>LEAFTL_MAX_GAMMA || bytes<LEAFTL_OOB_BYTES(gamma) ||
       Get32(spare+LEAFTL_OOB_RESERVED_BYTES+4U*gamma)==LEAFTL_LPN_NONE) return LEAFTL_OOB_INVALID;
    for(i=0;i<2U*gamma+1U;i++) {
        unsigned int ppn,entry=Get32(spare+LEAFTL_OOB_RESERVED_BYTES+4U*i);
        if(entry==LEAFTL_LPN_NONE) continue;
        if(!Neighbor(center,pages,(int)i-(int)gamma,&ppn)) return LEAFTL_OOB_INVALID;
        if(entry==lpn) {if(found!=LEAFTL_PPN_NONE) return LEAFTL_OOB_INVALID;found=ppn;}
    }
    if(found==LEAFTL_PPN_NONE) return LEAFTL_OOB_MISS;
    *actual=found;return LEAFTL_OOB_OK;
}
