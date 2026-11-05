// PC simulator for run-lftl: runs the real lftl_core/model/cmt code against a
// simulated NAND array and checks, on every access, that:
//   - the data read back is the latest version written to that LPN,
//   - pages are programmed in order, only into erased blocks,
//   - an erase names exactly the number of pages issued to the block (on the board
//     a wrong count makes the row address dependency check wait forever),
//   - every translation matches a full shadow map (LFTL_SHADOW_CHECK).
// It models logic only: no timing, no parallelism, no data buffer.
//
// Usage: sim_main [seed]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lftl_core.h"
#include "lftl_port.h"

#define TOTAL_SLICES	(LFTL_NUM_LINES * LFTL_SLICES_PER_LINE)

typedef struct { unsigned int lsa, ver; } PageTag;

static PageTag *nandTag;				// content of each user page
static unsigned int **nandTp;			// content of each translation page
static unsigned short (*blockProg)[USER_BLOCKS_PER_DIE];
static unsigned int *p2l, *shadow, *lpnVer;
static unsigned int staging[LFTL_ENTRIES_PER_TP];
static P_LFTL_META meta;
static unsigned long long simTickOffset;

static void Fail(const char *what, unsigned int a, unsigned int b)
{
	printf("\nFAIL: %s (%u, %u)\n", what, a, b);
	exit(1);
}

static void Program(unsigned int vsa)
{
	unsigned int die = vsa % USER_DIES;
	unsigned int block = vsa / LFTL_SLICES_PER_LINE;
	unsigned int page = (vsa / USER_DIES) % SLICES_PER_BLOCK;

	if(blockProg[die][block] != page)
		Fail("out of order program: expected page, got page", blockProg[die][block], page);
	blockProg[die][block]++;
}

static void CheckProgrammed(unsigned int vsa)
{
	if(((vsa / USER_DIES) % SLICES_PER_BLOCK) >= blockProg[vsa % USER_DIES][vsa / LFTL_SLICES_PER_LINE])
		Fail("read of unprogrammed page, vsa", vsa, 0);
}

unsigned int *LftlPortStagingBuf(void) { return staging; }

void LftlPortReadPageSync(unsigned int vsa)
{
	CheckProgrammed(vsa);
	if(!nandTp[vsa])
		Fail("translation read of a non translation page, vsa", vsa, 0);
	memcpy(staging, nandTp[vsa], sizeof(staging));
}

void LftlPortWritePageSync(unsigned int vsa)
{
	Program(vsa);
	nandTp[vsa] = malloc(sizeof(staging));
	memcpy(nandTp[vsa], staging, sizeof(staging));
}

void LftlPortCopyPage(unsigned int srcVsa, unsigned int dstVsa, unsigned int lsa)
{
	CheckProgrammed(srcVsa);
	if(nandTag[srcVsa].lsa != lsa)
		Fail("GC copied a page of another LPN: lsa, found", lsa, nandTag[srcVsa].lsa);
	if(nandTag[srcVsa].ver != lpnVer[lsa])
		Fail("GC copied a stale version: lsa, ver", lsa, nandTag[srcVsa].ver);
	Program(dstVsa);
	nandTag[dstVsa] = nandTag[srcVsa];
}

void LftlPortEraseBlock(unsigned int dieNo, unsigned int blockNo, unsigned int programmedPageCnt)
{
	unsigned int page;

	if(blockProg[dieNo][blockNo] != programmedPageCnt)
		Fail("erase count mismatch: actual, claimed", blockProg[dieNo][blockNo], programmedPageCnt);
	for(page = 0; page < programmedPageCnt; page++)
	{
		unsigned int vsa = dieNo + USER_DIES * (blockNo * SLICES_PER_BLOCK + page);

		free(nandTp[vsa]);
		nandTp[vsa] = NULL;
		nandTag[vsa].lsa = LSA_NONE;
	}
	blockProg[dieNo][blockNo] = 0;
}

unsigned int LftlPortLineIsBad(unsigned int lineNo) { return lineNo == 7; }	// exercise bad line handling
unsigned int LftlPortP2lGet(unsigned int vsa) { return p2l[vsa]; }
void LftlPortP2lSet(unsigned int vsa, unsigned int lsa) { p2l[vsa] = lsa; }
unsigned int LftlPortShadowGet(unsigned int lsa) { return shadow[lsa]; }
void LftlPortShadowSet(unsigned int lsa, unsigned int vsa) { shadow[lsa] = vsa; }
unsigned long long LftlPortTicks(void) { return clock() * 1000ULL / CLOCKS_PER_SEC + simTickOffset; }
unsigned int LftlPortTicksPerMs(void) { return 1; }
void LftlPortPrintLine(const char *label, unsigned int value) { printf("    %s %u\n", label, value); }
void LftlPortFatal(const char *message, unsigned int value) { Fail(message, value, 0); }

//------------------------------------------------------------------

static unsigned int numLsa;
static unsigned long long rngState;

