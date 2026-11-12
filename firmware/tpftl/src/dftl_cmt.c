//////////////////////////////////////////////////////////////////////////////////
// dftl_cmt.c for Cosmos+ OpenSSD -- TPFTL variant
//
// See dftl_cmt.h. Same data structure and replacement policy as run-lftl's
// lftl_cmt.c (TPFTL two-level LRU), with a TP node per DFTL translation page.
//////////////////////////////////////////////////////////////////////////////////

#include "dftl_cmt.h"
#include "dftl_gtd.h"
#include "memory_map.h"

P_DFTL_CMT_NODE_ARRAY dftlCmtNodePtr;
P_DFTL_CMT_HASH_TABLE dftlCmtHashPtr;

#define N(i)			(dftlCmtNodePtr->node[i])
#define C				(*dftlCmtHashPtr)
#define HashOf(lpn)		(((lpn) ^ ((lpn) >> 13)) & (DFTL_CMT_CAPACITY - 1))
#define TpOf(lpn)		DftlLpn2Mvpn(lpn)

static void TpLruUnlink(unsigned int tp)
{
	DFTL_CMT_TP_NODE *node = &C.tp[tp];

	if(node->prev != DFTL_CMT_NONE)
		C.tp[node->prev].next = node->next;
	else
		C.lruHead = node->next;

	if(node->next != DFTL_CMT_NONE)
		C.tp[node->next].prev = node->prev;
	else
		C.lruTail = node->prev;
}

static void TpLruPushHead(unsigned int tp)
{
	DFTL_CMT_TP_NODE *node = &C.tp[tp];

	node->prev = DFTL_CMT_NONE;
	node->next = C.lruHead;
	if(C.lruHead != DFTL_CMT_NONE)
		C.tp[C.lruHead].prev = tp;
	else
		C.lruTail = tp;
	C.lruHead = tp;
}

static void EntryUnlink(unsigned int tp, unsigned int idx)
{
	if(N(idx).lruPrev != DFTL_CMT_NONE)
		N(N(idx).lruPrev).lruNext = N(idx).lruNext;
	else
		C.tp[tp].head = N(idx).lruNext;

	if(N(idx).lruNext != DFTL_CMT_NONE)
		N(N(idx).lruNext).lruPrev = N(idx).lruPrev;
	else
		C.tp[tp].tail = N(idx).lruPrev;
}

static void EntryPushHead(unsigned int tp, unsigned int idx)
{
	DFTL_CMT_TP_NODE *node = &C.tp[tp];

	N(idx).lruPrev = DFTL_CMT_NONE;
	N(idx).lruNext = node->head;
	if(node->head != DFTL_CMT_NONE)
		N(node->head).lruPrev = idx;
	else
		node->tail = idx;
	node->head = idx;
}

static void EntryPushTail(unsigned int tp, unsigned int idx)
{
	DFTL_CMT_TP_NODE *node = &C.tp[tp];

	N(idx).lruNext = DFTL_CMT_NONE;
	N(idx).lruPrev = node->tail;
	if(node->tail != DFTL_CMT_NONE)
		N(node->tail).lruNext = idx;
	else
		node->head = idx;
	node->tail = idx;
}

static unsigned int Find(unsigned int lpn)
{
	unsigned int idx = C.headNode[HashOf(lpn)];

	while((idx != DFTL_CMT_NONE) && (N(idx).lpn != lpn))
		idx = N(idx).hashNext;

	return idx;
}

static void Touch(unsigned int idx)
{
	unsigned int tp = TpOf(N(idx).lpn);

	if(C.tp[tp].head != idx)
	{
		EntryUnlink(tp, idx);
		EntryPushHead(tp, idx);
	}
	if(C.lruHead != tp)
	{
		TpLruUnlink(tp);
		TpLruPushHead(tp);
	}
}

static void Insert(unsigned int lpn, unsigned int vsa, unsigned int dirty, unsigned int prefetched)
{
	unsigned int idx = C.freeHead;
	unsigned int tp = TpOf(lpn);
	unsigned int bucket = HashOf(lpn);

	C.freeHead = N(idx).lruNext;
	C.used++;

	N(idx).lpn = lpn;
	N(idx).vsa = vsa;
	N(idx).dirty = dirty;
	N(idx).hashNext = C.headNode[bucket];
	C.headNode[bucket] = idx;

	if(C.tp[tp].cnt == 0)
	{
		TpLruPushHead(tp);
		if(++C.prefetchCounter == 3)
		{
			C.prefetchCounter = 0;
			C.selectivePrefetchOn = 0;
		}
	}
	else if(!prefetched && (C.lruHead != tp))
	{
		TpLruUnlink(tp);
		TpLruPushHead(tp);
	}

	C.tp[tp].cnt++;
	if(dirty)
		C.tp[tp].dirtyCnt++;

	if(prefetched)
		EntryPushTail(tp, idx);
	else
		EntryPushHead(tp, idx);
}

