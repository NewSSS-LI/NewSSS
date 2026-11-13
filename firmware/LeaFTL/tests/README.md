## Current stages 18/19 validation

Run `python tests/run_firmware_validation.py` from the project root on Windows
with MSVC Build Tools + Windows SDK installed (optional `--gammas 4`). This uses
actual firmware C, mocked NAND/DMA, and checks NVMe FLUSH/FUA completion order,
failure/retry, OOB correction, GC, RMW, cache eviction and block/group boundaries.
It does not access an SSD. Logs/executables go into `%TEMP%/LeaFTL-host-validation`.

Stage 19 real ARM gamma=4 and DFTL-selection builds passed. Board results are
pending. Earlier SDK-pending statements below are historical.

Experiment scope: FLUSH/FUA waits for data + mapping writes, with NO restart or
power-loss recovery. Firmware boot still initializes NAND. Do not test restart
durability with this build. Inspect `LeaFtlStatsGet()` / `LeaFtlStatsMemory()` via
SDK debugger; reset counters only at an idle measurement boundary. `gc_programs`
is included in `data_programs`. Compare only error-free intervals. Stats use 96B.
For board comparison, record identical capacity/data-cache/workload and the
mapping DRAM budgets, fio JSON IOPS/bandwidth/latency, successful-program WAF and
all retained legacy memory. DFTL-selection build alone is not a performance test.

---

# LeaFTL host verification — paper-aligned membership and merge

Run from this `tests` directory in LeaFTL-Work. The production modules are
BSP-independent; tests and reference PPN maps are never linked into firmware.

## Build and run (Developer PowerShell for Visual Studio, x64)

```powershell
New-Item -ItemType Directory -Force build | Out-Null
cl /nologo /TC /std:c11 /W4 /WX /O2 /fp:strict /I ..\src ..\src\leaftl_crb.c test_leaftl_crb.c /Fo:build\ /Fe:build\test_leaftl_crb.exe
if ($LASTEXITCODE -ne 0) { throw 'CRB build failed' }
& .\build\test_leaftl_crb.exe
if ($LASTEXITCODE -ne 0) { throw 'CRB tests failed' }
cl /nologo /TC /std:c11 /W4 /WX /O2 /fp:strict /I ..\src ..\src\leaftl_crb.c ..\src\leaftl_segment.c test_leaftl_segment.c /Fo:build\ /Fe:build\test_leaftl_segment.exe
if ($LASTEXITCODE -ne 0) { throw 'Segment build failed' }
& .\build\test_leaftl_segment.exe
if ($LASTEXITCODE -ne 0) { throw 'Segment tests failed' }
cl /nologo /TC /std:c11 /W4 /WX /O2 /fp:strict /I ..\src ..\src\leaftl_crb.c ..\src\leaftl_segment.c ..\src\leaftl_index.c test_leaftl_index.c /Fo:build\ /Fe:build\test_leaftl_index.exe
if ($LASTEXITCODE -ne 0) { throw 'Index build failed' }
& .\build\test_leaftl_index.exe
if ($LASTEXITCODE -ne 0) { throw 'Index tests failed' }
```

Replace `/O2` with `/Od` for debug. Both configurations were tested with
strict floating-point semantics. Default round-to-nearest is required.
The final optimized index run used the full randomized suite. At the user's
request to shorten the remaining verification, final debug used the targeted
suite: `build\test_leaftl_index.exe --targeted` (38,912 oracle lookups,
7 compactions; Figure 13, CRB collision, K/I preservation and rollback cases).
CRB/segment tests and the codec comparison were already completed.

## Independent binary16 conversion oracle (x64 Python >=3.9)

```powershell
cl /nologo /TC /std:c11 /W4 /WX /O2 /fp:strict /LD ..\src\leaftl_crb.c ..\src\leaftl_segment.c /Fo:build\ /Fe:build\codec.dll /link /EXPORT:LeaFtlFloat16Encode /EXPORT:LeaFtlFloat16Decode
if ($LASTEXITCODE -ne 0) { throw 'Codec build failed' }
python verify_float16.py build\codec.dll
if ($LASTEXITCODE -ne 0) { throw 'Codec comparisons failed' }
```