static unsigned int Rand(void)
{
	rngState ^= rngState << 13;
	rngState ^= rngState >> 7;
	rngState ^= rngState << 17;
	return (unsigned int)(rngState >> 11);
}

static void Reset(unsigned int numGroups)
{
	unsigned int i;

	for(i = 0; i < TOTAL_SLICES; i++)
	{
		free(nandTp[i]);
		nandTp[i] = NULL;
		nandTag[i].lsa = LSA_NONE;
		p2l[i] = LSA_NONE;
		shadow[i] = VSA_NONE;
		lpnVer[i] = 0;
	}
	memset(blockProg, 0, sizeof(unsigned short) * USER_DIES * USER_BLOCKS_PER_DIE);
	numLsa = numGroups * LFTL_GROUP_SPAN;
	LftlInit(meta, numGroups);
}

static void WriteSlice(unsigned int lsa)
{
	unsigned int vsa = LftlWrite(lsa);

	Program(vsa);
	lpnVer[lsa]++;
	nandTag[vsa].lsa = lsa;
	nandTag[vsa].ver = lpnVer[lsa];
}

static void ReadSlice(unsigned int lsa)
{
	unsigned int vsa = LftlRead(lsa);

	if(lpnVer[lsa] == 0)
	{
		if(vsa != VSA_NONE)
			Fail("unwritten LPN has a mapping, lsa", lsa, vsa);
		return;
	}
	if(vsa == VSA_NONE)
		Fail("written LPN lost its mapping, lsa", lsa, 0);
	CheckProgrammed(vsa);
	if((nandTag[vsa].lsa != lsa) || (nandTag[vsa].ver != lpnVer[lsa]))
		Fail("wrong data: lsa, read ver", lsa, nandTag[vsa].ver);
}

typedef enum { W_SEQ, W_RAND, R_SEQ, R_RAND } Pattern;

// Issues whole "commands" of ioSlices, over the first rangeSlices LPNs, until
// totalSlices have been transferred.
static void RunIo(Pattern pattern, unsigned int rangeSlices, unsigned int ioSlices, unsigned long long totalSlices)
{
	unsigned long long done = 0;
	unsigned int cursor = 0, i;

	while(done < totalSlices)
	{
		unsigned int start;

		if((pattern == W_SEQ) || (pattern == R_SEQ))
		{
			start = cursor;
			cursor = (cursor + ioSlices >= rangeSlices) ? 0 : cursor + ioSlices;
		}
		else
			start = (Rand() % (rangeSlices / ioSlices)) * ioSlices;

		LftlHintRequest(start + ioSlices - 1);
		for(i = 0; i < ioSlices; i++)
			if((pattern == W_SEQ) || (pattern == W_RAND))
				WriteSlice(start + i);
			else
				ReadSlice(start + i);
		done += ioSlices;
	}
}

static void VerifyAll(void)
{
	unsigned int lsa;

	for(lsa = 0; lsa < numLsa; lsa++)
		ReadSlice(lsa);
}

static LFTL_STATS before;

static void PhaseBegin(void)
{
	before = *LftlStats();
}

static void PhaseReport(const char *name)
{
	const LFTL_STATS *now = LftlStats();
	unsigned int reads = now->hostReads - before.hostReads;
	unsigned int writes = now->hostWrites - before.hostWrites;
	unsigned int cmt = now->readCmtHits - before.readCmtHits;
	unsigned int model = now->readModelHits - before.readModelHits;
	unsigned int miss = now->readTpMisses - before.readTpMisses;
	unsigned int tpr = now->tpReads - before.tpReads;
	unsigned int tpw = now->tpWrites - before.tpWrites;
	unsigned int gcCopy = now->gcCopiedPages - before.gcCopiedPages;
	unsigned int gcGroups = now->gcGroups - before.gcGroups;
	unsigned int empty = now->linesFreedEmpty - before.linesFreedEmpty;

	printf("%-30s", name);
	if(reads)
		printf(" | R %9u cmt %5.1f%% model %5.1f%% dbl-read %5.1f%%", reads, 100.0 * cmt / reads, 100.0 * model / reads, 100.0 * miss / reads);
	if(writes)
		printf(" | W %9u WA %5.2f tpW/W %5.3f gc %5u emptyLines %5u", writes, (double)(writes + gcCopy + tpw) / writes, (double)tpw / writes, gcGroups, empty);
	(void)tpr;
	if(now->modelRejected)
		printf(" | REJECTED %u", now->modelRejected);
	printf("\n");
}

#define PHASE(name, code)	do { PhaseBegin(); code; PhaseReport(name); } while(0)

