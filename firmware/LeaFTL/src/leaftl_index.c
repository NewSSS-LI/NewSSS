#include "leaftl_index.h"
#define NONE LEAFTL_INDEX_NONE
#define CAP LEAFTL_INDEX_PLAN_CAPACITY
#define MAGIC 0x4c465449U

static int Ready(const LEAFTL_INDEX *index) { return index!=0 && index->initialized==MAGIC; }
static unsigned int FindGroup(const LEAFTL_INDEX *index,unsigned int id)
{
    unsigned int lo=0U,hi=index->group_count;
    while(lo<hi) { unsigned int mid=lo+(hi-lo)/2U;
        if(index->groups[mid].group_id<id) lo=mid+1U; else hi=mid; }
    return lo;
}
LEAFTL_INDEX_STATUS LeaFtlIndexInit(LEAFTL_INDEX *index,
    LEAFTL_INDEX_GROUP *groups,unsigned int group_capacity,
    LEAFTL_INDEX_LEVEL *levels,unsigned int level_capacity,
    LEAFTL_INDEX_NODE *nodes,unsigned int node_capacity)
{
    LEAFTL_INDEX value={0}; unsigned int i;
    if(index==0 || groups==0 || levels==0 || nodes==0 || group_capacity==0U ||
       level_capacity==0U || node_capacity==0U || group_capacity==NONE ||
       level_capacity==NONE || node_capacity==NONE || group_capacity>LEAFTL_MAX_CACHED_GROUPS ||
       node_capacity>LEAFTL_MAX_RESIDENT_SEGMENTS ||
       (uint64_t)level_capacity>(uint64_t)group_capacity*LEAFTL_MAX_LEVELS_PER_GROUP)
        return LEAFTL_INDEX_INVALID;
    for(i=0U;i<group_capacity;++i) {
        groups[i].group_id=NONE; groups[i].first_level=NONE;
        groups[i].level_count=0U; groups[i].segment_count=0U; groups[i].cache_state=0;LeaFtlCrbInit(&groups[i].crb);
    }
    for(i=0U;i<level_capacity;++i) {
        levels[i].first_segment=NONE; levels[i].root_segment=NONE;
        levels[i].segment_count=0U; levels[i].next=i+1U<level_capacity ? i+1U : NONE;
    }
    for(i=0U;i<node_capacity;++i) {
        nodes[i].next=i+1U<node_capacity ? i+1U : NONE;
        nodes[i].left=NONE; nodes[i].right=NONE;
    }
    value.groups=groups; value.levels=levels; value.nodes=nodes;
    value.group_capacity=group_capacity; value.level_capacity=level_capacity;
    value.node_capacity=node_capacity; value.free_node=0U; value.free_level=0U;
    value.initialized=MAGIC; *index=value; return LEAFTL_INDEX_OK;
}
static unsigned int AllocateLevel(LEAFTL_INDEX *index)
{
    unsigned int n=index->free_level; index->free_level=index->levels[n].next;
    index->levels[n].first_segment=NONE; index->levels[n].root_segment=NONE;
    index->levels[n].segment_count=0U; index->levels[n].next=NONE;
    ++index->level_count; return n;
}
static unsigned int AllocateNode(LEAFTL_INDEX *index,const LEAFTL_SEGMENT *model)
{
    unsigned int n=index->free_node; index->free_node=index->nodes[n].next;
    index->nodes[n].model=*model; index->nodes[n].next=NONE;
    index->nodes[n].left=NONE; index->nodes[n].right=NONE; ++index->node_count; return n;
}
static unsigned int BuildTree(LEAFTL_INDEX *index,unsigned int *cursor,unsigned int count)
{
    unsigned int left,root;
    if(count==0U) return NONE;
    left=BuildTree(index,cursor,count/2U); root=*cursor; *cursor=index->nodes[root].next;
    index->nodes[root].left=left;
    index->nodes[root].right=BuildTree(index,cursor,count-count/2U-1U); return root;
}
static void ReleaseGroup(LEAFTL_INDEX *index,const LEAFTL_INDEX_GROUP *group)
{
    unsigned int level=group->first_level;
    while(level!=NONE) {
        unsigned int next=index->levels[level].next,node=index->levels[level].first_segment;
        while(node!=NONE) {
            unsigned int next_node=index->nodes[node].next;
            index->nodes[node].next=index->free_node; index->nodes[node].left=NONE;
            index->nodes[node].right=NONE; index->free_node=node; --index->node_count; node=next_node;
        }
        index->levels[level].first_segment=NONE; index->levels[level].root_segment=NONE;
        index->levels[level].segment_count=0U; index->levels[level].next=index->free_level;
        index->free_level=level; --index->level_count; level=next;
    }
}

