//////////////////////////////////////////////////////////////////////////////////
// lftl_core.c for Cosmos+ OpenSSD
//
// Invariants:
//   I1. The truth for an LPN is its CMT entry if cached, else its translation
//       page (VSA_NONE if the group has no translation page yet).
//   I2. A set bitmap bit means the model predicts exactly that truth. Every
//       mapping change clears the bit first, and only LftlModelOnWrite and
//       LftlModelTrain set bits, both after comparing against the real VSA.
//   I3. A group has at most LFTL_GROUP_SPAN valid slices, less than one line's
//       capacity, so one free line is always enough for GC to rewrite a group and
//       the rewritten line always has room left.
//////////////////////////////////////////////////////////////////////////////////

#include "lftl_core.h"
#include "lftl_port.h"

static P_LFTL_META m;

#define GroupOf(lsa)		LFTL_GROUP_OF(lsa)
#define OffsetOf(lsa)		((lsa) % LFTL_GROUP_SPAN)
#define LineOf(vsa)			((vsa) / LFTL_SLICES_PER_LINE)
#define VsaOf(line, off)	((line) * LFTL_SLICES_PER_LINE + (off))

#define STATS_PRINT_QUIET_MS	2000

//------------------------------------------------------------------
// Lines
//------------------------------------------------------------------

static void FreeListPush(unsigned int line)
{
	m->line[line].owner = LFTL_OWNER_FREE;
	m->line[line].next = LFTL_LINE_NONE;
	m->line[line].prev = m->freeTail;
	if(m->freeTail != LFTL_LINE_NONE)
		m->line[m->freeTail].next = line;
	else
		m->freeHead = line;
	m->freeTail = line;
	m->freeLineCnt++;
}

static unsigned int FreeListPop(void)
{
	unsigned int line = m->freeHead;

	if(line == LFTL_LINE_NONE)
		LftlPortFatal("no free line", m->freeLineCnt);

	m->freeHead = m->line[line].next;
	if(m->freeHead != LFTL_LINE_NONE)
		m->line[m->freeHead].prev = LFTL_LINE_NONE;
	else
		m->freeTail = LFTL_LINE_NONE;
	m->freeLineCnt--;
	m->stats.linesAllocated++;

	return line;
}

// Queue erases for a line's programmed blocks and forget its reverse mappings.
static void EraseLine(unsigned int line)
{
	unsigned int die, off;
	unsigned int wp = m->line[line].wp;

	for(die = 0; die < USER_DIES; die++)
	{
		unsigned int programmed = (wp + USER_DIES - 1 - die) / USER_DIES;

		if(programmed != 0)
			LftlPortEraseBlock(die, line, programmed);
	}
	for(off = 0; off < wp; off++)
		LftlPortP2lSet(VsaOf(line, off), LSA_NONE);

	m->line[line].wp = 0;
	m->line[line].valid = 0;
}

static void GroupLinkLine(unsigned int g, unsigned int line)
{
	LFTL_GROUP *group = &m->group[g];

	m->line[line].owner = g;
	m->line[line].prev = LFTL_LINE_NONE;
	m->line[line].next = group->headLine;
	if(group->headLine != LFTL_LINE_NONE)
		m->line[group->headLine].prev = line;
	group->headLine = line;
	group->lineCnt++;
}

static void GroupUnlinkLine(unsigned int g, unsigned int line)
{
	LFTL_GROUP *group = &m->group[g];

	if(m->line[line].prev != LFTL_LINE_NONE)
		m->line[m->line[line].prev].next = m->line[line].next;
	else
		group->headLine = m->line[line].next;
	if(m->line[line].next != LFTL_LINE_NONE)
		m->line[m->line[line].next].prev = m->line[line].prev;
	group->lineCnt--;
}

// A closed line with nothing valid left needs no copying at all.
static void ReclaimIfEmpty(unsigned int g, unsigned int line)
{
	if((m->line[line].owner == g) && (m->line[line].valid == 0) && (line != m->group[g].curLine))
	{
		GroupUnlinkLine(g, line);
		EraseLine(line);
		FreeListPush(line);
		m->stats.linesFreedEmpty++;
	}
}

//------------------------------------------------------------------
// Translation pages
//------------------------------------------------------------------

static unsigned int TpFreeLine(void)
{
	unsigned int i;

	for(i = 0; i < LFTL_TP_LINES; i++)
		if((m->tpLine[i] != m->tpCurLine) && (m->line[m->tpLine[i]].wp == 0))
			return m->tpLine[i];

	return LFTL_LINE_NONE;
}

