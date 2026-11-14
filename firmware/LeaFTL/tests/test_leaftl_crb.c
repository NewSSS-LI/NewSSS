#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "leaftl_crb.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static unsigned int seed=0x87654321U;
static unsigned int Random(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
int main(void)
{
    LEAFTL_CRB crb,before; LEAFTL_MEMBERS mask,out;
    unsigned int owners[256],i,t,checks=0U;
    LeaFtlCrbInit(&crb); for(i=0U;i<256U;++i) owners[i]=256U;
    before=crb; LeaFtlMembersClear(&mask);
    CHECK(LeaFtlCrbPut(&crb,&mask)==LEAFTL_CRB_INVALID && memcmp(&before,&crb,sizeof(crb))==0);
    /* Every possible offset, including 0 and 255, as its own record. */
    for(i=256U;i>0U;--i) { LeaFtlMembersClear(&mask); LeaFtlMembersAdd(&mask,i-1U);
        CHECK(LeaFtlCrbPut(&crb,&mask)==LEAFTL_CRB_OK); }
    CHECK(crb.count==256U && crb.records==256U && LeaFtlCrbValidate(&crb));
    for(i=0U;i<256U;++i) CHECK(LeaFtlCrbContains(&crb,i,i));
    LeaFtlCrbInit(&crb);
    for(t=0U;t<3000U;++t) {
        unsigned int chosen[256],count=0U,x,j,put=(Random()&3U)!=0U;
        LeaFtlMembersClear(&mask);
        for(i=0U;i<1U+Random()%24U;++i) LeaFtlMembersAdd(&mask,Random()%256U);
        for(x=0U;x<256U;++x) if(LeaFtlMembersHas(&mask,x)) chosen[count++]=x;
        /* Reference record labels: deletion may change a record's key. */
        for(x=0U;x<256U;++x) if(LeaFtlMembersHas(&mask,x)) owners[x]=256U;
        for(i=0U;i<256U;++i) {
            unsigned int first=256U;
            for(x=0U;x<256U;++x) if(owners[x]==i) { first=x; break; }
            if(first!=256U && first!=i)
                for(x=0U;x<256U;++x) if(owners[x]==i) owners[x]=first;
        }
        if(put) {
            CHECK(count!=0U); for(j=0U;j<count;++j) owners[chosen[j]]=chosen[0];
            CHECK(LeaFtlCrbPut(&crb,&mask)==LEAFTL_CRB_OK);
        } else CHECK(LeaFtlCrbRemove(&crb,&mask)==LEAFTL_CRB_OK);
        CHECK(LeaFtlCrbValidate(&crb));
        {
            unsigned int key=256U,expected_count=0U;
            for(x=0U;x<256U;++x) if(owners[x]!=256U) ++expected_count;
            CHECK(expected_count==crb.count);
            for(x=0U;x<crb.count;++x) {
                if((crb.starts[x/8U]&(1U<<(x%8U)))!=0U) key=crb.offsets[x];
                CHECK(owners[crb.offsets[x]]==key);
            }
        }
        for(x=0U;x<256U;++x) {
            if(owners[x]!=256U) {
                CHECK(LeaFtlCrbContains(&crb,owners[x],x));
                CHECK(LeaFtlCrbGet(&crb,owners[x],&out)==LEAFTL_CRB_OK && LeaFtlMembersHas(&out,x));
            }
            ++checks;
        }
        /* Wrong owner MUST fail, even when global payload is nearly sorted. */
        for(i=0U;i<crb.count;++i) if(LeaFtlCrbContains(&crb,crb.offsets[i],(crb.offsets[i]+1U)%256U))
            CHECK(owners[(crb.offsets[i]+1U)%256U]==crb.offsets[i]);
    }
    printf("PASS: %u CRB owner checks, 3000 mutations, full 256 records and boundary offsets\n",checks);
    return 0;
}