/* The transaction plan mirrors levels/lists only, not address values. */
static void InitWork(LEAFTL_INDEX_WORKSPACE *work)
{
    unsigned int i;
    for(i=0U;i<CAP;++i) { work->nodes[i].next=i+1U<CAP ? i+1U : NONE;
        work->levels[i].next=i+1U<CAP ? i+1U : NONE; work->levels[i].first=NONE; }
    work->first=NONE; work->free_node=0U; work->free_level=0U;
    work->node_count=0U; work->level_count=0U; LeaFtlCrbInit(&work->crb);
}
static unsigned int NewLevel(LEAFTL_INDEX_WORKSPACE *work)
{
    unsigned int n=work->free_level; if(n==NONE) return NONE;
    work->free_level=work->levels[n].next; work->levels[n].next=NONE;
    work->levels[n].first=NONE; ++work->level_count; return n;
}
static unsigned int NewNode(LEAFTL_INDEX_WORKSPACE *work,const LEAFTL_SEGMENT *model)
{
    unsigned int n=work->free_node; if(n==NONE) return NONE;
    work->free_node=work->nodes[n].next; work->nodes[n].next=NONE;
    work->nodes[n].model=*model; ++work->node_count; return n;
}
static void DropNode(LEAFTL_INDEX_WORKSPACE *work,unsigned int node)
{ work->nodes[node].next=work->free_node; work->free_node=node; --work->node_count; }
static void DropLevel(LEAFTL_INDEX_WORKSPACE *work,unsigned int level)
{ work->levels[level].next=work->free_level; work->free_level=level; --work->level_count; }
static void RemoveEmpty(LEAFTL_INDEX_WORKSPACE *work)
{
    unsigned int *link=&work->first;
    while(*link!=NONE) {
        unsigned int l=*link;
        if(work->levels[l].first==NONE) { *link=work->levels[l].next; DropLevel(work,l); }
        else link=&work->levels[l].next;
    }
}
static int LoadGroup(const LEAFTL_INDEX *index,const LEAFTL_INDEX_GROUP *group,
    LEAFTL_INDEX_WORKSPACE *work)
{
    unsigned int level,*level_link;
    InitWork(work); if(group==0) return 1;
    if(!LeaFtlCrbValidate(&group->crb)) return 0;
    work->crb=group->crb; level_link=&work->first;
    for(level=group->first_level;level!=NONE;level=index->levels[level].next) {
        unsigned int node,l=NewLevel(work),*node_link;
        if(l==NONE) return 0;
        *level_link=l; level_link=&work->levels[l].next; node_link=&work->levels[l].first;
        for(node=index->levels[level].first_segment;node!=NONE;node=index->nodes[node].next) {
            unsigned int n=NewNode(work,&index->nodes[node].model);
            if(n==NONE) return 0;
            *node_link=n; node_link=&work->nodes[n].next;
        }
    }
    return 1;
}
static void Bounds(LEAFTL_SEGMENT *model,const LEAFTL_MEMBERS *members)
{
    unsigned int x,first=NONE,last=0U;
    for(x=0U;x<LEAFTL_GROUP_SIZE;++x) if(LeaFtlMembersHas(members,x)) {
        if(first==NONE) first=x;
        last=x;
    }
    model->first_offset=(unsigned char)first; model->length=(unsigned char)(last-first);
    /* Never change K or I, including a survivor with L=0. */
}
static void Difference(LEAFTL_MEMBERS *old,const LEAFTL_MEMBERS *newer)
{
    unsigned int i; for(i=0U;i<sizeof(old->bits);++i) old->bits[i]&=(unsigned char)~newer->bits[i];
}
/* CRB uniqueness is global within the group, not limited to current level.
 * Fix old node keys BEFORE changing the CRB, so a lost SLPA cannot orphan a
 * record. Exact new segments also remove old CRB members.
 */