static unsigned int TpTakeSlice(void)
{
	unsigned int vsa = VsaOf(m->tpCurLine, m->line[m->tpCurLine].wp);

	m->line[m->tpCurLine].wp++;
	m->line[m->tpCurLine].valid++;

	return vsa;
}

// Keep one translation line empty: compact the emptiest one into the current line.
static void TpCollect(void)
{
	unsigned int i, off, victim = LFTL_LINE_NONE;

	for(i = 0; i < LFTL_TP_LINES; i++)
		if((m->tpLine[i] != m->tpCurLine) && ((victim == LFTL_LINE_NONE) || (m->line[m->tpLine[i]].valid < m->line[victim].valid)))
			victim = m->tpLine[i];

	for(off = 0; off < m->line[victim].wp; off++)
	{
		unsigned int oldVsa = VsaOf(victim, off);
		unsigned int p2l = LftlPortP2lGet(oldVsa);

		if((p2l != LSA_NONE) && (m->gtd[p2l & ~LFTL_P2L_TP_FLAG] == oldVsa))
		{
			unsigned int newVsa = TpTakeSlice();

			LftlPortReadPageSync(oldVsa);
			LftlPortWritePageSync(newVsa);
			m->gtd[p2l & ~LFTL_P2L_TP_FLAG] = newVsa;
			LftlPortP2lSet(newVsa, p2l);
			m->stats.tpReads++;
			m->stats.tpWrites++;
			m->stats.tpGcCopiedPages++;
		}
	}

	EraseLine(victim);
	m->stats.tpGcCount++;
}

// May overwrite the staging buffer, so call it before filling the buffer.
static unsigned int TpAllocSlice(void)
{
	if(m->line[m->tpCurLine].wp == LFTL_SLICES_PER_LINE)
	{
		m->tpCurLine = TpFreeLine();
		if(m->tpCurLine == LFTL_LINE_NONE)
			LftlPortFatal("no free translation line", 0);

		if(TpFreeLine() == LFTL_LINE_NONE)
			TpCollect();
	}

	return TpTakeSlice();
}

static void TpInstall(unsigned int tp, unsigned int newVsa)
{
	unsigned int oldVsa = m->gtd[tp];

	if(oldVsa != VSA_NONE)
	{
		m->line[LineOf(oldVsa)].valid--;
		LftlPortP2lSet(oldVsa, LSA_NONE);
	}
	m->gtd[tp] = newVsa;
	LftlPortP2lSet(newVsa, LFTL_P2L_TP_FLAG | tp);
}

// Batched write-back: all dirty entries of the page go out in a single program.
static void TpWriteBack(unsigned int tp)
{
	unsigned int *buf;
	unsigned int newVsa = TpAllocSlice();
	unsigned int idx, i;

	buf = LftlPortStagingBuf();
	if(m->gtd[tp] != VSA_NONE)
	{
		LftlPortReadPageSync(m->gtd[tp]);
		m->stats.tpReads++;
	}
	else
		for(i = 0; i < LFTL_ENTRIES_PER_TP; i++)
			buf[i] = VSA_NONE;

	for(idx = m->cmt.tp[tp].head; idx != LFTL_CMT_NONE; idx = m->cmt.entry[idx].next)
		if(m->cmt.entry[idx].dirty)
		{
			buf[OffsetOf(m->cmt.entry[idx].lsa)] = m->cmt.entry[idx].vsa;
			LftlCmtSetClean(&m->cmt, idx);
		}

	LftlPortWritePageSync(newVsa);
	m->stats.tpWrites++;
	TpInstall(tp, newVsa);
}

//------------------------------------------------------------------
// Mapping cache
//------------------------------------------------------------------

static void CmtMakeRoom(void)
{
	if(m->cmt.used == LFTL_CMT_CAPACITY)
	{
		unsigned int idx = LftlCmtPickVictim(&m->cmt);

		if(m->cmt.entry[idx].dirty)
			TpWriteBack(GroupOf(m->cmt.entry[idx].lsa));
		LftlCmtRemove(&m->cmt, idx);
	}
}

