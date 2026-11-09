//////////////////////////////////////////////////////////////////////////////////
// lftl_cmt.c for Cosmos+ OpenSSD
//////////////////////////////////////////////////////////////////////////////////

#include "lftl_cmt.h"

#define HashOf(lsa)		(((lsa) ^ ((lsa) >> 13)) & (LFTL_CMT_CAPACITY - 1))
#define TpOf(lsa)		LFTL_GROUP_OF(lsa)

static void TpLruUnlink(P_LFTL_CMT cmt, unsigned int tp)
{
	LFTL_CMT_TP_NODE *node = &cmt->tp[tp];

	if(node->prev != LFTL_CMT_NONE)
		cmt->tp[node->prev].next = node->next;
	else
		cmt->lruHead = node->next;

	if(node->next != LFTL_CMT_NONE)
		cmt->tp[node->next].prev = node->prev;
	else
		cmt->lruTail = node->prev;
}

static void TpLruPushHead(P_LFTL_CMT cmt, unsigned int tp)
{
	LFTL_CMT_TP_NODE *node = &cmt->tp[tp];

	node->prev = LFTL_CMT_NONE;
	node->next = cmt->lruHead;
	if(cmt->lruHead != LFTL_CMT_NONE)
		cmt->tp[cmt->lruHead].prev = tp;
	else
		cmt->lruTail = tp;
	cmt->lruHead = tp;
}

static void EntryUnlink(P_LFTL_CMT cmt, unsigned int tp, unsigned int idx)
{
	LFTL_CMT_ENTRY *entry = &cmt->entry[idx];

	if(entry->prev != LFTL_CMT_NONE)
		cmt->entry[entry->prev].next = entry->next;
	else
		cmt->tp[tp].head = entry->next;

	if(entry->next != LFTL_CMT_NONE)
		cmt->entry[entry->next].prev = entry->prev;
	else
		cmt->tp[tp].tail = entry->prev;
}

static void EntryPushHead(P_LFTL_CMT cmt, unsigned int tp, unsigned int idx)
{
	LFTL_CMT_TP_NODE *node = &cmt->tp[tp];

	cmt->entry[idx].prev = LFTL_CMT_NONE;
	cmt->entry[idx].next = node->head;
	if(node->head != LFTL_CMT_NONE)
		cmt->entry[node->head].prev = idx;
	else
		node->tail = idx;
	node->head = idx;
}

static void EntryPushTail(P_LFTL_CMT cmt, unsigned int tp, unsigned int idx)
{
	LFTL_CMT_TP_NODE *node = &cmt->tp[tp];

	cmt->entry[idx].next = LFTL_CMT_NONE;
	cmt->entry[idx].prev = node->tail;
	if(node->tail != LFTL_CMT_NONE)
		cmt->entry[node->tail].next = idx;
	else
		node->head = idx;
	node->tail = idx;
}

void LftlCmtInit(P_LFTL_CMT cmt)
{
	unsigned int i;

	for(i = 0; i < LFTL_CMT_CAPACITY; i++)
	{
		cmt->entry[i].next = (i + 1 < LFTL_CMT_CAPACITY) ? (i + 1) : LFTL_CMT_NONE;
		cmt->hashHead[i] = LFTL_CMT_NONE;
	}
	for(i = 0; i < LFTL_MAX_GROUPS; i++)
	{
		cmt->tp[i].head = LFTL_CMT_NONE;
		cmt->tp[i].tail = LFTL_CMT_NONE;
		cmt->tp[i].cnt = 0;
		cmt->tp[i].dirtyCnt = 0;
	}

	cmt->freeHead = 0;
	cmt->used = 0;
	cmt->lruHead = LFTL_CMT_NONE;
	cmt->lruTail = LFTL_CMT_NONE;
	cmt->prefetchCounter = 0;
	cmt->selectivePrefetchOn = 1;
}

unsigned int LftlCmtFind(P_LFTL_CMT cmt, unsigned int lsa)
{
	unsigned int idx = cmt->hashHead[HashOf(lsa)];

	while((idx != LFTL_CMT_NONE) && (cmt->entry[idx].lsa != lsa))
		idx = cmt->entry[idx].hashNext;

	return idx;
}