static int UpdateCrb(LEAFTL_INDEX_WORKSPACE *work,const LEAFTL_MEMBERS *newer)
{
    unsigned int level;
    for(level=work->first;level!=NONE;level=work->levels[level].next) {
        unsigned int *link=&work->levels[level].first;
        while(*link!=NONE) {
            unsigned int node=*link;
            LEAFTL_SEGMENT *model=&work->nodes[node].model;
            if(LeaFtlSegmentIsCrb(model)) {
                LEAFTL_MEMBERS members;
                if(LeaFtlSegmentMembers(model,&work->crb,&members)!=LEAFTL_SEGMENT_OK) return 0;
                Difference(&members,newer);
                if(LeaFtlMembersCount(&members)==0U) {
                    *link=work->nodes[node].next; DropNode(work,node); continue;
                }
                Bounds(model,&members);
            }
            link=&work->nodes[node].next;
        }
    }
    if(LeaFtlCrbRemove(&work->crb,newer)!=LEAFTL_CRB_OK) return 0;
    RemoveEmpty(work); return 1;
}
static int Overlap(const LEAFTL_SEGMENT *a,const LEAFTL_SEGMENT *b)
{ return a->first_offset<=LEAFTL_SEGMENT_LAST(b) && b->first_offset<=LEAFTL_SEGMENT_LAST(a); }
static void LinkSorted(LEAFTL_INDEX_WORKSPACE *work,unsigned int level,unsigned int node)
{
    unsigned int *link=&work->levels[level].first;
    while(*link!=NONE && work->nodes[*link].model.first_offset<work->nodes[node].model.first_offset)
        link=&work->nodes[*link].next;
    work->nodes[node].next=*link; *link=node;
}
/* Algorithm 2 seg_merge: OLD & ~NEW; trim SLPA/L, remove empty old.
 * For exact segments an interior covered point remains implicitly masked
 * under the newer upper segment, exactly as bounds-only paper merge permits.
 */
static int SegMerge(LEAFTL_INDEX_WORKSPACE *work,const LEAFTL_MEMBERS *newer,unsigned int old)
{
    LEAFTL_SEGMENT *model=&work->nodes[old].model;
    LEAFTL_MEMBERS members,removed;
    unsigned int i;
    if(LeaFtlSegmentMembers(model,&work->crb,&members)!=LEAFTL_SEGMENT_OK) return -1;
    removed=members; Difference(&members,newer);
    for(i=0U;i<sizeof(removed.bits);++i) removed.bits[i]&=newer->bits[i];
    if(LeaFtlSegmentIsCrb(model) && LeaFtlCrbRemove(&work->crb,&removed)!=LEAFTL_CRB_OK) return -1;
    if(LeaFtlMembersCount(&members)==0U) return 0;
    Bounds(model,&members); return 1;
}
static unsigned int DownLevel(LEAFTL_INDEX_WORKSPACE *work,unsigned int level,unsigned int node)
{
    unsigned int lower=work->levels[level].next,old;
    if(lower!=NONE) {
        for(old=work->levels[lower].first;old!=NONE;old=work->nodes[old].next)
            if(Overlap(&work->nodes[node].model,&work->nodes[old].model)) break;
        if(old==NONE) return lower;
    }
    {
        unsigned int created=NewLevel(work); if(created==NONE) return NONE;
        work->levels[created].next=lower; work->levels[level].next=created; return created;
    }
}
/* Algorithm 1 seg_update. Same routine is used by insertion AND compaction.
 * A collision in the next level creates a level, never recursive merging.
 */