static void Remove(unsigned int idx)
{
	unsigned int tp = TpOf(N(idx).lpn);
	unsigned int *link = &C.headNode[HashOf(N(idx).lpn)];

	while(*link != idx)
		link = &N(*link).hashNext;
	*link = N(idx).hashNext;

	EntryUnlink(tp, idx);
	C.tp[tp].cnt--;
	if(N(idx).dirty)
		C.tp[tp].dirtyCnt--;

	if(C.tp[tp].cnt == 0)
	{
		TpLruUnlink(tp);
		if(--C.prefetchCounter == -3)
		{
			C.prefetchCounter = 0;
			C.selectivePrefetchOn = 1;
		}
	}

	N(idx).lruNext = C.freeHead;
	C.freeHead = idx;
	C.used--;
}

void DftlCmtInit(void)
{
	unsigned int i;

	dftlCmtNodePtr = (P_DFTL_CMT_NODE_ARRAY) DFTL_CMT_NODE_ARRAY_ADDR;
	dftlCmtHashPtr = (P_DFTL_CMT_HASH_TABLE) DFTL_CMT_HASH_TABLE_ADDR;

	for(i = 0; i < DFTL_CMT_CAPACITY; i++)
	{
		N(i).lruNext = (i + 1 < DFTL_CMT_CAPACITY) ? (i + 1) : DFTL_CMT_NONE;
		C.headNode[i] = DFTL_CMT_NONE;
	}
	for(i = 0; i < DFTL_NUM_TRANS_PAGES; i++)
	{
		C.tp[i].head = DFTL_CMT_NONE;
		C.tp[i].tail = DFTL_CMT_NONE;
		C.tp[i].cnt = 0;
		C.tp[i].dirtyCnt = 0;
	}

	C.freeHead = 0;
	C.used = 0;
	C.lruHead = DFTL_CMT_NONE;
	C.lruTail = DFTL_CMT_NONE;
	C.prefetchCounter = 0;
	C.selectivePrefetchOn = 1;
}

unsigned char DftlCmtGet(unsigned int lpn, unsigned int *vsa)
{
	unsigned int idx = Find(lpn);

	if(idx == DFTL_CMT_NONE)
		return 0;

	Touch(idx);
	*vsa = N(idx).vsa;
	return 1;
}

unsigned char DftlCmtContains(unsigned int lpn)
{
	return Find(lpn) != DFTL_CMT_NONE;
}

void DftlCmtAdd(unsigned int lpn, unsigned int vsa, unsigned char dirty)
{
	Insert(lpn, vsa, dirty ? 1 : 0, 0);
}

void DftlCmtAddPrefetched(unsigned int lpn, unsigned int vsa)
{
	Insert(lpn, vsa, 0, 1);
}

void DftlCmtUpdate(unsigned int lpn, unsigned int vsa)
{
	unsigned int idx = Find(lpn);

	if(idx == DFTL_CMT_NONE)
		return;

	if(!N(idx).dirty)
	{
		N(idx).dirty = 1;
		C.tp[TpOf(lpn)].dirtyCnt++;
	}
	N(idx).vsa = vsa;
}

unsigned char DftlCmtEvict(unsigned int *lpn, unsigned int *vsa, unsigned char *dirty)
{
	DFTL_CMT_TP_NODE *node;
	unsigned int idx;

	if(C.lruTail == DFTL_CMT_NONE)
		return 0;

	node = &C.tp[C.lruTail];
	if(node->dirtyCnt == node->cnt)
		idx = node->tail;
	else
		for(idx = node->tail; N(idx).dirty; idx = N(idx).lruPrev)
			;

	*lpn = N(idx).lpn;
	*vsa = N(idx).vsa;
	*dirty = (unsigned char) N(idx).dirty;
	Remove(idx);
	return 1;
}

unsigned char DftlCmtIsFull(void)
{
	return C.used == DFTL_CMT_CAPACITY;
}

unsigned int DftlCmtSelectivePrefetchOn(void)
{
	return C.selectivePrefetchOn;
}

unsigned int DftlCmtDirtyCount(unsigned int mvpn)
{
	return C.tp[mvpn].dirtyCnt;
}

void DftlCmtTakeDirtyOfTp(unsigned int mvpn, unsigned int *buf)
{
	unsigned int idx;

	if(C.tp[mvpn].dirtyCnt == 0)
		return;

	for(idx = C.tp[mvpn].head; idx != DFTL_CMT_NONE; idx = N(idx).lruNext)
		if(N(idx).dirty)
		{
			buf[1 + DftlLpn2Offset(N(idx).lpn)] = N(idx).vsa;
			N(idx).dirty = 0;
		}
	C.tp[mvpn].dirtyCnt = 0;
}

// GC moved a page: if its mapping is cached, point the entry at the new VSA and
// mark it dirty without changing its LRU position (GC is no access by the host).
// Returns 1 if cached, 0 otherwise.
unsigned char DftlCmtGcUpdate(unsigned int lpn, unsigned int vsa)
{
	unsigned int idx = Find(lpn);

	if(idx == DFTL_CMT_NONE)
		return 0;

	if(!N(idx).dirty)
	{
		N(idx).dirty = 1;
		C.tp[TpOf(lpn)].dirtyCnt++;
	}
	N(idx).vsa = vsa;
	return 1;
}
