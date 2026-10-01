//////////////////////////////////////////////////////////////////////////////////
// dftl_gtd.h for Cosmos+ OpenSSD
//
// GTD (Global Translation Directory) + translation page/block management.
// See DFTL paper Section 3.1-3.3. The GTD is always DRAM-resident (it is
// tiny: DFTL_NUM_TRANS_PAGES entries, ~8KB for this board) and tracks which
// physical slice (VSA) currently holds each translation page (MVPN). The
// full LPN->VSA mapping table itself lives on flash, packed
// DFTL_MAPPINGS_PER_TRANS_PAGE-at-a-time into translation pages; only the
// working set is cached in DRAM by dftl_cmt.c.
//////////////////////////////////////////////////////////////////////////////////

#ifndef DFTL_GTD_H_
#define DFTL_GTD_H_

#include "dftl_config.h"

#define DFTL_GTD_NONE	0xffffffff

typedef struct _DFTL_GTD_ENTRY {
	unsigned int mppn;		// VSA of the translation page holding this MVPN's mappings, or DFTL_GTD_NONE
} DFTL_GTD_ENTRY, *P_DFTL_GTD_ENTRY;

typedef struct _DFTL_GTD {
	DFTL_GTD_ENTRY entry[DFTL_NUM_TRANS_PAGES];
} DFTL_GTD, *P_DFTL_GTD;

// per-die translation block free list + current block (mirrors
// VIRTUAL_DIE_MAP's role but tracked separately so translation blocks never
// mix with GreedyFTL's data-block free list / GC victim list).
typedef struct _DFTL_TRANS_DIE_ENTRY {
	unsigned int currentBlock;	// page cursor for this block is virtualBlockMapPtr->block[dieNo][currentBlock].currentPage (reused, same as data blocks)
	unsigned int headFreeBlock;
	unsigned int tailFreeBlock;
	unsigned int freeBlockCnt;
	unsigned int blockList[DFTL_TRANS_BLOCKS_PER_DIE];	// the fixed set of block numbers reserved for translation use on this die
} DFTL_TRANS_DIE_ENTRY, *P_DFTL_TRANS_DIE_ENTRY;

typedef struct _DFTL_TRANS_DIE_MAP {
	DFTL_TRANS_DIE_ENTRY die[USER_DIES];
} DFTL_TRANS_DIE_MAP, *P_DFTL_TRANS_DIE_MAP;

void DftlGtdInit(void);

// LPN -> MVPN/offset helpers
#define DftlLpn2Mvpn(lpn)		((lpn) / (DFTL_MAPPINGS_PER_TRANS_PAGE))
#define DftlLpn2Offset(lpn)		((lpn) % (DFTL_MAPPINGS_PER_TRANS_PAGE))

// Algorithm 1's "fetch mapping entry": look up lpn's translation page via
// GTD, read it from flash, and return the VSA stored for lpn (VSA_NONE if
// this lpn has never been written / its translation page does not exist yet).
unsigned int DftlGtdLookupMapping(unsigned int lpn);

// Algorithm 1's "evict mapping entry" write-back: read-modify-write lpn's
// translation page with a new vsa value, updating the GTD to point at the
// new physical location of that translation page.
void DftlGtdWritebackMapping(unsigned int lpn, unsigned int vsa);

// Reclaim translation-block space for dieNo when its free list is empty.
// Simple full-scan variant (translation blocks are few and turn over far
// less often than data blocks, per the paper's own findings) rather than
// GreedyFTL's segregated invalid-count victim list.
void DftlGcTranslationBlock(unsigned int dieNo);

extern P_DFTL_GTD dftlGtdPtr;
extern P_DFTL_TRANS_DIE_MAP dftlTransDieMapPtr;

#endif /* DFTL_GTD_H_ */