static LEAFTL_INDEX_STATUS SegUpdate(LEAFTL_INDEX_WORKSPACE *work,unsigned int level,unsigned int node)
{
    LEAFTL_MEMBERS newer;
    unsigned int victims=NONE,*tail=&victims,*link=&work->levels[level].first;
    if(LeaFtlSegmentMembers(&work->nodes[node].model,&work->crb,&newer)!=LEAFTL_SEGMENT_OK)
        return LEAFTL_INDEX_INVALID;
    while(*link!=NONE) {
        unsigned int old=*link;
        if(Overlap(&work->nodes[node].model,&work->nodes[old].model)) {
            *link=work->nodes[old].next; *tail=old; tail=&work->nodes[old].next; *tail=NONE;
        } else link=&work->nodes[old].next;
    }
    while(victims!=NONE) {
        unsigned int old=victims,destination=level;
        int alive;
        victims=work->nodes[old].next;
        alive=SegMerge(work,&newer,old); if(alive<0) return LEAFTL_INDEX_INVALID;
        if(alive==0) { DropNode(work,old); continue; }
        if(Overlap(&work->nodes[node].model,&work->nodes[old].model)) {
            destination=DownLevel(work,level,old);
            if(destination==NONE) return LEAFTL_INDEX_NO_SPACE;
        }
        LinkSorted(work,destination,old);
    }
    LinkSorted(work,level,node); return LEAFTL_INDEX_OK;
}
static int Fits(const LEAFTL_INDEX *index,const LEAFTL_INDEX_GROUP *group,
    const LEAFTL_INDEX_WORKSPACE *work)
{
    unsigned int old_nodes=group!=0 ? group->segment_count : 0U;
    unsigned int old_levels=group!=0 ? group->level_count : 0U;
    return work->node_count<=LEAFTL_MAX_SEGMENTS_PER_GROUP &&
        work->level_count<=LEAFTL_MAX_LEVELS_PER_GROUP &&
        work->node_count<=index->node_capacity-index->node_count+old_nodes &&
        work->level_count<=index->level_capacity-index->level_count+old_levels;
}
static void Commit(LEAFTL_INDEX *index,unsigned int position,unsigned int id,int exists,
    const LEAFTL_INDEX_WORKSPACE *work)
{
    LEAFTL_INDEX_GROUP *group;
    unsigned int level,*level_link,i;
    if(exists) ReleaseGroup(index,&index->groups[position]);
    else {
        for(i=index->group_count;i>position;--i) index->groups[i]=index->groups[i-1U];
        ++index->group_count;
        index->groups[position].cache_state=0;
    }
    group=&index->groups[position]; group->group_id=id; group->crb=work->crb;
    group->segment_count=work->node_count; group->level_count=work->level_count;
    level_link=&group->first_level;
    for(level=work->first;level!=NONE;level=work->levels[level].next) {
        unsigned int l=AllocateLevel(index),node,*link=&index->levels[l].first_segment,cursor;
        *level_link=l; level_link=&index->levels[l].next;
        for(node=work->levels[level].first;node!=NONE;node=work->nodes[node].next) {
            unsigned int n=AllocateNode(index,&work->nodes[node].model);
            *link=n; link=&index->nodes[n].next; ++index->levels[l].segment_count;
        }
        cursor=index->levels[l].first_segment;
        index->levels[l].root_segment=BuildTree(index,&cursor,index->levels[l].segment_count);
    }
    *level_link=NONE;
}
static LEAFTL_INDEX_STATUS PlanInsert(const LEAFTL_INDEX *index,
    const LEAFTL_SEGMENT_INPUT *segments,unsigned int count,LEAFTL_INDEX_WORKSPACE *work)
{
    unsigned int i,id,position;
    int exists; const LEAFTL_INDEX_GROUP *group;
    if(!Ready(index) || count>LEAFTL_GROUP_SIZE || (count!=0U && (segments==0 || work==0)))
        return LEAFTL_INDEX_INVALID;
    if(count==0U) return LEAFTL_INDEX_OK;
    id=segments[0].group_id;
    for(i=0U;i<count;++i) if(!LeaFtlSegmentInputValidate(&segments[i]) ||
        segments[i].group_id!=id ||
        (i!=0U && LEAFTL_SEGMENT_LAST(&segments[i-1U].model)>=segments[i].model.first_offset))
            return LEAFTL_INDEX_INVALID;
    position=FindGroup(index,id); exists=position<index->group_count && index->groups[position].group_id==id;
    group=exists ? &index->groups[position] : 0;
    if(!LoadGroup(index,group,work)) return LEAFTL_INDEX_INVALID;
    for(i=0U;i<count;++i) {
        unsigned int node; LEAFTL_INDEX_STATUS status;
        if(!UpdateCrb(work,&segments[i].members)) return LEAFTL_INDEX_INVALID;
        if(LeaFtlSegmentIsCrb(&segments[i].model) &&
           LeaFtlCrbPut(&work->crb,&segments[i].members)!=LEAFTL_CRB_OK) return LEAFTL_INDEX_INVALID;
        if(work->first==NONE) { work->first=NewLevel(work); if(work->first==NONE) return LEAFTL_INDEX_NO_SPACE; }
        node=NewNode(work,&segments[i].model); if(node==NONE) return LEAFTL_INDEX_NO_SPACE;
        status=SegUpdate(work,work->first,node); if(status!=LEAFTL_INDEX_OK) return status;
    }
    RemoveEmpty(work);
    if((!exists && index->group_count==index->group_capacity) || !Fits(index,group,work)) return LEAFTL_INDEX_NO_SPACE;
    return LEAFTL_INDEX_OK;
}
LEAFTL_INDEX_STATUS LeaFtlIndexCanInsert(const LEAFTL_INDEX *index,
    const LEAFTL_SEGMENT_INPUT *segments,unsigned int count,LEAFTL_INDEX_WORKSPACE *work)
{ return PlanInsert(index,segments,count,work); }
LEAFTL_INDEX_STATUS LeaFtlIndexInsert(LEAFTL_INDEX *index,
    const LEAFTL_SEGMENT_INPUT *segments,unsigned int count,LEAFTL_INDEX_WORKSPACE *work)
{
    LEAFTL_INDEX_STATUS status=PlanInsert(index,segments,count,work);
    unsigned int position,id; int exists;
    if(status!=LEAFTL_INDEX_OK || count==0U) return status;
    id=segments[0].group_id; position=FindGroup(index,id);
    exists=position<index->group_count && index->groups[position].group_id==id;
    Commit(index,position,id,exists,work); return LEAFTL_INDEX_OK;
}
LEAFTL_INDEX_STATUS LeaFtlIndexLookup(const LEAFTL_INDEX *index,unsigned int lpn,unsigned int *ppn)
{
    unsigned int pos,level,id,x;
    if(!Ready(index) || ppn==0 || lpn==LEAFTL_LPN_NONE) return LEAFTL_INDEX_INVALID;
    id=LEAFTL_GROUP_ID(lpn); x=LEAFTL_GROUP_OFFSET(lpn); pos=FindGroup(index,id);
    if(pos==index->group_count || index->groups[pos].group_id!=id) return LEAFTL_INDEX_MISS;
    for(level=index->groups[pos].first_level;level!=NONE;level=index->levels[level].next) {
        unsigned int node=index->levels[level].root_segment;
        while(node!=NONE) {
            const LEAFTL_SEGMENT *model=&index->nodes[node].model;
            if(x<model->first_offset) node=index->nodes[node].left;
            else if(x>LEAFTL_SEGMENT_LAST(model)) node=index->nodes[node].right;
            else {
                LEAFTL_SEGMENT_STATUS status=LeaFtlSegmentPredict(model,&index->groups[pos].crb,id,lpn,ppn);
                if(status==LEAFTL_SEGMENT_OK) return LEAFTL_INDEX_OK;
                if(status!=LEAFTL_SEGMENT_MISS) return LEAFTL_INDEX_INVALID;
                break;
            }
        }
    }
    return LEAFTL_INDEX_MISS;
}
LEAFTL_INDEX_STATUS LeaFtlIndexGetGroupInfo(const LEAFTL_INDEX *index,unsigned int id,
    LEAFTL_INDEX_GROUP_INFO *info)
{
    unsigned int pos;
    if(!Ready(index) || info==0 || id>LEAFTL_GROUP_ID(LEAFTL_LPN_NONE-1U)) return LEAFTL_INDEX_INVALID;
    pos=FindGroup(index,id);
    if(pos==index->group_count || index->groups[pos].group_id!=id) return LEAFTL_INDEX_MISS;
    info->level_count=index->groups[pos].level_count; info->segment_count=index->groups[pos].segment_count;
    return LEAFTL_INDEX_OK;
}

