#include "leaftl_gc.h"
#include <stdint.h>
typedef struct { LEAFTL_MAPPING *mapping; const LEAFTL_GC_IO *io; int invalid; } GC_CONTEXT;
static void Drain(void *v) { GC_CONTEXT *c=(GC_CONTEXT *)v; c->io->drain(c->io->context); }
static LEAFTL_MAPPING_STATUS Lookup(void *v,unsigned int lpn,unsigned int *ppn)
{
    GC_CONTEXT *c=(GC_CONTEXT *)v;
    return c->io->lookup ? c->io->lookup(c->io->context,lpn,ppn) : LeaFtlMappingLookup(c->mapping,lpn,ppn);
}
static int Inspect(void *v,unsigned int page,unsigned int *lpn)
{
    GC_CONTEXT *c=(GC_CONTEXT *)v;
    unsigned int ppn=c->io->first_ppn+page,mapped;
    LEAFTL_MAPPING_STATUS status;
    *lpn=c->io->reverse(c->io->context,ppn);
    if(*lpn==LEAFTL_LPN_NONE) return 0;
    status=Lookup(v,*lpn,&mapped);
    if(status==LEAFTL_MAPPING_MISS) return 0;
    if(status!=LEAFTL_MAPPING_OK) { c->invalid=1; return 0; }
    return mapped==ppn;
}
static unsigned int Reserve(void *v,unsigned int n,unsigned int *first)
{
    GC_CONTEXT *c=(GC_CONTEXT *)v;
    unsigned int count;
    if(c->invalid) return 0;
    count=c->io->reserve(c->io->context,n,first);
    if(count && (uint64_t)*first<(uint64_t)c->io->first_ppn+c->io->page_count &&
       (uint64_t)*first+count>c->io->first_ppn) {
        c->invalid=1; c->io->abandon(c->io->context); return 0;
    }
    return count;
}
static int Program(void *v,unsigned int page,unsigned int lpn,unsigned int ppn)
{
    GC_CONTEXT *c=(GC_CONTEXT *)v;
    return c->io->read(c->io->context,c->io->first_ppn+page) &&
        c->io->program(c->io->context,lpn,ppn);
}
static void Publish(void *v,unsigned int page,unsigned int lpn,unsigned int ppn,unsigned int old)
{
    GC_CONTEXT *c=(GC_CONTEXT *)v; (void)page;
    c->io->publish(c->io->context,lpn,ppn,old);
}
static void Abandon(void *v) { GC_CONTEXT *c=(GC_CONTEXT *)v; c->io->abandon(c->io->context); }
LEAFTL_MAPPING_STATUS LeaFtlGcRelocate(LEAFTL_MAPPING *m,LEAFTL_WRITEBACK_WORK *w,
    const LEAFTL_GC_IO *io,unsigned int *moved)
{
    GC_CONTEXT ctx;
    LEAFTL_WRITEBACK_IO batch;
    LEAFTL_MAPPING_STATUS status;
    unsigned int page,lpn;
    if(!m || !w || !io || !moved || !io->page_count ||
       io->page_count>LEAFTL_FLUSH_SORT_CAPACITY ||
       io->first_ppn>UINT_MAX-(io->page_count-1U) ||
       !io->reverse || !io->drain || !io->reserve || !io->read ||
       !io->program || !io->publish || !io->abandon || !io->erase)
        return LEAFTL_MAPPING_INVALID;
    ctx.mapping=m; ctx.io=io; ctx.invalid=0;
    batch.context=&ctx; batch.buffer_count=io->page_count;
    batch.drain=Drain; batch.inspect=Inspect; batch.reserve=Reserve;
    batch.program=Program; batch.publish=Publish; batch.abandon=Abandon;
    batch.lookup=Lookup;
    status=LeaFtlWritebackFlush(m,w,&batch,moved);
    if(ctx.invalid) return LEAFTL_MAPPING_INVALID;
    if(status!=LEAFTL_MAPPING_OK) return status;
    /* Revalidate before erasing: no current mapping may point at the source. */
    for(page=0;page<io->page_count;page++) if(Inspect(&ctx,page,&lpn))
        return LEAFTL_MAPPING_BUSY;
    if(ctx.invalid) return LEAFTL_MAPPING_INVALID;
    return io->erase(io->context) ? LEAFTL_MAPPING_OK : LEAFTL_MAPPING_PROGRAM_FAILED;
}
