# DFTL on Cosmos+ OpenSSD

DFTL(Demand-based Flash Translation Layer, Gupta et al. ASPLOS'09) 구현. GreedyFTL(`run-gftl3`)을 베이스로 `run-dftl` 프로젝트를 복제한 뒤, 주소 변환 계층만 CMT(Cached Mapping Table, Segmented LRU) + GTD(Global Translation Directory) 기반으로 교체했습니다.

빌드에 필요한 나머지 파일(nvme/, request_*.c/h, data_buffer.c/h, ftl_config.c/h, main.c 등)은 원본 벤더 포크(`CRZ-Technology/OpenSSD-OpenChannelSSD`)의 `run-gftl3`와 동일하며, 이 폴더에는 새로 작성했거나 수정한 파일만 담았습니다.

## 새로 작성한 파일
- `dftl_config.h` — CMT 용량, translation page당 매핑 수, GTD 크기, 다이당 translation block 예약 개수 등 설정 상수
- `dftl_cmt.h` / `dftl_cmt.c` — Segmented LRU 캐시 (probationary + protected segment), 순수 자료구조, NAND I/O 없음
- `dftl_gtd.h` / `dftl_gtd.c` — GTD, translation page read/write, translation block 풀 관리, translation block GC

## 수정한 파일 (GreedyFTL 원본 대비)
- `address_translation.c/h` — `AddrTransRead`/`AddrTransWrite`/`InvalidateOldVsa`가 `logicalSliceMapPtr[]` 직접 접근 대신 `DftlGetMapping`/`DftlSetMapping`(CMT+GTD 경유)을 사용하도록 변경. `InitAddressMap()`에서 `DftlCmtInit()`/`DftlGtdInit()` 호출 추가.
- `garbage_collection.c` — valid page 판별과 forward map 갱신 두 곳만 DFTL 방식(lazy copying + batch update, 논문 3.3절)으로 교체. victim 선정/block erase/free list 관리 등 나머지 로직은 GreedyFTL과 동일.
- `memory_map.h` — CMT/GTD/translation block 풀을 위한 DRAM 주소 추가(기존 주소 체계는 건드리지 않고 뒤에 추가), translation page 스테이징 버퍼는 `RESERVED_DATA_BUFFER_BASE_ADDR` 재사용.

## 설계상 단순화 (문서화된 트레이드오프)
- 이 보드는 DRAM이 1GB라 전체 매핑 테이블(~30MB)이 원래 다 들어가서, DFTL 특유의 "SRAM 부족 해결"이라는 동기는 절실하지 않음 — `DFTL_CMT_CAPACITY`를 의도적으로 작게(기본 4096) 잡아서 demand-paging 효과가 FIO 결과에 나타나도록 함. 논문 Fig.11(SRAM 크기별 성능)처럼 이 값을 바꿔가며 재측정 가능.
- Translation block은 다이당 고정 개수(`DFTL_TRANS_BLOCKS_PER_DIE`, 기본 4개) 예약. GC는 GreedyFTL의 segregated invalid-count 방식 대신 단순 전체 스캔(해당 다이의 예약 블록 중 invalid 페이지 제일 많은 걸 victim으로) — translation block 개수가 적고 turnover가 드물어서 충분.
- Translation page I/O는 전부 동기(blocking) 처리 — 읽은 버퍼 내용을 바로 검사/수정해야 해서 GreedyFTL GC의 비동기 read+write 패턴과 다름. 정확성 우선, 성능 최적화는 추후 과제.

## 아직 안 한 것
- 실제 보드에서 빌드/Program FPGA/부팅 테스트 미완료 (다음 세션 작업)
- FIO 성능 측정 및 GreedyFTL과의 비교 미완료
