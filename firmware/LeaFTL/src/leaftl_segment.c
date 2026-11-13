#include <float.h>
#include <math.h>
#include <string.h>
#include "leaftl_segment.h"

#if FLT_RADIX != 2 || DBL_MANT_DIG < 53 || DBL_MAX_EXP < 1024
#error "LeaFTL floating model requires binary double with >=53 significant bits"
#endif
#if defined(__FAST_MATH__)
#error "LeaFTL requires strict floating-point semantics; disable fast-math"
#endif

static unsigned int RoundEvenPositive(double value)
{
    double lower = floor(value);
    double remainder = value - lower;
    unsigned int integer = (unsigned int)lower;
    return integer + (remainder > 0.5 ||
                     (remainder == 0.5 && (integer & 1U)) ? 1U : 0U);
}

uint16_t LeaFtlFloat16Encode(double value)
{
    unsigned int sign = signbit(value) ? 0x8000U : 0U;
    unsigned int fraction;
    int exponent;
    double magnitude;
    double mantissa;
    if (isnan(value))
        return 0x7e00U;
    magnitude = fabs(value);
    if (!isfinite(magnitude) || magnitude >= 65520.0)
        return (uint16_t)(sign | 0x7c00U);
    if (magnitude < 0x1p-14) {
        /* Includes signed zero, subnormal rounding, and carry to normal. */
        fraction = RoundEvenPositive(ldexp(magnitude, 24));
        return (uint16_t)(sign | fraction);
    }
    mantissa = frexp(magnitude, &exponent);
    fraction = RoundEvenPositive(mantissa * 2048.0 - 1024.0);
    exponent += 14;
    if (fraction == 1024U) {
        fraction = 0U;
        ++exponent;
    }
    return (uint16_t)(sign | ((unsigned int)exponent << 10) | fraction);
}

double LeaFtlFloat16Decode(uint16_t bits)
{
    unsigned int exponent = ((unsigned int)bits >> 10) & 31U;
    unsigned int fraction = (unsigned int)bits & 1023U;
    double value;
    if (exponent == 31U)
        value = fraction == 0U ? INFINITY : NAN;
    else if (exponent == 0U)
        value = ldexp((double)fraction, -24);
    else
        value = ldexp((double)(1024U + fraction), (int)exponent - 25);
    return (bits & 0x8000U) ? -value : value;
}

static double Slope(const LEAFTL_SEGMENT *model)
{ return LeaFtlFloat16Decode((uint16_t)(model->slope_bits & 0xfffeU)); }
static double Value(const LEAFTL_SEGMENT *model,unsigned int x)
{ return ceil(Slope(model)*(double)x+model->intercept); }
int LeaFtlSegmentIsCrb(const LEAFTL_SEGMENT *model)
{ return model!=0 && (model->slope_bits & 1U)!=0U; }

