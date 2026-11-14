#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "leaftl_index.h"

#define GROUPS 8U
#define LEVELS 1024U
#define NODES 4096U
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

typedef struct {
    LEAFTL_INDEX index;
    LEAFTL_INDEX_GROUP groups[GROUPS];
    LEAFTL_INDEX_LEVEL levels[LEVELS];
    LEAFTL_INDEX_NODE nodes[NODES];
    unsigned int oracle[GROUPS][LEAFTL_GROUP_SIZE];
} FIXTURE;

static FIXTURE state;
static FIXTURE snapshot;
static LEAFTL_INDEX_WORKSPACE work;
static unsigned int compactions;
static unsigned int random_attempts;
static unsigned int lookup_checks;
static unsigned int random_state = 0x8d21ac57U;
static LEAFTL_INDEX_COMPACTION_WORKSPACE compaction_work;
static unsigned int compaction_checks;

static unsigned int Random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void Init(unsigned int groups, unsigned int levels, unsigned int nodes)
{
    unsigned int g;
    unsigned int x;
    memset(&state, 0, sizeof(state));
    CHECK(groups <= GROUPS && levels <= LEVELS && nodes <= NODES);
    CHECK(LeaFtlIndexInit(&state.index, state.groups, groups, state.levels,
                         levels, state.nodes, nodes) == LEAFTL_INDEX_OK);
    for (g = 0U; g < GROUPS; ++g)
        for (x = 0U; x < LEAFTL_GROUP_SIZE; ++x)
            state.oracle[g][x] = LEAFTL_PPN_NONE;
}

static unsigned int Train(const LEAFTL_TRAINING_POINT *points,
                          unsigned int count, LEAFTL_SEGMENT_INPUT *segments)
{
    unsigned int produced;
    CHECK(LeaFtlSegmentTrain(points, count, segments, LEAFTL_GROUP_SIZE,
                            &produced) == LEAFTL_SEGMENT_OK);
    return produced;
}

static void PublishOracle(const LEAFTL_TRAINING_POINT *points, unsigned int count)
{
    unsigned int i;
    for (i = 0U; i < count; ++i) {
        unsigned int group = LEAFTL_GROUP_ID(points[i].lpn);
        CHECK(group < GROUPS);
        state.oracle[group][LEAFTL_GROUP_OFFSET(points[i].lpn)] = points[i].ppn;
    }
}

static void InsertPoints(const LEAFTL_TRAINING_POINT *points, unsigned int count)
{
    LEAFTL_SEGMENT_INPUT segments[LEAFTL_GROUP_SIZE];
    unsigned int produced = Train(points, count, segments);
    CHECK(LeaFtlIndexInsert(&state.index, segments, produced, &work) == LEAFTL_INDEX_OK);
    PublishOracle(points, count);
}

static unsigned int CheckTree(unsigned int root, unsigned int *cursor,
                              unsigned int *visits)
{
    unsigned int left_height;
    unsigned int right_height;
    if (root == LEAFTL_INDEX_NONE)
        return 0U;
    CHECK(root < state.index.node_capacity);
    CHECK(*visits < LEAFTL_GROUP_SIZE);
    ++*visits;
    left_height = CheckTree(state.nodes[root].left, cursor, visits);
    CHECK(root == *cursor);
    *cursor = state.nodes[root].next;
    right_height = CheckTree(state.nodes[root].right, cursor, visits);
    CHECK(left_height <= right_height + 1U && right_height <= left_height + 1U);
    return 1U + (left_height > right_height ? left_height : right_height);
}

