#include "leaftl_metadata.h"
#define NONE UINT_MAX
static int Page(const LEAFTL_METADATA *p,unsigned int block,unsigned int vsa,unsigned int *page)
{
    unsigned int delta;if(vsa==NONE || vsa<p->blocks[block].base) return 0;
    delta=vsa-p->blocks[block].base;if(delta%p->stride || delta/p->stride>=p->pages) return 0;
    *page=delta/p->stride;return 1;
}
int LeaFtlMetadataInit(LEAFTL_METADATA *p,LEAFTL_METADATA_BLOCK *blocks,unsigned int count,
    unsigned int pages,unsigned int stride,LEAFTL_FLASH_DIRECTORY_ENTRY *directory,unsigned int groups,
    unsigned char *gc_page,const LEAFTL_METADATA_IO *io)
{
    unsigned int i,j,page;
    if(!p || !blocks || count<3 || !pages || pages>256U || !stride || !directory || !groups || !gc_page ||
       !io || !io->program || !io->read || !io->erase) return 0;
    for(i=0;i<count;i++) {
        if((uint64_t)blocks[i].base+(uint64_t)(pages-1U)*stride>=UINT_MAX) return 0;
        for(j=0;j<i;j++) {
            uint64_t a=blocks[i].base,b=blocks[j].base,span=(uint64_t)pages*stride;
            if(a%stride==b%stride && a<b+span && b<a+span) return 0;
        }
    }
    p->blocks=blocks;p->count=count;p->pages=pages;p->stride=stride;p->directory=directory;
    p->groups=groups;p->gc_page=gc_page;p->current=NONE;p->busy=0;p->io=*io;
    for(i=0;i<count;i++) {blocks[i].used=0;blocks[i].bad=0;}
    /* Initialization does not silently accept references into an erased pool. */
    for(i=0;i<groups;i++) if(directory[i].vsa!=NONE) {
        for(j=0;j<count;j++) if(Page(p,j,directory[i].vsa,&page)) return 0;
    }
    return 1;
}
static unsigned int Live(const LEAFTL_METADATA *p,unsigned int block)
{
    unsigned char bits[32]={0};unsigned int i,page,count=0;
    for(i=0;i<p->groups;i++) if(Page(p,block,p->directory[i].vsa,&page)) bits[page/8U]|=(unsigned char)(1U<<(page%8U));
    for(i=0;i<p->pages;i++) if(bits[i/8U]&(1U<<(i%8U))) count++;
    return count;
}
static int Referenced(const LEAFTL_METADATA *p,unsigned int vsa)
{ unsigned int i;for(i=0;i<p->groups;i++) if(p->directory[i].vsa==vsa) return 1;return 0; }
static int Collect(LEAFTL_METADATA *p)
{
    unsigned int i,target=NONE,victim=NONE,best=NONE,page;
    for(i=0;i<p->count;i++) if(!p->blocks[i].bad && !p->blocks[i].used) {target=i;break;}
    if(target==NONE) return 0;
    for(i=0;i<p->count;i++) if(i!=p->current && i!=target && !p->blocks[i].bad && p->blocks[i].used) {
        unsigned int live=Live(p,i);
        if(live<p->blocks[i].used && live<best) {best=live;victim=i;}
    }
    if(victim==NONE) return 0;
    for(page=0;page<p->blocks[victim].used;page++) {
        unsigned int old=p->blocks[victim].base+page*p->stride,new_vsa,j;
        if(!Referenced(p,old)) continue;
        if(!p->io.read(p->io.context,old,p->gc_page) ||
           !LeaFtlFlashPageValid(p->gc_page,LEAFTL_FLASH_PAGE_BYTES)) return 0;
        new_vsa=p->blocks[target].base+p->blocks[target].used*p->stride;
        p->blocks[target].used++; /* Failed programs must never be reused. */
        if(!p->io.program(p->io.context,new_vsa,p->gc_page)) {p->blocks[target].bad=1;p->current=NONE;return 0;}
        for(j=0;j<p->groups;j++) if(p->directory[j].vsa==old) p->directory[j].vsa=new_vsa;
    }
    if(Live(p,victim)) return 0;
    if(!p->io.erase(p->io.context,p->blocks[victim].base)) p->blocks[victim].bad=1;
    else p->blocks[victim].used=0;
    p->current=target;return 1;
}
int LeaFtlMetadataCollect(LEAFTL_METADATA *p)
{ int result;if(!p || p->busy) return 0;p->busy=1;result=Collect(p);p->busy=0;return result; }
static int Allocate(LEAFTL_METADATA *p,unsigned int *vsa)
{
    unsigned int i,empty=0,first=NONE;
    if(p->current!=NONE && !p->blocks[p->current].bad && p->blocks[p->current].used<p->pages) goto available;
    for(i=0;i<p->count;i++) if(!p->blocks[i].bad && !p->blocks[i].used) {empty++;first=i;}
    if(empty>=2) p->current=first;
    else {
        /* Current is full, so eligible as a victim too. */
        p->current=NONE;
        if(!Collect(p)) return 0;
    }
available:
    *vsa=p->blocks[p->current].base+p->blocks[p->current].used*p->stride;
    p->blocks[p->current].used++;return 1;
}
int LeaFtlMetadataAppend(void *v,const unsigned char *page,unsigned int bytes,unsigned int *vsa)
{
    LEAFTL_METADATA *p=(LEAFTL_METADATA *)v;unsigned int allocated;int result=0;
    if(!p || p->busy || !page || !vsa || bytes!=LEAFTL_FLASH_PAGE_BYTES ||
       !LeaFtlFlashPageValid(page,bytes)) return 0;
    p->busy=1;
    if(Allocate(p,&allocated)) {
        result=p->io.program(p->io.context,allocated,page);
        if(!result) {p->blocks[p->current].bad=1;p->current=NONE;}
        else *vsa=allocated;
    }
    p->busy=0;return result;
}
int LeaFtlMetadataRead(void *v,unsigned int vsa,unsigned char *page,unsigned int bytes)
{
    LEAFTL_METADATA *p=(LEAFTL_METADATA *)v;unsigned int i,n;int result=0;
    if(!p || p->busy || !page || bytes!=LEAFTL_FLASH_PAGE_BYTES) return 0;
    for(i=0;i<p->count;i++) if(Page(p,i,vsa,&n) && n<p->blocks[i].used) {
        p->busy=1;result=p->io.read(p->io.context,vsa,page);p->busy=0;break;
    }
    return result;
}