/* Algorithm 2 across upper/lower pairs, before moving upper levels. Unlike
 * the removed implementation this only trims old bounds, not survivor runs.
 */
static int MergeLevelPairs(LEAFTL_INDEX_WORKSPACE *work)
{
    unsigned int upper;
    for(upper=work->first;upper!=NONE;upper=work->levels[upper].next) {
        unsigned int fresh;
        for(fresh=work->levels[upper].first;fresh!=NONE;fresh=work->nodes[fresh].next) {
            LEAFTL_MEMBERS newer; unsigned int lower;
            if(LeaFtlSegmentMembers(&work->nodes[fresh].model,&work->crb,&newer)!=LEAFTL_SEGMENT_OK) return 0;
            for(lower=work->levels[upper].next;lower!=NONE;lower=work->levels[lower].next) {
                unsigned int *link=&work->levels[lower].first;
                while(*link!=NONE) {
                    unsigned int old=*link;
                    if(Overlap(&work->nodes[fresh].model,&work->nodes[old].model)) {
                        int alive=SegMerge(work,&newer,old); if(alive<0) return 0;
                        if(alive==0) { *link=work->nodes[old].next; DropNode(work,old); continue; }
                    }
                    link=&work->nodes[old].next;
                }
            }
        }
    }
    RemoveEmpty(work); return 1;
}
LEAFTL_INDEX_STATUS LeaFtlIndexCompactGroup(LEAFTL_INDEX *index,unsigned int id,
    LEAFTL_INDEX_WORKSPACE *work,LEAFTL_INDEX_COMPACTION_RESULT *result)
{
    unsigned int pos,level,count=0U,i;
    LEAFTL_INDEX_GROUP *group; LEAFTL_INDEX_COMPACTION_RESULT outcome;
    if(!Ready(index) || work==0 || id>LEAFTL_GROUP_ID(LEAFTL_LPN_NONE-1U)) return LEAFTL_INDEX_INVALID;
    pos=FindGroup(index,id);
    if(pos==index->group_count || index->groups[pos].group_id!=id) return LEAFTL_INDEX_MISS;
    group=&index->groups[pos];
    if(!LoadGroup(index,group,work) || !MergeLevelPairs(work)) return LEAFTL_INDEX_INVALID;
    for(level=work->first;level!=NONE;level=work->levels[level].next) work->schedule[count++]=level;
    /* A finite original-level traversal prevents endless movement when
     * an exact older range still encloses a newer segment (Figure 13 T2).
     * Each upper level calls the SAME seg_update against its lower level.
     */
    if(count>1U) for(i=count-1U;i>0U;--i) {
        unsigned int source=work->schedule[i-1U],destination=work->levels[source].next;
        while(work->levels[source].first!=NONE) {
            unsigned int node=work->levels[source].first; LEAFTL_INDEX_STATUS status;
            work->levels[source].first=work->nodes[node].next;
            status=SegUpdate(work,destination,node); if(status!=LEAFTL_INDEX_OK) return status;
        }
        /* source is still linked; the next original source is its ancestor. */
        {
            unsigned int *link=&work->first;
            while(*link!=source) link=&work->levels[*link].next;
            *link=work->levels[source].next; DropLevel(work,source);
        }
    }
    RemoveEmpty(work);
    if(!Fits(index,group,work)) return LEAFTL_INDEX_NO_SPACE;
    outcome.segments_before=group->segment_count; outcome.levels_before=group->level_count;
    outcome.segments_after=work->node_count; outcome.levels_after=work->level_count;
    Commit(index,pos,id,1,work); if(result!=0) *result=outcome; return LEAFTL_INDEX_OK;
}

