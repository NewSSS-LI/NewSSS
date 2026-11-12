# GreedyFTL (페이지 매핑 기준)

Cosmos+ OpenSSD의 GreedyFTL(`run-gftl3`)이다. 전체 매핑을 DRAM에 두는 페이지 매핑 FTL로, LearnedFTL 논문의 ideal(페이지 매핑 상한)에 해당한다.

## 원본과 다른 점

- **32GB 실험 모드:** `-DEXP_LOGICAL_GB=32`로 빌드하면 호스트에 32GB를 보이고, die당 570블록만 쓴다(다른 FTL과 같은 플래시 공간).
- **GC 병렬 복사 (2026-11-12):**
  - 원본은 피해 블록의 die 안에서 한 페이지씩 차례로 복사했다.
  - 지금은 복사 대상 die를 16 die에 돌아가며 골라 병렬로 복사한다. LearnedFTL과 논문(FEMU)의 FTL과 조건을 맞추려는 것이다.
  - 피해 블록은 그대로 die별 greedy(무효 페이지가 가장 많은 블록)로 고른다.
  - 원본 GC는 이 폴더의 git 기록(PR #9의 커밋 f6828ec)에 있다.
