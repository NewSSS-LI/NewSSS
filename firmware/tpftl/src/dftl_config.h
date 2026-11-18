//////////////////////////////////////////////////////////////////////////////////
// dftl_config.h for Cosmos+ OpenSSD
//
// DFTL (Demand-based Flash Translation Layer) configuration.
// Reference: Gupta, Kim, Urgaonkar, "DFTL: A Flash Translation Layer Employing
// Demand-based Selective Caching of Page-level Address Mappings", ASPLOS'09.
//
// This module replaces GreedyFTL's fully-resident LOGICAL_SLICE_MAP (one
// in-DRAM entry per logical slice, ~32MB for this board's ~118GB capacity)
// with:
//   - CMT (Cached Mapping Table): a small, bounded, in-DRAM cache of the
//     hottest LPN->VSA mappings, evicted with a segmented LRU policy.
//   - GTD (Global Translation Directory): a small, always-resident in-DRAM
//     directory that tracks where each "translation page" (a flash page
//     packing many mapping entries) currently lives on flash.
//   - Translation pages/blocks on flash: hold the full mapping table,
//     analogous to how GreedyFTL's data pages hold host data.
//////////////////////////////////////////////////////////////////////////////////

#ifndef DFTL_CONFIG_H_
#define DFTL_CONFIG_H_

#include "ftl_config.h"

//------------------------------------------------------------------
// CMT (Cached Mapping Table) size.
//
// This is the DRAM budget knob that makes DFTL's demand-paging behavior
// actually happen. Set intentionally smaller than the working set so that
// CMT misses (and their extra translation-page read/write overhead) are
// observable in FIO results, mirroring the paper's own SRAM-size sensitivity
// study (Section 5.6 / Figure 11). Tune and re-run FIO to reproduce that
// experiment for this board.
//
// Must be a power of two (hash table sizing depends on it) and >= 4.
//------------------------------------------------------------------
#define DFTL_CMT_CAPACITY				65536

//------------------------------------------------------------------
// Translation page / GTD layout.
//
// One mapping entry (a VSA) is 4 bytes. The first 4 bytes of each
// translation page's data region store the page's own MVPN (its identity),
// so that garbage collection can identify which GTD slot a copied
// translation page belongs to just by reading the page.
//------------------------------------------------------------------
#define DFTL_BYTES_PER_MAPPING_ENTRY	(sizeof(unsigned int))
#define DFTL_MAPPINGS_PER_TRANS_PAGE	((BYTES_PER_DATA_REGION_OF_PAGE / DFTL_BYTES_PER_MAPPING_ENTRY) - 1)

// number of translation pages (= number of GTD entries = number of MVPNs)
// needed to cover every logical slice on the SSD.
#define DFTL_NUM_TRANS_PAGES			(((SLICES_PER_SSD) + (DFTL_MAPPINGS_PER_TRANS_PAGE) - 1) / (DFTL_MAPPINGS_PER_TRANS_PAGE))

//------------------------------------------------------------------
// Translation blocks.
//
// The paper keeps translation pages in their own blocks (Section 3.1) but
// reserves no fixed number of them. Like the LearnedFTL authors' FEMU code
// (trans_wp taking lines from the common free list), each die's translation
// current block is taken from the die's normal free block list when needed, and
// collected translation blocks go back to it. Translation GC moves the live
// translation pages of all of a die's translation blocks into one new block.
// It runs when free superblocks run low and the die holds
// DFTL_TRANS_GC_BLOCK_THRESHOLD translation blocks (LearnedFTL gc_threshold 5),
// or always when it holds DFTL_TRANS_MAX_BLOCKS_PER_DIE (bookkeeping bound).
//------------------------------------------------------------------
#ifndef DFTL_TRANS_GC_BLOCK_THRESHOLD
#define DFTL_TRANS_GC_BLOCK_THRESHOLD	5
#endif
#define DFTL_TRANS_MAX_BLOCKS_PER_DIE	32

//------------------------------------------------------------------
// TPFTL prefetching (request-level and selective): most mappings loaded by one
// translation page read. Same value as run-lftl's LFTL_PREFETCH_MAX.
//------------------------------------------------------------------
#define DFTL_PREFETCH_MAX				64

#endif /* DFTL_CONFIG_H_ */