int LeaFtlIndexValidateSnapshot(LEAFTL_INDEX_WORKSPACE *w)
{
    unsigned int l,n,levels=0,nodes=0,i;
    if(!w || !w->node_count || !w->level_count ||
       w->node_count>LEAFTL_MAX_SEGMENTS_PER_GROUP ||
       w->level_count>LEAFTL_MAX_LEVELS_PER_GROUP || !LeaFtlCrbValidate(&w->crb)) return 0;
    for(i=0;i<CAP;i++) w->schedule[i]=0;
    for(l=w->first;l!=NONE;l=w->levels[l].next) {
        unsigned int previous=NONE,count=0;
        if(l>=CAP || ++levels>w->level_count) return 0;
        for(n=w->levels[l].first;n!=NONE;n=w->nodes[n].next) {
            LEAFTL_MEMBERS members;
            const LEAFTL_SEGMENT *model;
            if(n>=CAP || w->schedule[n] || ++nodes>w->node_count) return 0;
            w->schedule[n]=1; model=&w->nodes[n].model;
            if(!LeaFtlSegmentValidate(model) ||
               (previous!=NONE && previous>=model->first_offset) ||
               LeaFtlSegmentMembers(model,&w->crb,&members)!=LEAFTL_SEGMENT_OK ||
               !LeaFtlMembersCount(&members)) return 0;
            previous=LEAFTL_SEGMENT_LAST(model); count++;
        }
        if(!count) return 0;
    }
    return nodes==w->node_count && levels==w->level_count;
}
LEAFTL_INDEX_STATUS LeaFtlIndexExportGroup(const LEAFTL_INDEX *index,unsigned int id,
    LEAFTL_INDEX_WORKSPACE *w)
{
    unsigned int pos;
    if(!Ready(index) || !w || id>LEAFTL_GROUP_ID(LEAFTL_LPN_NONE-1U)) return LEAFTL_INDEX_INVALID;
    pos=FindGroup(index,id);
    if(pos==index->group_count || index->groups[pos].group_id!=id) return LEAFTL_INDEX_MISS;
    if(!LoadGroup(index,&index->groups[pos],w) || !LeaFtlIndexValidateSnapshot(w)) return LEAFTL_INDEX_INVALID;
    return LEAFTL_INDEX_OK;
}
LEAFTL_INDEX_STATUS LeaFtlIndexRestoreGroup(LEAFTL_INDEX *index,unsigned int id,
    LEAFTL_INDEX_WORKSPACE *w)
{
    unsigned int pos; int exists;
    if(!Ready(index) || id>LEAFTL_GROUP_ID(LEAFTL_LPN_NONE-1U) ||
       !LeaFtlIndexValidateSnapshot(w)) return LEAFTL_INDEX_INVALID;
    if(id==LEAFTL_GROUP_ID(LEAFTL_LPN_NONE)) {
        unsigned int l,n;
        for(l=w->first;l!=NONE;l=w->levels[l].next)
            for(n=w->levels[l].first;n!=NONE;n=w->nodes[n].next)
                if(LEAFTL_SEGMENT_LAST(&w->nodes[n].model)==255U) return LEAFTL_INDEX_INVALID;
    }
    pos=FindGroup(index,id); exists=pos<index->group_count && index->groups[pos].group_id==id;
    if((!exists && index->group_count==index->group_capacity) ||
       !Fits(index,exists ? &index->groups[pos] : 0,w)) return LEAFTL_INDEX_NO_SPACE;
    Commit(index,pos,id,exists,w); return LEAFTL_INDEX_OK;
}