static unsigned int ExactStep(const LEAFTL_SEGMENT *model)
{
    double k=Slope(model),step;
    if(!(k>0.0 && k<=1.0)) return 0U;
    step=ceil(1.0/k);
    return step<LEAFTL_GROUP_SIZE ? (unsigned int)step : 0U;
}
int LeaFtlSegmentValidate(const LEAFTL_SEGMENT *model)
{
    double first,last;
    if(model==0 || LEAFTL_SEGMENT_LAST(model)>=LEAFTL_GROUP_SIZE ||
       (model->slope_bits & 0x7c00U)==0x7c00U) return 0;
    if(!LeaFtlSegmentIsCrb(model) && model->first_offset!=LEAFTL_SEGMENT_LAST(model)) {
        unsigned int step=ExactStep(model);
        if(step==0U || (LEAFTL_SEGMENT_LAST(model)-model->first_offset)%step!=0U) return 0;
    }
    first=Value(model,model->first_offset); last=Value(model,LEAFTL_SEGMENT_LAST(model));
    return isfinite(first) && isfinite(last) && first>=0.0 && last>=0.0 &&
           first<(double)LEAFTL_PPN_NONE && last<(double)LEAFTL_PPN_NONE;
}
LEAFTL_SEGMENT_STATUS LeaFtlSegmentMembers(const LEAFTL_SEGMENT *model,
    const LEAFTL_CRB *crb,LEAFTL_MEMBERS *members)
{
    unsigned int x,step;
    if(members==0 || !LeaFtlSegmentValidate(model)) return LEAFTL_SEGMENT_INVALID;
    if(LeaFtlSegmentIsCrb(model)) {
        LEAFTL_MEMBERS temp;
        if(LeaFtlCrbGet(crb,model->first_offset,&temp)!=LEAFTL_CRB_OK ||
           !LeaFtlMembersHas(&temp,LEAFTL_SEGMENT_LAST(model))) return LEAFTL_SEGMENT_INVALID;
        for(x=0U;x<LEAFTL_GROUP_SIZE;++x)
            if(LeaFtlMembersHas(&temp,x) && (x<model->first_offset || x>LEAFTL_SEGMENT_LAST(model)))
                return LEAFTL_SEGMENT_INVALID;
        *members=temp; return LEAFTL_SEGMENT_OK;
    }
    LeaFtlMembersClear(members);
    step=model->first_offset==LEAFTL_SEGMENT_LAST(model) ? 1U : ExactStep(model);
    for(x=model->first_offset;x<=LEAFTL_SEGMENT_LAST(model);x+=step) LeaFtlMembersAdd(members,x);
    return LEAFTL_SEGMENT_OK;
}
int LeaFtlSegmentContains(const LEAFTL_SEGMENT *model,const LEAFTL_CRB *crb,unsigned int group_id,unsigned int lpn)
{
    unsigned int x,step;
    if(!LeaFtlSegmentValidate(model) || lpn==LEAFTL_LPN_NONE ||
       group_id>LEAFTL_GROUP_ID(LEAFTL_LPN_NONE-1U) || LEAFTL_GROUP_ID(lpn)!=group_id ||
       (group_id==LEAFTL_GROUP_ID(LEAFTL_LPN_NONE) && LEAFTL_SEGMENT_LAST(model)==255U)) return 0;
    x=LEAFTL_GROUP_OFFSET(lpn);
    if(x<model->first_offset || x>LEAFTL_SEGMENT_LAST(model)) return 0;
    if(LeaFtlSegmentIsCrb(model)) return LeaFtlCrbContains(crb,model->first_offset,x);
    if(model->first_offset==LEAFTL_SEGMENT_LAST(model)) return 1;
    step=ExactStep(model);
    return (x-model->first_offset)%step==0U;
}
LEAFTL_SEGMENT_STATUS LeaFtlSegmentPredict(const LEAFTL_SEGMENT *model,
    const LEAFTL_CRB *crb,unsigned int group_id,unsigned int lpn,unsigned int *ppn)
{
    if(ppn==0 || !LeaFtlSegmentValidate(model) || lpn==LEAFTL_LPN_NONE ||
       group_id>LEAFTL_GROUP_ID(LEAFTL_LPN_NONE-1U) ||
       (group_id==LEAFTL_GROUP_ID(LEAFTL_LPN_NONE) && LEAFTL_SEGMENT_LAST(model)==255U) ||
       (LeaFtlSegmentIsCrb(model) && crb==0)) return LEAFTL_SEGMENT_INVALID;
    if(!LeaFtlSegmentContains(model,crb,group_id,lpn)) return LEAFTL_SEGMENT_MISS;
    *ppn=(unsigned int)Value(model,LEAFTL_GROUP_OFFSET(lpn)); return LEAFTL_SEGMENT_OK;
}
static int ExactMatches(const LEAFTL_SEGMENT *model,const LEAFTL_MEMBERS *actual)
{
    LEAFTL_MEMBERS inferred;
    LEAFTL_SEGMENT exact=*model;
    exact.slope_bits &= 0xfffeU;
    if(LeaFtlSegmentMembers(&exact,0,&inferred)!=LEAFTL_SEGMENT_OK) return 0;
    return memcmp(&inferred,actual,sizeof(inferred))==0;
}
int LeaFtlSegmentInputValidate(const LEAFTL_SEGMENT_INPUT *input)
{
    unsigned int x;
    if(input==0 || input->group_id>LEAFTL_GROUP_ID(LEAFTL_LPN_NONE-1U) ||
       (input->group_id==LEAFTL_GROUP_ID(LEAFTL_LPN_NONE) && LEAFTL_SEGMENT_LAST(&input->model)==255U) ||
       !LeaFtlSegmentValidate(&input->model) ||
       !LeaFtlMembersHas(&input->members,input->model.first_offset) ||
       !LeaFtlMembersHas(&input->members,LEAFTL_SEGMENT_LAST(&input->model))) return 0;
    for(x=0U;x<LEAFTL_GROUP_SIZE;++x)
        if(LeaFtlMembersHas(&input->members,x) &&
           (x<input->model.first_offset || x>LEAFTL_SEGMENT_LAST(&input->model))) return 0;
    return LeaFtlSegmentIsCrb(&input->model) || ExactMatches(&input->model,&input->members);
}

