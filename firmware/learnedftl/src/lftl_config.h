//////////////////////////////////////////////////////////////////////////////////
// lftl_config.h for Cosmos+ OpenSSD
//
// LearnedFTL (Wang et al., HPCA'24) ported to the Cosmos+ Micron 8C2W board.
//
// Board geometry fixes the whole layout of the design:
//   line (superblock) = the same virtual block on all 16 dies = 4096 slices
//   translation page  = 16KB / 4B = 4096 entries
// so one translation page covers exactly one line's worth of LPNs, and
//   group = 1 GTD entry = 1 translation page = at most one line's worth of LPNs.
// (The paper needs 64 GTD entries per group only because its pages are 4KB.)
//
// The firmware's VSA is already the paper's virtual PPN: die + 16*(block*256 +
// page), i.e. channel, way, page, block from the least significant digit up.
// Therefore VSA == line * 4096 + offset-in-line.
//////////////////////////////////////////////////////////////////////////////////

#ifndef LFTL_CONFIG_H_
#define LFTL_CONFIG_H_

#ifdef LFTL_HOST_TEST
// PC simulator (see test/): geometry can be shrunk with -D to stress GC.
#ifndef USER_BLOCKS_PER_DIE
#define USER_BLOCKS_PER_DIE				2048
#endif
#define USER_DIES						16
#define SLICES_PER_BLOCK				256
#define BYTES_PER_DATA_REGION_OF_PAGE	16384
#define LSA_NONE						0xffffffff
#define VSA_NONE						0xffffffff
#else
#include "address_translation.h"
#endif

//------------------------------------------------------------------
// Experiment knobs
//------------------------------------------------------------------

// 1: LearnedFTL. 0: same firmware without models, i.e. plain TPFTL, the
// baseline the paper builds on.
#ifndef LFTL_ENABLE_MODEL
#define LFTL_ENABLE_MODEL				1
#endif

// Number of cached mappings. Must be a power of two. 32768 entries are 1.56% of
// the 32GB experiment mode's 2,097,152 mappings, near the paper's 1.5%.
#ifndef LFTL_CMT_CAPACITY
#define LFTL_CMT_CAPACITY				32768
#endif

// A group holding this many lines is collected (paper/repo gc_threshold).
#ifndef LFTL_GC_LINE_THRESHOLD
#define LFTL_GC_LINE_THRESHOLD			5
#endif

// LPNs per group (<= 4096). The difference to the line size is headroom that every
// group keeps inside its line after GC. At 4096 a fully written group's line is
// full right after GC, and uniform random writes degenerate to one whole-group
// copy every few writes (measured WA in the thousands, see the design notes).
// The paper's answer, cross-group allocation, is not implemented in its repo.
// 3776 was picked from full-scale simulation: 3840 gives random write WA 15.6, 3776
// gives 12.8, and 3712 gives 10.8 but sequential rewrite WA 4.5 and almost no spare lines.
#ifndef LFTL_GROUP_SPAN
#define LFTL_GROUP_SPAN					3776
#endif

// When free lines drop to this count, collect the group with the most
// reclaimable pages. Must stay >= 1: GC itself consumes one free line.
#define LFTL_GC_FREE_LINE_WATERMARK		4

// 1: keep the full mapping table as a shadow (LOGICAL_SLICE_MAP on the board,
// which is already allocated) and check every translation against it. A
// mismatch stops the firmware. For bring-up only; turn off when measuring.
#ifndef LFTL_SHADOW_CHECK
#define LFTL_SHADOW_CHECK				0
#endif

//------------------------------------------------------------------
// Layout (derived from the board geometry, not tunable)
//------------------------------------------------------------------

#define LFTL_NUM_LINES					(USER_BLOCKS_PER_DIE)
#define LFTL_SLICES_PER_LINE			((USER_DIES) * (SLICES_PER_BLOCK))
#define LFTL_ENTRIES_PER_TP				((BYTES_PER_DATA_REGION_OF_PAGE) / 4)
#define LFTL_SLICES_PER_GROUP			(LFTL_ENTRIES_PER_TP)	// table size; only LFTL_GROUP_SPAN entries are used
#define LFTL_GROUP_OF(lsa)				((lsa) / LFTL_GROUP_SPAN)
#define LFTL_MAX_GROUPS					(LFTL_NUM_LINES)

// Lines used for a reduced-capacity experiment (EXP_LOGICAL_GB): the same
// lines-per-group ratio as the whole board, 2048 lines for 1999 groups. The
// paper's 6.25% over-provisioning is not enough here: every group needs a line
// of its own because cross-group allocation is not implemented.
#define LFTL_LINES_FOR_GROUPS(g)		(((g) * 2048 + 1998) / 1999)

// The design relies on these; a different board must fail here, not at runtime.
typedef char lftlCheckLineFitsGroup[(LFTL_SLICES_PER_LINE == LFTL_SLICES_PER_GROUP) ? 1 : -1];
typedef char lftlCheckGroupSpan[(LFTL_GROUP_SPAN <= LFTL_SLICES_PER_GROUP) ? 1 : -1];
typedef char lftlCheckCmtPowerOfTwo[((LFTL_CMT_CAPACITY & (LFTL_CMT_CAPACITY - 1)) == 0) ? 1 : -1];

// Lines reserved for translation pages. 1843 live TPs fit in half a line, so
// three lines (12288 slots) leave plenty of room for out-of-place updates.
#define LFTL_TP_LINES					3

// A model piece covers 64 LPNs and a group has 64 pieces, the same 64 LPNs per
// piece as the paper's 8 pieces per 512-entry translation page.
#define LFTL_PIECE_SHIFT				6
#define LFTL_LPNS_PER_PIECE				(1 << LFTL_PIECE_SHIFT)
#define LFTL_PIECES_PER_GROUP			(LFTL_SLICES_PER_GROUP >> LFTL_PIECE_SHIFT)

// Sequential initialization: shortest run worth replacing a piece's model for.
#define LFTL_SEQ_MIN_RUN				2

// Upper bound on mappings prefetched from one translation page read.
#define LFTL_PREFETCH_MAX				64

#endif /* LFTL_CONFIG_H_ */
