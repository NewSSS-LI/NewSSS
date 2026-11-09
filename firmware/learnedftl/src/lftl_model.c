//////////////////////////////////////////////////////////////////////////////////
// lftl_model.c for Cosmos+ OpenSSD
//////////////////////////////////////////////////////////////////////////////////

#include "lftl_model.h"

#define PieceOf(x)		((x) >> LFTL_PIECE_SHIFT)
#define RelOf(x)		((x) & (LFTL_LPNS_PER_PIECE - 1))
#define BitWord(x)		((x) >> 5)
#define BitMask(x)		(1u << ((x) & 31))

#define TOTAL_SLICES	((LFTL_NUM_LINES) * (LFTL_SLICES_PER_LINE))

// y = k * rel + b in float32 from the stored float16 values, rounded half up
// like the paper's prototype, then turned back into a VSA.
static unsigned int PredictPiece(const LFTL_MODEL *model, unsigned int piece, unsigned int rel)
{
	float y = (float)model->k[piece] * (float)rel + (float)model->b[piece];
	int r;
	long long vsa;

	// Also rejects NaN and infinity, which a float16 overflow produces.
	if((model->ref == VSA_NONE) || !((y > -(float)TOTAL_SLICES) && (y < (float)TOTAL_SLICES)))
		return VSA_NONE;

	r = (int)y;
	if(y - (float)r >= 0.5f)
		r++;
	else if(y - (float)r < -0.5f)
		r--;

	vsa = (long long)model->ref + r;
	if((vsa < 0) || (vsa >= TOTAL_SLICES))
		return VSA_NONE;

	return (unsigned int)vsa;
}

// Offset of vsa from the model's starting VSA, as the float the model fits.
static float OffsetFromRef(const LFTL_MODEL *model, unsigned int vsa)
{
	return (float)(int)(vsa - model->ref);
}

void LftlModelReset(P_LFTL_MODEL model)
{
	unsigned int i;

	model->ref = VSA_NONE;
	for(i = 0; i < LFTL_PIECES_PER_GROUP; i++)
	{
		model->k[i] = (_Float16)1.0f;
		model->b[i] = (_Float16)0.0f;
	}
	for(i = 0; i < LFTL_SLICES_PER_GROUP / 32; i++)
		model->bitmap[i] = 0;
}

unsigned int LftlModelLookup(const LFTL_MODEL *model, unsigned int x)
{
	if(model->bitmap[BitWord(x)] & BitMask(x))
		return PredictPiece(model, PieceOf(x), RelOf(x));

	return VSA_NONE;
}

void LftlModelClearBit(P_LFTL_MODEL model, unsigned int x)
{
	model->bitmap[BitWord(x)] &= ~BitMask(x);
}

void LftlModelOnWrite(P_LFTL_MODEL model, unsigned int x, unsigned int runStart, unsigned int vsa)
{
	unsigned int piece = PieceOf(x);
	unsigned int rel = RelOf(x);
	unsigned int pieceStart = piece << LFTL_PIECE_SHIFT;
	unsigned int first = (runStart > pieceStart) ? runStart : pieceStart;
	unsigned int runLen = x - first + 1;
	unsigned int word = BitWord(pieceStart);
	unsigned int oldValid, i;
	_Float16 oldK, oldB;

	// A group that never trained counts from the line it is writing into.
	if(model->ref == VSA_NONE)
		model->ref = vsa - vsa % LFTL_SLICES_PER_LINE;

	// The current parameters already predict this write.
	if(PredictPiece(model, piece, rel) == vsa)
	{
		model->bitmap[BitWord(x)] |= BitMask(x);
		return;
	}

	// Paper's step 4: replace the model only if the run beats what it serves now.
	oldValid = __builtin_popcount(model->bitmap[word]) + __builtin_popcount(model->bitmap[word + 1]);
	if((runLen < LFTL_SEQ_MIN_RUN) || (runLen <= oldValid))
		return;

	// y = x + b through this write. If float16 cannot hold b exactly, the new
	// model would predict nothing, so the old one is kept.
	oldK = model->k[piece];
	oldB = model->b[piece];
	model->k[piece] = (_Float16)1.0f;
	model->b[piece] = (_Float16)(OffsetFromRef(model, vsa) - (float)rel);
	if(PredictPiece(model, piece, rel) != vsa)
	{
		model->k[piece] = oldK;
		model->b[piece] = oldB;
		return;
	}

	model->bitmap[word] = 0;
	model->bitmap[word + 1] = 0;
	for(i = first; i <= x; i++)
		if(PredictPiece(model, piece, RelOf(i)) == vsa - (x - i))
			model->bitmap[BitWord(i)] |= BitMask(i);
}

