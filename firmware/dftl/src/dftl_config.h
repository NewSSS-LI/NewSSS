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
#define DFTL_CMT_CAPACITY				4096

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
// Translation block pool.
//
// A small, fixed number of blocks per die is reserved exclusively for
// translation pages (mirrors the paper's "Translation Blocks" vs.
// "Data Blocks" split, Section 3.1). Reserved at InitAddressMap() time by
// pulling blocks out of the normal per-die free list before any data is
// written. Sized with headroom over DFTL_NUM_TRANS_PAGES so translation-block
// GC has room to operate (out-of-place updates need slack, same as data
// blocks).
//------------------------------------------------------------------
#define DFTL_TRANS_BLOCKS_PER_DIE		4

#endif /* DFTL_CONFIG_H_ */