GCC/Clang reproduction (not executed here):

```sh
mkdir -p build
cc -std=c99 -Wall -Wextra -Werror -O2 -fno-fast-math -ffp-contract=off \
  -I ../src ../src/leaftl_crb.c \
  ../src/leaftl_segment.c ../src/leaftl_index.c \
  test_leaftl_index.c -lm -o build/test_leaftl_index
./build/test_leaftl_index
```

## Current contracts (supersede the previous stride/repacking implementation)

- Segment K is binary16 with bit 0 reserved for type, I is uint32; prediction
  is `ceil(K*group_offset+I)`. Training is floating-point PLR and revalidates
  all supplied PPNs AFTER half quantization/type encoding. Gamma remains zero.
- Exact membership is Algorithm 2's `ceil(1/K)` stride inferred from K; there
  is no explicit stored stride. A newly created singleton uses K=0, whereas
  a singleton produced by trimming keeps its original nonzero K and I.
- An irregular membership, or a VSA slope for which the exact inference rule
  is unsuitable, uses the type-1 GROUP CRB even when all predictions are exact.
  This is not permission to enable gamma>0 without OOB correction.
- Training outputs `LEAFTL_SEGMENT_INPUT` (model plus transient bitmap).
  Index insertion copies the model and installs type-1 membership in the CRB.
  No per-node persistent bitmap or high-precision shadow slope is retained.
- A CRB record's offsets are contiguous and sorted; records are ordered by
  their first offset (SLPA), with globally unique offsets. A delimiter bitmap
  replaces ambiguous null-byte separators so 0 and 255 remain valid offsets.
  Key search is binary over record heads, followed by binary search in that
  record; the globally nearly-sorted payload is not treated as fully sorted.
- `LeaFtlIndexInsert(index, inputs, count, workspace)` uses paper seg_update:
  put at L0, seg_merge old memberships, delete empty old, trim SLPA/L without
  changing K/I, then move still-overlapping old ranges down. A collision below
  creates an intermediate level instead of recursive updates.
- New exact mappings also remove old CRB membership. If an old SLPA is
  overwritten, the segment and its CRB key move together to the next member.
- `seg_merge` means `old_members & ~new_members`. It does not mean merging
  equal-coefficient adjacent segments. The old coalescing and survivor-stride
  repartitioning code has been removed.
- Compaction uses cross-level seg_merge and the SAME seg_update routine to
  move upper segments into lower levels, removing empty upper levels. It
  performs one finite traversal of original levels (bottom-up); collision
  levels created during that pass are not recursively revisited.
- Exact interior stale members can remain implicitly hidden below a newer
  segment, as permitted by the paper's bounds-only trim. Consequently we do
  not claim every exact LPN occurs only once across all levels. Correct latest
  lookup and CRB uniqueness are the invariants. Irreducible range overlaps can
  keep multiple levels after repeated compaction.
- Every insertion/compaction is planned in caller-owned workspace first.
  On INVALID/NO_SPACE the persistent index, CRB, counters, pools and optional
  result remain unchanged. Workspace may change. Existing group slots are
  credited so full-pool overwrite can reclaim its old node in the same call.
- Buffers, inputs, workspace and outputs must be disjoint; single-threaded
  operation only. Cache eviction/Flash I/O, unmap and crash recovery are later.

## Earlier paper-alignment verification (before 8-byte conversion)

- CRB: 3,000 randomized mutations, 768,000 owner checks, full 256 records,
  key renaming, wrong-owner rejection, boundaries 0/255, unique actual offsets.
- Segment: 6,371 oracle batches, all 256 offsets per batch; exact and CRB
  paths, irregular holes without stride splitting, negative slopes, large
  PPNs, maximal group, fractional/ceil bounds, malformed input and capacity.
- Index: Figure 13 T0..T8 with CRB lookups and exact final L0 layout
  `[0,15] [16,31] [32,90] [200,255]`; CRB SLPA collisions; exact-over-CRB
  overwrite; exact and CRB singletons preserving K/I bits; lower-level collisions.
