//////////////////////////////////////////////////////////////////////////////////
// lftl_cmt.h for Cosmos+ OpenSSD
//
// TPFTL-style two-level LRU mapping cache: an LRU list of translation page nodes,
// each holding an LRU list of its cached entries. Pure data structure.
// All links are array indices, as in the rest of this firmware.
//////////////////////////////////////////////////////////////////////////////////

#ifndef LFTL_CMT_H_
#define LFTL_CMT_H_

#include "lftl_config.h"

#define LFTL_CMT_NONE	0xffffffff

typedef struct _LFTL_CMT_ENTRY {
	unsigned int lsa;
	unsigned int vsa;
	unsigned int hashNext;
	unsigned int prev;		// within its TP node; free list uses next only
	unsigned int next;
	unsigned int dirty;
} LFTL_CMT_ENTRY;

typedef struct _LFTL_CMT_TP_NODE {
	unsigned int head;		// most recently used entry
	unsigned int tail;
	unsigned int prev;		// TP LRU list
	unsigned int next;
	unsigned int cnt;
	unsigned int dirtyCnt;
} LFTL_CMT_TP_NODE;

typedef struct _LFTL_CMT {
	LFTL_CMT_ENTRY entry[LFTL_CMT_CAPACITY];
	unsigned int hashHead[LFTL_CMT_CAPACITY];
	LFTL_CMT_TP_NODE tp[LFTL_MAX_GROUPS];
	unsigned int freeHead;
	unsigned int used;
	unsigned int lruHead;	// hottest TP
	unsigned int lruTail;	// coldest TP
	int prefetchCounter;	// TPFTL's selective prefetch on/off counter
	unsigned int selectivePrefetchOn;
} LFTL_CMT, *P_LFTL_CMT;

void LftlCmtInit(P_LFTL_CMT cmt);

// Lookup without touching LRU state. Returns entry index or LFTL_CMT_NONE.
unsigned int LftlCmtFind(P_LFTL_CMT cmt, unsigned int lsa);

// Move a found entry and its TP node to the most recently used position.
void LftlCmtTouch(P_LFTL_CMT cmt, unsigned int idx);

// Caller guarantees room. Prefetched entries go to the cold end of their TP's list.
void LftlCmtInsert(P_LFTL_CMT cmt, unsigned int lsa, unsigned int vsa, unsigned int dirty, unsigned int prefetched);

void LftlCmtSetDirty(P_LFTL_CMT cmt, unsigned int idx, unsigned int vsa);
void LftlCmtSetClean(P_LFTL_CMT cmt, unsigned int idx);

// Coldest entry of the coldest TP, preferring clean entries (TPFTL clean-first).
unsigned int LftlCmtPickVictim(P_LFTL_CMT cmt);

void LftlCmtRemove(P_LFTL_CMT cmt, unsigned int idx);

#endif /* LFTL_CMT_H_ */
