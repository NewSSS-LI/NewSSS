//////////////////////////////////////////////////////////////////////////////////
// dftl_gtd.c for Cosmos+ OpenSSD
//
// See dftl_gtd.h. Translation page layout in the staging buffer (32-bit
// words): word[0] = this page's own MVPN (identity, used by GC to tell
// whether a copy is still the live one via the GTD); word[1 + offset] =
// VSA for LPN (mvpn * DFTL_MAPPINGS_PER_TRANS_PAGE + offset), or VSA_NONE
// if that LPN has never been written.
//
// All NAND I/O here is synchronous because the code inspects/patches buffer
// contents between steps -- unlike GreedyFTL's GC valid-page-copy, which never
// looks at the data it moves. Each access waits for its own request only
// (WaitForRequest), not for every queue in the SSD (SyncAllLowLevelReqDone),
// so requests already running on other dies keep going.
//////////////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include "dftl_gtd.h"
#include "dftl_cmt.h"
#include "memory_map.h"

P_DFTL_GTD dftlGtdPtr;
P_DFTL_TRANS_DIE_MAP dftlTransDieMapPtr;

#define DftlMvpn2Die(mvpn)	((mvpn) % USER_DIES)

// Poll until this one request has completed and returned to the free queue.
// The scheduling calls never allocate a request slot, so the slot cannot be
// reused while it is polled. Same approach as LearnedFTL's lftl_port_cosmos.c.
static void WaitForRequest(unsigned int reqSlotTag)
{
	while(reqPoolPtr->reqPool[reqSlotTag].reqQueueType != REQ_QUEUE_TYPE_FREE)
	{
		CheckDoneNvmeDmaReq();
		SchedulingNandReq();
	}
}

static void ReadTransPage(unsigned int vsa, unsigned int bufAddr)
{
	unsigned int reqSlotTag = GetFromFreeReqQ();

	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ADDR;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
	reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr = bufAddr;
	reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = vsa;

	SelectLowLevelReqQ(reqSlotTag);
	WaitForRequest(reqSlotTag);
}

static void WriteTransPage(unsigned int vsa, unsigned int bufAddr)
{
	unsigned int reqSlotTag = GetFromFreeReqQ();

	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ADDR;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
	reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr = bufAddr;
	reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = vsa;

	SelectLowLevelReqQ(reqSlotTag);
	WaitForRequest(reqSlotTag);
}

// A block from the die's normal free list becomes the translation current block.
static void DftlOpenTransBlock(unsigned int dieNo)
{
	P_DFTL_TRANS_DIE_ENTRY die = &dftlTransDieMapPtr->die[dieNo];
	unsigned int blockNo;

	if(die->blockCnt == DFTL_TRANS_MAX_BLOCKS_PER_DIE)
		assert(!"[WARNING] DFTL: too many translation blocks on a die [WARNING]");

	blockNo = GetFromFbList(dieNo, GET_FREE_BLOCK_GC);
	if(blockNo == BLOCK_FAIL)
		assert(!"[WARNING] DFTL: no free block for translation pages [WARNING]");

	die->blockList[die->blockCnt++] = blockNo;
	die->currentBlock = blockNo;
}

unsigned int DftlTransBlockCount(unsigned int dieNo)
{
	return dftlTransDieMapPtr->die[dieNo].blockCnt;
}

static void DftlEraseTransBlock(unsigned int dieNo, unsigned int blockNo)
{
	unsigned int pageNo, reqSlotTag = GetFromFreeReqQ();

	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_ERASE;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_NONE;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
	reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
	reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = Vorg2VsaTranslation(dieNo, blockNo, 0);
	reqPoolPtr->reqPool[reqSlotTag].nandInfo.programmedPageCnt = virtualBlockMapPtr->block[dieNo][blockNo].currentPage;

	SelectLowLevelReqQ(reqSlotTag);
	WaitForRequest(reqSlotTag);

	// Same state as a data block after EraseBlock, then back to the common pool.
	virtualBlockMapPtr->block[dieNo][blockNo].free = 1;
	virtualBlockMapPtr->block[dieNo][blockNo].eraseCnt++;
	virtualBlockMapPtr->block[dieNo][blockNo].invalidSliceCnt = 0;
	virtualBlockMapPtr->block[dieNo][blockNo].currentPage = 0;
	for(pageNo = 0; pageNo < USER_PAGES_PER_BLOCK; pageNo++)
		virtualSliceMapPtr->virtualSlice[Vorg2VsaTranslation(dieNo, blockNo, pageNo)].logicalSliceAddr = LSA_NONE;

	PutToFbList(dieNo, blockNo);
}

