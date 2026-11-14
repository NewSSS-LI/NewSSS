#ifndef LEAFTL_FLASH_H_
#define LEAFTL_FLASH_H_
#include "leaftl_mapping.h"
#define LEAFTL_FLASH_HEADER_BYTES 12U
#define LEAFTL_FLASH_PAGE_CRC_BYTES 4U
#define LEAFTL_FLASH_PAGE_BYTES 16384U
#define LEAFTL_FLASH_MAX_BYTES (LEAFTL_FLASH_HEADER_BYTES+ \
    2U*LEAFTL_MAX_LEVELS_PER_GROUP+8U*LEAFTL_MAX_SEGMENTS_PER_GROUP+288U)
typedef struct { uint32_t vsa; uint16_t offset,length; } LEAFTL_FLASH_DIRECTORY_ENTRY;
typedef char LeaFtlDirectoryExactly8[(sizeof(LEAFTL_FLASH_DIRECTORY_ENTRY)==8U)?1:-1];
typedef char LeaFtlGroupFitsPage[(LEAFTL_FLASH_MAX_BYTES<=LEAFTL_FLASH_PAGE_BYTES-LEAFTL_FLASH_PAGE_CRC_BYTES)?1:-1];
typedef enum { LEAFTL_FLASH_OK=0,LEAFTL_FLASH_MISS=1,
    LEAFTL_FLASH_INVALID=-1,LEAFTL_FLASH_NO_SPACE=-2,LEAFTL_FLASH_IO_ERROR=-3,
    LEAFTL_FLASH_BUSY=-4 } LEAFTL_FLASH_STATUS;
/* Synchronous, exclusive metadata I/O. Append must allocate a NEW metadata
 * page and return success only after full program completion. VSA is the
 * interleaved NAND address, unlike the learned die-major data PPN.
 * Callbacks never modify/reenter mapping/directory and own failed allocations.
 * Metadata pool allocation/cache eviction/GC are connected in step 12. */
typedef struct {
    void *context;
    int (*append)(void *,const unsigned char *,unsigned int,unsigned int *vsa);
    int (*read)(void *,unsigned int vsa,unsigned char *,unsigned int);
} LEAFTL_FLASH_IO;
typedef struct {
    LEAFTL_FLASH_DIRECTORY_ENTRY *directory;
    unsigned char *page;
    unsigned int logical_count,page_bytes,busy;
    LEAFTL_FLASH_IO io;
    void *snapshot_context;
    LEAFTL_FLASH_STATUS (*snapshot)(void *,LEAFTL_MAPPING *,unsigned int,LEAFTL_INDEX_WORKSPACE *);
} LEAFTL_FLASH;
void LeaFtlFlashDirectoryInit(LEAFTL_FLASH_DIRECTORY_ENTRY *,unsigned int group_count);
/* LE group record: group ID u32, level/segment/CRB-offset/CRB-record
 * counts u16 each (12 bytes), followed by levels + 8-byte segments + CRB.
 * No per-group version, generation, CRC, gamma or PPN-mode copies. Page I/O
 * owns ONE shared CRC32 footer (4 bytes). Decode checks structure only.
 * Work/bytes/outputs disjoint; decoder may change work on rejection. */
LEAFTL_FLASH_STATUS LeaFtlFlashEncode(LEAFTL_INDEX_WORKSPACE *,unsigned int group_id,
    unsigned char *,unsigned int capacity,unsigned int *length);
LEAFTL_FLASH_STATUS LeaFtlFlashDecode(const unsigned char *,unsigned int length,
    unsigned int expected_group,LEAFTL_INDEX_WORKSPACE *);
/* Directory and staging buffer caller-owned; no heap, no power-loss recovery.
 * Init does NOT clear an existing directory. */
LEAFTL_FLASH_STATUS LeaFtlFlashInit(LEAFTL_FLASH *,LEAFTL_FLASH_DIRECTORY_ENTRY *,
    unsigned int logical_count,unsigned char *,unsigned int page_bytes,const LEAFTL_FLASH_IO *);
LEAFTL_FLASH_STATUS LeaFtlFlashStore(LEAFTL_FLASH *,LEAFTL_MAPPING *,unsigned int group_id);
/* Sorted unique IDs, <= resident group limit. Packs whole records into pages.
 * Commits each completed page; on failure *stored is the committed prefix.
 * No directory changes for the failing page. Shared caller-owned staging,
 * exclusively leased through return; IDs/output must not alias staging/work.
 * Single Store is a convenience wrapper and still programs one NAND page. */
LEAFTL_FLASH_STATUS LeaFtlFlashStoreMany(LEAFTL_FLASH *,LEAFTL_MAPPING *,
    const unsigned int *group_ids,unsigned int count,unsigned int *stored);
/* Load only an absent RAM group; resident (possibly dirty) groups return BUSY.
 * Decode/capacity errors leave the index unchanged. Directory is RAM-only
 * until checkpoint/recovery at step 18; this is NOT a durable NVMe FLUSH. */
LEAFTL_FLASH_STATUS LeaFtlFlashLoad(LEAFTL_FLASH *,LEAFTL_MAPPING *,unsigned int group_id);
/* Internal exclusive-owner primitives; permit snapshot provider read during
 * StoreMany using a DISJOINT page buffer. Never modify resident index. */
int LeaFtlFlashPageValid(const unsigned char *,unsigned int);
LEAFTL_FLASH_STATUS LeaFtlFlashReadSnapshot(LEAFTL_FLASH *,unsigned int,
    unsigned char *read_page,LEAFTL_INDEX_WORKSPACE *);
#endif
