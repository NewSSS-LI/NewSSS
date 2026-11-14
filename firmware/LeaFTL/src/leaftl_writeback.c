#include "leaftl_writeback.h"
LEAFTL_MAPPING_STATUS LeaFtlWritebackFlush(LEAFTL_MAPPING *m,LEAFTL_WRITEBACK_WORK *w,
    const LEAFTL_WRITEBACK_IO *io,unsigned int *committed)
{
    unsigned int n=0,i,j,pos=0,run,first,end,k,token,ppn;
    LEAFTL_MAPPING_STATUS status=LEAFTL_MAPPING_OK;
    LEAFTL_FLUSH_ENTRY entry;
    if(!m || !w || !io || !committed || !io->drain || !io->inspect ||
       !io->reserve || !io->program || !io->publish || !io->abandon)
        return LEAFTL_MAPPING_INVALID;
    if(w->busy || m->pending_token) return LEAFTL_MAPPING_BUSY;
    w->busy=1U; *committed=0U; io->drain(io->context);
    for(i=0;i<io->buffer_count;i++) {
        unsigned int lpn;
        if(!io->inspect(io->context,i,&lpn)) continue;
        if(n==LEAFTL_FLUSH_SORT_CAPACITY) break;
        if(lpn>=m->logical_count) { status=LEAFTL_MAPPING_INVALID; goto done; }
        entry.id=i; entry.lpn=lpn; entry.old_ppn=UINT_MAX;
        /* Stable ID sort BEFORE any physical allocation. */
        for(j=n;j>0U && w->entries[j-1U].lpn>lpn;j--)
            w->entries[j]=w->entries[j-1U];
        w->entries[j]=entry; n++;
    }
    for(i=1;i<n;i++) if(w->entries[i-1U].lpn==w->entries[i].lpn) {
        status=LEAFTL_MAPPING_INVALID; goto done;
    }
    while(pos<n) {
        run=io->reserve(io->context,n-pos,&first);
        if(!run) { status=LEAFTL_MAPPING_NO_SPACE; goto done; }
        if(run>n-pos || first>UINT_MAX-(run-1U)) {
            status=LEAFTL_MAPPING_INVALID; goto abandon;
        }
        end=pos+run;
        w->run_first_ppn=first;w->run_start=pos;w->run_count=run;
        while(pos<end) {
            unsigned int start=pos;
            for(k=0;pos+k<end &&
                w->entries[pos+k].lpn/LEAFTL_GROUP_SIZE==w->entries[pos].lpn/LEAFTL_GROUP_SIZE;k++) {
                w->points[k].lpn=w->entries[pos+k].lpn;
                w->points[k].ppn=first+k;
                status=io->lookup ? io->lookup(io->context,w->points[k].lpn,&ppn) :
                    LeaFtlMappingLookup(m,w->points[k].lpn,&ppn);
                if(status!=LEAFTL_MAPPING_OK && status!=LEAFTL_MAPPING_MISS) goto abandon;
                w->entries[pos+k].old_ppn=status==LEAFTL_MAPPING_OK ? ppn : UINT_MAX;
            }
            status=LeaFtlMappingPrepare(m,w->points,k,&token);
            if(status!=LEAFTL_MAPPING_OK) goto abandon;
            for(j=0;j<k;j++) if(!io->program(io->context,w->entries[pos+j].id,
                w->entries[pos+j].lpn,first+j)) {
                status=LeaFtlMappingComplete(m,token,0); goto abandon;
            }
            status=LeaFtlMappingComplete(m,token,1);
            if(status!=LEAFTL_MAPPING_OK) {
                (void)LeaFtlMappingAbort(m,token); goto abandon;
            }
            for(j=0;j<k;j++) io->publish(io->context,w->entries[start+j].id,
                w->entries[start+j].lpn,first+j,w->entries[start+j].old_ppn);
            *committed+=k; pos+=k; first+=k;
        }
    }
    goto done;
abandon:
    io->abandon(io->context);
done:
    w->run_count=0U;w->busy=0U; return status;
}
