# 파일 준비 현황

## 복사해 둔 기존 파일: 아직 수정하지 않음

| 파일 | 지금 역할 | LeaFTL 이식 방향 |
|---|---|---|
| `address_translation.c/h` | `DftlGetMapping`, `DftlSetMapping`, 읽기/쓰기 주소 변환, 물리 공간 할당 | 학습 매핑 조회/갱신 경로의 접점. 호출자와 초기화 순서를 함께 수정 |
| `dftl_cmt.c/h` | 개별 LPN→VSA 항목의 segmented-LRU 캐시 | 비교용 보존. 학습 세그먼트 캐시를 별도 구현한 뒤 단계적으로 교체 |
| `dftl_gtd.c/h` | GTD와 고정 형식 translation page, translation block GC | 블록 I/O 관리는 재사용 검토. 세그먼트·CRB 저장 형식과 디렉터리는 새로 설계 |
| `dftl_config.h` | CMT 용량·translation page·예약 블록 수 | baseline 유지. LeaFTL 설정은 별도 파일에 정의 |
| `data_buffer.c/h` | 데이터 캐시·버퍼 관리 | flush 묶음 수집·중복 LPN 처리·정렬을 추가할 후보 |
| `request_transform.c/h` | 버퍼의 요청을 NAND 요청으로 전환 | PPA 할당 전 정렬, 실제 할당 주소 수집, 학습 갱신 순서 연결 |
| `garbage_collection.c/h` | valid page 검사, relocation, 매핑 갱신 | 새 매핑으로 유효성 확인, relocation 결과의 재학습 및 최신 매핑 갱신 |
| `memory_map.h` | 고정 DRAM 영역 배치 | 세그먼트/그룹/CRB/학습 임시 버퍼 영역과 상한 설계 |
| `request_format.h`, `request_schedule.c`, `nsc_driver.c` 및 관련 헤더 | 요청 구조·실행·저수준 NAND 접근 | gamma>0 단계에서 OOB 전달·읽기 완료 검증 경로 확인 후 최소 수정 |
| `main.c`, `ftl_config.c/h`, `lscript.ld` | 진입·설정·링커 메모리 배치 | 초기화·설정 충돌 확인. 디바이스 geometry와 메모리 주소를 임의 변경하지 않음 |
| `nvme/` | NVMe 명령 처리 | 기본 경로 유지. flush 의미와 완료 시점은 영속성 단계에서 점검 |
| `.project`, `.cproject` | Xilinx SDK 프로젝트 설정 | Import 전 이름·artifact·BSP·include/link 경로 점검 |

## 앞으로 만들 파일: 목록만 준비, 파일/함수 구현은 하지 않음

| 제안 파일 | 책임 |
|---|---|
| `src/leaftl_config.h` | group 크기, gamma, RAM 예산, 세그먼트·레벨·CRB 상한 |
| `src/leaftl_segment.c/h` | 구간 선형 학습, 예측, 반올림, 유효 LPN membership |
| `src/leaftl_index.c/h` | 그룹별 다중 레벨, 조회, 중복 갱신, merge/compaction |
| `src/leaftl_crb.c/h` | approximate segment의 실제 LPN 소속 관리 |
| `src/leaftl_mapping.c/h` | 주소 변환과 학습 인덱스 사이의 통합 인터페이스 |
| `src/leaftl_flash.c/h` | 인덱스 serialization, flash-backed cache, 디렉터리·메타데이터 GC |
| `src/leaftl_stats.c/h` | 메모리·조회·예측·학습·GC 통계 |
| `tests/` | 보드와 분리한 호스트 측 모델·인덱스 검증. 구현 시 추가 |

빈 `.c/.h`는 만들지 않았다. 구현된 모듈로 오인하거나 SDK 자동 빌드에 포함되지 않도록 이름과 역할만 문서화했다.
