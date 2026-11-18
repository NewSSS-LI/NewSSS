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

// Per-die translation blocks. They come from the die's normal free block list and
// go back to it after translation GC; they are never on the data GC victim list.
typedef struct _DFTL_TRANS_DIE_ENTRY {
	unsigned int currentBlock;	// page cursor for this block is virtualBlockMapPtr->block[dieNo][currentBlock].currentPage (reused, same as data blocks)
	unsigned int blockCnt;
	unsigned int blockList[DFTL_TRANS_MAX_BLOCKS_PER_DIE];	// translation blocks of this die, current one included
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

// Translation GC of dieNo: the live translation pages of all its translation
// blocks move into one new block, the old blocks go back to the free list.
void DftlGcTranslationBlock(unsigned int dieNo);

// Number of translation blocks dieNo holds (for the GC trigger).
unsigned int DftlTransBlockCount(unsigned int dieNo);

// GC batch update: new VSAs of uncached moved pages of one translation page.
void DftlGtdUpdateMappings(unsigned int mvpn, const unsigned int *offset, const unsigned int *vsa, unsigned int count);

extern P_DFTL_GTD dftlGtdPtr;
extern P_DFTL_TRANS_DIE_MAP dftlTransDieMapPtr;

#endif /* DFTL_GTD_H_ */
