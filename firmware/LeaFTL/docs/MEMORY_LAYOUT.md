# 5·6번: OpenSSD 매핑 인터페이스와 DRAM 배치

기준일: 2026-11-12. 현재 Cosmos+ OpenSSD DFTL 소스와 Cortex-A9 SDK 구성을 기준으로 한다.
실제 보드 BSP·SDK 전체 빌드나 보드 업로드 결과를 의미하지 않는다.

## 5번 — 매핑 인터페이스

`leaftl_mapping.c/h`에 아래 흐름을 구현했다.

1. `LeaFtlMappingPrepare`: 한 그룹의 정렬·중복 없는 `(LPN, 실제 할당 VSA)` 배치를
   학습하고 `LeaFtlIndexCanInsert`로 공간과 merge 결과를 사전 검사한다.
2. 성공하면 token을 발급하고 pending 상태가 된다. 이 시점에는 공개 매핑이 바뀌지 않는다.
3. 모든 NAND program이 완료된 뒤 `LeaFtlMappingComplete(token, 1)`을 호출하면
   새 매핑을 발행한다. `Complete(token, 0)`이나 `Abort`는 이전 매핑을 보존한다.
4. `Lookup`은 pending VSA를 반환하지 않는다. pending 데이터 읽기는 향후 data buffer
   경로에서 처리한다. 중복 완료·오래된 token은 STALE로 거부하며 token은 wrap하지 않는다.

한 context에는 pending 배치 하나만 허용한다. pending 중 추가 prepare/compaction은
BUSY이다. group 밖·중복·역순·주소 상한 초과·잘못된 VSA·자원 부족을 거부한다.
context가 index를 독점하며 prepare와 complete 사이 외부 index 변경을 허용하지 않는다.
index의 preflight와 실제 insert는 동일한 transaction planner를 공유한다.

아직 NAND 요청 큐를 연결하거나 기존 VSA를 실제 무효화하지 않는다. program 실패 시
할당됐으나 공개되지 않은 VSA 정리, DMA 대기, FLUSH/FUA와 요청 순서는 7~9번 이후
통합에서 연결한다. 이 API는 전원 장애 원자성을 보장한다고 주장하지 않는다.

## 6번 — 실제 sizeof 기반 arena

`leaftl_memory.h/c`는 heap 없이 호출자가 제공한 RAM에 아래 영역을 64-byte 정렬로 배치한다.

| 영역 | 기본 예약 |
|---|---:|
| mapping context와 index handle | 각각 실제 sizeof를 64-byte 정렬 |
| 상주 그룹·CRB | 256 × sizeof(LEAFTL_INDEX_GROUP) |
| 레벨 pool | 65,536 × sizeof(LEAFTL_INDEX_LEVEL) |
| node pool | 65,536 × sizeof(LEAFTL_INDEX_NODE) |
| transaction workspace | 실제 sizeof, 현재 MSVC x64 31,032 bytes |
| 학습 points | 256 × 8 = 2,048 bytes |
| 학습된 입력·소속 bitmap | 256 × 48 = 12,288 bytes |
| flush 정렬용 buffer ID | 기본 256 × 4 bytes, 실행 로직은 7번 |
| Flash directory 예비 공간 | 관리 LPN group당 raw 16 bytes, 저장 형식은 11번 |

directory 예비 공간은 serialization이나 Flash directory 기능을 구현한 것이 아니다.
CRB와 tree 링크는 group/node sizeof에 포함하며 별도로 중복 계산하지 않는다.
allocator를 사용하지 않으므로 숨겨진 heap overhead는 없다. alignment padding은 총 크기에 포함한다.

`LeaFtlMemoryInit(arena, bytes, logical_count, &mapping)`은 정렬·범위·예산을 검사한 뒤
index와 mapping context를 초기화한다. logical_count는 실제 host에 노출한 LPN 수이다.
directory/flush 예약을 실제 metadata로 해석하거나 NAND에 쓰지 않는다.

`memory_map.h`는 기존 DFTL 마지막 주소 뒤에 이 arena를 추가한다. 기존 NVMe·NAND·DMA·
forward/reverse map·CMT·GTD의 시작 주소는 유지한다. geometry를 바꾸거나 사용하지 않는
기존 full map을 비용 집계에서 빼지 않는다. `FTL_MANAGEMENT_END_ADDR`에는 새 arena까지 포함한다.

