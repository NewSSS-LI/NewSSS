//////////////////////////////////////////////////////////////////////////////////
// lftl_model.c for Cosmos+ OpenSSD
//////////////////////////////////////////////////////////////////////////////////

#include "lftl_model.h"

#define PieceOf(x)		((x) >> LFTL_PIECE_SHIFT)
#define RelOf(x)		((x) & (LFTL_LPNS_PER_PIECE - 1))
#define BitWord(x)		((x) >> 5)
#define BitMask(x)		(1u << ((x) & 31))

static inline unsigned int PredictPiece(const LFTL_MODEL *model, unsigned int piece, unsigned int rel)
{
	return model->base[piece] + (((unsigned int)model->k[piece] * rel + (1u << (LFTL_K_SHIFT - 1))) >> LFTL_K_SHIFT);
}

// Division rounded to the nearest integer. den must be positive.
static long long RoundDiv(long long num, long long den)
{
	long long doubled = 2 * num + den;
	long long q = doubled / (2 * den);

	if((doubled < 0) && (doubled % (2 * den) != 0))
		q--;

	return q;
}

void LftlModelReset(P_LFTL_MODEL model)
{
	unsigned int i;

	for(i = 0; i < LFTL_PIECES_PER_GROUP; i++)
	{
		model->base[i] = 0;
		model->k[i] = LFTL_K_ONE;
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

	model->k[piece] = LFTL_K_ONE;
	model->base[piece] = vsa - rel;
	model->bitmap[word] = 0;
	model->bitmap[word + 1] = 0;
	for(i = first; i <= x; i++)
		model->bitmap[BitWord(i)] |= BitMask(i);
}

unsigned int LftlModelTrain(P_LFTL_MODEL model, const unsigned int *map)
{
	unsigned int piece, rel, predictable = 0;

	for(piece = 0; piece < LFTL_PIECES_PER_GROUP; piece++)
	{
		const unsigned int *pieceMap = &map[piece << LFTL_PIECE_SHIFT];
		unsigned int word = piece * (LFTL_LPNS_PER_PIECE / 32);
		long long n = 0, sumX = 0, sumY = 0, sumXX = 0, sumXY = 0, den;
		unsigned int firstVsa = 0, firstRel = 0, prevRel = 0, prevVsa = 0;
		unsigned int runLen = 0, bestRunLen = 0, bestRunBase = 0;
		unsigned int fitHits = 0;
		long long k;

		model->bitmap[word] = 0;
		model->bitmap[word + 1] = 0;
		model->base[piece] = 0;
		model->k[piece] = LFTL_K_ONE;

		// Step 1: map is indexed by LPN, so the points are already sorted.
		for(rel = 0; rel < LFTL_LPNS_PER_PIECE; rel++)
		{
			unsigned int vsa = pieceMap[rel];
			long long y;

			if(vsa == VSA_NONE)
				continue;

			if(n == 0)
			{
				firstVsa = vsa;
				firstRel = rel;
			}

			y = (long long)vsa - (long long)firstVsa;
			n++;
			sumX += rel;
			sumY += y;
			sumXX += rel * rel;
			sumXY += rel * y;

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

		// Step 3: least squares fit, y relative to the first point in the piece.
		den = n * sumXX - sumX * sumX;
		if(den == 0)
		{
			k = LFTL_K_ONE;
			model->base[piece] = firstVsa - firstRel;
		}
		else
		{
			k = RoundDiv((n * sumXY - sumX * sumY) * LFTL_K_ONE, den);
			if(k < 0)
				k = 0;
			if(k > LFTL_K_ONE)
				k = LFTL_K_ONE;
			model->base[piece] = firstVsa + (unsigned int)(int)RoundDiv(sumY * LFTL_K_ONE - k * sumX, n * LFTL_K_ONE);
		}
		model->k[piece] = (unsigned short)k;

		// Step 4: evaluate. Only exact predictions get a bit.
		for(rel = 0; rel < LFTL_LPNS_PER_PIECE; rel++)
			if((pieceMap[rel] != VSA_NONE) && (PredictPiece(model, piece, rel) == pieceMap[rel]))
				fitHits++;

		// With LPN holes a fit can land between the runs and predict almost
		// nothing. y = x + b on the longest run is then the better model.
		if(bestRunLen > fitHits)
		{
			model->k[piece] = LFTL_K_ONE;
			model->base[piece] = bestRunBase;
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