LEAFTL_INDEX_STATUS LeaFtlIndexRemoveGroup(LEAFTL_INDEX *index,unsigned int id)
{
    unsigned int pos,i;
    if(!Ready(index)) return LEAFTL_INDEX_INVALID;
    pos=FindGroup(index,id);
    if(pos==index->group_count || index->groups[pos].group_id!=id) return LEAFTL_INDEX_MISS;
    ReleaseGroup(index,&index->groups[pos]);
    for(i=pos+1;i<index->group_count;i++) index->groups[i-1]=index->groups[i];
    --index->group_count;return LEAFTL_INDEX_OK;
}
LEAFTL_INDEX_STATUS LeaFtlIndexOverlayGroup(const LEAFTL_INDEX *index,unsigned int id,
    LEAFTL_INDEX_WORKSPACE *w,LEAFTL_SEGMENT_INPUT *inputs)
{
    unsigned int pos,rank;
    const LEAFTL_INDEX_GROUP *g;
    if(!Ready(index) || !inputs || !LeaFtlIndexValidateSnapshot(w)) return LEAFTL_INDEX_INVALID;
    pos=FindGroup(index,id);
    if(pos==index->group_count || index->groups[pos].group_id!=id) return LEAFTL_INDEX_OK;
    g=&index->groups[pos];
    for(rank=g->level_count;rank>0;rank--) {
        unsigned int l=g->first_level,j,n,count=0,target;
        for(j=1;j<rank;j++) l=index->levels[l].next;
        for(n=index->levels[l].first_segment;n!=NONE;n=index->nodes[n].next) {
            if(count==LEAFTL_GROUP_SIZE) return LEAFTL_INDEX_INVALID;
            inputs[count].group_id=id;inputs[count].model=index->nodes[n].model;
            if(LeaFtlSegmentMembers(&inputs[count].model,&g->crb,&inputs[count].members)!=LEAFTL_SEGMENT_OK)
                return LEAFTL_INDEX_INVALID;
            count++;
        }
        /* UpdateCrb may drop empty old levels; allocate the fresh level AFTER. */
        for(j=0;j<count;j++) if(!UpdateCrb(w,&inputs[j].members)) return LEAFTL_INDEX_INVALID;
        target=NewLevel(w);if(target==NONE) return LEAFTL_INDEX_NO_SPACE;
        w->levels[target].next=w->first;w->first=target;
        for(j=0;j<count;j++) {
            if(LeaFtlSegmentIsCrb(&inputs[j].model) &&
               LeaFtlCrbPut(&w->crb,&inputs[j].members)!=LEAFTL_CRB_OK) return LEAFTL_INDEX_INVALID;
            n=NewNode(w,&inputs[j].model);if(n==NONE) return LEAFTL_INDEX_NO_SPACE;
            LinkSorted(w,target,n);
        }
        if(!MergeLevelPairs(w)) return LEAFTL_INDEX_INVALID;
    }
    return LeaFtlIndexValidateSnapshot(w) ? LEAFTL_INDEX_OK : LEAFTL_INDEX_NO_SPACE;
}
