# TPFTL on Cosmos+ OpenSSD

TPFTL(Zhou et al., "An Efficient Page-level FTL to Optimize Address Translation in Flash Memory", EuroSys'15) 구현이다. LearnedFTL 논문(HPCA'24)이 비교 대상으로 쓴 TPFTL과 같은 구성을 목표로 했다.

- 매핑 캐시: TPFTL
- 데이터 배치: 페이지 단위
- GC: greedy

## 구성

`firmware/dftl`(고친 DFTL)을 복사하고 매핑 캐시(CMT)만 TPFTL 방식으로 바꿨다. 할당, GC, 변환 페이지 관리(GTD)는 DFTL과 같다. API 이름도 DFTL 것(`Dftl*`)을 그대로 써서 나머지 코드는 바뀌지 않는다.

| TPFTL 기능 | 구현 |
|---|---|
| 2단계 LRU | 변환 페이지(TP) 노드의 LRU 목록. 노드마다 캐시된 항목의 LRU 목록 (`dftl_cmt.c`) |
| clean 먼저 교체 | 가장 차가운 TP 노드에서 가장 차가운 clean 항목을 내보냄. 그 노드에 clean이 없을 때만 dirty |
| batch update | dirty 항목을 내보낼 때 같은 변환 페이지의 dirty 항목을 모두 함께 씀 (`DftlCmtTakeDirtyOfTp`) |
| 요청 단위 prefetch | 한 NVMe 명령이 걸친 LPN의 매핑을 한 번의 변환 페이지 읽기로 함께 올림 (`DftlHintRequest`) |
| 선택적 prefetch | 캐시된 앞쪽 연속 항목 수만큼 뒤쪽도 올림. TP 노드 생성·소멸 카운터(±3)로 켜고 끔. 최대 64개 (`DFTL_PREFETCH_MAX`) |

- **GC 중 매핑 조회:** prefetch하지 않는다(`DftlGetMappingNoPrefetch`).
- **교체 정책:** `firmware/learnedftl`의 `lftl_cmt.c`와 같다. 그래서 LearnedFTL에서 모델만 뺀 것과 캐시 동작이 같다.

## 설정

- **CMT:** 65,536 항목. 32GB 모드 전체 매핑 2,097,152개의 3.1%이며, DFTL과 같다.
- **빌드:** `-DEXP_LOGICAL_GB=32`로 빌드한다. 다른 펌웨어와 같다.
- **부팅 확인:** UART에 `[ TPFTL: CMT 65536 entries, prefetch up to 64, page-level greedy GC ]`가 찍힌다.

## 검증

보드에서 FIO verify로 확인했다(2026-11-11, 오류 0).
- 32GB 전체를 순차로 쓰고 읽어 비교
- 16KB 랜덤 쓰기 6GB 후 읽어 비교
