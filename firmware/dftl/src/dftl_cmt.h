//////////////////////////////////////////////////////////////////////////////////
// dftl_cmt.h for Cosmos+ OpenSSD
//
// CMT (Cached Mapping Table): a bounded, in-DRAM LPN->VSA cache with a
// segmented-LRU (probationary + protected) replacement policy, as described
// in DFTL paper Section 3.2. Pure data structure, no NAND I/O here -- the
// caller (dftl_gtd.c) is responsible for fetching/writing back translation
// pages on miss/eviction.
//
// Adapted (design only, reimplemented for this board's 32-bit LPN/VSA
// domain and DRAM layout) from the segmented-LRU cache in
// https://github.com/tatetian/DFTL (cmt.c), whose cache.c/cmt.c was the
// only fully-implemented, platform-independent piece of that repo.
//////////////////////////////////////////////////////////////////////////////////

#ifndef DFTL_CMT_H_
#define DFTL_CMT_H_

#include "dftl_config.h"

#define DFTL_CMT_NONE	0xffffffff

// one slot per possible cached entry; hash-chained (hashNext) for lookup,
// doubly-linked (lruPrev/lruNext) within whichever LRU segment it is in.
// All links are array indices into cmtNodePtr->node[], not pointers --
// matches this codebase's convention for DRAM-resident tables (e.g.
// VIRTUAL_BLOCK_MAP's prevBlock/nextBlock).
typedef struct _DFTL_CMT_NODE {
	unsigned int lpn;
	unsigned int vsa;
	unsigned int hashNext;
	unsigned int lruPrev;
	unsigned int lruNext;
	unsigned char valid;
	unsigned char dirty;
	unsigned char protectedSeg;	// 1 = protected segment, 0 = probationary
} DFTL_CMT_NODE, *P_DFTL_CMT_NODE;

typedef struct _DFTL_CMT_NODE_ARRAY {
	DFTL_CMT_NODE node[DFTL_CMT_CAPACITY];
} DFTL_CMT_NODE_ARRAY, *P_DFTL_CMT_NODE_ARRAY;

typedef struct _DFTL_CMT_HASH_TABLE {
	unsigned int headNode[DFTL_CMT_CAPACITY];	// bucket -> node index, or DFTL_CMT_NONE
} DFTL_CMT_HASH_TABLE, *P_DFTL_CMT_HASH_TABLE;

void DftlCmtInit(void);

// Returns 1 (hit, *vsa filled, entry promoted to protected segment) or 0 (miss).
unsigned char DftlCmtGet(unsigned int lpn, unsigned int *vsa);

// Insert a new (lpn, vsa) pair into the probationary segment. Caller must
// ensure the CMT is not full (evict first if DftlCmtIsFull()).
void DftlCmtAdd(unsigned int lpn, unsigned int vsa, unsigned char dirty);

// Update the VSA of an already-cached lpn and mark it dirty. No-op if lpn
// is not cached.
void DftlCmtUpdate(unsigned int lpn, unsigned int vsa);

// Evict the LRU entry of the probationary segment (demoting the protected
// segment's LRU entry into probationary first if probationary is empty).
// Returns 1 and fills *lpn/*vsa/*dirty, or 0 if the CMT is empty.
unsigned char DftlCmtEvict(unsigned int *lpn, unsigned int *vsa, unsigned char *dirty);

unsigned char DftlCmtIsFull(void);

// GC: update a cached mapping in place (dirty, LRU untouched). 1 if cached.
unsigned char DftlCmtGcUpdate(unsigned int lpn, unsigned int vsa);

// Batch update support (DFTL paper, Section 3.3): number of dirty cached entries
// whose LPN belongs to translation page mvpn.
unsigned int DftlCmtDirtyCount(unsigned int mvpn);

// If lpn is cached and dirty, mark it clean, fill *vsa and return 1, else 0.
// Called only after the entry's value has been put into a translation page
// that is about to be programmed.
unsigned char DftlCmtTakeDirty(unsigned int lpn, unsigned int *vsa);

extern P_DFTL_CMT_NODE_ARRAY dftlCmtNodePtr;
extern P_DFTL_CMT_HASH_TABLE dftlCmtHashPtr;

#endif /* DFTL_CMT_H_ */
