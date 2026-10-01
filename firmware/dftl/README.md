# DFTL on Cosmos+ OpenSSD

DFTL(Demand-based Flash Translation Layer, Gupta et al. ASPLOS'09) 구현. GreedyFTL(`run-gftl3`)을 베이스로 `run-dftl` 프로젝트를 복제한 뒤, 주소 변환 계층만 CMT(Cached Mapping Table, Segmented LRU) + GTD(Global Translation Directory) 기반으로 교체했습니다.

**이 폴더(`run-dftl/`) 자체가 완전한 SDK 프로젝트입니다** (`.project`, `.cproject`, `src/` 전체 42개 파일 포함) — Xilinx SDK 워크스페이스에 그대로 Import해서 빌드할 수 있습니다.

## 빌드하려면 추가로 필요한 것 (이 레포에는 없음, 각자 보유)

- **`run-gftl3_bsp`** (Board Support Package) — `.project`가 이걸 의존성으로 참조함. 같은 SDK 워크스페이스 안에 `run-gftl3_bsp`가 먼저 Import되어 있어야 빌드됨.
- **하드웨어 플랫폼(비트스트림 `sys_top_wrapper.bit` + `system.hdf`)** — Program FPGA 할 때 필요. 하드웨어는 GreedyFTL과 완전히 동일해서(소프트웨어만 바뀜) 기존에 쓰던 것 그대로 사용 가능.

## 사용법 요약

1. 기존 SDK 워크스페이스(run-gftl3, run-gftl3_bsp, hw_platform이 이미 있는 곳)에 이 `run-dftl` 폴더를 복사
2. SDK: File → Import → Existing Projects into Workspace → 이 폴더 지정
3. Build
4. Xilinx Tools → Program FPGA (기존 비트스트림, GreedyFTL 때 쓰던 것 그대로)
5. `run-dftl` 우클릭 → Run As → Launch on Hardware (GDB)

## 새로 작성한 파일
- `src/dftl_config.h` — CMT 용량(`DFTL_CMT_CAPACITY`, 기본 4096), translation page당 매핑 수, GTD 크기, 다이당 translation block 예약 개수 등 설정 상수
- `src/dftl_cmt.h` / `src/dftl_cmt.c` — Segmented LRU 캐시 (probationary + protected segment), 순수 자료구조, NAND I/O 없음
- `src/dftl_gtd.h` / `src/dftl_gtd.c` — GTD, translation page read/write, translation block 풀 관리, translation block GC

## 수정한 파일 (GreedyFTL 원본 대비)
- `src/address_translation.c/h` — `AddrTransRead`/`AddrTransWrite`/`InvalidateOldVsa`가 `logicalSliceMapPtr[]` 직접 접근 대신 `DftlGetMapping`/`DftlSetMapping`(CMT+GTD 경유)을 사용하도록 변경. `InitAddressMap()`에서 `DftlCmtInit()`/`DftlGtdInit()` 호출 추가.
- `src/garbage_collection.c` — valid page 판별과 forward map 갱신 두 곳만 DFTL 방식(lazy copying + batch update, 논문 3.3절)으로 교체. victim 선정/block erase/free list 관리 등 나머지 로직은 GreedyFTL과 동일.
- `src/memory_map.h` — CMT/GTD/translation block 풀을 위한 DRAM 주소 추가(기존 주소 체계는 건드리지 않고 뒤에 추가), translation page 스테이징 버퍼는 `RESERVED_DATA_BUFFER_BASE_ADDR` 재사용.

나머지 파일(`nvme/`, `request_*.c/h`, `data_buffer.c/h`, `ftl_config.c/h`, `main.c`, `nsc_driver.c/h` 등)은 원본 벤더 포크(`CRZ-Technology/OpenSSD-OpenChannelSSD`)의 `run-gftl3`와 100% 동일, 전혀 수정 안 함.

## 설계상 단순화 (문서화된 트레이드오프)
- 이 보드는 DRAM이 1GB라 전체 매핑 테이블(~30MB)이 원래 다 들어가서, DFTL 특유의 "SRAM 부족 해결"이라는 동기는 절실하지 않음 — `DFTL_CMT_CAPACITY`를 의도적으로 작게(기본 4096) 잡아서 demand-paging 효과가 FIO 결과에 나타나도록 함. 논문 Fig.11(SRAM 크기별 성능)처럼 이 값을 바꿔가며 재측정 가능.
- Translation block은 다이당 고정 개수(`DFTL_TRANS_BLOCKS_PER_DIE`, 기본 4개) 예약. GC는 GreedyFTL의 segregated invalid-count 방식 대신 단순 전체 스캔(해당 다이의 예약 블록 중 invalid 페이지 제일 많은 걸 victim으로) — translation block 개수가 적고 turnover가 드물어서 충분.
- Translation page I/O는 전부 동기(blocking) 처리 — 읽은 버퍼 내용을 바로 검사/수정해야 해서 GreedyFTL GC의 비동기 read+write 패턴과 다름. 정확성 우선, 성능 최적화는 추후 과제.

## 현재 상태 (2026-09-30/10-01 기준)
- ✅ 빌드 성공 (에러 0)
- ✅ 실제 보드에서 부팅/FTL 초기화 정상 확인
- ✅ GreedyFTL 대비 FIO 성능 비교 완료 (raw device, bs=128k/iodepth=4/size=128M) — GreedyFTL이 7~30배 빠름, DFTL은 반복 접근 시 캐시 워밍업→plateau 패턴 확인. 결과는 `../../results/` 참고.
- [ ] `DFTL_CMT_CAPACITY` 스윕 실험(메모리-성능 트레이드오프 곡선) 미완료
- [ ] Translation block GC 장시간 동작 검증 미완료