// Double read path: read the translation page, cache the mapping and whatever
// else TPFTL's request-level and selective prefetching ask for.
static unsigned int FetchMapping(unsigned int lsa)
{
	unsigned int g = GroupOf(lsa);
	unsigned int prefetchVsa[LFTL_PREFETCH_MAX];
	unsigned int prefetchEnd = lsa, count = 0;
	unsigned int groupEnd = g * LFTL_GROUP_SPAN + LFTL_GROUP_SPAN - 1;
	unsigned int *buf, vsa, i;

	if(m->gtd[g] == VSA_NONE)
		return VSA_NONE;

	// Request-level prefetch: the rest of this NVMe command.
	if((m->reqEndLsa > lsa) && (m->reqEndLsa < lsa + LFTL_PREFETCH_MAX))
		prefetchEnd = m->reqEndLsa;

	// Selective prefetch: as many successors as there are cached predecessors.
	if(m->cmt.selectivePrefetchOn)
	{
		unsigned int run = 0;

		while((run < LFTL_PREFETCH_MAX - 1) && (OffsetOf(lsa) > run) && (LftlCmtFind(&m->cmt, lsa - run - 1) != LFTL_CMT_NONE))
			run++;
		if(lsa + run > prefetchEnd)
			prefetchEnd = lsa + run;
	}
	if(prefetchEnd > groupEnd)
		prefetchEnd = groupEnd;

	LftlPortReadPageSync(m->gtd[g]);
	m->stats.tpReads++;

	// CmtMakeRoom below may reuse the staging buffer: copy what is needed first.
	buf = LftlPortStagingBuf();
	vsa = buf[OffsetOf(lsa)];
	for(i = lsa + 1; i <= prefetchEnd; i++)
		prefetchVsa[count++] = buf[OffsetOf(i)];

	CmtMakeRoom();
	LftlCmtInsert(&m->cmt, lsa, vsa, 0, 0);

	for(i = 0; i < count; i++)
		if((prefetchVsa[i] != VSA_NONE) && (LftlCmtFind(&m->cmt, lsa + 1 + i) == LFTL_CMT_NONE))
		{
			CmtMakeRoom();
			LftlCmtInsert(&m->cmt, lsa + 1 + i, prefetchVsa[i], 0, 1);
		}

	return vsa;
}

// Model lookup guarded by the reverse map. By I2 the guard can never fire; a
// rejection is a bug, so it is counted and served through the translation page.
static unsigned int ModelLookup(unsigned int lsa)
{
#if LFTL_ENABLE_MODEL
	unsigned int g = GroupOf(lsa);
	unsigned int vsa = LftlModelLookup(&m->model[g], OffsetOf(lsa));

	if(vsa == VSA_NONE)
		return VSA_NONE;

	if(LftlPortP2lGet(vsa) != lsa)
	{
		m->stats.modelRejected++;
		LftlModelClearBit(&m->model[g], OffsetOf(lsa));
		return VSA_NONE;
	}

	return vsa;
#else
	(void)lsa;
	return VSA_NONE;
#endif
}

static void ShadowCheck(unsigned int lsa, unsigned int vsa)
{
#if LFTL_SHADOW_CHECK
	if(LftlPortShadowGet(lsa) != vsa)
		LftlPortFatal("mapping differs from shadow, lsa", lsa);
#else
	(void)lsa;
	(void)vsa;
#endif
}

//------------------------------------------------------------------
// Group GC and model training
//------------------------------------------------------------------

