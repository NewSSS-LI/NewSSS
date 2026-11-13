#include "leaftl_cache.h"
#define NONE LEAFTL_INDEX_NONE
static LEAFTL_INDEX_GROUP *Group(LEAFTL_MAPPING *m,unsigned int id)
{
    unsigned int lo=0,hi=m->index->group_count;
    while(lo<hi) {unsigned int mid=lo+(hi-lo)/2;if(m->index->groups[mid].group_id<id) lo=mid+1;else hi=mid;}
    return lo<m->index->group_count && m->index->groups[lo].group_id==id ? &m->index->groups[lo] : 0;
}
static unsigned int Cost(unsigned int g,unsigned int n,unsigned int l)
{ return g*(unsigned int)sizeof(LEAFTL_INDEX_GROUP)+n*(unsigned int)sizeof(LEAFTL_INDEX_NODE)+l*(unsigned int)sizeof(LEAFTL_INDEX_LEVEL); }
unsigned int LeaFtlCacheBytes(const LEAFTL_MAPPING *m)
{ return Cost(m->index->group_count,m->index->node_count,m->index->level_count); }
static LEAFTL_MAPPING_STATUS MapStatus(LEAFTL_FLASH_STATUS s)
{
    if(s==LEAFTL_FLASH_OK) return LEAFTL_MAPPING_OK;
    if(s==LEAFTL_FLASH_MISS) return LEAFTL_MAPPING_MISS;
    if(s==LEAFTL_FLASH_NO_SPACE) return LEAFTL_MAPPING_NO_SPACE;
    if(s==LEAFTL_FLASH_BUSY) return LEAFTL_MAPPING_BUSY;
    return LEAFTL_MAPPING_IO_ERROR;
}
static void Touch(LEAFTL_MAPPING *m,LEAFTL_INDEX_GROUP *g)
{
    LEAFTL_CACHE *c=m->cache;unsigned int i,j;
    if(c->clock==LEAFTL_CACHE_STAMP) {
        /* Rank existing stamps without using transaction workspace. */
        for(i=0;i<m->index->group_count;i++) {
            unsigned int rank=1;
            for(j=0;j<m->index->group_count;j++)
                if((m->index->groups[j].cache_state&LEAFTL_CACHE_STAMP)<
                   (m->index->groups[i].cache_state&LEAFTL_CACHE_STAMP)) rank++;
            c->ids[i]=rank;
        }
        for(i=0;i<m->index->group_count;i++) m->index->groups[i].cache_state=
            (m->index->groups[i].cache_state&~LEAFTL_CACHE_STAMP)|c->ids[i];
        c->clock=m->index->group_count;
    }
    g->cache_state=(g->cache_state&~LEAFTL_CACHE_STAMP)|++c->clock;
}
void LeaFtlCacheHit(LEAFTL_MAPPING *m,unsigned int id)
{ LEAFTL_INDEX_GROUP *g;if(!m->cache || m->cache->busy) return;g=Group(m,id);if(g) Touch(m,g); }
static LEAFTL_FLASH_STATUS Snapshot(void *v,LEAFTL_MAPPING *m,unsigned int id,LEAFTL_INDEX_WORKSPACE *w)
{
    LEAFTL_CACHE *c=(LEAFTL_CACHE *)v;LEAFTL_INDEX_GROUP *g=Group(m,id);
    LEAFTL_INDEX_STATUS s;LEAFTL_FLASH_STATUS f;
    if(g && ((g->cache_state&LEAFTL_CACHE_COMPLETE) || c->flash->directory[id].vsa==UINT_MAX)) {
        s=LeaFtlIndexExportGroup(m->index,id,w);
        return s==LEAFTL_INDEX_OK ? LEAFTL_FLASH_OK : LEAFTL_FLASH_INVALID;
    }
    f=LeaFtlFlashReadSnapshot(c->flash,id,c->read_page,w);
    if(f!=LEAFTL_FLASH_OK) return f;
    if(!g) return LEAFTL_FLASH_OK;
    s=LeaFtlIndexOverlayGroup(m->index,id,w,m->pending_segments);
    return s==LEAFTL_INDEX_OK ? LEAFTL_FLASH_OK : s==LEAFTL_INDEX_NO_SPACE ? LEAFTL_FLASH_NO_SPACE : LEAFTL_FLASH_INVALID;
}
LEAFTL_MAPPING_STATUS LeaFtlCacheInit(LEAFTL_CACHE *c,LEAFTL_MAPPING *m,LEAFTL_FLASH *f,
    unsigned int budget,unsigned char *read_page,unsigned char *protected_record)
{
    if(!c || !m || !f || !read_page || !protected_record || !f->directory || !f->page ||
       f->logical_count!=m->logical_count || m->cache || m->pending_token || m->index->group_count ||
       budget<Cost(1,1,1)) return LEAFTL_MAPPING_INVALID;
    c->flash=f;c->read_page=read_page;c->protected_record=protected_record;
    c->budget=budget;c->clock=0;c->busy=0;m->cache=c;
    f->snapshot=Snapshot;f->snapshot_context=c;return LEAFTL_MAPPING_OK;
}
static int Selected(const LEAFTL_CACHE *c,unsigned int count,unsigned int id)
{ unsigned int i;for(i=0;i<count;i++) if(c->ids[i]==id) return 1;return 0; }
static int Fits(LEAFTL_MAPPING *m,unsigned int id,unsigned int nodes,unsigned int levels,
    unsigned int removed_groups,unsigned int removed_nodes,unsigned int removed_levels)
{
    LEAFTL_INDEX_GROUP *g=Group(m,id);LEAFTL_INDEX *idx=m->index;
    unsigned int ng=idx->group_count-removed_groups+(g ? 0U : 1U);
    unsigned int nn=idx->node_count-removed_nodes-(g ? g->segment_count : 0U)+nodes;
    unsigned int nl=idx->level_count-removed_levels-(g ? g->level_count : 0U)+levels;
    return ng<=idx->group_capacity && nn<=idx->node_capacity && nl<=idx->level_capacity &&
        Cost(ng,nn,nl)<=m->cache->budget;
}
static LEAFTL_MAPPING_STATUS Room(LEAFTL_MAPPING *m,unsigned int id,unsigned int nodes,unsigned int levels)
{
    LEAFTL_CACHE *c=m->cache;unsigned int selected=0,rn=0,rl=0,i,stored=0;
    LEAFTL_FLASH_STATUS status;
    if(nodes>LEAFTL_MAX_SEGMENTS_PER_GROUP || levels>LEAFTL_MAX_LEVELS_PER_GROUP ||
       nodes>m->index->node_capacity || levels>m->index->level_capacity || Cost(1,nodes,levels)>c->budget)
        return LEAFTL_MAPPING_NO_SPACE;
    if(Fits(m,id,nodes,levels,0,0,0)) return LEAFTL_MAPPING_OK;
    while(selected<LEAFTL_CACHE_EVICT_BATCH || !Fits(m,id,nodes,levels,selected,rn,rl)) {
        LEAFTL_INDEX_GROUP *victim=0;
        for(i=0;i<m->index->group_count;i++) {
            LEAFTL_INDEX_GROUP *g=&m->index->groups[i];
            if(g->group_id==id || Selected(c,selected,g->group_id)) continue;
            if(!victim || (g->cache_state&LEAFTL_CACHE_STAMP)<(victim->cache_state&LEAFTL_CACHE_STAMP)) victim=g;
        }
        if(!victim) break;
        if(!(victim->cache_state&LEAFTL_CACHE_DIRTY)) {
            (void)LeaFtlIndexRemoveGroup(m->index,victim->group_id);
            if(!selected && Fits(m,id,nodes,levels,0,0,0)) return LEAFTL_MAPPING_OK;
            continue;
        }
        c->ids[selected++]=victim->group_id;rn+=victim->segment_count;rl+=victim->level_count;
    }
    if(!Fits(m,id,nodes,levels,selected,rn,rl)) return LEAFTL_MAPPING_NO_SPACE;
    if(!selected) return LEAFTL_MAPPING_OK;
    for(i=1;i<selected;i++) {unsigned int j=i,x=c->ids[i];while(j && c->ids[j-1]>x) {c->ids[j]=c->ids[j-1];j--;}c->ids[j]=x;}
    status=LeaFtlFlashStoreMany(c->flash,m,c->ids,selected,&stored);
    for(i=0;i<stored;i++) (void)LeaFtlIndexRemoveGroup(m->index,c->ids[i]);
    return MapStatus(status);
}
LEAFTL_MAPPING_STATUS LeaFtlCachePlanRoom(LEAFTL_MAPPING *m,unsigned int id,unsigned int nodes,unsigned int levels)
{
    LEAFTL_MAPPING_STATUS status;
    if(!m->cache) return LEAFTL_MAPPING_OK;
    if(m->cache->busy || m->pending_token) return LEAFTL_MAPPING_BUSY;
    m->cache->busy=1;status=Room(m,id,nodes,levels);m->cache->busy=0;return status;
}
LEAFTL_MAPPING_STATUS LeaFtlCacheEnsure(LEAFTL_MAPPING *m,unsigned int id)
{
    LEAFTL_CACHE *c=m->cache;LEAFTL_INDEX_GROUP *g;
    LEAFTL_FLASH_STATUS fs;LEAFTL_MAPPING_STATUS status;unsigned int bytes,nodes,levels;
    if(!c) return LEAFTL_MAPPING_OK;
    g=Group(m,id);
    if(g) {
        Touch(m,g);
        if((g->cache_state&LEAFTL_CACHE_COMPLETE) || c->flash->directory[id].vsa==UINT_MAX) return LEAFTL_MAPPING_OK;
        /* RAM hit is resolved by caller without I/O, even for a partial group. */
    } else if(c->flash->directory[id].vsa==UINT_MAX) return LEAFTL_MAPPING_MISS;
    if(c->busy || m->pending_token) return LEAFTL_MAPPING_BUSY;
    c->busy=1;fs=Snapshot(c,m,id,m->workspace);status=MapStatus(fs);
    if(status!=LEAFTL_MAPPING_OK) goto done;
    nodes=m->workspace->node_count;levels=m->workspace->level_count;
    fs=LeaFtlFlashEncode(m->workspace,id,c->protected_record,LEAFTL_FLASH_MAX_BYTES,&bytes);
    status=MapStatus(fs);if(status!=LEAFTL_MAPPING_OK) goto done;
    status=Room(m,id,nodes,levels);if(status!=LEAFTL_MAPPING_OK) goto done;
    fs=LeaFtlFlashDecode(c->protected_record,bytes,id,m->workspace);
    status=MapStatus(fs);if(status!=LEAFTL_MAPPING_OK) goto done;
    if(LeaFtlIndexRestoreGroup(m->index,id,m->workspace)!=LEAFTL_INDEX_OK) {status=LEAFTL_MAPPING_NO_SPACE;goto done;}
    g=Group(m,id);g->cache_state|=LEAFTL_CACHE_COMPLETE;Touch(m,g);
done:
    c->busy=0;return status;
}
void LeaFtlCacheDirty(LEAFTL_MAPPING *m,unsigned int id)
{
    LEAFTL_INDEX_GROUP *g;if(!m->cache) return;
    g=Group(m,id);if(!g) return;
    g->cache_state|=LEAFTL_CACHE_DIRTY;
    if(m->cache->flash->directory[id].vsa==UINT_MAX) g->cache_state|=LEAFTL_CACHE_COMPLETE;
    Touch(m,g);
}
LEAFTL_MAPPING_STATUS LeaFtlCacheFlush(LEAFTL_MAPPING *m)
{
    LEAFTL_CACHE *c=m->cache;unsigned int i,count=0,stored=0;LEAFTL_FLASH_STATUS s;
    if(!c) return LEAFTL_MAPPING_OK;
    if(c->busy || m->pending_token) return LEAFTL_MAPPING_BUSY;
    c->busy=1;
    for(i=0;i<m->index->group_count;i++) if(m->index->groups[i].cache_state&LEAFTL_CACHE_DIRTY)
        c->ids[count++]=m->index->groups[i].group_id;
    s=count ? LeaFtlFlashStoreMany(c->flash,m,c->ids,count,&stored) : LEAFTL_FLASH_OK;
    for(i=0;i<stored;i++) {LEAFTL_INDEX_GROUP *g=Group(m,c->ids[i]);g->cache_state&=~LEAFTL_CACHE_DIRTY;}
    c->busy=0;return MapStatus(s);
}
