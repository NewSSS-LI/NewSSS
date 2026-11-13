#ifndef LEAFTL_OOB_H_
#define LEAFTL_OOB_H_
#include <stddef.h>
#include "leaftl_config.h"
/* Keep the first aligned word erased, including potential bad-block marks.
 * This is within the controller's USER spare, never physical ECC parity. */
#define LEAFTL_OOB_RESERVED_BYTES 4U
#define LEAFTL_OOB_BYTES(gamma) (LEAFTL_OOB_RESERVED_BYTES+4U*(2U*(gamma)+1U))
typedef enum { LEAFTL_OOB_OK=0,LEAFTL_OOB_MISS=1,LEAFTL_OOB_INVALID=-1 } LEAFTL_OOB_STATUS;
/* Getter returns actual LPN of a known allocated PPN, or LPN_NONE for an
 * unknown/unallocated neighbor. Active sorted run includes future programs.
 * Physical neighbors must share one die-major block (never VSA +/- 1).
 * Output is LE LPNs in slot order [-gamma..+gamma], with NONE padding.
 * Center must be known; invalid arguments leave spare unchanged. */
LEAFTL_OOB_STATUS LeaFtlOobBuild(unsigned char *spare,unsigned int user_spare_bytes,
    unsigned int center_ppn,unsigned int pages_per_block,unsigned int gamma,
    unsigned int (*lpn_at)(void *,unsigned int ppn),void *context);
/* Decode reverse neighbor lookup for stage 16. Structural validation only;
 * NAND ECC/status validation remains mandatory before trusting these bytes.
 * Ambiguous matches/out-of-block populated slots are rejected, not guessed. */
LEAFTL_OOB_STATUS LeaFtlOobFind(const unsigned char *spare,unsigned int user_spare_bytes,
    unsigned int center_ppn,unsigned int pages_per_block,unsigned int gamma,
    unsigned int lpn,unsigned int *actual_ppn);
#endif
