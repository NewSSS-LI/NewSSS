//////////////////////////////////////////////////////////////////////////////////
// lftl_model.h for Cosmos+ OpenSSD
//
// In-place-update linear model + bitmap filter (LearnedFTL paper, Section III-B).
// One model per group. No NAND I/O and no firmware dependency in here.
//
// Number format follows the paper (Section V, "Memory consumption"): k and b are
// stored as 2B float16. Training and prediction compute in float32, and the
// predicted offset is rounded to the nearest integer. b is an offset from the
// group's starting VSA (paper: "offset of PPNs from this GTD entry's starting
// PPN"), which is the start of the line the group received at its last GC.
//
// float16 represents every integer only up to 2048, while one board line holds
// 4096 slices. Predictions that come out wrong because of this simply get no bit:
// a bit is set only after the stored float16 parameters reproduce the real VSA.
//
// Other differences from the paper's <k, b, off>[N] (see docs):
//   - Pieces have a fixed LPN range, so "off" is implicit and lookup is O(1).
//
// The integer-only variant (Q0.15 k, 32-bit b) is kept in learnedftl-fixed.
//////////////////////////////////////////////////////////////////////////////////

#ifndef LFTL_MODEL_H_
#define LFTL_MODEL_H_

#include "lftl_config.h"

typedef struct _LFTL_MODEL {
	unsigned int ref;						// VSA the b offsets count from, VSA_NONE if unset
	_Float16 k[LFTL_PIECES_PER_GROUP];
	_Float16 b[LFTL_PIECES_PER_GROUP];
	unsigned int bitmap[LFTL_SLICES_PER_GROUP / 32];
} LFTL_MODEL, *P_LFTL_MODEL;

void LftlModelReset(P_LFTL_MODEL model);

// VSA of LPN offset x if its bit is set, otherwise VSA_NONE.
unsigned int LftlModelLookup(const LFTL_MODEL *model, unsigned int x);

// Must be called before the mapping of offset x changes.
void LftlModelClearBit(P_LFTL_MODEL model, unsigned int x);

// Sequential initialization (Section III-E1) for a write of offset x to vsa.
// runStart is the offset where the contiguous run ending at x began.
void LftlModelOnWrite(P_LFTL_MODEL model, unsigned int x, unsigned int runStart, unsigned int vsa);

// Training via GC (Section III-E2, steps 3 and 4). map[x] is the VSA of offset x
// or VSA_NONE, ref is the start VSA of the line GC wrote the group to (VSA_NONE
// if the group is empty). Rebuilds the model and returns how many LPNs are
// predictable.
unsigned int LftlModelTrain(P_LFTL_MODEL model, const unsigned int *map, unsigned int ref);

#endif /* LFTL_MODEL_H_ */
