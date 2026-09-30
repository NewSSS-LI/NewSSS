//////////////////////////////////////////////////////////////////////////////////
// dftl_cmt.c for Cosmos+ OpenSSD
//
// Segmented-LRU Cached Mapping Table. See dftl_cmt.h for design notes.
//
// Segmented LRU: entries start in the probationary segment; a hit promotes
// an entry to the protected segment (MRU end). If the protected segment
// grows past half of DFTL_CMT_CAPACITY, its LRU entry is demoted back to
// the probationary segment's MRU end. Eviction always takes the
// probationary segment's LRU entry (demoting from protected first if
// probationary is empty). This keeps frequently re-referenced mappings
// ("hot" LPNs) resistant to eviction by a burst of one-time accesses,
// exactly the property DFTL relies on to keep translation overhead low
// under temporally-local workloads (paper Section 3.2 / 5.2).
//////////////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include "dftl_cmt.h"
#include "memory_map.h"

P_DFTL_CMT_NODE_ARRAY dftlCmtNodePtr;
P_DFTL_CMT_HASH_TABLE dftlCmtHashPtr;

#define DFTL_CMT_PROTECTED_CAPACITY	(DFTL_CMT_CAPACITY / 2)

static unsigned int freeListHead;			// singly-linked via lruNext

static unsigned int probHead, probTail, probCount;
static unsigned int protHead, protTail, protCount;

static unsigned int Hash(unsigned int lpn)
{
	return lpn % DFTL_CMT_CAPACITY;
}

static void ListRemove(unsigned int *head, unsigned int *tail, unsigned int idx)
{
	unsigned int prev = dftlCmtNodePtr->node[idx].lruPrev;
	unsigned int next = dftlCmtNodePtr->node[idx].lruNext;

	if(prev != DFTL_CMT_NONE)
		dftlCmtNodePtr->node[prev].lruNext = next;
	else
		*head = next;

	if(next != DFTL_CMT_NONE)
		dftlCmtNodePtr->node[next].lruPrev = prev;
	else
		*tail = prev;

	dftlCmtNodePtr->node[idx].lruPrev = DFTL_CMT_NONE;
	dftlCmtNodePtr->node[idx].lruNext = DFTL_CMT_NONE;
}

static void ListPushMru(unsigned int *head, unsigned int *tail, unsigned int idx)
{
	dftlCmtNodePtr->node[idx].lruPrev = DFTL_CMT_NONE;
	dftlCmtNodePtr->node[idx].lruNext = *head;

	if(*head != DFTL_CMT_NONE)
		dftlCmtNodePtr->node[*head].lruPrev = idx;
	else
		*tail = idx;

	*head = idx;
}

static unsigned int HashFind(unsigned int lpn)
{
	unsigned int idx = dftlCmtHashPtr->headNode[Hash(lpn)];

	while(idx != DFTL_CMT_NONE)
	{
		if(dftlCmtNodePtr->node[idx].valid && dftlCmtNodePtr->node[idx].lpn == lpn)
			return idx;
		idx = dftlCmtNodePtr->node[idx].hashNext;
	}
	return DFTL_CMT_NONE;
}

static void HashInsert(unsigned int idx)
{
	unsigned int bucket = Hash(dftlCmtNodePtr->node[idx].lpn);

	dftlCmtNodePtr->node[idx].hashNext = dftlCmtHashPtr->headNode[bucket];
	dftlCmtHashPtr->headNode[bucket] = idx;
}

static void HashRemove(unsigned int idx)
{
	unsigned int bucket = Hash(dftlCmtNodePtr->node[idx].lpn);
	unsigned int cur = dftlCmtHashPtr->headNode[bucket];
	unsigned int prev = DFTL_CMT_NONE;

	while(cur != DFTL_CMT_NONE)
	{
		if(cur == idx)
		{
			if(prev == DFTL_CMT_NONE)
				dftlCmtHashPtr->headNode[bucket] = dftlCmtNodePtr->node[cur].hashNext;
			else
				dftlCmtNodePtr->node[prev].hashNext = dftlCmtNodePtr->node[cur].hashNext;
			return;
		}
		prev = cur;
		cur = dftlCmtNodePtr->node[cur].hashNext;
	}
}