static unsigned int DftlTakeTransSlice(unsigned int dieNo)
{
	P_DFTL_TRANS_DIE_ENTRY die = &dftlTransDieMapPtr->die[dieNo];
	unsigned int vsa;

	vsa = Vorg2VsaTranslation(dieNo, die->currentBlock, virtualBlockMapPtr->block[dieNo][die->currentBlock].currentPage);
	virtualBlockMapPtr->block[dieNo][die->currentBlock].currentPage++;

	return vsa;
}

// Runs translation GC only (never data GC), so it is safe inside data GC's batch
// update. Below the free superblock watermark the die's translation blocks are
// collected when there are at least two (the GC frees all but one); with
// enough free blocks they are only collected at the bookkeeping bound.
static unsigned int DftlAllocTransSlice(unsigned int dieNo)
{
	P_DFTL_TRANS_DIE_ENTRY die = &dftlTransDieMapPtr->die[dieNo];

	if(virtualBlockMapPtr->block[dieNo][die->currentBlock].currentPage == USER_PAGES_PER_BLOCK)
	{
		if(((FreeSuperblockCount() <= GC_FREE_SUPERBLOCK_WATERMARK) && (die->blockCnt >= 2)) ||
			(die->blockCnt == DFTL_TRANS_MAX_BLOCKS_PER_DIE))
			DftlGcTranslationBlock(dieNo);
		if(virtualBlockMapPtr->block[dieNo][die->currentBlock].currentPage == USER_PAGES_PER_BLOCK)
			DftlOpenTransBlock(dieNo);
	}

	return DftlTakeTransSlice(dieNo);
}

void DftlGtdInit(void)
{
	unsigned int mvpn, dieNo;

	dftlGtdPtr = (P_DFTL_GTD) DFTL_GTD_ADDR;
	dftlTransDieMapPtr = (P_DFTL_TRANS_DIE_MAP) DFTL_TRANS_DIE_MAP_ADDR;

	for(mvpn = 0; mvpn < DFTL_NUM_TRANS_PAGES; mvpn++)
		dftlGtdPtr->entry[mvpn].mppn = DFTL_GTD_NONE;

	// Each die starts with one translation block from its free list; more are
	// taken when needed (DftlAllocTransSlice).
	for(dieNo = 0; dieNo < USER_DIES; dieNo++)
	{
		dftlTransDieMapPtr->die[dieNo].blockCnt = 0;
		DftlOpenTransBlock(dieNo);
	}
}

unsigned int DftlGtdLookupMapping(unsigned int lpn)
{
	unsigned int mvpn = DftlLpn2Mvpn(lpn);
	unsigned int offset = DftlLpn2Offset(lpn);
	unsigned int mppn = dftlGtdPtr->entry[mvpn].mppn;
	unsigned int *buf = (unsigned int*) DFTL_TRANS_STAGING_BUF_ADDR;

	if(mppn == DFTL_GTD_NONE)
		return VSA_NONE;	// this lpn's translation page has never been written

	ReadTransPage(mppn, DFTL_TRANS_STAGING_BUF_ADDR);

	return buf[1 + offset];
}

unsigned int DftlGtdFetchMappings(unsigned int lpn, unsigned int endLpn, unsigned int *vsa)
{
	unsigned int mppn = dftlGtdPtr->entry[DftlLpn2Mvpn(lpn)].mppn;
	unsigned int *buf = (unsigned int*) DFTL_TRANS_STAGING_BUF_ADDR;
	unsigned int i, offset = DftlLpn2Offset(lpn);

	if(mppn == DFTL_GTD_NONE)
		return 0;

	ReadTransPage(mppn, DFTL_TRANS_STAGING_BUF_ADDR);
	for(i = 0; i <= endLpn - lpn; i++)
		vsa[i] = buf[1 + offset + i];

	return endLpn - lpn + 1;
}

void DftlGtdWritebackMapping(unsigned int lpn, unsigned int vsa)
{
	unsigned int mvpn = DftlLpn2Mvpn(lpn);
	unsigned int offset = DftlLpn2Offset(lpn);
	unsigned int dieNo = DftlMvpn2Die(mvpn);
	unsigned int oldMppn, newMppn;
	unsigned int *buf = (unsigned int*) DFTL_TRANS_STAGING_BUF_ADDR;
	unsigned int i;

	// Allocate first: it may run translation GC, which uses the staging buffer and
	// may move this mvpn's page, so the GTD entry is read after it.
	newMppn = DftlAllocTransSlice(dieNo);
	oldMppn = dftlGtdPtr->entry[mvpn].mppn;

	if(oldMppn != DFTL_GTD_NONE)
	{
		unsigned int oldDie = Vsa2VdieTranslation(oldMppn);
		unsigned int oldBlock = Vsa2VblockTranslation(oldMppn);

		ReadTransPage(oldMppn, DFTL_TRANS_STAGING_BUF_ADDR);	// read-modify-write
		virtualBlockMapPtr->block[oldDie][oldBlock].invalidSliceCnt++;
	}
	else
	{
		buf[0] = mvpn;
		for(i = 1; i <= DFTL_MAPPINGS_PER_TRANS_PAGE; i++)
			buf[i] = VSA_NONE;
	}

	buf[1 + offset] = vsa;

	// Batch update: every other dirty cached entry of
	// this translation page goes into the same program and becomes clean, so
	// evicting them later costs no further translation page write.
	DftlCmtTakeDirtyOfTp(mvpn, buf);

	WriteTransPage(newMppn, DFTL_TRANS_STAGING_BUF_ADDR);

	dftlGtdPtr->entry[mvpn].mppn = newMppn;
}

