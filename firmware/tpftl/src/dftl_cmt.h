//////////////////////////////////////////////////////////////////////////////////
// dftl_cmt.h for Cosmos+ OpenSSD -- TPFTL variant
//
// TPFTL mapping cache (Zhou et al., "An Efficient Page-level FTL to Optimize
// Address Translation in Flash Memory", EuroSys'15), the TPFTL that the
// LearnedFTL paper (HPCA'24) compares against:
//   - two-level LRU: an LRU list of translation page (TP) nodes, each holding an
//     LRU list of its cached entries;
//   - clean-first replacement: the victim is the coldest clean entry of the
//     coldest TP node, a dirty one only if that node has no clean entry;
//   - batch update: a dirty victim's write-back takes every dirty entry of its
//     TP node with it (dftl_gtd.c, DftlCmtTakeDirtyOfTp);
//   - request-level and selective prefetching (address_translation.c).
//
// The policy is the same as run-lftl's lftl_cmt.c, so this firmware differs from
// LearnedFTL with the model off only in data allocation and GC (page-level,
// greedy, as in the LearnedFTL paper's TPFTL on FEMU). The API keeps the DFTL
// names so the rest of run-dftl is unchanged.
//////////////////////////////////////////////////////////////////////////////////

#ifndef DFTL_CMT_H_
#define DFTL_CMT_H_

#include "dftl_config.h"

#define DFTL_CMT_NONE	0xffffffff

typedef struct _DFTL_CMT_NODE {
	unsigned int lpn;
	unsigned int vsa;
	unsigned int hashNext;
	unsigned int lruPrev;	// within its TP node
	unsigned int lruNext;	// within its TP node; the free list uses this link only
	unsigned int dirty;
} DFTL_CMT_NODE, *P_DFTL_CMT_NODE;

typedef struct _DFTL_CMT_TP_NODE {
	unsigned int head;		// most recently used entry
	unsigned int tail;
	unsigned int prev;		// TP LRU list
	unsigned int next;
	unsigned int cnt;
	unsigned int dirtyCnt;
} DFTL_CMT_TP_NODE;

typedef struct _DFTL_CMT_NODE_ARRAY {
	DFTL_CMT_NODE node[DFTL_CMT_CAPACITY];
} DFTL_CMT_NODE_ARRAY, *P_DFTL_CMT_NODE_ARRAY;

// Hash buckets plus the TP nodes and cache state (kept in this struct so that
// memory_map.h needs no change).
typedef struct _DFTL_CMT_HASH_TABLE {
	unsigned int headNode[DFTL_CMT_CAPACITY];
	DFTL_CMT_TP_NODE tp[DFTL_NUM_TRANS_PAGES];
	unsigned int freeHead;
	unsigned int used;
	unsigned int lruHead;	// hottest TP
	unsigned int lruTail;	// coldest TP
	int prefetchCounter;	// selective prefetch on/off counter
	unsigned int selectivePrefetchOn;
} DFTL_CMT_HASH_TABLE, *P_DFTL_CMT_HASH_TABLE;

void DftlCmtInit(void);

// Returns 1 (hit, *vsa filled, entry and its TP node moved to the MRU position) or 0.
unsigned char DftlCmtGet(unsigned int lpn, unsigned int *vsa);

// Lookup without touching LRU state (selective prefetch).
unsigned char DftlCmtContains(unsigned int lpn);

// Insert a new entry. Caller must ensure the CMT is not full. Prefetched entries
// go to the cold end of their TP node and do not move the node.
void DftlCmtAdd(unsigned int lpn, unsigned int vsa, unsigned char dirty);
void DftlCmtAddPrefetched(unsigned int lpn, unsigned int vsa);

// Update the VSA of an already-cached lpn and mark it dirty. No-op if not cached.
void DftlCmtUpdate(unsigned int lpn, unsigned int vsa);

// Remove the clean-first victim. Returns 1 and fills *lpn/*vsa/*dirty, or 0 if empty.
unsigned char DftlCmtEvict(unsigned int *lpn, unsigned int *vsa, unsigned char *dirty);

unsigned char DftlCmtIsFull(void);

unsigned int DftlCmtSelectivePrefetchOn(void);

// Batch update: number of dirty cached entries of translation page mvpn.
unsigned int DftlCmtDirtyCount(unsigned int mvpn);

// Batch update: put every dirty entry of translation page mvpn into buf
// (buf[1 + offset] = vsa) and mark them clean. Called only when that page is
// about to be programmed.
void DftlCmtTakeDirtyOfTp(unsigned int mvpn, unsigned int *buf);

extern P_DFTL_CMT_NODE_ARRAY dftlCmtNodePtr;
extern P_DFTL_CMT_HASH_TABLE dftlCmtHashPtr;

#endif /* DFTL_CMT_H_ */