void DftlCmtInit(void)
{
	unsigned int i;

	dftlCmtNodePtr = (P_DFTL_CMT_NODE_ARRAY) DFTL_CMT_NODE_ARRAY_ADDR;
	dftlCmtHashPtr = (P_DFTL_CMT_HASH_TABLE) DFTL_CMT_HASH_TABLE_ADDR;

	for(i = 0; i < DFTL_CMT_CAPACITY; i++)
		dftlCmtHashPtr->headNode[i] = DFTL_CMT_NONE;

	for(i = 0; i < DFTL_CMT_CAPACITY; i++)
	{
		dftlCmtNodePtr->node[i].valid = 0;
		dftlCmtNodePtr->node[i].dirty = 0;
		dftlCmtNodePtr->node[i].protectedSeg = 0;
		dftlCmtNodePtr->node[i].hashNext = DFTL_CMT_NONE;
		dftlCmtNodePtr->node[i].lruPrev = DFTL_CMT_NONE;
		// free list threaded through lruNext
		dftlCmtNodePtr->node[i].lruNext = (i + 1 < DFTL_CMT_CAPACITY) ? (i + 1) : DFTL_CMT_NONE;
	}
	freeListHead = 0;

	probHead = probTail = DFTL_CMT_NONE;
	protHead = protTail = DFTL_CMT_NONE;
	probCount = 0;
	protCount = 0;
}

unsigned char DftlCmtIsFull(void)
{
	return (freeListHead == DFTL_CMT_NONE);
}

unsigned char DftlCmtGet(unsigned int lpn, unsigned int *vsa)
{
	unsigned int idx = HashFind(lpn);

	if(idx == DFTL_CMT_NONE)
		return 0;

	*vsa = dftlCmtNodePtr->node[idx].vsa;

	if(dftlCmtNodePtr->node[idx].protectedSeg)
	{
		// already protected: just move to MRU of protected segment
		ListRemove(&protHead, &protTail, idx);
		ListPushMru(&protHead, &protTail, idx);
	}
	else
	{
		// promote probationary -> protected
		ListRemove(&probHead, &probTail, idx);
		probCount--;

		dftlCmtNodePtr->node[idx].protectedSeg = 1;
		ListPushMru(&protHead, &protTail, idx);
		protCount++;

		// keep protected segment bounded; demote its LRU back to probationary
		if(protCount > DFTL_CMT_PROTECTED_CAPACITY)
		{
			unsigned int demoted = protTail;
			ListRemove(&protHead, &protTail, demoted);
			protCount--;

			dftlCmtNodePtr->node[demoted].protectedSeg = 0;
			ListPushMru(&probHead, &probTail, demoted);
			probCount++;
		}
	}

	return 1;
}

void DftlCmtAdd(unsigned int lpn, unsigned int vsa, unsigned char dirty)
{
	unsigned int idx;

	if(freeListHead == DFTL_CMT_NONE)
		assert(!"[WARNING] DFTL CMT add called while full [WARNING]");

	idx = freeListHead;
	freeListHead = dftlCmtNodePtr->node[idx].lruNext;

	dftlCmtNodePtr->node[idx].lpn = lpn;
	dftlCmtNodePtr->node[idx].vsa = vsa;
	dftlCmtNodePtr->node[idx].valid = 1;
	dftlCmtNodePtr->node[idx].dirty = dirty;
	dftlCmtNodePtr->node[idx].protectedSeg = 0;
	dftlCmtNodePtr->node[idx].lruPrev = DFTL_CMT_NONE;
	dftlCmtNodePtr->node[idx].lruNext = DFTL_CMT_NONE;

	HashInsert(idx);
	ListPushMru(&probHead, &probTail, idx);
	probCount++;
}

void DftlCmtUpdate(unsigned int lpn, unsigned int vsa)
{
	unsigned int idx = HashFind(lpn);

	if(idx == DFTL_CMT_NONE)
		return;

	dftlCmtNodePtr->node[idx].vsa = vsa;
	dftlCmtNodePtr->node[idx].dirty = 1;

	if(dftlCmtNodePtr->node[idx].protectedSeg)
	{
		ListRemove(&protHead, &protTail, idx);
		ListPushMru(&protHead, &protTail, idx);
	}
	else
	{
		ListRemove(&probHead, &probTail, idx);
		ListPushMru(&probHead, &probTail, idx);
	}
}

unsigned char DftlCmtEvict(unsigned int *lpn, unsigned int *vsa, unsigned char *dirty)
{
	unsigned int victim;

	if(probHead == DFTL_CMT_NONE)
	{
		// nothing in probationary; pull protected's LRU in first (if any)
		if(protTail == DFTL_CMT_NONE)
			return 0;	// CMT entirely empty

		victim = protTail;
		ListRemove(&protHead, &protTail, victim);
		protCount--;

		dftlCmtNodePtr->node[victim].protectedSeg = 0;
		ListPushMru(&probHead, &probTail, victim);
		probCount++;
	}

	victim = probTail;
	ListRemove(&probHead, &probTail, victim);
	probCount--;

	*lpn = dftlCmtNodePtr->node[victim].lpn;
	*vsa = dftlCmtNodePtr->node[victim].vsa;
	*dirty = dftlCmtNodePtr->node[victim].dirty;

	HashRemove(victim);

	dftlCmtNodePtr->node[victim].valid = 0;
	dftlCmtNodePtr->node[victim].lruNext = freeListHead;
	freeListHead = victim;

	return 1;
}
