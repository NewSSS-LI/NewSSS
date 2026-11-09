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

static void DftlPutToTransFbList(unsigned int dieNo, unsigned int blockNo)
{
	P_DFTL_TRANS_DIE_ENTRY die = &dftlTransDieMapPtr->die[dieNo];

	if(die->tailFreeBlock != BLOCK_NONE)
	{
		virtualBlockMapPtr->block[dieNo][blockNo].prevBlock = die->tailFreeBlock;
		virtualBlockMapPtr->block[dieNo][blockNo].nextBlock = BLOCK_NONE;
		virtualBlockMapPtr->block[dieNo][die->tailFreeBlock].nextBlock = blockNo;
		die->tailFreeBlock = blockNo;
	}
	else
	{
		virtualBlockMapPtr->block[dieNo][blockNo].prevBlock = BLOCK_NONE;
		virtualBlockMapPtr->block[dieNo][blockNo].nextBlock = BLOCK_NONE;
		die->headFreeBlock = blockNo;
		die->tailFreeBlock = blockNo;
	}
	die->freeBlockCnt++;
}

static unsigned int DftlGetFromTransFbList(unsigned int dieNo)
{
	P_DFTL_TRANS_DIE_ENTRY die = &dftlTransDieMapPtr->die[dieNo];
	unsigned int evictedBlockNo = die->headFreeBlock;

	if(evictedBlockNo == BLOCK_NONE)
		return BLOCK_FAIL;

	if(virtualBlockMapPtr->block[dieNo][evictedBlockNo].nextBlock != BLOCK_NONE)
	{
		die->headFreeBlock = virtualBlockMapPtr->block[dieNo][evictedBlockNo].nextBlock;
		virtualBlockMapPtr->block[dieNo][die->headFreeBlock].prevBlock = BLOCK_NONE;
	}
	else
	{
		die->headFreeBlock = BLOCK_NONE;
		die->tailFreeBlock = BLOCK_NONE;
	}

	virtualBlockMapPtr->block[dieNo][evictedBlockNo].free = 0;
	virtualBlockMapPtr->block[dieNo][evictedBlockNo].nextBlock = BLOCK_NONE;
	virtualBlockMapPtr->block[dieNo][evictedBlockNo].prevBlock = BLOCK_NONE;
	die->freeBlockCnt--;

	return evictedBlockNo;
}

static void DftlEraseTransBlock(unsigned int dieNo, unsigned int blockNo)
{
	unsigned int reqSlotTag = GetFromFreeReqQ();

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

	virtualBlockMapPtr->block[dieNo][blockNo].free = 1;
	virtualBlockMapPtr->block[dieNo][blockNo].eraseCnt++;
	virtualBlockMapPtr->block[dieNo][blockNo].invalidSliceCnt = 0;
	virtualBlockMapPtr->block[dieNo][blockNo].currentPage = 0;

	DftlPutToTransFbList(dieNo, blockNo);
}

static unsigned int DftlAllocTransSlice(unsigned int dieNo)
{
	P_DFTL_TRANS_DIE_ENTRY die = &dftlTransDieMapPtr->die[dieNo];
	unsigned int currentBlock = die->currentBlock;
	unsigned int vsa;

	if(virtualBlockMapPtr->block[dieNo][currentBlock].currentPage == USER_PAGES_PER_BLOCK)
	{
		currentBlock = DftlGetFromTransFbList(dieNo);
		if(currentBlock == BLOCK_FAIL)
		{
			DftlGcTranslationBlock(dieNo);
			currentBlock = DftlGetFromTransFbList(dieNo);
			if(currentBlock == BLOCK_FAIL)
				assert(!"[WARNING] DFTL: no free translation block available [WARNING]");
		}
		die->currentBlock = currentBlock;
	}

	vsa = Vorg2VsaTranslation(dieNo, currentBlock, virtualBlockMapPtr->block[dieNo][currentBlock].currentPage);
	virtualBlockMapPtr->block[dieNo][currentBlock].currentPage++;

	return vsa;
}