/* Validate both active ownership and free lists, not just lookup outputs. */
static void CheckStructure(void)
{
    unsigned char seen_nodes[NODES] = {0};
    unsigned char seen_levels[LEVELS] = {0};
    unsigned int total_nodes = 0U;
    unsigned int total_levels = 0U;
    unsigned int g;
    unsigned int slot;
    unsigned int free_count;
    CHECK(state.index.group_count <= state.index.group_capacity);
    for (g = 0U; g < state.index.group_count; ++g) {
        unsigned int group_nodes = 0U, crb_nodes = 0U;
        unsigned char keys[256] = {0};
        CHECK(LeaFtlCrbValidate(&state.groups[g].crb));
        unsigned int group_levels = 0U;
        unsigned int level = state.groups[g].first_level;
        if (g != 0U)
            CHECK(state.groups[g - 1U].group_id < state.groups[g].group_id);
        while (level != LEAFTL_INDEX_NONE) {
            unsigned int node;
            unsigned int level_nodes = 0U;
            unsigned int prior_end = 0U;
            unsigned int cursor;
            unsigned int visits = 0U;
            CHECK(level < state.index.level_capacity && seen_levels[level] == 0U);
            seen_levels[level] = 1U;
            ++group_levels;
            for (node = state.levels[level].first_segment;
                 node != LEAFTL_INDEX_NONE; node = state.nodes[node].next) {
                CHECK(node < state.index.node_capacity && seen_nodes[node] == 0U);
                seen_nodes[node] = 1U;
                /* Group identity is stored by the enclosing group, not the 8-byte model. */
                CHECK(LeaFtlSegmentValidate(&state.nodes[node].model));
                if (LeaFtlSegmentIsCrb(&state.nodes[node].model)) {
                    LEAFTL_MEMBERS members;
                    unsigned int key=state.nodes[node].model.first_offset;
                    CHECK(keys[key]==0U); keys[key]=1U; ++crb_nodes;
                    CHECK(LeaFtlSegmentMembers(&state.nodes[node].model,&state.groups[g].crb,&members)==LEAFTL_SEGMENT_OK);
                }

                if (level_nodes != 0U)
                    CHECK(prior_end < state.nodes[node].model.first_offset);
                prior_end = LEAFTL_SEGMENT_LAST(&state.nodes[node].model);
                ++level_nodes;
            }
            CHECK(level_nodes > 0U && level_nodes == state.levels[level].segment_count);
            cursor = state.levels[level].first_segment;
            (void)CheckTree(state.levels[level].root_segment, &cursor, &visits);
            CHECK(cursor == LEAFTL_INDEX_NONE && visits == level_nodes);
            group_nodes += level_nodes;
            level = state.levels[level].next;
        }
        CHECK(crb_nodes == state.groups[g].crb.records);
        CHECK(group_levels == state.groups[g].level_count);
        CHECK(group_nodes == state.groups[g].segment_count);
        CHECK(group_levels <= LEAFTL_MAX_LEVELS_PER_GROUP);
        CHECK(group_nodes <= LEAFTL_MAX_SEGMENTS_PER_GROUP);
        total_nodes += group_nodes;
        total_levels += group_levels;
    }
    CHECK(total_nodes == state.index.node_count);
    CHECK(total_levels == state.index.level_count);
    free_count = 0U;
    for (slot = state.index.free_node; slot != LEAFTL_INDEX_NONE;
         slot = state.nodes[slot].next) {
        CHECK(slot < state.index.node_capacity && seen_nodes[slot] == 0U);
        seen_nodes[slot] = 1U;
        ++free_count;
    }
    CHECK(free_count + total_nodes == state.index.node_capacity);
    free_count = 0U;
    for (slot = state.index.free_level; slot != LEAFTL_INDEX_NONE;
         slot = state.levels[slot].next) {
        CHECK(slot < state.index.level_capacity && seen_levels[slot] == 0U);
        seen_levels[slot] = 1U;
        ++free_count;
    }
    CHECK(free_count + total_levels == state.index.level_capacity);
}

static void CheckOracle(void)
{
    unsigned int group;
    unsigned int x;
    for (group = 0U; group < GROUPS; ++group) {
        for (x = 0U; x < LEAFTL_GROUP_SIZE; ++x) {
            unsigned int ppn = 0xa5a5a5a5U;
            LEAFTL_INDEX_STATUS status = LeaFtlIndexLookup(
                &state.index, group * LEAFTL_GROUP_SIZE + x, &ppn);
            if (state.oracle[group][x] == LEAFTL_PPN_NONE) {
                CHECK(status == LEAFTL_INDEX_MISS && ppn == 0xa5a5a5a5U);
            } else {
                CHECK(status == LEAFTL_INDEX_OK && ppn == state.oracle[group][x]);
            }
            ++lookup_checks;
        }
    }
    CheckStructure();
}

