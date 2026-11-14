#ifndef LEAFTL_SEGMENT_H_
#define LEAFTL_SEGMENT_H_
#include <stdint.h>
#include <stddef.h>
#include "leaftl_crb.h"
typedef enum { LEAFTL_SEGMENT_OK=0, LEAFTL_SEGMENT_MISS=1,
               LEAFTL_SEGMENT_INVALID=-1, LEAFTL_SEGMENT_NO_SPACE=-2 } LEAFTL_SEGMENT_STATUS;

typedef struct { unsigned int lpn; unsigned int ppn; } LEAFTL_TRAINING_POINT;
/* Paper field sizes, in RAM AND serialization: SLPA 1B, L 1B, K 2B, I 4B.
 * ceil(K*group_offset+I); half K's bit 0 is exact/CRB type. I is uint32,
 * NOT float32: even large singleton PPNs remain exactly representable.
 * Training splits when no uint32 I fits. K/I never change during merge.
 * Group ID belongs to the enclosing group, not each stored segment.
 */
typedef struct {
    unsigned char first_offset;
    unsigned char length;
    uint16_t slope_bits;
    uint32_t intercept;
} LEAFTL_SEGMENT;
typedef char LeaFtlSegmentExactly8[(sizeof(LEAFTL_SEGMENT)==8U)?1:-1];
typedef char LeaFtlSegmentLayout[(offsetof(LEAFTL_SEGMENT,length)==1U &&
    offsetof(LEAFTL_SEGMENT,slope_bits)==2U && offsetof(LEAFTL_SEGMENT,intercept)==4U)?1:-1];
#define LEAFTL_SEGMENT_LAST(model) ((unsigned int)(model)->first_offset+(unsigned int)(model)->length)

/* Transient training/insertion payload. Actual memberships are installed
 * in the GROUP CRB for type 1; this bitmap is not retained in index nodes.
 */
typedef struct {
    unsigned int group_id; /* Transient input, NOT stored per index node. */
    LEAFTL_SEGMENT model;
    LEAFTL_MEMBERS members;
} LEAFTL_SEGMENT_INPUT;

uint16_t LeaFtlFloat16Encode(double value);
double LeaFtlFloat16Decode(uint16_t bits);
int LeaFtlSegmentIsCrb(const LEAFTL_SEGMENT *model);
int LeaFtlSegmentValidate(const LEAFTL_SEGMENT *model);
int LeaFtlSegmentInputValidate(const LEAFTL_SEGMENT_INPUT *input);
LEAFTL_SEGMENT_STATUS LeaFtlSegmentMembers(const LEAFTL_SEGMENT *model,
    const LEAFTL_CRB *crb, LEAFTL_MEMBERS *members);
int LeaFtlSegmentContains(const LEAFTL_SEGMENT *model,const LEAFTL_CRB *crb,
    unsigned int group_id,unsigned int lpn);
LEAFTL_SEGMENT_STATUS LeaFtlSegmentPredict(const LEAFTL_SEGMENT *model,
    const LEAFTL_CRB *crb,unsigned int group_id,unsigned int lpn,unsigned int *ppn);
/* Explicit endian-stable 8-byte representation. Decode validates all fields;
 * caller still validates group/LPN limits. No shadow coefficients retained.
 */
LEAFTL_SEGMENT_STATUS LeaFtlSegmentEncode8(const LEAFTL_SEGMENT *model,unsigned char bytes[8]);
LEAFTL_SEGMENT_STATUS LeaFtlSegmentDecode8(const unsigned char bytes[8],LEAFTL_SEGMENT *model);

/* Strictly LPN-sorted unique points, ONE group, count <=256. Floating PLR,
 * binary16 quantization, then +/- configured gamma revalidation of EVERY
 * point. Nonzero-error predictions always use CRB, even for regular LPAs.
 * With gamma>0 the selected model also stays in its allocated PPN point span
 * for partial-flush safety; no hidden coefficient or prediction clamping.
 * Approximate learning stops at nonconsecutive PPN runs; gamma=0 behavior
 * for arbitrary/negative-slope training data remains unchanged.
 * Exact membership is ceil(1/K), otherwise CRB is mandatory (also when
 * prediction error is zero). No fixed-stride fallback or split on holes.
 * Empty succeeds; NULL output with capacity=0 is a size query. On NO_SPACE
 * required count is returned; on INVALID count=0. Output array unchanged
 * on error; buffers/count must be disjoint. No heap or mutable static state.
 */
LEAFTL_SEGMENT_STATUS LeaFtlSegmentTrain(const LEAFTL_TRAINING_POINT *points,
    unsigned int count, LEAFTL_SEGMENT_INPUT *segments,unsigned int capacity,
    unsigned int *segment_count);
#endif
