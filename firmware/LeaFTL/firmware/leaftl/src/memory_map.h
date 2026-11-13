//////////////////////////////////////////////////////////////////////////////////
// memory_map.h for Cosmos+ OpenSSD
// Copyright (c) 2017 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Static Memory Allocator
// File Name: memory_map.h
//
// Version: v1.0.0
//
// Description:
//	 - allocate DRAM address space (0x0010_0000 ~ 0x3FFF_FFFF) to each module
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#ifndef MEMORY_MAP_H_
#define MEMORY_MAP_H_

#include "data_buffer.h"
#include "address_translation.h"
#include "request_allocation.h"
#include "request_schedule.h"
#include "request_transform.h"
#include "garbage_collection.h"
#include "dftl_cmt.h"
#include "dftl_gtd.h"
#include "leaftl_memory.h"

#define DRAM_START_ADDR					0x00100000

#define MEMORY_SEGMENTS_START_ADDR		DRAM_START_ADDR
#define MEMORY_SEGMENTS_END_ADDR		0x001FFFFF

#define NVME_MANAGEMENT_START_ADDR		0x00200000
#define NVME_MANAGEMENT_END_ADDR		0x002FFFFF

#define RESERVED0_START_ADDR			0x00300000
#define RESERVED0_END_ADDR				0x0FFFFFFF

#define FTL_MANAGEMENT_START_ADDR		0x10000000
// Uncached & Unbuffered
//for data buffer
#define DATA_BUFFER_BASE_ADDR 					0x10000000
#define TEMPORARY_DATA_BUFFER_BASE_ADDR			(DATA_BUFFER_BASE_ADDR + AVAILABLE_DATA_BUFFER_ENTRY_COUNT * BYTES_PER_DATA_REGION_OF_SLICE)
#define SPARE_DATA_BUFFER_BASE_ADDR				(TEMPORARY_DATA_BUFFER_BASE_ADDR + AVAILABLE_TEMPORARY_DATA_BUFFER_ENTRY_COUNT * BYTES_PER_DATA_REGION_OF_SLICE)
#define TEMPORARY_SPARE_DATA_BUFFER_BASE_ADDR	(SPARE_DATA_BUFFER_BASE_ADDR + AVAILABLE_DATA_BUFFER_ENTRY_COUNT * BYTES_PER_SPARE_REGION_OF_SLICE)
#define RESERVED_DATA_BUFFER_BASE_ADDR 			(TEMPORARY_SPARE_DATA_BUFFER_BASE_ADDR + AVAILABLE_TEMPORARY_DATA_BUFFER_ENTRY_COUNT * BYTES_PER_SPARE_REGION_OF_SLICE)
//for nand request completion
#define COMPLETE_FLAG_TABLE_ADDR			0x17000000
#define STATUS_REPORT_TABLE_ADDR			(COMPLETE_FLAG_TABLE_ADDR + sizeof(COMPLETE_FLAG_TABLE))
#define ERROR_INFO_TABLE_ADDR				(STATUS_REPORT_TABLE_ADDR + sizeof(STATUS_REPORT_TABLE))
#define TEMPORARY_PAY_LOAD_ADDR				(ERROR_INFO_TABLE_ADDR+ sizeof(ERROR_INFO_TABLE))
// cached & buffered
// for buffers
#define DATA_BUFFER_MAP_ADDR		 		0x18000000
#define DATA_BUFFFER_HASH_TABLE_ADDR		(DATA_BUFFER_MAP_ADDR + sizeof(DATA_BUF_MAP))
#define TEMPORARY_DATA_BUFFER_MAP_ADDR 		(DATA_BUFFFER_HASH_TABLE_ADDR + sizeof(DATA_BUF_HASH_TABLE))
// for map tables
#define LOGICAL_SLICE_MAP_ADDR				(TEMPORARY_DATA_BUFFER_MAP_ADDR + sizeof(TEMPORARY_DATA_BUF_MAP))
#define VIRTUAL_SLICE_MAP_ADDR				(LOGICAL_SLICE_MAP_ADDR + sizeof(LOGICAL_SLICE_MAP))
#define VIRTUAL_BLOCK_MAP_ADDR				(VIRTUAL_SLICE_MAP_ADDR + sizeof(VIRTUAL_SLICE_MAP))
#define PHY_BLOCK_MAP_ADDR					(VIRTUAL_BLOCK_MAP_ADDR + sizeof(VIRTUAL_BLOCK_MAP))
#define BAD_BLOCK_TABLE_INFO_MAP_ADDR		(PHY_BLOCK_MAP_ADDR + sizeof(PHY_BLOCK_MAP))
#define VIRTUAL_DIE_MAP_ADDR				(BAD_BLOCK_TABLE_INFO_MAP_ADDR + sizeof(BAD_BLOCK_TABLE_INFO_MAP))
// for GC victim selection
#define GC_VICTIM_MAP_ADDR					(VIRTUAL_DIE_MAP_ADDR + sizeof(VIRTUAL_DIE_MAP))
// for request pool
#define REQ_POOL_ADDR						(GC_VICTIM_MAP_ADDR + sizeof(GC_VICTIM_MAP))
// for dependency table
#define ROW_ADDR_DEPENDENCY_TABLE_ADDR		(REQ_POOL_ADDR + sizeof(REQ_POOL))
// for request scheduler
#define DIE_STATE_TABLE_ADDR				(ROW_ADDR_DEPENDENCY_TABLE_ADDR + sizeof(ROW_ADDR_DEPENDENCY_TABLE))
#define RETRY_LIMIT_TABLE_ADDR				(DIE_STATE_TABLE_ADDR + sizeof(DIE_STATE_TABLE))
#define WAY_PRIORITY_TABLE_ADDR 			(RETRY_LIMIT_TABLE_ADDR + sizeof(RETRY_LIMIT_TABLE))

