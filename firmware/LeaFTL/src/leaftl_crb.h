#ifndef LEAFTL_CRB_H_
#define LEAFTL_CRB_H_
#include <stdint.h>
#include "leaftl_config.h"

/* Group-local bitmap, used for Algorithm 2's temporary membership sets. */
typedef struct { unsigned char bits[LEAFTL_GROUP_SIZE / 8U]; } LEAFTL_MEMBERS;

/* Paper section 3.4: each record is a contiguous sorted list of actual
 * offsets; records are ordered by their first member (SLPA), and no offset
 * occurs twice. Boundary bits replace ambiguous null-byte delimiters so
 * that BOTH 0 and 255 remain usable offsets. No LPN->PPN map is stored.
 */
typedef struct {
    unsigned char offsets[LEAFTL_GROUP_SIZE];
    unsigned char starts[LEAFTL_GROUP_SIZE / 8U];
    uint16_t count;
    uint16_t records;
} LEAFTL_CRB;

typedef enum { LEAFTL_CRB_OK=0, LEAFTL_CRB_MISS=1,
               LEAFTL_CRB_INVALID=-1 } LEAFTL_CRB_STATUS;
void LeaFtlMembersClear(LEAFTL_MEMBERS *members);
void LeaFtlMembersAdd(LEAFTL_MEMBERS *members, unsigned int offset);
int LeaFtlMembersHas(const LEAFTL_MEMBERS *members, unsigned int offset);
unsigned int LeaFtlMembersCount(const LEAFTL_MEMBERS *members);
void LeaFtlCrbInit(LEAFTL_CRB *crb);
int LeaFtlCrbValidate(const LEAFTL_CRB *crb);
/* Writes are transactional. Put removes these offsets from ALL older
 * records, renames their keys to first remaining member, and inserts the
 * new record. Empty records disappear. Remove handles exact overwrites too.
 */
LEAFTL_CRB_STATUS LeaFtlCrbPut(LEAFTL_CRB *crb, const LEAFTL_MEMBERS *members);
LEAFTL_CRB_STATUS LeaFtlCrbRemove(LEAFTL_CRB *crb, const LEAFTL_MEMBERS *members);
LEAFTL_CRB_STATUS LeaFtlCrbGet(const LEAFTL_CRB *crb, unsigned int start,
                              LEAFTL_MEMBERS *members);
int LeaFtlCrbContains(const LEAFTL_CRB *crb, unsigned int start, unsigned int offset);
#endif