static int Fit(const LEAFTL_TRAINING_POINT *points,unsigned int count,double lower,
    double upper,LEAFTL_SEGMENT_INPUT *candidate)
{
    unsigned int i,approximate=0U,min_ppn=points[0].ppn,max_ppn=points[0].ppn;
    double il=-DBL_MAX,iu=DBL_MAX,k;
    candidate->model.slope_bits=(uint16_t)(LeaFtlFloat16Encode(lower+(upper-lower)*0.5)&0xfffeU);
    k=Slope(&candidate->model); if(!isfinite(k)) return 0;
    LeaFtlMembersClear(&candidate->members);
    for(i=1U;i<count;++i) {
        if(points[i].ppn<min_ppn) min_ppn=points[i].ppn;
        if(points[i].ppn>max_ppn) max_ppn=points[i].ppn;
    }
    for(i=0U;i<count;++i) {
        double residual=(double)points[i].ppn-k*(double)LEAFTL_GROUP_OFFSET(points[i].lpn);
        double lo=residual-(double)LEAFTL_GAMMA-1.0;
        double hi=residual+(double)LEAFTL_GAMMA;
        if(lo>il) il=lo;
        if(hi<iu) iu=hi;
#if LEAFTL_GAMMA > 0U
        /* Partial flushes may leave unprogrammed block tails. Pick a model
         * whose predictions stay inside this known allocated point span,
         * rather than issuing reads of an unknown future page. No saturation. */
        lo=(double)min_ppn-1.0-k*(double)LEAFTL_GROUP_OFFSET(points[i].lpn);
        hi=(double)max_ppn-k*(double)LEAFTL_GROUP_OFFSET(points[i].lpn);
        if(lo>il) il=lo;
        if(hi<iu) iu=hi;
#endif
        LeaFtlMembersAdd(&candidate->members,LEAFTL_GROUP_OFFSET(points[i].lpn));
    }
    if(!(il<iu)) return 0;
    /* Integer I must satisfy the OPEN lower / CLOSED upper ceil band.
     * No float32 rounding, saturation of predicted addresses, or hidden I.
     */
    {
        double chosen=floor(iu);
        if(chosen>(double)UINT32_MAX) chosen=(double)UINT32_MAX;
        if(chosen<0.0 || !(il<chosen)) return 0;
        candidate->model.intercept=(uint32_t)chosen;
    }
    for(i=0U;i<count;++i) {
        double predicted=Value(&candidate->model,LEAFTL_GROUP_OFFSET(points[i].lpn));
        if(fabs(predicted-(double)points[i].ppn)>(double)LEAFTL_GAMMA) return 0;
        if(predicted!=(double)points[i].ppn) approximate=1U;
    }
    if(approximate || !ExactMatches(&candidate->model,&candidate->members)) candidate->model.slope_bits |= 1U;
    if(!LeaFtlSegmentInputValidate(candidate)) return 0;
    return 1;
}
static unsigned int Build(const LEAFTL_TRAINING_POINT *points,unsigned int count,
    LEAFTL_SEGMENT_INPUT *out)
{
    unsigned int used=1U;
    double lower=-DBL_MAX,upper=DBL_MAX;
    out->model.intercept=points[0].ppn; out->group_id=LEAFTL_GROUP_ID(points[0].lpn);
    out->model.slope_bits=0U; out->model.first_offset=(unsigned char)LEAFTL_GROUP_OFFSET(points[0].lpn);
    out->model.length=0U;
    LeaFtlMembersClear(&out->members); LeaFtlMembersAdd(&out->members,out->model.first_offset);
    while(used<count) {
        unsigned int i;
        LEAFTL_SEGMENT_INPUT candidate=*out;
#if LEAFTL_GAMMA > 0U
        /* Real sorted flushes allocate consecutive usable PPAs. Do not learn
         * an approximate segment across an unallocated physical gap/die run. */
        if(points[used].ppn!=points[used-1U].ppn+1U) break;
#endif
        for(i=0U;i<used;++i) {
            double dx=(double)(points[used].lpn-points[i].lpn);
            double dy=(double)points[used].ppn-(double)points[i].ppn;
            double radius=2.0*(double)LEAFTL_GAMMA+1.0;
            double pl=(dy-radius)/dx,pu=(dy+radius)/dx;
            if(pl>lower) lower=pl;
            if(pu<upper) upper=pu;
        }
        if(!(lower<upper)) break;
        candidate.model.length=(unsigned char)(LEAFTL_GROUP_OFFSET(points[used].lpn)-candidate.model.first_offset);
        if(!Fit(points,used+1U,lower,upper,&candidate)) break;
        *out=candidate; ++used;
    }
    return used;
}
LEAFTL_SEGMENT_STATUS LeaFtlSegmentTrain(const LEAFTL_TRAINING_POINT *points,
    unsigned int count,LEAFTL_SEGMENT_INPUT *segments,unsigned int capacity,unsigned int *segment_count)
{
    unsigned int i,needed=0U;
    LEAFTL_SEGMENT_INPUT temp={0};
    if(segment_count==0) return LEAFTL_SEGMENT_INVALID;
    *segment_count=0U;
    if(count>LEAFTL_MAX_TRAINING_POINTS || (count!=0U && points==0) ||
       (count!=0U && capacity!=0U && segments==0)) return LEAFTL_SEGMENT_INVALID;
    for(i=0U;i<count;++i) if(points[i].lpn==LEAFTL_LPN_NONE || points[i].ppn==LEAFTL_PPN_NONE ||
        LEAFTL_GROUP_ID(points[i].lpn)!=LEAFTL_GROUP_ID(points[0].lpn) ||
        (i!=0U && points[i].lpn<=points[i-1U].lpn)) return LEAFTL_SEGMENT_INVALID;
    for(i=0U;i<count;++needed) i+=Build(points+i,count-i,&temp);
    *segment_count=needed; if(needed>capacity) return LEAFTL_SEGMENT_NO_SPACE;
    needed=0U;
    for(i=0U;i<count;++needed) i+=Build(points+i,count-i,&segments[needed]);
    return LEAFTL_SEGMENT_OK;
}