컴파일 시 metadata 8MiB 예산, DRAM 0x40000000 경계, cached 영역, data DMA/completion
영역, flush ID 용량을 검사한다. 계산을 64-bit로 하여 큰 설정의 32-bit wrap을 방지한다.
실제 SDK 컴파일에서는 그 컴파일러의 sizeof와 BSP geometry로 다시 검사된다.

## 링커와 OpenSSD SDK

- 기존 `-mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard`와 BSP 경로를 유지한다.
- `.cproject`의 Debug/Release C/C++ linker group에 `-lm`을 추가했다.
- `lscript.ld`의 코드·data·heap·stack 영역을 main.c가 의도한 0x00100000~0x001fffff로
  제한했다. 전체 DRAM을 코드가 점유할 수 있던 기존 설정을 닫고 NVMe 시작 전 경계를 ASSERT한다.
- 31KiB workspace와 학습 배열을 8KiB 기본 stack에 두지 않고 고정 RAM arena에 배치한다.
- 실제 target stack 사용량·ISR 중첩·BSP link 결과는 이후 SDK 통합 시 확인한다.
- 보드의 FPU/BSP 제약으로 고정소수점 전환이 필요하면 기존 계획대로 연산부와 검증을 함께 변경한다.

## 최소 검증 결과

PC 테스트용 `tests/bsp_stub/xparameters.h`로 **가상의 8채널·16die** 구성을 만들었다.
이 stub은 펌웨어 경로나 실제 BSP를 대체하지 않는다.

- mapping prepare/publish/program failure/abort/BUSY/STALE/NO_SPACE/token 소진 및 범위 검증 통과.
- 정렬·arena 크기·예산 검증, 32-bit host compiler 문법/정적 경계 검증 통과.
- metadata 예산 1KiB, 정렬 ID pool 1개, level pool 0개, 과대 node pool을 각각 컴파일 오류로 차단.
- index planner 분리 후 Figure 13·CRB·계수·실패 처리 핵심 회귀만 실행: 38,912회 조회·7회 compaction 통과.
- linker 경계 선언과 SDK XML을 정적으로 확인했다. GNU/ARM SDK link는 수행하지 않았다.

가상 8채널/MSVC x64 계산 결과:

| 항목 | 값 |
|---|---:|
| 기존 forward full map 예약 | 33,554,432 bytes (현재 DFTL에서 미사용이어도 포함) |
| reverse map 예약 | 33,554,432 bytes |
| 기존 FTL end-exclusive | 0x1c27fc44 |
| 새 arena 시작 | 0x1c27fc80 |
| 새 arena end-exclusive | 0x1c55e240 |
| 새 arena 총 크기 | 3,007,936 bytes (약 2.87MiB, directory 524,288 bytes 포함) |

실제 보드 채널 수·주소 값은 BSP를 읽는 SDK 컴파일 결과에 따른다. 이 수치를 실제 보드에서
측정한 것으로 해석하지 않는다. 펌웨어 호출 연결과 보드 테스트는 후속 단계다.

앞으로 세부 단계 검증은 사용자 요청대로 변경 핵심·경계·실패 처리만 최소 실행하고,
대규모 반복 검증은 13·17·19번 통합 시점에 수행한다.

Current stored segment is exactly 8 bytes; index node is 20 bytes. See [SEGMENT_8BYTE.md](SEGMENT_8BYTE.md) for integer-intercept constraints and updated verification.


### 7·8번 추가
고정 arena에 writeback scratch 5,124 bytes(64-byte 정렬 적용)를 추가했다. synthetic 8ch/16die 총 예약은 **3,013,120 bytes**, end-exclusive **0x1c55f680**으로 갱신한다. 실제 보드 수치는 BSP geometry/32-bit ABI로 다시 산출해야 한다.


### 11번 메모리 축소 반영 (최신)
디렉터리 8B/그룹, 전용 16KiB staging 제거(기존 공용 uncached 영역 재사용), 미사용 1KiB flush-ID 예약 제거: synthetic x64 8ch/16die arena **2,755,200 bytes**, end-exclusive **0x1c520700**. 초기 11번 대비 273KiB 절감이며 segment node 등 조회 구조는 유지한다. 실제 BSP/ARM ABI 수치는 다시 산출해야 한다.