- 4,000 mixed exact/CRB random update attempts, 1,888 compactions and
  12,083,200 oracle lookups. All group values/holes, tree/list/CRB consistency,
  active/free pool partition and coefficient provenance are checked.
- Atomic rejection AFTER a multi-segment plan already renamed an old CRB key;
  full-pool overwrite and slot reuse; absent group/invalid/partial last group.
- Binary16: 321,548 independent comparisons against Python struct's half codec,
  including all 65,536 patterns and neighboring-double/tie/overflow boundaries.

MSVC x64 runtime sizes: segment=8, CRB=292, group=308, node=20,
level=16, transaction workspace=31,032 bytes. These are actual runtime sizes,
actual 8-byte model plus separate index metadata. Workspace must be budgeted off-stack
at step 6. No board/SDK build was run. Future fixed-point arithmetic must
repeat these exact membership/value/rounding/merge tests before use.

See [paper alignment and remaining differences](../../../docs/LeaFTL/%234%EB%8B%A8%EA%B3%84%20-%20%EB%85%BC%EB%AC%B8%20%EB%8C%80%EC%A1%B0%20%EB%B0%8F%20%EB%B3%B4%EC%A0%95%20%EA%B2%B0%EA%B3%BC.md).

## Steps 5 and 6: minimal mapping/memory check

From Developer PowerShell in tests (bsp_stub is HOST-TEST ONLY):

```powershell
cl /nologo /TC /std:c11 /W4 /WX /O2 /fp:strict /I bsp_stub /I ..\src ..\src\leaftl_crb.c ..\src\leaftl_segment.c ..\src\leaftl_index.c ..\src\leaftl_mapping.c ..\src\leaftl_memory.c test_leaftl_mapping_memory.c /Fo:build\ /Fe:build\mapping_memory.exe
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& .\build\mapping_memory.exe
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
```

Only mapping state transitions, address/capacity errors, alignment and memory
boundaries are checked. The synthetic BSP assumes 8 channels/16 dies; it is
not board detection and must NEVER be on the firmware include path. Four
negative compilation configurations and a 32-bit host syntax check were run.
After extracting the index preflight planner, the existing index regression
ran ONLY with --targeted (38,912 lookups, 7 compactions), not the random suite.

New arena: 3,007,936 bytes in the synthetic x64 configuration. The raw future
directory reservation is not an implemented Flash format. Actual target
sizes are checked by memory_map.h at SDK compilation. Linker ASSERT and SDK
XML were inspected; an ARM SDK build/link was NOT run. Details: 
[#5-6단계 - 매핑 인터페이스와 DRAM 배치.md](../../../docs/LeaFTL/%235-6%EB%8B%A8%EA%B3%84%20-%20%EB%A7%A4%ED%95%91%20%EC%9D%B8%ED%84%B0%ED%8E%98%EC%9D%B4%EC%8A%A4%EC%99%80%20DRAM%20%EB%B0%B0%EC%B9%98.md).

Future individual steps use minimal changed-path/boundary/failure checks.
Large repeated/random tests are deferred to stages 13, 17 and 19.

## Current 8-byte model revision - minimal verification

SLPA 1B + L 1B + binary16 K/type 2B + uint32 I 4B. Group ID is outside
the model, supplied by its owning group. Train inputs carry it transiently.
Integer I is chosen only inside every member's valid ceil interval; otherwise
the segment splits. No float32 loss or hidden double I is used.
Encode8/Decode8 use explicit little endian and exactly 8 bytes.

For minimal segment tests, run the compiled test with --targeted. The full
loops remain available but were NOT rerun for this revision. Current executed
checks: 9 segment batches x 256 offsets, 8-byte golden/roundtrip/bounds cases,
index --targeted (38,912 lookups, 7 compactions), mapping/memory and 32-bit host
compilation. ARM SDK build/board upload remain pending.
See [#2단계 - 8바이트 세그먼트 표현.md](../../../docs/LeaFTL/%232%EB%8B%A8%EA%B3%84%20-%208%EB%B0%94%EC%9D%B4%ED%8A%B8%20%EC%84%B8%EA%B7%B8%EB%A8%BC%ED%8A%B8%20%ED%91%9C%ED%98%84.md).