void DftlGtdInit(void)
{
	unsigned int mvpn, dieNo, i, blockNo;

	dftlGtdPtr = (P_DFTL_GTD) DFTL_GTD_ADDR;
	dftlTransDieMapPtr = (P_DFTL_TRANS_DIE_MAP) DFTL_TRANS_DIE_MAP_ADDR;

	for(mvpn = 0; mvpn < DFTL_NUM_TRANS_PAGES; mvpn++)
		dftlGtdPtr->entry[mvpn].mppn = DFTL_GTD_NONE;

	for(dieNo = 0; dieNo < USER_DIES; dieNo++)
	{
		dftlTransDieMapPtr->die[dieNo].headFreeBlock = BLOCK_NONE;
		dftlTransDieMapPtr->die[dieNo].tailFreeBlock = BLOCK_NONE;
		dftlTransDieMapPtr->die[dieNo].freeBlockCnt = 0;

		// carve DFTL_TRANS_BLOCKS_PER_DIE blocks out of the (already fully
		// populated, per InitBlockDieMap()) per-die data free list and
		// dedicate them to translation-page storage.
		for(i = 0; i < DFTL_TRANS_BLOCKS_PER_DIE; i++)
		{
			blockNo = GetFromFbList(dieNo, GET_FREE_BLOCK_GC);
			if(blockNo == BLOCK_FAIL)
				assert(!"[WARNING] DFTL: not enough free blocks to reserve translation pool [WARNING]");

			dftlTransDieMapPtr->die[dieNo].blockList[i] = blockNo;
			DftlPutToTransFbList(dieNo, blockNo);
		}

		dftlTransDieMapPtr->die[dieNo].currentBlock = DftlGetFromTransFbList(dieNo);
		if(dftlTransDieMapPtr->die[dieNo].currentBlock == BLOCK_FAIL)
			assert(!"[WARNING] DFTL: translation block pool init failed [WARNING]");
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

void DftlGtdWritebackMapping(unsigned int lpn, unsigned int vsa)
{
	unsigned int mvpn = DftlLpn2Mvpn(lpn);
	unsigned int offset = DftlLpn2Offset(lpn);
	unsigned int oldMppn = dftlGtdPtr->entry[mvpn].mppn;
	unsigned int dieNo = DftlMvpn2Die(mvpn);
	unsigned int newMppn;
	unsigned int *buf = (unsigned int*) DFTL_TRANS_STAGING_BUF_ADDR;
	unsigned int i;

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

	newMppn = DftlAllocTransSlice(dieNo);
	WriteTransPage(newMppn, DFTL_TRANS_STAGING_BUF_ADDR);

	dftlGtdPtr->entry[mvpn].mppn = newMppn;
}

void DftlGcTranslationBlock(unsigned int dieNo)
{
	unsigned int i, victimBlock, maxInvalid, pageNo, pageCnt, mvpn, vsaOfPage, newMppn;
	unsigned int *buf = (unsigned int*) DFTL_TRANS_STAGING_BUF_ADDR;

	victimBlock = BLOCK_NONE;
	maxInvalid = 0;

	for(i = 0; i < DFTL_TRANS_BLOCKS_PER_DIE; i++)
	{
		unsigned int b = dftlTransDieMapPtr->die[dieNo].blockList[i];

		if(b == dftlTransDieMapPtr->die[dieNo].currentBlock)
			continue;
		if(virtualBlockMapPtr->block[dieNo][b].free)
			continue;
		if(virtualBlockMapPtr->block[dieNo][b].invalidSliceCnt >= maxInvalid)
		{
			maxInvalid = virtualBlockMapPtr->block[dieNo][b].invalidSliceCnt;
			victimBlock = b;
		}
	}

	if(victimBlock == BLOCK_NONE)
		assert(!"[WARNING] DFTL: translation block GC found no reclaimable victim [WARNING]");

	pageCnt = virtualBlockMapPtr->block[dieNo][victimBlock].currentPage;
	for(pageNo = 0; pageNo < pageCnt; pageNo++)
	{
		vsaOfPage = Vorg2VsaTranslation(dieNo, victimBlock, pageNo);
		ReadTransPage(vsaOfPage, DFTL_TRANS_STAGING_BUF_ADDR);
		mvpn = buf[0];

		// still the live copy for this mvpn? (GTD may already point
		// elsewhere if it was overwritten again after this copy was made)
		if(mvpn < DFTL_NUM_TRANS_PAGES && dftlGtdPtr->entry[mvpn].mppn == vsaOfPage)
		{
			newMppn = DftlAllocTransSlice(dieNo);
			WriteTransPage(newMppn, DFTL_TRANS_STAGING_BUF_ADDR);
			dftlGtdPtr->entry[mvpn].mppn = newMppn;
		}
	}

	DftlEraseTransBlock(dieNo, victimBlock);
}