static void GcGroup(unsigned int g)
{
	LFTL_GROUP *group = &m->group[g];
	unsigned int *map = m->gcMap;
	unsigned int *buf;
	unsigned long long startTick = LftlPortTicks(), trainTick;
	unsigned int x, idx, line, next, oldHead, count = 0, newLine = LFTL_LINE_NONE, tpVsa;

	// Step 1: regulate. The page is indexed by LPN, so there is nothing to sort.
	buf = LftlPortStagingBuf();
	if(m->gtd[g] != VSA_NONE)
	{
		LftlPortReadPageSync(m->gtd[g]);
		m->stats.tpReads++;
		for(x = 0; x < LFTL_SLICES_PER_GROUP; x++)
			map[x] = buf[x];
	}
	else
		for(x = 0; x < LFTL_SLICES_PER_GROUP; x++)
			map[x] = VSA_NONE;

	for(idx = m->cmt.tp[g].head; idx != LFTL_CMT_NONE; idx = m->cmt.entry[idx].next)
		map[OffsetOf(m->cmt.entry[idx].lsa)] = m->cmt.entry[idx].vsa;

	for(x = 0; x < LFTL_SLICES_PER_GROUP; x++)
		if(map[x] != VSA_NONE)
			count++;
	if(count != group->validCnt)
		LftlPortFatal("gc: valid count mismatch, group", g);

	// Detach the old lines. They stay readable until their erases are queued.
	oldHead = group->headLine;
	for(line = oldHead; line != LFTL_LINE_NONE; line = m->line[line].next)
		m->line[line].owner = LFTL_OWNER_GC;
	group->headLine = LFTL_LINE_NONE;
	group->curLine = LFTL_LINE_NONE;
	group->lineCnt = 0;
	group->runNextX = 0xffff;

	// Step 2: write valid pages back in LPN order to obtain contiguous VSAs.
	if(count != 0)
	{
		newLine = FreeListPop();
		GroupLinkLine(g, newLine);
		group->curLine = newLine;

		for(x = 0; x < LFTL_SLICES_PER_GROUP; x++)
			if(map[x] != VSA_NONE)
			{
				unsigned int lsa = g * LFTL_GROUP_SPAN + x;
				unsigned int newVsa = VsaOf(newLine, m->line[newLine].wp);

				LftlPortCopyPage(map[x], newVsa, lsa);
				LftlPortP2lSet(newVsa, lsa);
#if LFTL_SHADOW_CHECK
				LftlPortShadowSet(lsa, newVsa);
#endif
				m->line[newLine].wp++;
				m->line[newLine].valid++;
				map[x] = newVsa;
			}
		m->stats.gcCopiedPages += count;
	}

	// Only this group's single translation page has to be rewritten.
	tpVsa = TpAllocSlice();
	buf = LftlPortStagingBuf();
	for(x = 0; x < LFTL_SLICES_PER_GROUP; x++)
		buf[x] = map[x];
	LftlPortWritePageSync(tpVsa);
	m->stats.tpWrites++;
	TpInstall(g, tpVsa);

	for(idx = m->cmt.tp[g].head; idx != LFTL_CMT_NONE; idx = m->cmt.entry[idx].next)
	{
		m->cmt.entry[idx].vsa = map[OffsetOf(m->cmt.entry[idx].lsa)];
		LftlCmtSetClean(&m->cmt, idx);
	}

	// Steps 3 and 4: train and evaluate.
	trainTick = LftlPortTicks();
#if LFTL_ENABLE_MODEL
	m->stats.gcTrainedLpns += LftlModelTrain(&m->model[g], map, (newLine != LFTL_LINE_NONE) ? VsaOf(newLine, 0) : VSA_NONE);
#endif
	m->trainTickSum += LftlPortTicks() - trainTick;

	// The copies are queued, so the dependency check holds these erases back.
	for(line = oldHead; line != LFTL_LINE_NONE; line = next)
	{
		next = m->line[line].next;
		EraseLine(line);
		FreeListPush(line);
	}

	m->stats.gcGroups++;
	m->gcTickSum += LftlPortTicks() - startTick;
}

static unsigned int InvalidSlices(unsigned int g)
{
	LFTL_GROUP *group = &m->group[g];
	unsigned int invalid = group->lineCnt * LFTL_SLICES_PER_LINE - group->validCnt;

	// Slices not issued yet in the current line are free, not invalid.
	if(group->curLine != LFTL_LINE_NONE)
		invalid -= LFTL_SLICES_PER_LINE - m->line[group->curLine].wp;

	return invalid;
}

// Free lines are short and group "writer" needs space. Collect whichever group
// reclaims the most: a group holding several lines gives lines back, the writer
// itself at least gets room in its own line.
static unsigned int SelectVictimGroup(unsigned int writer)
{
	unsigned int g, best = writer, bestInvalid = InvalidSlices(writer);

	for(g = 0; g < m->numGroups; g++)
		if(m->group[g].lineCnt >= 2)
		{
			unsigned int invalid = InvalidSlices(g);

			if(invalid > bestInvalid)
			{
				best = g;
				bestInvalid = invalid;
			}
		}

	if(bestInvalid == 0)
		LftlPortFatal("no GC victim, free lines", m->freeLineCnt);

	return best;
}

