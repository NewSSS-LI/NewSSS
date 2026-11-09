//////////////////////////////////////////////////////////////////////////////////
// lftl_core.h for Cosmos+ OpenSSD
//
// Replaces GreedyFTL's address translation, free slice allocation and GC:
//   - LftlRead/LftlWrite are the bodies of AddrTransRead/AddrTransWrite.
//   - Data lines are owned by groups (group-based allocation, Section III-D).
//   - GC collects one whole group, rewrites its valid pages in LPN order and
//     retrains its model (Section III-E2).
//////////////////////////////////////////////////////////////////////////////////

#ifndef LFTL_CORE_H_
#define LFTL_CORE_H_

#include "lftl_cmt.h"
#include "lftl_model.h"

#define LFTL_LINE_NONE		0xffff
#define LFTL_OWNER_FREE		0xffff
#define LFTL_OWNER_TP		0xfffe
#define LFTL_OWNER_BAD		0xfffd
#define LFTL_OWNER_GC		0xfffc

// P2L value of a translation page: flag | group number.
#define LFTL_P2L_TP_FLAG	0x80000000

typedef struct _LFTL_LINE {
	unsigned short owner;	// group number or LFTL_OWNER_*
	unsigned short prev;	// group's line list or free list
	unsigned short next;
	unsigned short valid;	// valid slices
	unsigned short wp;		// slices issued (programmed or queued)
	unsigned short reserved0;
} LFTL_LINE;

typedef struct _LFTL_GROUP {
	unsigned int validCnt;
	unsigned int runNextVsa;	// VSA that would continue the current run
	unsigned short headLine;
	unsigned short curLine;		// line taking new writes, LFTL_LINE_NONE if none
	unsigned short lineCnt;
	unsigned short runStart;	// offset where the current run began
	unsigned short runNextX;	// offset that would continue it
	unsigned short reserved0;
} LFTL_GROUP;

typedef struct _LFTL_STATS {
	unsigned int hostReads;		// translations for reads of mapped or unmapped LPNs
	unsigned int hostWrites;
	unsigned int readCmtHits;
	unsigned int readModelHits;
	unsigned int readTpMisses;	// reads that needed a translation page (double read)
	unsigned int readUnmapped;
	unsigned int writeModelHits;// old VSA found through the model, no TP read
	unsigned int tpReads;
	unsigned int tpWrites;
	unsigned int gcGroups;
	unsigned int gcCopiedPages;
	unsigned int gcTrainedLpns;
	unsigned int tpGcCount;
	unsigned int tpGcCopiedPages;
	unsigned int linesAllocated;
	unsigned int linesFreedEmpty;	// fully invalid lines reclaimed without copying
	unsigned int modelRejected;		// P2L disagreed with a set bit; must stay 0
	unsigned int gcMs;
	unsigned int trainUs;
} LFTL_STATS;

typedef struct _LFTL_META {
	LFTL_MODEL model[LFTL_MAX_GROUPS];
	LFTL_CMT cmt;
	LFTL_GROUP group[LFTL_MAX_GROUPS];
	LFTL_LINE line[LFTL_NUM_LINES];
	unsigned int gtd[LFTL_MAX_GROUPS];
	unsigned int gcMap[LFTL_SLICES_PER_GROUP];
	unsigned int tpLine[LFTL_TP_LINES];
	unsigned int tpCurLine;
	unsigned int freeHead;
	unsigned int freeTail;
	unsigned int freeLineCnt;
	unsigned int numGroups;
	unsigned int reqEndLsa;
	LFTL_STATS stats;
	LFTL_STATS statsAtLastPrint;
	unsigned long long lastActivityTick;
	unsigned long long gcTickSum;
	unsigned long long trainTickSum;
} LFTL_META, *P_LFTL_META;

// numGroups = logical capacity in slices / LFTL_GROUP_SPAN, rounded up.
// Only lines below numLines are used (all of them: LFTL_NUM_LINES).
void LftlInit(P_LFTL_META meta, unsigned int numGroups, unsigned int numLines);

// VSA of lsa or VSA_NONE if it was never written.
unsigned int LftlRead(unsigned int lsa);

// Invalidate lsa's old slice and allocate a new one. May run GC.
unsigned int LftlWrite(unsigned int lsa);

// Last slice of the NVMe command being translated; bounds request-level prefetch.
void LftlHintRequest(unsigned int endLsa);

// Call from the idle path. Prints the counters since the last print once the
// device has been quiet for a while, so one FIO run produces one report.
void LftlIdleTick(void);

const LFTL_STATS *LftlStats(void);

#endif /* LFTL_CORE_H_ */
