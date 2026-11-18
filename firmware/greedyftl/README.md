# GreedyFTL (페이지 매핑 기준)

Cosmos+ OpenSSD의 GreedyFTL(`run-gftl3`)이다. 전체 매핑을 DRAM에 두는 페이지 매핑 FTL로, LearnedFTL 논문의 ideal(페이지 매핑 상한)에 해당한다.

## 원본과 다른 점

- **32GB 실험 모드:** `-DEXP_LOGICAL_GB=32`로 빌드하면 호스트에 32GB를 보이고, die당 570블록만 쓴다(다른 FTL과 같은 플래시 공간).
- **Superblock GC (2026-11-17):**
  - 원본은 die 하나의 free block이 예비 1개만 남으면 그 die만 GC했다. 2026-11-12(#15)에는 복사 대상만 16 die에 돌아가며 골랐다.
  - 지금은 16 die를 하나의 superblock(die마다 block 1개) 단위로 본다. free superblock(die별 free block 수의 최솟값)이 3개 미만이 되면(`GC_FREE_SUPERBLOCK_WATERMARK` 2) 16 die 모두에서 각자 greedy victim(invalid page가 가장 많은 block)을 하나씩 골라 한꺼번에 정리한다. free superblock이 3개 이상이 될 때까지 반복한다.
  - 시작 조건은 [LearnedFTL 저자 레포](https://github.com/astlxmu/LearnedFTL)([`ld-tpftl.c`](https://github.com/astlxmu/LearnedFTL/blob/main/hw/femu/bbssd/ld-tpftl.c)의 `free_line_threshold` 3)와 같다. 모든 FTL이 같은 GC 시작 조건을 쓴다.
  - valid page는 victim과 같은 die 안으로 복사한다. 16 die가 동시에 GC하므로 복사도 동시에 진행되고, page 순서대로 die를 번갈아 요청을 넣어 request queue에 모든 die의 복사가 섞인다.
  - 효과(보드, 32GB를 채운 뒤): 16KB random write 34.3 → 96.5 MiB/s, 4KB random write 30GB 약 1,460 → 3,230 IOPS.
  - 원본 GC는 이 폴더의 git 기록(PR #9의 커밋 f6828ec)에 있다.