int main(int argc, char **argv)
{
	unsigned int seed = (argc > 1) ? (unsigned int)atoi(argv[1]) : 1;
	// same 10% over-provisioning as the firmware
	unsigned int numGroups = (USER_BLOCKS_PER_DIE * 9 / 10 * LFTL_SLICES_PER_LINE + LFTL_GROUP_SPAN - 1) / LFTL_GROUP_SPAN;
	unsigned int capacity, smallRange;
	unsigned long long full;

	rngState = 0x9E3779B97F4A7C15ULL ^ ((unsigned long long)seed << 32 | seed);

	nandTag = malloc(sizeof(PageTag) * TOTAL_SLICES);
	nandTp = calloc(TOTAL_SLICES, sizeof(unsigned int *));
	blockProg = malloc(sizeof(unsigned short) * USER_DIES * USER_BLOCKS_PER_DIE);
	p2l = malloc(sizeof(unsigned int) * TOTAL_SLICES);
	shadow = malloc(sizeof(unsigned int) * TOTAL_SLICES);
	lpnVer = malloc(sizeof(unsigned int) * TOTAL_SLICES);
	meta = malloc(sizeof(LFTL_META));

	setvbuf(stdout, NULL, _IOLBF, 4096);
	printf("lines %d, span %d, groups %u, model %s, CMT %d, gc threshold %d, seed %u, meta %u KB (model %u KB)\n\n",
		LFTL_NUM_LINES, LFTL_GROUP_SPAN, numGroups, LFTL_ENABLE_MODEL ? "on" : "off", LFTL_CMT_CAPACITY, LFTL_GC_LINE_THRESHOLD, seed,
		(unsigned int)(sizeof(LFTL_META) / 1024), (unsigned int)(sizeof(LFTL_MODEL) * numGroups / 1024));

	// Smallest legal read on empty media.
	Reset(numGroups);
	capacity = numLsa;
	full = capacity;
	smallRange = (capacity / 16 < 131072) ? capacity / 16 : 131072;	// fio size=2048M

	printf("[A] the FIO pattern used so far: 128KB I/O over a small range\n");
	PHASE("read of unwritten range", RunIo(R_RAND, smallRange, 8, smallRange));
	PHASE("seq write", RunIo(W_SEQ, smallRange, 8, smallRange));
	PHASE("seq read", RunIo(R_SEQ, smallRange, 8, smallRange));
	PHASE("rand read", RunIo(R_RAND, smallRange, 8, smallRange));
	PHASE("rand write x1", RunIo(W_RAND, smallRange, 8, smallRange));
	PHASE("rand read", RunIo(R_RAND, smallRange, 8, smallRange));
	PHASE("rand write x6 (GC warm-up)", RunIo(W_RAND, smallRange, 8, 6ULL * smallRange));
	PHASE("rand read", RunIo(R_RAND, smallRange, 8, smallRange));
	PHASE("verify all", VerifyAll());

	printf("\n[B] paper-like: warm the whole SSD, then read\n");
	Reset(numGroups);
	PHASE("seq fill", RunIo(W_SEQ, capacity, 32, full));
	PHASE("rand read 16KB", RunIo(R_RAND, capacity, 1, full));
	PHASE("rand write 16KB x1", RunIo(W_RAND, capacity, 1, full));
	PHASE("rand read 16KB", RunIo(R_RAND, capacity, 1, full));
	PHASE("rand write 16KB x5", RunIo(W_RAND, capacity, 1, 5 * full));
	PHASE("rand read 16KB", RunIo(R_RAND, capacity, 1, full));
	PHASE("seq read 16KB", RunIo(R_SEQ, capacity, 1, full));
	PHASE("seq rewrite", RunIo(W_SEQ, capacity, 32, full));
	PHASE("rand read 16KB", RunIo(R_RAND, capacity, 1, full));
	PHASE("verify all", VerifyAll());

	printf("\n[C] partially valid groups: every LPN has holes\n");
	Reset(numGroups);
	{
		unsigned int lsa;

		PhaseBegin();
		for(lsa = 0; lsa < capacity; lsa++)
			if(Rand() % 100 < 60)
				WriteSlice(lsa);
		PhaseReport("write random 60% of LPNs");
		PHASE("overwrite same range x4", RunIo(W_RAND, capacity, 1, 4 * full));
		PHASE("rand read 16KB", RunIo(R_RAND, capacity, 1, full));
		PHASE("verify all", VerifyAll());
	}

	printf("\n[D] skewed writes, free lines run out, watermark GC\n");
	Reset(numGroups);
	PHASE("seq fill", RunIo(W_SEQ, capacity, 32, full));
	{
		unsigned long long i;

		PhaseBegin();
		for(i = 0; i < 6 * full; i++)
		{
			unsigned int lsa = (Rand() % 100 < 80) ? Rand() % (capacity / 5) : Rand() % capacity;

			LftlHintRequest(lsa);
			WriteSlice(lsa);
			if((i & 7) == 0)
				ReadSlice(Rand() % capacity);
		}
		PhaseReport("80/20 mixed x6");
	}
	PHASE("verify all", VerifyAll());

	printf("\n[E] stats report path\n");
	PhaseBegin();
	LftlIdleTick();
	simTickOffset += 5000;
	LftlIdleTick();
	printf("    (report printed above when counters moved)\n");

	if(LftlStats()->modelRejected != 0)
		Fail("model predictions rejected by the P2L guard", LftlStats()->modelRejected, 0);

	printf("\nPASS: no integrity, ordering, erase-count or shadow failure; modelRejected = 0\n");
	return 0;
}
