#include "leaftl_flash.h"
#include <string.h>
#define NONE LEAFTL_INDEX_NONE

/* Packed CRB bytes can start at ANY alignment in the borrowed DMA page.
 * Cosmos maps that page Strongly-ordered: libc memcpy's word accesses can
 * data-abort even though unaligned accesses work in ordinary cached RAM.
 * Volatile byte accesses prevent compiler widening / memcpy substitution.
 * Use the same rule on decode, where the DMA page is the source. */
static void CopyPackedBytes(unsigned char *dst,const unsigned char *src,unsigned int count)
{
    volatile unsigned char *d=dst;
    const volatile unsigned char *s=src;
    unsigned int i;
    for(i=0;i<count;i++) d[i]=s[i];
}

static void Put16(unsigned char *p,unsigned int x) { p[0]=(unsigned char)x;p[1]=(unsigned char)(x>>8); }
static void Put32(unsigned char *p,unsigned int x)
{ unsigned int i;for(i=0;i<4;i++) p[i]=(unsigned char)(x>>(8U*i)); }
static unsigned int Get16(const unsigned char *p) { return (unsigned int)p[0]|((unsigned int)p[1]<<8); }
static unsigned int Get32(const unsigned char *p)
{ return (unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24); }
static unsigned int Crc(const unsigned char *p,unsigned int n)
{
    static const uint32_t table[16]={0x00000000U,0x1db71064U,0x3b6e20c8U,0x26d930acU,
        0x76dc4190U,0x6b6b51f4U,0x4db26158U,0x5005713cU,0xedb88320U,0xf00f9344U,
        0xd6d6a3e8U,0xcb61b38cU,0x9b64c2b0U,0x86d3d2d4U,0xa00ae278U,0xbdbdf21cU};
    uint32_t crc=UINT32_MAX;unsigned int i;
    for(i=0;i<n;i++) {
        crc^=p[i];crc=(crc>>4)^table[crc&15U];crc=(crc>>4)^table[crc&15U];
    }
    return crc^UINT32_MAX;
}
static unsigned int RecordSize(const unsigned char *p)
{
    unsigned int c=Get16(p+8);
    return LEAFTL_FLASH_HEADER_BYTES+2U*Get16(p+4)+8U*Get16(p+6)+c+(c+7U)/8U;
}
void LeaFtlFlashDirectoryInit(LEAFTL_FLASH_DIRECTORY_ENTRY *d,unsigned int count)
{
    unsigned int i;if(!d) return;
    for(i=0;i<count;i++) { d[i].vsa=UINT_MAX;d[i].offset=0;d[i].length=0; }
}
LEAFTL_FLASH_STATUS LeaFtlFlashEncode(LEAFTL_INDEX_WORKSPACE *w,unsigned int id,
    unsigned char *bytes,unsigned int capacity,unsigned int *length)
{
    unsigned int size,l,n,at=LEAFTL_FLASH_HEADER_BYTES;
    if(!bytes || !length || id>LEAFTL_GROUP_ID(LEAFTL_LPN_NONE-1U) ||
       !LeaFtlIndexValidateSnapshot(w)) return LEAFTL_FLASH_INVALID;
    size=LEAFTL_FLASH_HEADER_BYTES+2U*w->level_count+8U*w->node_count+
        w->crb.count+((unsigned int)w->crb.count+7U)/8U;
    if(size>capacity) return LEAFTL_FLASH_NO_SPACE;
    Put32(bytes,id);Put16(bytes+4,w->level_count);Put16(bytes+6,w->node_count);
    Put16(bytes+8,w->crb.count);Put16(bytes+10,w->crb.records);
    for(l=w->first;l!=NONE;l=w->levels[l].next) {
        unsigned int count=0;
        for(n=w->levels[l].first;n!=NONE;n=w->nodes[n].next) count++;
        Put16(bytes+at,count);at+=2;
        for(n=w->levels[l].first;n!=NONE;n=w->nodes[n].next) {
            (void)LeaFtlSegmentEncode8(&w->nodes[n].model,bytes+at);at+=8;
        }
    }
    CopyPackedBytes(bytes+at,w->crb.offsets,w->crb.count);at+=w->crb.count;
    CopyPackedBytes(bytes+at,w->crb.starts,((unsigned int)w->crb.count+7U)/8U);
    *length=size;return LEAFTL_FLASH_OK;
}
LEAFTL_FLASH_STATUS LeaFtlFlashDecode(const unsigned char *bytes,unsigned int available,
    unsigned int id,LEAFTL_INDEX_WORKSPACE *w)
{
    unsigned int size,levels,nodes,crb_count,records,l,n=0,at=LEAFTL_FLASH_HEADER_BYTES;
    if(!bytes || !w || available<LEAFTL_FLASH_HEADER_BYTES || Get32(bytes)!=id ||
       id>LEAFTL_GROUP_ID(LEAFTL_LPN_NONE-1U)) return LEAFTL_FLASH_INVALID;
    size=RecordSize(bytes);levels=Get16(bytes+4);nodes=Get16(bytes+6);
    crb_count=Get16(bytes+8);records=Get16(bytes+10);
    if(!levels || levels>LEAFTL_MAX_LEVELS_PER_GROUP || !nodes ||
       nodes>LEAFTL_MAX_SEGMENTS_PER_GROUP || crb_count>LEAFTL_GROUP_SIZE ||
       records>crb_count || size!=available) return LEAFTL_FLASH_INVALID;
    memset(w,0,sizeof(*w));w->first=0;w->level_count=levels;w->node_count=nodes;
    for(l=0;l<levels;l++) {
        unsigned int count,j;
        if(at>size-2U) return LEAFTL_FLASH_INVALID;
        count=Get16(bytes+at);at+=2;
        if(!count || count>nodes-n || (uint64_t)at+8ULL*count>size) return LEAFTL_FLASH_INVALID;
        w->levels[l].first=n;w->levels[l].next=l+1U<levels ? l+1U : NONE;
        for(j=0;j<count;j++,n++) {
            if(LeaFtlSegmentDecode8(bytes+at,&w->nodes[n].model)!=LEAFTL_SEGMENT_OK)
                return LEAFTL_FLASH_INVALID;
            at+=8;w->nodes[n].next=j+1U<count ? n+1U : NONE;
        }
    }
    if(n!=nodes || at+crb_count+(crb_count+7U)/8U!=size) return LEAFTL_FLASH_INVALID;
    w->crb.count=(uint16_t)crb_count;w->crb.records=(uint16_t)records;
    CopyPackedBytes(w->crb.offsets,bytes+at,crb_count);at+=crb_count;
    CopyPackedBytes(w->crb.starts,bytes+at,(crb_count+7U)/8U);
    w->free_node=nodes;w->free_level=levels;
    {
        unsigned int i;
        for(i=nodes;i<LEAFTL_INDEX_PLAN_CAPACITY;i++) w->nodes[i].next=
            i+1U<LEAFTL_INDEX_PLAN_CAPACITY ? i+1U : NONE;
        for(i=levels;i<LEAFTL_INDEX_PLAN_CAPACITY;i++) {
            w->levels[i].first=NONE;w->levels[i].next=i+1U<LEAFTL_INDEX_PLAN_CAPACITY ? i+1U : NONE;
        }
    }
    return LeaFtlIndexValidateSnapshot(w) ? LEAFTL_FLASH_OK : LEAFTL_FLASH_INVALID;
}
LEAFTL_FLASH_STATUS LeaFtlFlashInit(LEAFTL_FLASH *f,LEAFTL_FLASH_DIRECTORY_ENTRY *d,
    unsigned int logical_count,unsigned char *page,unsigned int page_bytes,const LEAFTL_FLASH_IO *io)
{
    if(!f || !d || !logical_count || !page || page_bytes!=LEAFTL_FLASH_PAGE_BYTES ||
       !io || !io->append || !io->read) return LEAFTL_FLASH_INVALID;
    f->directory=d;f->page=page;f->logical_count=logical_count;
    f->page_bytes=page_bytes;f->busy=0;f->io=*io;
    f->snapshot=0;f->snapshot_context=0;return LEAFTL_FLASH_OK;
}
static int Ready(const LEAFTL_FLASH *f,const LEAFTL_MAPPING *m,unsigned int id)
{
    return f && m && f->directory && f->page && f->io.append && f->io.read &&
        f->page_bytes==LEAFTL_FLASH_PAGE_BYTES && f->logical_count==m->logical_count &&
        id<LEAFTL_GROUP_COUNT(f->logical_count);
}
static int LogicalBounds(LEAFTL_INDEX_WORKSPACE *w,unsigned int id,unsigned int logical_count)
{
    unsigned int l,n,remaining=logical_count-id*LEAFTL_GROUP_SIZE;
    for(l=w->first;l!=NONE;l=w->levels[l].next)
        for(n=w->levels[l].first;n!=NONE;n=w->nodes[n].next)
            if(LEAFTL_SEGMENT_LAST(&w->nodes[n].model)>=remaining) return 0;
    return 1;
}
static LEAFTL_FLASH_STATUS IndexStatus(LEAFTL_INDEX_STATUS s)
{
    if(s==LEAFTL_INDEX_OK) return LEAFTL_FLASH_OK;
    if(s==LEAFTL_INDEX_MISS) return LEAFTL_FLASH_MISS;
    if(s==LEAFTL_INDEX_NO_SPACE) return LEAFTL_FLASH_NO_SPACE;
    return LEAFTL_FLASH_INVALID;
}
static LEAFTL_FLASH_STATUS CommitPage(LEAFTL_FLASH *f,unsigned int used,unsigned int *stored)
{
    unsigned int at,vsa=UINT_MAX;
    Put32(f->page+f->page_bytes-4U,Crc(f->page,f->page_bytes-4U));
    if(!f->io.append(f->io.context,f->page,f->page_bytes,&vsa) || vsa==UINT_MAX)
        return LEAFTL_FLASH_IO_ERROR;
    /* Validate allocator contract before publishing ANY directory on page. */
    for(at=0;at<used;at+=RecordSize(f->page+at))
        if(f->directory[Get32(f->page+at)].vsa==vsa) return LEAFTL_FLASH_IO_ERROR;
    for(at=0;at<used;) {
        unsigned int id=Get32(f->page+at),length=RecordSize(f->page+at);
        LEAFTL_FLASH_DIRECTORY_ENTRY entry;
        entry.vsa=vsa;entry.offset=(uint16_t)at;entry.length=(uint16_t)length;
        f->directory[id]=entry;at+=length;(*stored)++;
    }
    return LEAFTL_FLASH_OK;
}
LEAFTL_FLASH_STATUS LeaFtlFlashStoreMany(LEAFTL_FLASH *f,LEAFTL_MAPPING *m,
    const unsigned int *ids,unsigned int count,unsigned int *stored)
{
    unsigned int i,used=0,length;
    LEAFTL_FLASH_STATUS status=LEAFTL_FLASH_OK;
    if(!stored || !ids || !count || count>LEAFTL_MAX_CACHED_GROUPS) return LEAFTL_FLASH_INVALID;
    for(i=0;i<count;i++) if(!Ready(f,m,ids[i]) || (i && ids[i]<=ids[i-1U])) return LEAFTL_FLASH_INVALID;
    if(f->busy || m->pending_token) return LEAFTL_FLASH_BUSY;
    f->busy=1;*stored=0;memset(f->page,0xff,f->page_bytes);
    for(i=0;i<count;i++) {
        status=f->snapshot ? f->snapshot(f->snapshot_context,m,ids[i],m->workspace) :
            IndexStatus(LeaFtlIndexExportGroup(m->index,ids[i],m->workspace));
        if(status!=LEAFTL_FLASH_OK) goto done;
        if(!LogicalBounds(m->workspace,ids[i],m->logical_count)) {status=LEAFTL_FLASH_INVALID;goto done;}
        status=LeaFtlFlashEncode(m->workspace,ids[i],f->page+used,f->page_bytes-4U-used,&length);
        if(status==LEAFTL_FLASH_NO_SPACE && used) {
            status=CommitPage(f,used,stored);if(status!=LEAFTL_FLASH_OK) goto done;
            used=0;memset(f->page,0xff,f->page_bytes);
            status=LeaFtlFlashEncode(m->workspace,ids[i],f->page,f->page_bytes-4U,&length);
        }
        if(status!=LEAFTL_FLASH_OK) goto done;
        used+=length;
    }
    if(used) status=CommitPage(f,used,stored);
done:
    f->busy=0;return status;
}
LEAFTL_FLASH_STATUS LeaFtlFlashStore(LEAFTL_FLASH *f,LEAFTL_MAPPING *m,unsigned int id)
{ unsigned int stored;return LeaFtlFlashStoreMany(f,m,&id,1,&stored); }
LEAFTL_FLASH_STATUS LeaFtlFlashLoad(LEAFTL_FLASH *f,LEAFTL_MAPPING *m,unsigned int id)
{
    LEAFTL_FLASH_STATUS status;LEAFTL_FLASH_DIRECTORY_ENTRY entry;
    LEAFTL_INDEX_GROUP_INFO info;
    if(!Ready(f,m,id)) return LEAFTL_FLASH_INVALID;
    if(f->busy || m->pending_token) return LEAFTL_FLASH_BUSY;
    entry=f->directory[id];if(entry.vsa==UINT_MAX) return LEAFTL_FLASH_MISS;
    /* A resident group may have newer dirty writes. Never roll it back by
     * silently loading an older flash snapshot; cache eviction is step 12. */
    {
        LEAFTL_INDEX_STATUS s=LeaFtlIndexGetGroupInfo(m->index,id,&info);
        if(s==LEAFTL_INDEX_OK) return LEAFTL_FLASH_BUSY;
        if(s!=LEAFTL_INDEX_MISS) return LEAFTL_FLASH_INVALID;
    }
    if(entry.length<LEAFTL_FLASH_HEADER_BYTES ||
       (unsigned int)entry.offset+entry.length>f->page_bytes-4U)
        return LEAFTL_FLASH_INVALID;
    f->busy=1;
    if(!f->io.read(f->io.context,entry.vsa,f->page,f->page_bytes)) {status=LEAFTL_FLASH_IO_ERROR;goto done;}
    if(Get32(f->page+f->page_bytes-4U)!=Crc(f->page,f->page_bytes-4U)) {status=LEAFTL_FLASH_INVALID;goto done;}
    status=LeaFtlFlashDecode(f->page+entry.offset,entry.length,id,m->workspace);
    if(status!=LEAFTL_FLASH_OK) goto done;
    if(!LogicalBounds(m->workspace,id,m->logical_count)) {status=LEAFTL_FLASH_INVALID;goto done;}
    status=IndexStatus(LeaFtlIndexRestoreGroup(m->index,id,m->workspace));
done:
    f->busy=0;return status;
}