LEAFTL_SEGMENT_STATUS LeaFtlSegmentEncode8(const LEAFTL_SEGMENT *model,unsigned char bytes[8])
{
    unsigned int i;
    if(bytes==0 || !LeaFtlSegmentValidate(model)) return LEAFTL_SEGMENT_INVALID;
    bytes[0]=model->first_offset; bytes[1]=model->length;
    bytes[2]=(unsigned char)model->slope_bits; bytes[3]=(unsigned char)(model->slope_bits>>8);
    for(i=0U;i<4U;++i) bytes[4U+i]=(unsigned char)(model->intercept>>(8U*i));
    return LEAFTL_SEGMENT_OK;
}
LEAFTL_SEGMENT_STATUS LeaFtlSegmentDecode8(const unsigned char bytes[8],LEAFTL_SEGMENT *model)
{
    LEAFTL_SEGMENT value; unsigned int i;
    if(bytes==0 || model==0) return LEAFTL_SEGMENT_INVALID;
    value.first_offset=bytes[0]; value.length=bytes[1];
    value.slope_bits=(uint16_t)((unsigned int)bytes[2]|((unsigned int)bytes[3]<<8));
    value.intercept=0U;
    for(i=0U;i<4U;++i) value.intercept|=(uint32_t)bytes[4U+i]<<(8U*i);
    if(!LeaFtlSegmentValidate(&value)) return LEAFTL_SEGMENT_INVALID;
    *model=value; return LEAFTL_SEGMENT_OK;
}