// --- DFTL: CMT + GTD (appended after the existing chain so none of the
// addresses above are disturbed; LOGICAL_SLICE_MAP above is allocated but
// unused by DFTL -- see dftl_cmt.c / dftl_gtd.c for why address_translation.c
// no longer reads/writes it directly) ---
#define DFTL_CMT_NODE_ARRAY_ADDR			(WAY_PRIORITY_TABLE_ADDR + sizeof(WAY_PRIORITY_TABLE))
#define DFTL_CMT_HASH_TABLE_ADDR			(DFTL_CMT_NODE_ARRAY_ADDR + sizeof(DFTL_CMT_NODE_ARRAY))
#define DFTL_GTD_ADDR						(DFTL_CMT_HASH_TABLE_ADDR + sizeof(DFTL_CMT_HASH_TABLE))
#define DFTL_TRANS_DIE_MAP_ADDR				(DFTL_GTD_ADDR + sizeof(DFTL_GTD))

// Preserve all legacy addresses, including the unused full forward map.
#define LEGACY_FTL_END_EXCLUSIVE ((uint64_t)DFTL_TRANS_DIE_MAP_ADDR+sizeof(DFTL_TRANS_DIE_MAP))
#define LEAFTL_MEMORY_BASE_ADDR LEAFTL_RAM_ALIGN(LEGACY_FTL_END_EXCLUSIVE)
#define LEAFTL_MAPPING_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_CONTEXT_OFFSET)
#define LEAFTL_INDEX_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_INDEX_OFFSET)
#define LEAFTL_GROUP_ARRAY_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_GROUPS_OFFSET)
#define LEAFTL_LEVEL_ARRAY_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_LEVELS_OFFSET)
#define LEAFTL_NODE_ARRAY_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_NODES_OFFSET)
#define LEAFTL_WORKSPACE_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_WORKSPACE_OFFSET)
#define LEAFTL_TRAINING_POINTS_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_POINTS_OFFSET)
#define LEAFTL_TRAINED_INPUTS_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_INPUTS_OFFSET)
#define LEAFTL_DIRECTORY_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_DIRECTORY_OFFSET)
#define LEAFTL_FLASH_CONTEXT_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_FLASH_CONTEXT_OFFSET)
#define LEAFTL_FLASH_PAGE_ADDR RESERVED_DATA_BUFFER_BASE_ADDR /* shared uncached staging; exclusive FTL owner */
#define LEAFTL_CACHE_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_CACHE_OFFSET)
#define LEAFTL_METADATA_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_METADATA_OFFSET)
#define LEAFTL_METADATA_BLOCKS_ADDR (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RAM_METADATA_BLOCKS_OFFSET)
#define LEAFTL_CACHE_READ_ADDR (RESERVED_DATA_BUFFER_BASE_ADDR+0x8000U)
#define LEAFTL_METADATA_GC_ADDR (RESERVED_DATA_BUFFER_BASE_ADDR+0x10000U)
#define LEAFTL_CACHE_PROTECTED_ADDR (RESERVED_DATA_BUFFER_BASE_ADDR+0x18000U)
#define LEAFTL_RESOLVE_PAGE_ADDR (RESERVED_DATA_BUFFER_BASE_ADDR+0x20000U)
typedef char LeaFtlResolveScratchFits[(0x20000U+BYTES_PER_DATA_REGION_OF_PAGE+BYTES_PER_SPARE_REGION_OF_PAGE<=0x200000U)?1:-1];
typedef char LeaFtlMetadataPoolFits[(USER_DIES*DFTL_TRANS_BLOCKS_PER_DIE<=LEAFTL_METADATA_BLOCK_CAPACITY)?1:-1];
typedef char LeaFtlBorrowedScratchFits[(0x18000U+LEAFTL_FLASH_MAX_BYTES<=0x200000U)?1:-1];
typedef char LeaFtlMetadataPageGeometry[(LEAFTL_FLASH_PAGE_BYTES==BYTES_PER_DATA_REGION_OF_PAGE)?1:-1];
// Physical slice count is an upper bound on the eventual host-visible LPNs.
#define LEAFTL_RESERVED_RAM_BYTES LEAFTL_RAM_BYTES(SLICES_PER_SSD)
#define LEAFTL_MEMORY_END_EXCLUSIVE (LEAFTL_MEMORY_BASE_ADDR+LEAFTL_RESERVED_RAM_BYTES)
#define FTL_MANAGEMENT_END_ADDR (LEAFTL_MEMORY_END_EXCLUSIVE-1ULL)