int LeaFtlFlashPageValid(const unsigned char *page,unsigned int bytes)
{ return page && bytes==LEAFTL_FLASH_PAGE_BYTES && Get32(page+bytes-4U)==Crc(page,bytes-4U); }
LEAFTL_FLASH_STATUS LeaFtlFlashReadSnapshot(LEAFTL_FLASH *f,unsigned int id,
    unsigned char *page,LEAFTL_INDEX_WORKSPACE *w)
{
    LEAFTL_FLASH_DIRECTORY_ENTRY entry;
    if(!f || !f->directory || !f->io.read || !page || !w || id>=LEAFTL_GROUP_COUNT(f->logical_count))
        return LEAFTL_FLASH_INVALID;
    entry=f->directory[id];if(entry.vsa==UINT_MAX) return LEAFTL_FLASH_MISS;
    if(entry.length<LEAFTL_FLASH_HEADER_BYTES || (unsigned int)entry.offset+entry.length>f->page_bytes-4U)
        return LEAFTL_FLASH_INVALID;
    if(!f->io.read(f->io.context,entry.vsa,page,f->page_bytes)) return LEAFTL_FLASH_IO_ERROR;
    if(!LeaFtlFlashPageValid(page,f->page_bytes)) return LEAFTL_FLASH_INVALID;
    return LeaFtlFlashDecode(page+entry.offset,entry.length,id,w);
}
