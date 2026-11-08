//////////////////////////////////////////////////////////////////////////////////
// lftl_model.h for Cosmos+ OpenSSD
//
// In-place-update linear model + bitmap filter (LearnedFTL paper, Section III-B).
// One model per group. No NAND I/O and no firmware dependency in here.
//
// Differences from the paper's <k, b, off>[N] (see docs for the reasoning):
//   - Pieces have a fixed LPN range, so "off" is implicit and lookup is O(1).
//   - k is unsigned Q0.15 and all arithmetic is integer. Training and prediction
//     run the same integer code, so a bit set at training time cannot be
//     invalidated by floating point rounding at lookup time.
//   - b is the absolute VSA predicted for the piece's first LPN. Pieces of one
//     group may therefore point into different lines.
//////////////////////////////////////////////////////////////////////////////////

#ifndef LFTL_MODEL_H_
#define LFTL_MODEL_H_

#include "lftl_config.h"

typedef struct _LFTL_MODEL {
	unsigned int base[LFTL_PIECES_PER_GROUP];
	unsigned short k[LFTL_PIECES_PER_GROUP];
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
// or VSA_NONE. Rebuilds the model and returns how many LPNs are predictable.
unsigned int LftlModelTrain(P_LFTL_MODEL model, const unsigned int *map);

#endif /* LFTL_MODEL_H_ */