static unsigned int AllocSlice(unsigned int g)
{
	LFTL_GROUP *group = &m->group[g];
	unsigned int line, vsa;

	while((group->curLine == LFTL_LINE_NONE) || (m->line[group->curLine].wp == LFTL_SLICES_PER_LINE))
	{
		if(group->curLine != LFTL_LINE_NONE)
		{
			unsigned int closed = group->curLine;

			group->curLine = LFTL_LINE_NONE;
			ReclaimIfEmpty(g, closed);
		}

		// GC can leave this group with a usable line, so re-evaluate after it.
		if(group->lineCnt >= LFTL_GC_LINE_THRESHOLD)
			GcGroup(g);
		else if(m->freeLineCnt <= LFTL_GC_FREE_LINE_WATERMARK)
			GcGroup(SelectVictimGroup(g));
		else
		{
			line = FreeListPop();
			GroupLinkLine(g, line);
			group->curLine = line;
		}
	}

	line = group->curLine;
	vsa = VsaOf(line, m->line[line].wp);
	m->line[line].wp++;

	return vsa;
}

//------------------------------------------------------------------
// Entry points
//------------------------------------------------------------------

void LftlInit(P_LFTL_META meta, unsigned int numGroups)
{
	unsigned int i, line, usable = 0, tpFound = 0;

	m = meta;
	m->numGroups = numGroups;
	m->freeHead = LFTL_LINE_NONE;
	m->freeTail = LFTL_LINE_NONE;
	m->freeLineCnt = 0;
	m->reqEndLsa = 0;
	m->gcTickSum = 0;
	m->trainTickSum = 0;
	m->lastActivityTick = 0;
	for(i = 0; i < sizeof(LFTL_STATS) / sizeof(unsigned int); i++)
	{
		((unsigned int *)&m->stats)[i] = 0;
		((unsigned int *)&m->statsAtLastPrint)[i] = 0;
	}

	LftlCmtInit(&m->cmt);

	for(i = 0; i < LFTL_MAX_GROUPS; i++)
	{
		LftlModelReset(&m->model[i]);
		m->gtd[i] = VSA_NONE;
		m->group[i].validCnt = 0;
		m->group[i].runNextVsa = VSA_NONE;
		m->group[i].headLine = LFTL_LINE_NONE;
		m->group[i].curLine = LFTL_LINE_NONE;
		m->group[i].lineCnt = 0;
		m->group[i].runStart = 0;
		m->group[i].runNextX = 0xffff;
	}

	// A line needs its block on every die. The firmware's remapping replaces bad
	// blocks with spares, so a bad virtual block here means the spares ran out.
	for(line = 0; line < LFTL_NUM_LINES; line++)
	{
		m->line[line].wp = 0;
		m->line[line].valid = 0;

		if(LftlPortLineIsBad(line))
		{
			m->line[line].owner = LFTL_OWNER_BAD;
			continue;
		}

		usable++;
		if(tpFound < LFTL_TP_LINES)
		{
			m->line[line].owner = LFTL_OWNER_TP;
			m->tpLine[tpFound++] = line;
		}
		else
			FreeListPush(line);
	}

	// Each group may hold a line and GC needs the watermark. One more guarantees a
	// group with two lines exists whenever free lines run low.
	if(usable < numGroups + LFTL_TP_LINES + LFTL_GC_FREE_LINE_WATERMARK + 1)
		LftlPortFatal("too few good lines, usable", usable);

	m->tpCurLine = m->tpLine[0];
}

unsigned int LftlRead(unsigned int lsa)
{
	unsigned int idx, vsa;

	if(GroupOf(lsa) >= m->numGroups)
		LftlPortFatal("read beyond capacity, lsa", lsa);

	m->stats.hostReads++;

	idx = LftlCmtFind(&m->cmt, lsa);
	if(idx != LFTL_CMT_NONE)
	{
		LftlCmtTouch(&m->cmt, idx);
		m->stats.readCmtHits++;
		vsa = m->cmt.entry[idx].vsa;
		ShadowCheck(lsa, vsa);
		return vsa;
	}

	vsa = ModelLookup(lsa);
	if(vsa != VSA_NONE)
	{
		m->stats.readModelHits++;
		ShadowCheck(lsa, vsa);
		return vsa;
	}

	if(m->gtd[GroupOf(lsa)] == VSA_NONE)
	{
		m->stats.readUnmapped++;
		ShadowCheck(lsa, VSA_NONE);
		return VSA_NONE;
	}

	vsa = FetchMapping(lsa);
	m->stats.readTpMisses++;
	ShadowCheck(lsa, vsa);

	return vsa;
}

