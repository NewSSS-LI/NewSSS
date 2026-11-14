#include "leaftl_mapping.h"
#include "leaftl_cache.h"
#define MAPPING_READY 0x4c464d50U
static int Ready(const LEAFTL_MAPPING *m) { return m!=0 && m->initialized==MAPPING_READY; }
static LEAFTL_MAPPING_STATUS IndexStatus(LEAFTL_INDEX_STATUS status)
{
    if(status==LEAFTL_INDEX_OK) return LEAFTL_MAPPING_OK;
    if(status==LEAFTL_INDEX_MISS) return LEAFTL_MAPPING_MISS;
    if(status==LEAFTL_INDEX_NO_SPACE) return LEAFTL_MAPPING_NO_SPACE;
    return LEAFTL_MAPPING_INVALID;
}
LEAFTL_MAPPING_STATUS LeaFtlMappingInit(LEAFTL_MAPPING *m,LEAFTL_INDEX *index,
    LEAFTL_INDEX_WORKSPACE *work,LEAFTL_TRAINING_POINT *points,
    LEAFTL_SEGMENT_INPUT *segments,unsigned int capacity,unsigned int logical_count)
{
    LEAFTL_MAPPING value={0}; LEAFTL_INDEX_GROUP_INFO info; LEAFTL_INDEX_STATUS status;
    if(m==0 || index==0 || work==0 || points==0 || segments==0 ||
       capacity==0U || capacity>LEAFTL_MAX_TRAINING_POINTS || logical_count==0U)
        return LEAFTL_MAPPING_INVALID;
    status=LeaFtlIndexGetGroupInfo(index,0U,&info);
    if(status!=LEAFTL_INDEX_OK && status!=LEAFTL_INDEX_MISS) return LEAFTL_MAPPING_INVALID;
    value.index=index; value.workspace=work; value.pending_points=points;
    value.pending_segments=segments; value.capacity=capacity; value.logical_count=logical_count;
    value.next_token=1U; value.initialized=MAPPING_READY; *m=value; return LEAFTL_MAPPING_OK;
}
LEAFTL_MAPPING_STATUS LeaFtlMappingLookup(LEAFTL_MAPPING *m,unsigned int lpn,unsigned int *vsa)
{
    LEAFTL_MAPPING_STATUS status;
    if(!Ready(m) || vsa==0 || lpn>=m->logical_count || lpn==LEAFTL_LPN_NONE)
        return LEAFTL_MAPPING_INVALID;
    status=IndexStatus(LeaFtlIndexLookup(m->index,lpn,vsa));
    if(status==LEAFTL_MAPPING_OK) {LeaFtlCacheHit(m,LEAFTL_GROUP_ID(lpn));return status;}
    if(status!=LEAFTL_MAPPING_MISS || !m->cache) return status;
    status=LeaFtlCacheEnsure(m,LEAFTL_GROUP_ID(lpn));
    return status==LEAFTL_MAPPING_OK ? IndexStatus(LeaFtlIndexLookup(m->index,lpn,vsa)) : status;
}
LEAFTL_MAPPING_STATUS LeaFtlMappingPrepare(LEAFTL_MAPPING *m,
    const LEAFTL_TRAINING_POINT *points,unsigned int count,unsigned int *token)
{
    unsigned int i,models=0U; LEAFTL_SEGMENT_STATUS trained; LEAFTL_INDEX_STATUS planned;
    if(!Ready(m) || points==0 || token==0 || count==0U || count>m->capacity)
        return LEAFTL_MAPPING_INVALID;
    if(m->pending_token!=0U) return LEAFTL_MAPPING_BUSY;
    if(m->next_token==UINT_MAX) return LEAFTL_MAPPING_TOKEN_EXHAUSTED;
    for(i=0U;i<count;++i) {
        if(points[i].lpn>=m->logical_count || points[i].lpn==LEAFTL_LPN_NONE ||
           points[i].ppn==LEAFTL_PPN_NONE ||
           LEAFTL_GROUP_ID(points[i].lpn)!=LEAFTL_GROUP_ID(points[0].lpn) ||
           (i!=0U && points[i].lpn<=points[i-1U].lpn)) return LEAFTL_MAPPING_INVALID;
    }
    for(i=0U;i<count;++i) m->pending_points[i]=points[i];
    trained=LeaFtlSegmentTrain(m->pending_points,count,m->pending_segments,m->capacity,&models);
    if(trained!=LEAFTL_SEGMENT_OK)
        return trained==LEAFTL_SEGMENT_NO_SPACE ? LEAFTL_MAPPING_NO_SPACE : LEAFTL_MAPPING_INVALID;
    planned=LeaFtlIndexCanInsert(m->index,m->pending_segments,models,m->workspace);
    if(m->cache && (planned==LEAFTL_INDEX_OK || planned==LEAFTL_INDEX_NO_SPACE) &&
       LeaFtlIndexValidateSnapshot(m->workspace)) {
        LEAFTL_MAPPING_STATUS room=LeaFtlCachePlanRoom(m,LEAFTL_GROUP_ID(m->pending_points[0].lpn),
            m->workspace->node_count,m->workspace->level_count);
        if(room!=LEAFTL_MAPPING_OK) return room;
        /* Victim merge reuses segment scratch; reconstruct this pending batch. */
        trained=LeaFtlSegmentTrain(m->pending_points,count,m->pending_segments,m->capacity,&models);
        if(trained!=LEAFTL_SEGMENT_OK) return LEAFTL_MAPPING_INVALID;
        planned=LeaFtlIndexCanInsert(m->index,m->pending_segments,models,m->workspace);
    }
    if(planned!=LEAFTL_INDEX_OK) return IndexStatus(planned);
    m->pending_count=count; m->pending_segment_count=models;
    m->pending_token=m->next_token++; *token=m->pending_token; return LEAFTL_MAPPING_OK;
}
static void ClearPending(LEAFTL_MAPPING *m)
{ m->pending_token=0U; m->pending_count=0U; m->pending_segment_count=0U; }
LEAFTL_MAPPING_STATUS LeaFtlMappingComplete(LEAFTL_MAPPING *m,unsigned int token,int success)
{
    LEAFTL_INDEX_STATUS status;
    if(!Ready(m)) return LEAFTL_MAPPING_INVALID;
    if(token==0U || token!=m->pending_token) return LEAFTL_MAPPING_STALE;
    if(!success) { ClearPending(m); return LEAFTL_MAPPING_PROGRAM_FAILED; }
    status=LeaFtlIndexInsert(m->index,m->pending_segments,m->pending_segment_count,m->workspace);
    if(status!=LEAFTL_INDEX_OK) return IndexStatus(status);
    LeaFtlCacheDirty(m,LEAFTL_GROUP_ID(m->pending_points[0].lpn));
    ClearPending(m); return LEAFTL_MAPPING_OK;
}
LEAFTL_MAPPING_STATUS LeaFtlMappingAbort(LEAFTL_MAPPING *m,unsigned int token)
{
    if(!Ready(m)) return LEAFTL_MAPPING_INVALID;
    if(token==0U || token!=m->pending_token) return LEAFTL_MAPPING_STALE;
    ClearPending(m); return LEAFTL_MAPPING_OK;
}
LEAFTL_MAPPING_STATUS LeaFtlMappingCompact(LEAFTL_MAPPING *m,unsigned int group,
    LEAFTL_INDEX_COMPACTION_RESULT *result)
{
    if(!Ready(m) || group>=LEAFTL_GROUP_COUNT(m->logical_count)) return LEAFTL_MAPPING_INVALID;
    if(m->pending_token!=0U) return LEAFTL_MAPPING_BUSY;
    {
        LEAFTL_MAPPING_STATUS status;
        if(m->cache) {status=LeaFtlCacheEnsure(m,group);if(status!=LEAFTL_MAPPING_OK) return status;}
        status=IndexStatus(LeaFtlIndexCompactGroup(m->index,group,m->workspace,result));
        if(status==LEAFTL_MAPPING_OK) LeaFtlCacheDirty(m,group);
        return status;
    }
}