void LftlCmtTouch(P_LFTL_CMT cmt, unsigned int idx)
{
	unsigned int tp = TpOf(cmt->entry[idx].lsa);

	if(cmt->tp[tp].head != idx)
	{
		EntryUnlink(cmt, tp, idx);
		EntryPushHead(cmt, tp, idx);
	}
	if(cmt->lruHead != tp)
	{
		TpLruUnlink(cmt, tp);
		TpLruPushHead(cmt, tp);
	}
}

void LftlCmtInsert(P_LFTL_CMT cmt, unsigned int lsa, unsigned int vsa, unsigned int dirty, unsigned int prefetched)
{
	unsigned int idx = cmt->freeHead;
	unsigned int tp = TpOf(lsa);
	unsigned int bucket = HashOf(lsa);

	cmt->freeHead = cmt->entry[idx].next;
	cmt->used++;

	cmt->entry[idx].lsa = lsa;
	cmt->entry[idx].vsa = vsa;
	cmt->entry[idx].dirty = dirty;
	cmt->entry[idx].hashNext = cmt->hashHead[bucket];
	cmt->hashHead[bucket] = idx;

	if(cmt->tp[tp].cnt == 0)
	{
		TpLruPushHead(cmt, tp);
		if(++cmt->prefetchCounter == 3)
		{
			cmt->prefetchCounter = 0;
			cmt->selectivePrefetchOn = 0;
		}
	}
	else if(!prefetched && (cmt->lruHead != tp))
	{
		TpLruUnlink(cmt, tp);
		TpLruPushHead(cmt, tp);
	}

	cmt->tp[tp].cnt++;
	if(dirty)
		cmt->tp[tp].dirtyCnt++;

	if(prefetched)
		EntryPushTail(cmt, tp, idx);
	else
		EntryPushHead(cmt, tp, idx);
}

void LftlCmtSetDirty(P_LFTL_CMT cmt, unsigned int idx, unsigned int vsa)
{
	if(!cmt->entry[idx].dirty)
	{
		cmt->entry[idx].dirty = 1;
		cmt->tp[TpOf(cmt->entry[idx].lsa)].dirtyCnt++;
	}
	cmt->entry[idx].vsa = vsa;
}

void LftlCmtSetClean(P_LFTL_CMT cmt, unsigned int idx)
{
	if(cmt->entry[idx].dirty)
	{
		cmt->entry[idx].dirty = 0;
		cmt->tp[TpOf(cmt->entry[idx].lsa)].dirtyCnt--;
	}
}

unsigned int LftlCmtPickVictim(P_LFTL_CMT cmt)
{
	LFTL_CMT_TP_NODE *node = &cmt->tp[cmt->lruTail];
	unsigned int idx;

	if(node->dirtyCnt == node->cnt)
		return node->tail;

	for(idx = node->tail; cmt->entry[idx].dirty; idx = cmt->entry[idx].prev)
		;

	return idx;
}

void LftlCmtRemove(P_LFTL_CMT cmt, unsigned int idx)
{
	LFTL_CMT_ENTRY *entry = &cmt->entry[idx];
	unsigned int tp = TpOf(entry->lsa);
	unsigned int *link = &cmt->hashHead[HashOf(entry->lsa)];

	while(*link != idx)
		link = &cmt->entry[*link].hashNext;
	*link = entry->hashNext;

	EntryUnlink(cmt, tp, idx);
	cmt->tp[tp].cnt--;
	if(entry->dirty)
		cmt->tp[tp].dirtyCnt--;

	if(cmt->tp[tp].cnt == 0)
	{
		TpLruUnlink(cmt, tp);
		if(--cmt->prefetchCounter == -3)
		{
			cmt->prefetchCounter = 0;
			cmt->selectivePrefetchOn = 1;
		}
	}

	entry->next = cmt->freeHead;
	cmt->freeHead = idx;
	cmt->used--;
}