// GC batch update (DFTL paper Section 3.3): the new VSAs of pages that one GC run
// moved and whose mappings are not cached, all of translation page mvpn, written
// with one read and one write. Dirty cached entries of the page go along as in
// DftlGtdWritebackMapping.
void DftlGtdUpdateMappings(unsigned int mvpn, const unsigned int *offset, const unsigned int *vsa, unsigned int count)
{
	unsigned int dieNo = DftlMvpn2Die(mvpn);
	unsigned int oldMppn, newMppn;
	unsigned int *buf = (unsigned int*) DFTL_TRANS_STAGING_BUF_ADDR;
	unsigned int i;

	// Allocate first: it may run translation GC, which uses the staging buffer.
	newMppn = DftlAllocTransSlice(dieNo);
	oldMppn = dftlGtdPtr->entry[mvpn].mppn;

	if(oldMppn != DFTL_GTD_NONE)
	{
		ReadTransPage(oldMppn, DFTL_TRANS_STAGING_BUF_ADDR);
		virtualBlockMapPtr->block[Vsa2VdieTranslation(oldMppn)][Vsa2VblockTranslation(oldMppn)].invalidSliceCnt++;
	}
	else
	{
		buf[0] = mvpn;
		for(i = 1; i <= DFTL_MAPPINGS_PER_TRANS_PAGE; i++)
			buf[i] = VSA_NONE;
	}

	for(i = 0; i < count; i++)
		buf[1 + offset[i]] = vsa[i];

	DftlCmtTakeDirtyOfTp(mvpn, buf);

	WriteTransPage(newMppn, DFTL_TRANS_STAGING_BUF_ADDR);
	dftlGtdPtr->entry[mvpn].mppn = newMppn;
}

// The live pages are found through the GTD (every translation page of this die
// is mvpn % USER_DIES == dieNo), so dead pages are not read. They fit in the new
// block: a die holds DFTL_NUM_TRANS_PAGES / USER_DIES of them at most.
void DftlGcTranslationBlock(unsigned int dieNo)
{
	P_DFTL_TRANS_DIE_ENTRY die = &dftlTransDieMapPtr->die[dieNo];
	unsigned int oldBlock[DFTL_TRANS_MAX_BLOCKS_PER_DIE];
	unsigned int i, mvpn, mppn, oldCnt = die->blockCnt, newMppn;

	for(i = 0; i < oldCnt; i++)
		oldBlock[i] = die->blockList[i];
	die->blockCnt = 0;
	DftlOpenTransBlock(dieNo);

	for(mvpn = dieNo; mvpn < DFTL_NUM_TRANS_PAGES; mvpn += USER_DIES)
	{
		mppn = dftlGtdPtr->entry[mvpn].mppn;
		if(mppn == DFTL_GTD_NONE)
			continue;
		if(Vsa2VdieTranslation(mppn) != dieNo)
			assert(!"[WARNING] DFTL: translation page on the wrong die [WARNING]");

		for(i = 0; i < oldCnt; i++)
			if(Vsa2VblockTranslation(mppn) == oldBlock[i])
				break;
		if(i == oldCnt)
			continue;	// already in the new block

		if(virtualBlockMapPtr->block[dieNo][die->currentBlock].currentPage == USER_PAGES_PER_BLOCK)
			assert(!"[WARNING] DFTL: live translation pages exceed one block [WARNING]");
		ReadTransPage(mppn, DFTL_TRANS_STAGING_BUF_ADDR);
		newMppn = DftlTakeTransSlice(dieNo);
		WriteTransPage(newMppn, DFTL_TRANS_STAGING_BUF_ADDR);
		dftlGtdPtr->entry[mvpn].mppn = newMppn;
	}

	for(i = 0; i < oldCnt; i++)
		DftlEraseTransBlock(dieNo, oldBlock[i]);
}
