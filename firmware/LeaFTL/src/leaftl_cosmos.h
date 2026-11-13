#ifndef LEAFTL_COSMOS_H_
#define LEAFTL_COSMOS_H_
#include "leaftl_writeback.h"
/* Learned path by default; override to 0 for DFTL baseline. SDK compiler
 * LEAFTL_GAMMA selects the experiment (stage 16 Debug/Release: gamma=4). */
#ifndef LEAFTL_FIRMWARE_WRITEBACK
#define LEAFTL_FIRMWARE_WRITEBACK 1
#endif
int LeaFtlCosmosActive(void);
int LeaFtlCosmosInit(unsigned int logical_count);
int LeaFtlCosmosFlush(void);
/* Data + dirty mapping write completion only. Directory/allocator remain
 * volatile; no checkpoint, restart recovery or power-loss guarantee. */
int LeaFtlCosmosSync(void);
unsigned int LeaFtlCosmosRead(unsigned int lpn);
void LeaFtlCosmosNandDone(unsigned int tag,unsigned int status);
unsigned int LeaFtlReserveRun(unsigned int requested,unsigned int *first_ppn);
void LeaFtlAbandonRun(void);
void LeaFtlAbandonGcRun(void);
void LeaFtlPublishPage(unsigned int lpn,unsigned int ppn,unsigned int old_ppn);
unsigned int LeaFtlPpnToVsa(unsigned int ppn);
unsigned int LeaFtlVsaToPpn(unsigned int vsa);
LEAFTL_MAPPING_STATUS LeaFtlCosmosLastStatus(void);
int LeaFtlCosmosLoad(unsigned int id,unsigned int lpn);
int LeaFtlCosmosGc(unsigned int die);
int LeaFtlCollectGarbage(unsigned int die);
unsigned int LeaFtlReserveGcRun(unsigned int die,unsigned int n,unsigned int *first);
int LeaFtlCosmosErase(unsigned int die,unsigned int block);
void LeaFtlFreeErasedBlock(unsigned int die,unsigned int block);
#endif