unsigned int LftlWrite(unsigned int lsa)
{
	unsigned int g = GroupOf(lsa);
	unsigned int x = OffsetOf(lsa);
	LFTL_GROUP *group = &m->group[g];
	unsigned int idx, oldVsa, newVsa, oldLine = LFTL_LINE_NONE;

	if(g >= m->numGroups)
		LftlPortFatal("write beyond capacity, lsa", lsa);

	m->stats.hostWrites++;

	// Find the old slice: cache, then model, then the translation page.
	idx = LftlCmtFind(&m->cmt, lsa);
	if(idx != LFTL_CMT_NONE)
	{
		LftlCmtTouch(&m->cmt, idx);
		oldVsa = m->cmt.entry[idx].vsa;
	}
	else
	{
		oldVsa = ModelLookup(lsa);
		if(oldVsa != VSA_NONE)
			m->stats.writeModelHits++;
		else
			oldVsa = FetchMapping(lsa);
	}
	ShadowCheck(lsa, oldVsa);

	// Invalidate before allocating: GC inside AllocSlice must not copy this page.
#if LFTL_ENABLE_MODEL
	LftlModelClearBit(&m->model[g], x);
#endif
	if(oldVsa != VSA_NONE)
	{
		oldLine = LineOf(oldVsa);
		LftlPortP2lSet(oldVsa, LSA_NONE);
		m->line[oldLine].valid--;
		group->validCnt--;
	}

	// Make the cache entry exist now. Nothing below can evict it, so AllocSlice
	// and the final update never need to write a translation page back mid-way.
	idx = LftlCmtFind(&m->cmt, lsa);
	if(idx == LFTL_CMT_NONE)
	{
		CmtMakeRoom();
		LftlCmtInsert(&m->cmt, lsa, VSA_NONE, 1, 0);
	}
	else
		LftlCmtSetDirty(&m->cmt, idx, VSA_NONE);

	newVsa = AllocSlice(g);

	idx = LftlCmtFind(&m->cmt, lsa);
	LftlCmtSetDirty(&m->cmt, idx, newVsa);
	LftlPortP2lSet(newVsa, lsa);
	m->line[LineOf(newVsa)].valid++;
	group->validCnt++;

	// Sequential initialization: track the contiguous run per group.
	if((group->runNextX != x) || (group->runNextVsa != newVsa))
		group->runStart = x;
	group->runNextX = x + 1;
	group->runNextVsa = newVsa + 1;
#if LFTL_ENABLE_MODEL
	LftlModelOnWrite(&m->model[g], x, group->runStart, newVsa);
#endif

#if LFTL_SHADOW_CHECK
	LftlPortShadowSet(lsa, newVsa);
#endif

	if(oldLine != LFTL_LINE_NONE)
		ReclaimIfEmpty(g, oldLine);

	return newVsa;
}

void LftlHintRequest(unsigned int endLsa)
{
	m->reqEndLsa = endLsa;
}

const LFTL_STATS *LftlStats(void)
{
	m->stats.gcMs = (unsigned int)(m->gcTickSum / LftlPortTicksPerMs());
	m->stats.trainUs = (unsigned int)(m->trainTickSum * 1000 / LftlPortTicksPerMs());
	return &m->stats;
}

void LftlIdleTick(void)
{
	static const char *const labels[] = {
		"hostReads", "hostWrites", "readCmtHits", "readModelHits", "readTpMisses", "readUnmapped",
		"writeModelHits", "tpReads", "tpWrites", "gcGroups", "gcCopiedPages", "gcTrainedLpns",
		"tpGcCount", "tpGcCopiedPages", "linesAllocated", "linesFreedEmpty", "modelRejected",
		"gcMs", "trainUs"
	};
	static unsigned int lastSeenOps = 0;
	unsigned int ops = m->stats.hostReads + m->stats.hostWrites;
	const unsigned int *now, *before;
	unsigned int i;

	if(ops != lastSeenOps)
	{
		lastSeenOps = ops;
		m->lastActivityTick = LftlPortTicks();
		return;
	}

	if(ops == m->statsAtLastPrint.hostReads + m->statsAtLastPrint.hostWrites)
		return;

	if(LftlPortTicks() - m->lastActivityTick < (unsigned long long)STATS_PRINT_QUIET_MS * LftlPortTicksPerMs())
		return;

	now = (const unsigned int *)LftlStats();
	before = (const unsigned int *)&m->statsAtLastPrint;

	LftlPortPrintLine("[LFTL] --- since last report --- freeLines", m->freeLineCnt);
	for(i = 0; i < sizeof(labels) / sizeof(labels[0]); i++)
		LftlPortPrintLine(labels[i], now[i] - before[i]);

	m->statsAtLastPrint = m->stats;
}