static LEAFTL_SEGMENT_INPUT Manual(unsigned int group,const unsigned char *offsets,
    unsigned int count,double k,double intercept,int crb)
{
    LEAFTL_SEGMENT_INPUT input={0}; unsigned int i;
    CHECK(count!=0U);
    input.group_id=group; input.model.intercept=(uint32_t)ceil(intercept);
    input.model.slope_bits=(uint16_t)((LeaFtlFloat16Encode(k)&0xfffeU)|(crb?1U:0U));
    input.model.first_offset=offsets[0]; input.model.length=(unsigned char)(offsets[count-1U]-offsets[0]);
    for(i=0U;i<count;++i) LeaFtlMembersAdd(&input.members,offsets[i]);
    CHECK(LeaFtlSegmentInputValidate(&input)); return input;
}
static void InsertManual(const LEAFTL_SEGMENT_INPUT *input)
{
    unsigned int x;
    CHECK(LeaFtlIndexInsert(&state.index,input,1U,&work)==LEAFTL_INDEX_OK);
    for(x=0U;x<256U;++x) if(LeaFtlMembersHas(&input->members,x)) {
        double k=LeaFtlFloat16Decode(input->model.slope_bits&0xfffeU);
        state.oracle[input->group_id][x]=(unsigned int)ceil(k*x+input->model.intercept);
    }
}
static int SameCoefficients(const LEAFTL_SEGMENT *a,const LEAFTL_SEGMENT *b)
{
    return a->slope_bits==b->slope_bits &&
        memcmp(&a->intercept,&b->intercept,sizeof(a->intercept))==0;
}
static void CheckCoefficients(unsigned int group)
{
    unsigned int g,before_group=LEAFTL_INDEX_NONE;
    for(g=0U;g<snapshot.index.group_count;++g) if(snapshot.groups[g].group_id==group) before_group=g;
    CHECK(before_group!=LEAFTL_INDEX_NONE);
    for(g=0U;g<state.index.group_count;++g) if(state.groups[g].group_id==group) {
        unsigned int l;
        for(l=state.groups[g].first_level;l!=LEAFTL_INDEX_NONE;l=state.levels[l].next) {
            unsigned int n;
            for(n=state.levels[l].first_segment;n!=LEAFTL_INDEX_NONE;n=state.nodes[n].next) {
                unsigned int bl; int found=0;
                for(bl=snapshot.groups[before_group].first_level;bl!=LEAFTL_INDEX_NONE;bl=snapshot.levels[bl].next) {
                    unsigned int bn;
                    for(bn=snapshot.levels[bl].first_segment;bn!=LEAFTL_INDEX_NONE;bn=snapshot.nodes[bn].next)
                        if(SameCoefficients(&snapshot.nodes[bn].model,&state.nodes[n].model)) found=1;
                }
                CHECK(found); /* Includes nonzero-K trimmed singletons. */
            }
        }
    }
}
static void Compact(unsigned int group)
{
    LEAFTL_INDEX_COMPACTION_RESULT result;
    LEAFTL_INDEX_GROUP_INFO before,after;
    CHECK(LeaFtlIndexGetGroupInfo(&state.index,group,&before)==LEAFTL_INDEX_OK);
    snapshot=state;
    CHECK(LeaFtlIndexCompactGroup(&state.index,group,&work,&result)==LEAFTL_INDEX_OK);
    CHECK(LeaFtlIndexGetGroupInfo(&state.index,group,&after)==LEAFTL_INDEX_OK);
    CHECK(result.segments_before==before.segment_count && result.segments_after==after.segment_count);
    CHECK(result.levels_before==before.level_count && result.levels_after==after.level_count);
    CheckCoefficients(group); CheckOracle(); ++compactions;
}
static void PaperFigure13(void)
{
    LEAFTL_TRAINING_POINT points[256]; LEAFTL_INDEX_GROUP_INFO info;
    LEAFTL_SEGMENT_INPUT input;
    const unsigned char a[]={75U,78U,82U},b[]={72U,73U,80U};
    unsigned int i;
    Init(GROUPS,LEVELS,NODES);
    for(i=0U;i<64U;++i) { points[i].lpn=i;points[i].ppn=1000U+i; }
    InsertPoints(points,64U); /* T0 */
    for(i=0U;i<56U;++i) { points[i].lpn=200U+i;points[i].ppn=2000U+i; }
    InsertPoints(points,56U); /* T1 */
    for(i=0U;i<16U;++i) { points[i].lpn=16U+i;points[i].ppn=3000U+i; }
    InsertPoints(points,16U); /* T2: old [0,63] is NOT repartitioned. */
    CHECK(LeaFtlIndexGetGroupInfo(&state.index,0U,&info)==LEAFTL_INDEX_OK);
    CHECK(info.segment_count==3U && info.level_count==2U);
    input=Manual(0U,a,3U,1.0,4000.5,1); InsertManual(&input); /* T3 */
    input=Manual(0U,b,3U,1.0,5000.5,1); InsertManual(&input); /* T4 */
    CheckOracle(); /* T5 LPN50 lower exact; T6 LPN78 lower CRB. */
    {
        unsigned int ppn;
        CHECK(LeaFtlIndexLookup(&state.index,50U,&ppn)==LEAFTL_INDEX_OK && ppn==1050U);
        CHECK(LeaFtlIndexLookup(&state.index,78U,&ppn)==LEAFTL_INDEX_OK && ppn==4079U);
    }
    for(i=0U;i<59U;++i) { points[i].lpn=32U+i;points[i].ppn=6000U+i; }
    InsertPoints(points,59U); /* T7 exact overwrites BOTH approximate records. */
    CHECK(state.groups[0].crb.count==0U && state.groups[0].crb.records==0U);
    Compact(0U); /* T8 */
    CHECK(LeaFtlIndexGetGroupInfo(&state.index,0U,&info)==LEAFTL_INDEX_OK);
    CHECK(info.level_count==1U && info.segment_count==4U);
    {
        const unsigned char first[]={0U,16U,32U,200U},last[]={15U,31U,90U,255U};
        unsigned int n=state.levels[state.groups[0].first_level].first_segment;
        for(i=0U;i<4U;++i) {
            CHECK(n!=LEAFTL_INDEX_NONE && state.nodes[n].model.first_offset==first[i]);
            CHECK(LEAFTL_SEGMENT_LAST(&state.nodes[n].model)==last[i]); n=state.nodes[n].next;
        }
        CHECK(n==LEAFTL_INDEX_NONE);
    }
    Compact(0U);
}
static void CrbCollisions(void)
{
    const unsigned char a[]={100U,101U,103U,104U,106U},b[]={102U,105U,107U,108U};
    const unsigned char c[]={100U,102U},single[]={101U};
    LEAFTL_SEGMENT_INPUT input; LEAFTL_TRAINING_POINT points[256]; unsigned int i;
    Init(GROUPS,LEVELS,NODES);
    input=Manual(0U,a,5U,1.0,1000.5,1); InsertManual(&input);
    input=Manual(0U,b,4U,1.0,2000.5,1); InsertManual(&input); CheckOracle();
    input=Manual(0U,c,2U,1.0,3000.5,1); InsertManual(&input); CheckOracle();
    CHECK(state.groups[0].crb.records==3U);
    CHECK(LeaFtlCrbContains(&state.groups[0].crb,100U,102U));
    CHECK(LeaFtlCrbContains(&state.groups[0].crb,101U,106U));
    CHECK(LeaFtlCrbContains(&state.groups[0].crb,105U,108U));
    CHECK(!LeaFtlCrbContains(&state.groups[0].crb,100U,101U));
    /* Exact overwrite of a CRB record's SLPA: rename old to 103, K/I kept. */
    input=Manual(0U,single,1U,0.0,0.0,0); InsertManual(&input); CheckOracle();
    CHECK(LeaFtlCrbContains(&state.groups[0].crb,103U,106U));
    Compact(0U);
    for(i=0U;i<256U;++i) {points[i].lpn=i;points[i].ppn=10000U+i;}
    InsertPoints(points,256U); CheckOracle();
    CHECK(state.groups[0].crb.records==0U);
    CHECK(state.groups[0].segment_count==1U && state.groups[0].level_count==1U);
    /* CRB-backed singleton retains K/I too, including SLPA=255. */
    Init(GROUPS,LEVELS,NODES);
    {
        const unsigned char endpoints[]={0U,255U},zero[]={0U};
        LEAFTL_SEGMENT old;
        unsigned int level,n; int found=0;
        input=Manual(0U,endpoints,2U,1.0,9.5,1); old=input.model; InsertManual(&input);
        input=Manual(0U,zero,1U,0.0,999U,0); InsertManual(&input); CheckOracle();
        CHECK(LeaFtlCrbContains(&state.groups[0].crb,255U,255U));
        Compact(0U);
        for(level=state.groups[0].first_level;level!=LEAFTL_INDEX_NONE;level=state.levels[level].next)
            for(n=state.levels[level].first_segment;n!=LEAFTL_INDEX_NONE;n=state.nodes[n].next)
                if(state.nodes[n].model.first_offset==255U) {
                    CHECK(LEAFTL_SEGMENT_LAST(&state.nodes[n].model)==255U);
                    CHECK(SameCoefficients(&old,&state.nodes[n].model));found=1;
                }
        CHECK(found);
    }
}
static void InsertMergeAndSingleton(void)
{
    LEAFTL_TRAINING_POINT points[64]; LEAFTL_SEGMENT_INPUT models[256];
    LEAFTL_SEGMENT old; unsigned int i,n;
    Init(GROUPS,LEVELS,NODES);
    for(i=0U;i<64U;++i) { points[i].lpn=i;points[i].ppn=1000U+i; }
    CHECK(Train(points,64U,models)==1U); old=models[0].model; InsertPoints(points,64U);
    for(i=0U;i<32U;++i) {points[i].lpn=i;points[i].ppn=2000U+i;}
    InsertPoints(points,32U); CheckOracle();
    CHECK(state.groups[0].level_count==1U && state.groups[0].segment_count==2U);
    n=state.levels[state.groups[0].first_level].first_segment;
    n=state.nodes[n].next;
    CHECK(state.nodes[n].model.first_offset==32U && LEAFTL_SEGMENT_LAST(&state.nodes[n].model)==63U);
    CHECK(SameCoefficients(&old,&state.nodes[n].model));
    /* Preserve old K/I when trimming to a SINGLE point, at insert and compact. */
    for(i=0U;i<31U;++i) {points[i].lpn=32U+i;points[i].ppn=3000U+i;}
    InsertPoints(points,31U); CheckOracle(); Compact(0U);
    {
        unsigned int level; int found=0;
        for(level=state.groups[0].first_level;level!=LEAFTL_INDEX_NONE;level=state.levels[level].next)
            for(n=state.levels[level].first_segment;n!=LEAFTL_INDEX_NONE;n=state.nodes[n].next)
                if(state.nodes[n].model.first_offset==63U) {
                    CHECK(LEAFTL_SEGMENT_LAST(&state.nodes[n].model)==63U);
                    CHECK(SameCoefficients(&old,&state.nodes[n].model)); found=1;
                }
        CHECK(found);
    }
    /* Interior update is masked by level order, not split into new strides. */
    Init(GROUPS,LEVELS,NODES);
    for(i=0U;i<64U;++i) {points[i].lpn=i;points[i].ppn=1000U+i;}
    InsertPoints(points,64U);
    for(i=0U;i<16U;++i) {points[i].lpn=16U+i;points[i].ppn=2000U+i;}
    InsertPoints(points,16U); Compact(0U);
    CHECK(state.groups[0].segment_count==2U && state.groups[0].level_count==2U);
    for(i=0U;i<4U;++i) {points[i].lpn=20U+i;points[i].ppn=3000U+i;}
    InsertPoints(points,4U); CheckOracle();
    CHECK(state.groups[0].level_count==3U); /* Collision creates a new level. */
}
static void AtomicFailureAndReuse(void)
{
    LEAFTL_TRAINING_POINT points[64]; LEAFTL_SEGMENT_INPUT inputs[256];
    LEAFTL_INDEX_COMPACTION_RESULT result={1U,2U,3U,4U},before=result;
    unsigned int i,count;
    Init(1U,1U,1U);
    points[0].lpn=0U;points[0].ppn=0U; InsertPoints(points,1U);
    for(i=0U;i<100U;++i) { points[0].ppn=i;InsertPoints(points,1U); }
    CheckOracle(); /* Full pool replacement reuses the removed old node/level. */
    points[0].lpn=1U;points[0].ppn=200U;count=Train(points,1U,inputs);
    snapshot=state;
    CHECK(LeaFtlIndexInsert(&state.index,inputs,count,&work)==LEAFTL_INDEX_NO_SPACE);
    CHECK(memcmp(&snapshot,&state,sizeof(state))==0);
    CHECK(LeaFtlIndexInsert(&state.index,0,1U,&work)==LEAFTL_INDEX_INVALID);
    CHECK(LeaFtlIndexInsert(&state.index,inputs,count,0)==LEAFTL_INDEX_INVALID);
    CHECK(LeaFtlIndexCompactGroup(&state.index,1U,&work,&result)==LEAFTL_INDEX_MISS);
    CHECK(LeaFtlIndexCompactGroup(&state.index,0U,0,&result)==LEAFTL_INDEX_INVALID);
    CHECK(memcmp(&snapshot,&state,sizeof(state))==0 && memcmp(&result,&before,sizeof(result))==0);
    /* A late invalid input must not commit an earlier valid segment. */
    inputs[1]=inputs[0];inputs[1].group_id=1U;
    CHECK(LeaFtlIndexInsert(&state.index,inputs,2U,&work)==LEAFTL_INDEX_INVALID);
    CHECK(memcmp(&snapshot,&state,sizeof(state))==0);
    /* Pool allows old full range but not the extra collision level. */
    Init(1U,1U,4U);
    for(i=0U;i<64U;++i) {points[i].lpn=i;points[i].ppn=1000U+i;}
    InsertPoints(points,64U);
    points[0].lpn=20U;points[0].ppn=2000U;count=Train(points,1U,inputs);snapshot=state;
    CHECK(LeaFtlIndexInsert(&state.index,inputs,count,&work)==LEAFTL_INDEX_NO_SPACE);
    CHECK(memcmp(&snapshot,&state,sizeof(state))==0);CheckOracle();
    Compact(0U);
    /* Failed multi-segment batch must roll back an earlier CRB key rename. */
    Init(1U,1U,4U);
    {
        const unsigned char sparse[]={0U,2U,4U},zero[]={0U},three[]={3U};
        LEAFTL_SEGMENT_INPUT old=Manual(0U,sparse,3U,1.0,99.5,1);
        InsertManual(&old);
        inputs[0]=Manual(0U,zero,1U,0.0,500U,0);
        inputs[1]=Manual(0U,three,1U,0.0,600U,0);
        snapshot=state;
        CHECK(LeaFtlIndexInsert(&state.index,inputs,2U,&work)==LEAFTL_INDEX_NO_SPACE);
        CHECK(memcmp(&snapshot,&state,sizeof(state))==0);
        CHECK(LeaFtlCrbContains(&state.groups[0].crb,0U,0U));
        CHECK(LeaFtlCrbContains(&state.groups[0].crb,0U,4U));
        CheckOracle();
    }
    /* Final partial group with offset 254; 255 is sentinel, not a CRB delimiter. */
    Init(GROUPS,LEVELS,NODES);
    points[0].lpn=0xfffffffeU;points[0].ppn=0xfffffffeU;
    count=Train(points,1U,inputs);
    CHECK(LeaFtlIndexInsert(&state.index,inputs,count,&work)==LEAFTL_INDEX_OK);
    CHECK(LeaFtlIndexCompactGroup(&state.index,0x00ffffffU,&work,&result)==LEAFTL_INDEX_OK);
    { unsigned int ppn=999U;
      CHECK(LeaFtlIndexLookup(&state.index,0xfffffffeU,&ppn)==LEAFTL_INDEX_OK && ppn==0xfffffffeU);
      CHECK(LeaFtlIndexLookup(&state.index,0xffffffffU,&ppn)==LEAFTL_INDEX_INVALID && ppn==0xfffffffeU); }
    CheckStructure();
}
static void RandomUpdates(void)
{
    unsigned int iteration;
    Init(GROUPS,LEVELS,NODES);
    for(iteration=0U;iteration<4000U;++iteration) {
        LEAFTL_TRAINING_POINT points[256]; LEAFTL_SEGMENT_INPUT inputs[256];
        unsigned char selected[256]={0};
        unsigned int group=Random()%GROUPS,count=0U,i,selections=1U+Random()%32U;
        unsigned int base=1000U+Random()%10000000U,mode=iteration%4U,produced;
        LEAFTL_INDEX_STATUS status;
        ++random_attempts;
        for(i=0U;i<selections;++i) selected[Random()%256U]=1U;
        for(i=0U;i<256U;++i) if(selected[i]) {
            unsigned int ppn=mode==0U ? base+i : mode==1U ? base-i : Random();
            points[count].lpn=group*256U+i;points[count++].ppn=ppn==LEAFTL_PPN_NONE?0U:ppn;
        }
        produced=Train(points,count,inputs);snapshot=state;
        status=LeaFtlIndexInsert(&state.index,inputs,produced,&work);
        if(status==LEAFTL_INDEX_OK) PublishOracle(points,count);
        else {CHECK(status==LEAFTL_INDEX_NO_SPACE);CHECK(memcmp(&snapshot,&state,sizeof(state))==0);}
        CheckOracle();
        if(iteration%17U==0U || status==LEAFTL_INDEX_NO_SPACE) {
            unsigned int g;
            for(g=0U;g<GROUPS;++g) {
                LEAFTL_INDEX_GROUP_INFO info;
                if(LeaFtlIndexGetGroupInfo(&state.index,g,&info)==LEAFTL_INDEX_OK) Compact(g);
            }
        }
    }
}
int main(int argc,char **argv)
{
    PaperFigure13(); puts("PASS: paper Figure 13 layout"); fflush(stdout);
    CrbCollisions(); puts("PASS: CRB collision cases"); fflush(stdout);
    InsertMergeAndSingleton(); AtomicFailureAndReuse();
    puts("PASS: merge coefficients and atomic capacity cases"); fflush(stdout);
    if(argc<2 || strcmp(argv[1],"--targeted")!=0) RandomUpdates();
    printf("PASS: %u oracle lookups, %u mixed exact/CRB update attempts, %u compactions\n",
        lookup_checks,random_attempts,compactions);
    printf("PASS: Figure 13 T0-T8 exact final layout; SLPA collisions; exact-over-CRB deletion\n");
    printf("PASS: K/I bit preservation including singletons; list/tree/CRB/free-pool invariants; atomic failure\n");
    printf("Sizes group=%u level=%u node=%u workspace=%u bytes\n",(unsigned)sizeof(LEAFTL_INDEX_GROUP),
        (unsigned)sizeof(LEAFTL_INDEX_LEVEL),(unsigned)sizeof(LEAFTL_INDEX_NODE),(unsigned)sizeof(work));
    return 0;
}
