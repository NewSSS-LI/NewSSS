//////////////////////////////////////////////////////////////////////////////////
// lftl_port.h for Cosmos+ OpenSSD
//
// Everything lftl_core.c needs from the platform. lftl_port_cosmos.c implements
// it on the board; test/sim_port.c implements it on a PC so the same core code
// can be run against a simulated NAND with a data integrity check.
//////////////////////////////////////////////////////////////////////////////////

#ifndef LFTL_PORT_H_
#define LFTL_PORT_H_

#include "lftl_config.h"

// One translation page sized, DMA-safe buffer.
unsigned int *LftlPortStagingBuf(void);

// Both return only after the NAND operation has completed, while the rest of the
// firmware's queues keep making progress.
void LftlPortReadPageSync(unsigned int vsa);
void LftlPortWritePageSync(unsigned int vsa);

// Queue a valid page copy. Returns without waiting.
void LftlPortCopyPage(unsigned int srcVsa, unsigned int dstVsa, unsigned int lsa);

// Queue the erase of one block. programmedPageCnt must equal the number of pages
// issued to the block, otherwise the row address dependency check never passes.
void LftlPortEraseBlock(unsigned int dieNo, unsigned int blockNo, unsigned int programmedPageCnt);

unsigned int LftlPortLineIsBad(unsigned int lineNo);

// Reverse map (VSA -> LSA). Stored in DRAM, as in GreedyFTL.
unsigned int LftlPortP2lGet(unsigned int vsa);
void LftlPortP2lSet(unsigned int vsa, unsigned int lsa);

// Shadow forward map, used only when LFTL_SHADOW_CHECK is on.
unsigned int LftlPortShadowGet(unsigned int lsa);
void LftlPortShadowSet(unsigned int lsa, unsigned int vsa);

unsigned long long LftlPortTicks(void);
unsigned int LftlPortTicksPerMs(void);

void LftlPortPrintLine(const char *label, unsigned int value);
void LftlPortFatal(const char *message, unsigned int value);

#endif /* LFTL_PORT_H_ */
