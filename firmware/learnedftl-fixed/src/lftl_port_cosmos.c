//////////////////////////////////////////////////////////////////////////////////
// lftl_port_cosmos.c for Cosmos+ OpenSSD
//
// Board implementation of lftl_port.h on top of the firmware's request pool.
//////////////////////////////////////////////////////////////////////////////////

#include "xil_printf.h"
#include "xtime_l.h"
#include "memory_map.h"
#include "lftl_port.h"

// Translation page buffer. Uncached, DMA-safe, and otherwise used only by the bad
// block table code at boot and at NVMe shutdown, never while translating.
#define LFTL_STAGING_BUF_ADDR	RESERVED_DATA_BUFFER_BASE_ADDR

// Wait for this one request instead of SyncAllLowLevelReqDone(), so a translation
// page access does not have to drain every queue in the SSD. The scheduling calls
// here never allocate a request slot, so the slot cannot be reused while polled.
static void WaitForRequest(unsigned int reqSlotTag)
{
	while(reqPoolPtr->reqPool[reqSlotTag].reqQueueType != REQ_QUEUE_TYPE_FREE)
	{
		CheckDoneNvmeDmaReq();
		SchedulingNandReq();
	}
}

static void IssueTransPageReq(unsigned int vsa, unsigned int reqCode)
{
	unsigned int reqSlotTag = GetFromFreeReqQ();

	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = reqCode;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ADDR;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
	reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr = LFTL_STAGING_BUF_ADDR;
	reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = vsa;

	SelectLowLevelReqQ(reqSlotTag);
	WaitForRequest(reqSlotTag);
}

unsigned int *LftlPortStagingBuf(void)
{
	return (unsigned int *) LFTL_STAGING_BUF_ADDR;
}

void LftlPortReadPageSync(unsigned int vsa)
{
	IssueTransPageReq(vsa, REQ_CODE_READ);
}

void LftlPortWritePageSync(unsigned int vsa)
{
	IssueTransPageReq(vsa, REQ_CODE_WRITE);
}

// Same read + write pair as GreedyFTL's GC, chained through a temporary buffer.
// The source and target die differ here, unlike in GreedyFTL. The buffer is picked
// by target die: per-die page order stays intact and the 16 dies copy in parallel.
void LftlPortCopyPage(unsigned int srcVsa, unsigned int dstVsa, unsigned int lsa)
{
	unsigned int bufEntry = AllocateTempDataBuf(Vsa2VdieTranslation(dstVsa));
	unsigned int reqSlotTag;

	reqSlotTag = GetFromFreeReqQ();
	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
	reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lsa;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_TEMP_ENTRY;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
	reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = bufEntry;
	UpdateTempDataBufEntryInfoBlockingReq(bufEntry, reqSlotTag);
	reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = srcVsa;
	SelectLowLevelReqQ(reqSlotTag);

	reqSlotTag = GetFromFreeReqQ();
	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
	reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lsa;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_TEMP_ENTRY;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
	reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = bufEntry;
	UpdateTempDataBufEntryInfoBlockingReq(bufEntry, reqSlotTag);
	reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = dstVsa;
	SelectLowLevelReqQ(reqSlotTag);
}

void LftlPortEraseBlock(unsigned int dieNo, unsigned int blockNo, unsigned int programmedPageCnt)
{
	unsigned int reqSlotTag = GetFromFreeReqQ();

	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_ERASE;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_NONE;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
	reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = Vorg2VsaTranslation(dieNo, blockNo, 0);
	reqPoolPtr->reqPool[reqSlotTag].nandInfo.programmedPageCnt = programmedPageCnt;

	SelectLowLevelReqQ(reqSlotTag);
}

unsigned int LftlPortLineIsBad(unsigned int lineNo)
{
	unsigned int dieNo;

	for(dieNo = 0; dieNo < USER_DIES; dieNo++)
		if(virtualBlockMapPtr->block[dieNo][lineNo].bad)
			return 1;

	return 0;
}

unsigned int LftlPortP2lGet(unsigned int vsa)
{
	return virtualSliceMapPtr->virtualSlice[vsa].logicalSliceAddr;
}

void LftlPortP2lSet(unsigned int vsa, unsigned int lsa)
{
	virtualSliceMapPtr->virtualSlice[vsa].logicalSliceAddr = lsa;
}

unsigned int LftlPortShadowGet(unsigned int lsa)
{
	return logicalSliceMapPtr->logicalSlice[lsa].virtualSliceAddr;
}

void LftlPortShadowSet(unsigned int lsa, unsigned int vsa)
{
	logicalSliceMapPtr->logicalSlice[lsa].virtualSliceAddr = vsa;
}

unsigned long long LftlPortTicks(void)
{
	XTime now;

	XTime_GetTime(&now);
	return now;
}

unsigned int LftlPortTicksPerMs(void)
{
	return COUNTS_PER_SECOND / 1000;
}

void LftlPortPrintLine(const char *label, unsigned int value)
{
	xil_printf("%s %d\r\n", label, value);
}

void LftlPortFatal(const char *message, unsigned int value)
{
	xil_printf("\r\n[LFTL FATAL] %s %d\r\n", message, value);
	while(1)
		;
}