// C99 compile-time guards; sizeof is evaluated by the actual target compiler.
typedef char LeaFtlMetadataBudget[(LEAFTL_RESERVED_RAM_BYTES<=LEAFTL_METADATA_RAM_BUDGET_BYTES)?1:-1];
typedef char LeaFtlMetadataInsideDram[(LEAFTL_MEMORY_END_EXCLUSIVE<=0x40000000ULL)?1:-1];
typedef char LeaFtlMetadataCached[(LEAFTL_MEMORY_BASE_ADDR>=DATA_BUFFER_MAP_ADDR)?1:-1];
typedef char LeaFtlDataDmaBoundary[((uint64_t)RESERVED_DATA_BUFFER_BASE_ADDR+0x200000ULL<=COMPLETE_FLAG_TABLE_ADDR)?1:-1];
typedef char LeaFtlCompletionBoundary[((uint64_t)TEMPORARY_PAY_LOAD_ADDR+0x1000ULL<=DATA_BUFFER_MAP_ADDR)?1:-1];
typedef char LeaFtlSortCapacity[(AVAILABLE_DATA_BUFFER_ENTRY_COUNT<=LEAFTL_FLUSH_SORT_CAPACITY)?1:-1];
typedef char LeaFtlCodeNvmeBoundary[(MEMORY_SEGMENTS_END_ADDR<NVME_MANAGEMENT_START_ADDR)?1:-1];


// translation-page staging buffer: reuses RESERVED_DATA_BUFFER_BASE_ADDR
// (uncached/unbuffered, DMA-safe). Safe to share because that address is
// otherwise only touched once, at boot, by RecoverBadBlockTable() --
// before InitFTL() finishes and any host request (hence any DFTL translation
// I/O) can occur.
#define DFTL_TRANS_STAGING_BUF_ADDR			RESERVED_DATA_BUFFER_BASE_ADDR

#define RESERVED1_START_ADDR				(FTL_MANAGEMENT_END_ADDR + 1)
#define RESERVED1_END_ADDR					0x3FFFFFFF

#define DRAM_END_ADDR						0x3FFFFFFF

#endif /* MEMORY_MAP_H_ */