unsigned int LftlModelTrain(P_LFTL_MODEL model, const unsigned int *map, unsigned int ref)
{
	unsigned int piece, rel, predictable = 0;

	LftlModelReset(model);
	if(ref == VSA_NONE)
		return 0;
	model->ref = ref;

	for(piece = 0; piece < LFTL_PIECES_PER_GROUP; piece++)
	{
		const unsigned int *pieceMap = &map[piece << LFTL_PIECE_SHIFT];
		unsigned int n = 0, firstRel = 0, prevRel = 0, prevVsa = 0;
		unsigned int runLen = 0, bestRunLen = 0, bestRunBase = 0;
		unsigned int fitHits = 0;
		float sumX = 0, sumY = 0, sumXX = 0, sumXY = 0, firstY = 0, den, k, b;

		// Step 1: map is indexed by LPN, so the points are already sorted.
		for(rel = 0; rel < LFTL_LPNS_PER_PIECE; rel++)
		{
			unsigned int vsa = pieceMap[rel];
			float x = (float)rel, y;

			if(vsa == VSA_NONE)
				continue;

			y = OffsetFromRef(model, vsa);
			if(n == 0)
			{
				firstRel = rel;
				firstY = y;
			}
			n++;
			sumX += x;
			sumY += y;
			sumXX += x * x;
			sumXY += x * y;

			if((runLen != 0) && (rel == prevRel + 1) && (vsa == prevVsa + 1))
				runLen++;
			else
				runLen = 1;
			if(runLen > bestRunLen)
			{
				bestRunLen = runLen;
				bestRunBase = vsa - rel;
			}
			prevRel = rel;
			prevVsa = vsa;
		}

		if(n == 0)
			continue;

		// Step 3: least squares fit in float32, the same formula as the paper's
		// prototype (LeastSquareNew).
		den = (float)n * sumXX - sumX * sumX;
		if(den == 0.0f)
		{
			k = 1.0f;
			b = firstY - (float)firstRel;
		}
		else
		{
			k = ((float)n * sumXY - sumX * sumY) / den;
			b = (sumXX * sumY - sumX * sumXY) / den;
		}
		model->k[piece] = (_Float16)k;
		model->b[piece] = (_Float16)b;

		// Step 4: evaluate with the stored float16 values. Only exact predictions get a bit.
		for(rel = 0; rel < LFTL_LPNS_PER_PIECE; rel++)
			if((pieceMap[rel] != VSA_NONE) && (PredictPiece(model, piece, rel) == pieceMap[rel]))
				fitHits++;

		// With LPN holes a fit can land between the runs and predict almost
		// nothing. y = x + b on the longest run is then the better model.
		if(bestRunLen > fitHits)
		{
			model->k[piece] = (_Float16)1.0f;
			model->b[piece] = (_Float16)OffsetFromRef(model, bestRunBase);
		}

		for(rel = 0; rel < LFTL_LPNS_PER_PIECE; rel++)
			if((pieceMap[rel] != VSA_NONE) && (PredictPiece(model, piece, rel) == pieceMap[rel]))
			{
				unsigned int x = (piece << LFTL_PIECE_SHIFT) + rel;

				model->bitmap[BitWord(x)] |= BitMask(x);
				predictable++;
			}
	}

	return predictable;
}
