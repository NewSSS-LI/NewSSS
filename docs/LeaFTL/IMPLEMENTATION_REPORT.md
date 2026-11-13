# DFTL 기반 LeaFTL 구현 보고서

## 전체 구현 순서 (1–19번)

기존 번호를 기준으로 정리했다. 1–19번의 코드·검증 기록을 연결했으며, 실제 보드 검증은 대기 중이다. 18번은 사용자 요청에 따라 checkpoint·전원 복구를 제외했다. 19번 보드 성능 비교는 대기 중이다. CRB는 4번에서 앞당겨 구현했으므로 14번에서는 확장·재검증한다. 파일 경로는 별도 표시가 없으면 `firmware/leaftl/src/` 기준이다.

| 번호 | 구현 내용 | 주요 파일 | 현재 상태 | 작성한 보고서 |
|---|---|---|---|---|
| 1 | 그룹 크기·gamma·주소 및 자원 상한 정의 | `leaftl_config.h` | 구현·PC 확인 | [1번 기록](#step-1), [상세](%231%EB%8B%A8%EA%B3%84%20-%20%EC%84%A4%EC%A0%95%EA%B3%BC%20%EC%A3%BC%EC%86%8C%20%EA%B3%84%EC%95%BD.md) |
| 2 | 부동소수점 학습·예측·membership·8-byte 모델 | `leaftl_segment.c/h` | 구현·PC 검증 | [2번 기록](#step-2), [8-byte 수정](%232%EB%8B%A8%EA%B3%84%20-%208%EB%B0%94%EC%9D%B4%ED%8A%B8%20%EC%84%B8%EA%B7%B8%EB%A8%BC%ED%8A%B8%20%ED%91%9C%ED%98%84.md) |
| 3 | 그룹별 다중 레벨 삽입·최신 매핑 조회 | `leaftl_index.c/h` | 구현·PC 검증 | [3번 기록](#step-3) |
| 4 | 논문식 CRB·seg_merge·삽입 merge·compaction | `leaftl_crb.c/h`, `leaftl_index.c/h` | 구현·PC 검증 | [4번 기록](#step-4), [논문 반영표](%234%EB%8B%A8%EA%B3%84%20-%20%EB%85%BC%EB%AC%B8%20%EB%8C%80%EC%A1%B0%20%EB%B0%8F%20%EB%B3%B4%EC%A0%95%20%EA%B2%B0%EA%B3%BC.md) |
| 5 | 통합 매핑 API·pending·완료 후 발행 | `leaftl_mapping.c/h` | 구현·최소 검증 | [5번 기록](#step-5), [상세](%235-6%EB%8B%A8%EA%B3%84%20-%20%EB%A7%A4%ED%95%91%20%EC%9D%B8%ED%84%B0%ED%8E%98%EC%9D%B4%EC%8A%A4%EC%99%80%20DRAM%20%EB%B0%B0%EC%B9%98.md) |
| 6 | OpenSSD DRAM 배치·초기화·메모리 경계 | `leaftl_memory.c/h`, `memory_map.h`, `lscript.ld` | 구현·최소 검증 | [6번 기록](#step-6), [메모리 배치](%235-6%EB%8B%A8%EA%B3%84%20-%20%EB%A7%A4%ED%95%91%20%EC%9D%B8%ED%84%B0%ED%8E%98%EC%9D%B4%EC%8A%A4%EC%99%80%20DRAM%20%EB%B0%B0%EC%B9%98.md) |
| 7 | dirty 버퍼 수집·LPN 기준 ID 정렬 | `leaftl_writeback.c/h`, `data_buffer.c/h` | 구현·최소 검증 | [7·8번 공동 기록](#step-7-8) |
| 8 | 정렬 후 연속 page 할당·실제 PPN 학습·쓰기 연결 | `leaftl_cosmos.c/h`, `address_translation.c`, `request_transform.c`, `request_allocation.c`, `ftl_config.c` | 핵심 경로 구현·기본 비활성 | [7·8번 공동 기록](#step-7-8) |
| 9 | 읽기·쓰기 완료 및 오류 처리 보강 | `leaftl_cosmos.c/h`, `request_transform.c` | 구현·최소 PC 검증 | [9번 기록](#step-9) |
| 10 | GC 유효 페이지 판정·relocation 재학습 | `leaftl_gc.c/h`, `garbage_collection.c`, `address_translation.c`, `leaftl_cosmos.c/h`, `leaftl_memory.h` | 구현·최소 PC 검증 | [10번 기록](#step-10) |
| 11 | segment·CRB Flash 저장 형식·디렉터리 | `leaftl_flash.c/h`, `leaftl_index.c/h`, `leaftl_memory.c/h`, `memory_map.h` | 최소형 저장·묶음 저장 구현·PC 검증 | [11번 기록](#step-11) |
| 12 | Flash-backed 매핑 캐시·miss/eviction·메타데이터 GC | `leaftl_cache.c/h`, `leaftl_metadata.c/h`, `leaftl_mapping.c/h`, `leaftl_index.c/h`, `leaftl_flash.c/h`, `leaftl_cosmos.c`, `leaftl_memory.h`, `memory_map.h` | 구현·최소 PC 검증 | [12번 기록](#step-12) |
| 13 | gamma=0 전체 경로 통합·기본 경로 전환 | `leaftl_cosmos.c/h`, `ftl_config.c`, `address_translation.c`, `.cproject`, `tests/test_leaftl_firmware_path.c` | 코드·PC 통합·실제 ARM SDK 빌드 완료 / **보드 1회차 대기** | [13번 기록](#step-13) |
| 14 | gamma>0 모델·CRB 확장 및 충돌·소속 재검증 | `leaftl_config.h`, `leaftl_segment.c/h`, `tests/test_leaftl_gamma_oob.c` | 모델·CRB PC 검증 / ARM 모듈 빌드 완료 | [14번 기록](#step-14) |
| 15 | OOB layout 및 쓰기 메타데이터 전달 | `leaftl_oob.c/h`, `leaftl_writeback.c/h`, `leaftl_cosmos.c` | 쓰기·GC 연결 / gamma=0 통합·ARM 빌드 완료 | [15번 기록](#step-15) |
| 16 | 읽기 완료 후 OOB 검증·오예측 보정·재읽기 | `leaftl_cosmos.c/h`, `leaftl_writeback.c/h`, `leaftl_gc.c/h`, `memory_map.h`, `.cproject` | gamma=0/1/4/16 PC 실행·gamma=4 ARM 빌드 완료 | [16번 기록](#step-16) |
| 17 | gamma>0·OOB·보정·GC 통합 검증 | `leaftl_cosmos.c/h`, `address_translation.c`, `tests/` | PC 통합·gamma=4 ARM 빌드 완료 / **보드 2회차 대기** | [17번 기록](#step-17) |
| 18 | FLUSH/FUA 데이터·매핑 쓰기 완료 연결 (checkpoint·전원 복구 제외) | `leaftl_cosmos.c/h`, `request_transform.c/h`, `nvme/nvme_io_cmd.c` | 구현·PC 검증·ARM 빌드 완료 | [18번 기록](#step-18) |
| 19 | 무결성·메모리·I/O/WAF 계측 및 DFTL 비교 준비 | `leaftl_stats.h`, `leaftl_cosmos.c`, `tests/` | 계측·PC 검증·양 경로 ARM 빌드 완료 / **보드 성능 비교 대기** | [19번 기록](#step-19) |

현재 13번에서 `LEAFTL_FIRMWARE_WRITEBACK=1`로 기본 경로를 전환했고 실제 ARM SDK 빌드·PC 통합 검증을 통과했다. 이전 단계의 기본 비활성/SDK 미검증 기록은 당시 상태이며, 실제 보드 업로드는 13·17·19번 후 총 3회 일정으로 유지하고 아직 첫 업로드를 수행하지 않았다.


## 기록 원칙

- 앞으로 모든 구현 단계의 주요 변경과 검증 결과를 이 문서에 누적 기록한다.
- 각 단계에는 목적, 생성·수정 파일, 주요 구현 내용, 검증 결과, 남은 작업을 기록한다.
- 설정 정의, 호스트에서 동작하는 알고리즘, 펌웨어 연결, 보드 검증의 완료 여부를 구분한다.
- 보드 테스트는 13번, 17번, 19번 완료 후 총 3회로 계획한다.
- 단계별 상세 문서가 있으면 이 보고서에서 연결한다.

Verification policy: minimal core/boundary/failure checks per implementation step; large repeated tests at stages 13, 17 and 19, per user instruction.

현재 세그먼트는 8 bytes(SLPA 1 + L 1 + half K 2 + uint32 I 4)이다.
이전 단계의 크기 수치는 당시 기록이며, 최신 내용은 #2단계 - 8바이트 세그먼트 표현.md와 추가 수정 절을 따른다.

<a id="step-1"></a>

## 1번 — 그룹 크기 및 초기 설정·주소 규칙 정의

기록일: 2026-11-12

### 목적과 변경 파일

앞으로 구현할 log-structured 매핑 인덱스의 관리 단위와 공통 설정을 정의했다.

- 생성: `firmware/leaftl/src/leaftl_config.h`
- 생성: [#1단계 - 설정과 주소 계약.md](%231%EB%8B%A8%EA%B3%84%20-%20%EC%84%A4%EC%A0%95%EA%B3%BC%20%EC%A3%BC%EC%86%8C%20%EA%B3%84%EC%95%BD.md) — 주소 체계와 설정의 상세 근거
- 기존 DFTL 소스와 실행 경로는 변경하지 않았다.

### 1. log-structured 인덱스의 그룹 크기 지정

`LEAFTL_GROUP_SIZE = 256`으로 정의했다. 한 그룹은 연속된 LPN 256개를 담당한다.
이는 세그먼트 개수나 레벨 개수를 뜻하지 않는다. 마지막 그룹은 실제 주소 개수에
따라 일부만 사용될 수 있다.

```
그룹 0: LPN   0~255
그룹 1: LPN 256~511
그룹 2: LPN 512~767
```

이후 구현할 구조는 `그룹 → 레벨 → 세그먼트`이다. 각 그룹이 독립적인 여러 레벨을
갖고, 각 레벨에 해당 그룹 주소의 매핑을 표현하는 세그먼트들이 들어간다.
그룹 안의 매핑이 규칙적이면 적은 세그먼트로 표현하고 불규칙하면 더 나누어 표현한다.
매핑 갱신에 따라 여러 레벨에 이전 세그먼트가 남을 수 있다.

그룹 내부 위치를 0~255의 1바이트 offset으로 표현하기 위한 계산 매크로를 추가했다.

| 매크로 | 의미 |
|---|---|
| `LEAFTL_GROUP_ID(lpn)` | `lpn / 256`으로 그룹 번호 계산 |
| `LEAFTL_GROUP_OFFSET(lpn)` | `lpn % 256`으로 그룹 내부 위치 계산 |
| `LEAFTL_GROUP_COUNT(lpn_count)` | 관리할 LPN 개수에 필요한 그룹 수 계산. 덧셈 overflow 방지 |

예를 들어 LPN 300은 그룹 1의 offset 44이다.

**현재는 그룹 크기와 계산 규칙만 정의했다. 그룹 자료구조, 다중 레벨,
최신 매핑 우선 조회, merge·compaction은 아직 구현하지 않았다.**

### 2. 초기 정확 조회 설정

- `LEAFTL_GAMMA = 0`: 초기 버전에서는 예측 오차를 허용하지 않도록 설정했다.
- CRB·OOB 이웃 규칙·오예측 보정이 준비되기 전 `gamma>0` 설정은 컴파일 오류로 차단한다.
- 실제 학습·조회 알고리즘은 2번부터 구현한다.

### 3. 학습에 사용할 주소 규칙 확정

- host LBA는 4 KiB, 논리 슬라이스와 NAND page data는 각각 16 KiB임을 기존 코드에서 확인했다.
- 모델 입력 x는 그룹 내부 offset, 출력 y는 기존 VSA 정수로 정했다.
- 초기 모델의 PPN은 VSA를 의미한다. 실제 NAND row로의 변환은 기존 VSA 변환 경로를 사용한다.
- VSA는 die를 섞어 번호를 부여하므로 `VSA±1`을 같은 die·block의 물리 이웃으로
  취급하지 않는다고 명시했다. gamma>0의 이웃 좌표와 OOB 처리는 후속 단계에서 확정한다.
- 아직 주소 변환 함수나 물리 할당 정책을 변경한 것은 아니다.

### 4. 자원 상한과 메모리 예산 정의

| 설정 | 초기값 | 의미 |
|---|---:|---|
| 상주 그룹 수 상한 | 256 | 전체 그룹 수와 별개인 캐시 상한 |
| 그룹별 레벨 수 상한 | 256 | 여러 버전의 세그먼트를 관리할 레벨 한도 |
| 그룹별 세그먼트 수 상한 | 1,024 | 이전 레벨까지 포함한 한도 |
| 전체 상주 세그먼트 수 상한 | 65,536 | 상주 그룹들이 공유하는 총 한도 |
| 한 번의 그룹 단위 학습 입력 상한 | 256 | 임시 학습 입력 개수 |
| 그룹별 CRB 고유 LPN 수 상한 | 256 | 향후 CRB를 위한 상한 정의 |
| 그룹별 CRB byte 예산 | 512 bytes | 구분자 포함 예산이며 저장 형식은 미정 |
| 추가 LeaFTL 메타데이터 RAM 예산 | 8 MiB | 디렉터리·세그먼트·CRB·관리 정보·임시 공간 포함 |
| 세그먼트 저장 크기 목표 | 8 bytes | 논문 형식 목표이며 실제 C 구조체 크기를 의미하지 않음 |

이 값들은 초기 설정이다. 배열 할당, allocator, CRB, 메모리 배치는 아직 구현하지 않았다.
6번에서 실제 자료구조 크기와 기존 map·버퍼를 포함한 전체 DRAM 사용량을 계산한다.
한도에 도달하면 유효 매핑을 버리지 않고 compaction·writeback·eviction 또는
명시적 실패로 처리해야 한다는 규칙을 문서화했다. 해당 동작은 후속 단계에서 구현한다.

### 5. 자료형과 잘못된 설정 검사

- 8-bit byte와 32-bit unsigned int 환경인지 컴파일 시 확인한다.
- 미매핑 주소 표식으로 `0xffffffffU`를 정의했다.
- 상주 그룹 0개, 허용 범위 밖 레벨 수, singleton 256개를 수용하지 못하는
  그룹별 세그먼트 한도, 부족한 총 세그먼트 pool, RAM 예산 0을 컴파일 오류로 차단한다.
- BSP를 include하지 않는 헤더로 작성하여 다음 단계의 PC용 순수 C 모듈에서 사용할 수 있게 했다.

### 검증 결과

1번 작업 당시 MSVC C 모드 `/W4 /WX /Zs`로 검증했다.

- 통과: 헤더 중복 include, 그룹 경계 255/256, offset 0/255,
  빈 그룹·부분 그룹·전체 그룹의 개수, UINT_MAX에서의 그룹 수 계산.
- 의도대로 거부: `gamma=1`, 상주 그룹 0, 레벨 257, 그룹별 세그먼트 255,
  총 세그먼트 pool 512, RAM 예산 0.
- SDK 펌웨어 전체 빌드와 보드 업로드는 수행하지 않았다.

### 다음 작업

2번에서 `leaftl_segment.c/h`와 호스트 테스트를 추가하여 실제 세그먼트 학습,
예측, LPN 소속 판정을 구현한다.

<a id="step-2"></a>

## 2번 — 부동소수점 학습과 exact·CRB 소속 판정 (보정 반영)

기록일: 2026-11-12

- `leaftl_segment.c/h`의 학습은 double PLR, 실제 binary16 기울기 저장,
  저장 모델 전체 입력 재검증, `ceil(K*x+I)` 예측을 유지한다.
- 저장 stride 필드를 제거했다. exact는 Algorithm 2의 `ceil(1/K)` 규칙으로
  LPN 소속을 추론하고, 해당 규칙으로 표현되지 않는 집합은 그룹 CRB를 사용한다.
- irregular LPN 간격이 바뀌었다는 이유로 세그먼트를 나누던 구현을 제거했다.
  CRB 경로에서도 gamma=0 예측 정확성을 요구한다. gamma>0은 계속 차단한다.
- `LEAFTL_SEGMENT_INPUT`은 모델과 일시적 membership bitmap을 제공한다.
  런타임 node에 이 bitmap을 숨겨 보관하지 않는다.
- 새로 학습하는 singleton은 K=0이지만, merge로 한 점이 남으면 기존 K/I를 그대로
  평가한다. 더 이상 singleton 정규화로 계수를 바꾸지 않는다.
- runtime model은 MSVC x64에서 16 bytes이다. double 절편과 최종 8-byte 인코딩의
  차이는 후속 직렬화 작업으로 남는다.
- 검증: exact·CRB 양쪽을 포함한 6,371개 배치의 그룹 내 256 offset 대조 통과.
  binary16 codec은 Python 독립 oracle 321,548건과 일치했다.

<a id="step-3"></a>

## 3번 — 그룹별 CRB와 log-structured 삽입·조회 (보정 반영)

기록일: 2026-11-12

- 신규 `leaftl_crb.c/h`: 그룹마다 실제 LPN offset을 record별로 연속·정렬 저장하고,
  record는 SLPA 순으로 정렬한다. offset은 CRB 전체에서 중복되지 않는다.
- null byte 대신 record 경계 bitmap을 사용하여 offset 0과 255를 모두 지원한다.
  논문의 논리 CRB 구조를 구현하되 물리 delimiter encoding은 이식용 선택이다.
- 새 매핑이 이전 CRB offset을 덮으면 기존 record 및 segment의 SLPA/끝을 같이
  갱신한다. 시작 LPN 충돌, 빈 record 삭제, 새 exact의 이전 CRB 덮어쓰기도 처리한다.
- `LeaFtlIndexInsert(..., workspace)`는 입력/CRB/merge/레벨 변경을 caller-owned
  transaction plan에서 수행한 뒤 자원을 확인하고 commit한다.
- 같은 레벨의 범위 비중첩, 다른 레벨의 범위 중첩 허용, 최신 레벨 우선 조회를 유지한다.
  각 범위 hit의 소속 확인은 exact 공식 또는 실제 그룹 CRB를 사용한다.
- group/level/node pool이 부족하면 CRB를 포함한 기존 인덱스를 보존한다.
- CRB 단독 검증: 3,000회 갱신, 768,000회 owner 대조, 전체 256 record와 0/255 통과.

<a id="step-4"></a>

## 4번 — 논문식 seg_merge·삽입 중 merge·레벨 간 compaction (보정 반영)

기록일: 2026-11-12

이번 보정은 이전 stride survivor 재분할·동일 계수 coalescing·그룹 전체 interval
재배치 방식을 대체한다. 상세한 다섯 가지 반영표와 남은 차이는
[#4단계 - 논문 대조 및 보정 결과.md](%234%EB%8B%A8%EA%B3%84%20-%20%EB%85%BC%EB%AC%B8%20%EB%8C%80%EC%A1%B0%20%EB%B0%8F%20%EB%B3%B4%EC%A0%95%20%EA%B2%B0%EA%B3%BC.md)에 기록했다.

| 수정 항목 | 현재 구현 |
|---|---|
| 실제 소속과 CRB | exact `ceil(1/K)`, approximate 그룹 CRB. 별도 저장 stride 제거 |
| compaction 진행 | 상·하위 seg_merge 후 상위 세그먼트를 하위에 같은 SegUpdate로 반영하고 빈 레벨 제거 |
| merge 의미 | `old_members & ~new_members`로 이전 시작·길이 갱신 및 빈 이전 세그먼트 제거 |
| 삽입 시 merge | L0 삽입 중 즉시 SegMerge. 범위 겹침이 남으면 하위 이동, 하위 충돌은 새 레벨 |
| 계수 유지 | 살아남은 모든 세그먼트의 K/I 비트 유지. 한 점만 남아도 변경하지 않음 |

compaction은 원래 레벨을 아래쪽부터 유한 순회하고 새 충돌 레벨을 같은 pass에서
재귀 재방문하지 않는다. 임의 survivor 재학습이나 coefficient coalescing은 수행하지
않는다. exact의 내부 오래된 주소는 논문의 bounds-only merge처럼 상위 최신 segment에
가려져 남을 수 있다. '전체 레벨에서 모든 exact LPN은 한 번만 등장'한다고 주장하지 않는다.
CRB membership 유일성과 최신 조회 정확성은 유지한다.

검증 결과:

- 최종 optimized 전체 suite: 4,000회 exact·CRB 혼합 갱신, compaction 1,888회,
  정답 map 조회 12,083,200회 통과.
- Figure 13 T0–T8을 구성하여 T8의 단일 L0 범위가 `[0..15] [16..31] [32..90]
  [200..255]`인지 직접 확인했다. T5의 이전 exact, T6의 CRB 소속 조회도 검사했다.
- SLPA 충돌/재명명, exact-over-CRB 삭제, exact·CRB singleton의 K/I bit 보존,
  하위 레벨 충돌 생성과 tree/list/CRB/free pool 불변식을 검사했다.
- 앞선 입력이 CRB 키를 바꾼 뒤 후속 입력의 공간 부족으로 실패하는 batch도
  전체 기존 상태가 byte 단위로 유지됨을 확인했다.
- 사용자 요청에 따라 마지막 대규모 debug 반복은 축소했다. 최종 debug 핵심 suite
  `--targeted`는 38,912회 조회와 7회 compaction을 통과했다. CRB·모델 단독 debug와
  optimized 검증은 이미 수행했다.

생성·수정 파일: `src/leaftl_crb.c/h`, `src/leaftl_segment.c/h`, `src/leaftl_index.c/h`,
`src/leaftl_config.h`의 안내 문구, `tests/test_leaftl_crb.c`,
`tests/test_leaftl_segment.c`, `tests/test_leaftl_index.c`, `tests/README.md`,
이 보고서 및 논문 대조 문서. 기존 DFTL 실행 경로는 수정하지 않았다.

현재 MSVC x64 크기: CRB 292 bytes, group 308 bytes, level 16 bytes,
node 32 bytes, caller-owned transaction workspace 46,392 bytes.
기존 7,712-byte 작업 공간과 달라졌으므로 6번에서 새 크기를 DRAM 예산에 포함한다.
firmware stack에는 올리지 않으며 caller-owned 별도 영역을 계획한다.

전체 8-byte 저장 형식, 타깃 FPU/BSP 확인 및 필요 시 고정소수점 전환은 후속 작업이다.
연산 표현을 바꾸면 동일한 소속·계수·주소 정확성 검증을 다시 수행한다.
CRB 작업은 기존 14번 계획에서 앞당겨 이번 단계에 반영했다. 14번 이후에는
Flash 저장·복구 및 gamma>0/OOB 연계 검증이 남는다.
100만 write 주기·firmware 호출·Flash backing·GC·OOB·전원 복구는 아직 연결하지 않았다.
보드 테스트는 13·17·19번 후 총 3회 일정 그대로다. 다음 작업은 5번 매핑 인터페이스이다.


<a id="step-5"></a>

## 5번 — 통합 매핑 인터페이스

기록일: 2026-11-12

- 신규 `leaftl_mapping.c/h`: published 조회, 정렬된 그룹 배치 학습·preflight,
  token 기반 pending/완료/실패/취소, BUSY·STALE·주소 상한·용량 오류를 구현했다.
- `leaftl_index.c/h`: commit하지 않는 `LeaFtlIndexCanInsert`를 추가했다.
  preflight와 실제 삽입이 동일한 planner를 사용한다.
- 모든 program 성공 전에는 새 VSA를 공개하지 않는다. pending 읽기는 이전 공개 매핑만
  반환하며 buffer를 통한 최신 데이터 처리는 7~9번 연결 범위이다.
- 한 context에 pending 배치 하나, index 독점 사용 계약이다. 실패·취소는 이전 매핑을 보존하고,
  token wrap·이전 완료 호출을 거부한다. NAND orphan 정리·기존 VSA 무효화·FLUSH/FUA는 후속이다.
- prepare/publish/failure/abort/BUSY/STALE/NO_SPACE/token 소진과 범위 핵심 테스트를 통과했다.

<a id="step-6"></a>

## 6번 — OpenSSD DRAM 배치와 경계 검사

기록일: 2026-11-12

- 신규 `leaftl_memory.c/h`: mapping/index/group·CRB/level/node/workspace/학습/정렬/
  directory 예비 영역을 실제 sizeof와 64-byte 정렬로 배치하고 명시적 초기화 API를 제공한다.
- `memory_map.h`: 기존 주소들을 유지하면서 DFTL 뒤에 새 arena를 예약한다.
  RAM 예산·DRAM 끝·cached 영역·DMA/completion 경계·정렬 ID 용량을 컴파일 시 검사한다.
- `lscript.ld`: code/data/heap/stack을 1MiB 코드 창으로 제한하고 NVMe 경계를 ASSERT한다.
  workspace 46,392 bytes 등 큰 배열은 stack이 아니라 arena에 둔다.
- `.cproject`: 기존 Cosmos+ OpenSSD Cortex-A9/VFPv3/hard-float/BSP 경로는 유지하고 libm을 연결한다.
- 기존 미사용 full forward map, reverse map, CMT/GTD도 유지·집계했다. directory 예비 16B/group은
  미래 Flash format을 구현한 것이 아니며 11번에서 실제 형식을 결정한다.
- 가상 8채널·16die/MSVC x64에서 새 arena는 3,810,752 bytes(약 3.63MiB)로 8MiB 예산 이내다.
  시작 0x1c27fc80, end-exclusive 0x1c622240이다. 실제 BSP 확인 수치가 아니다.
- 기본 forward/reverse map은 각각 32MiB이며 비용 집계에서 제외하지 않았다.
- arena 정렬·크기 검사 및 잘못된 설정 4종 compile-fail, 32-bit host compiler 정적 검사 통과.
- planner 분리 후 기존 index 핵심 회귀만 실행해 38,912회 조회·7회 compaction을 통과했다.
  이번에 대규모 무작위 재검증은 수행하지 않았다.
- SDK XML과 linker 경계 선언을 확인했다. 실제 ARM SDK 빌드·GNU 링크·보드 업로드는 미수행이다.

파일·API·주소 표·한계와 재현 방법은 [#5-6단계 - 매핑 인터페이스와 DRAM 배치.md](%235-6%EB%8B%A8%EA%B3%84%20-%20%EB%A7%A4%ED%95%91%20%EC%9D%B8%ED%84%B0%ED%8E%98%EC%9D%B4%EC%8A%A4%EC%99%80%20DRAM%20%EB%B0%B0%EC%B9%98.md)와
`tests/README.md`에 기록했다. 생성 테스트는 `test_leaftl_mapping_memory.c`와 테스트 전용 BSP stub이다.
5·6번은 구현했고, 기존 DFTL의 NAND/NVMe 실행 경로 호출 연결은 7번 이후 작업이다.


## 추가 수정 — 논문 필드 크기의 8-byte 세그먼트

기록일: 2026-11-12

- `LEAFTL_SEGMENT`를 SLPA 1B + L 1B + binary16 K/type 2B + uint32 I 4B,
  실제 8 bytes로 바꿨다. sizeof와 필드 위치를 compile-time 검사한다.
- group ID는 모델에서 제거해 그룹 인덱스 문맥으로 관리한다. 끝 offset 대신 L을 저장하고
  merge는 first/L만 갱신하므로 K/I bit 보존을 유지한다.
- 학습은 double 연산을 유지하되 저장 가능한 uint32 절편 구간을 구해 재검증한다.
  음수/소수 절편 등 표현 불가능한 구간은 분할한다. 주소 손실이나 숨겨진 double 절편은 없다.
- endian을 고정한 Encode8/Decode8을 추가했다. 이것은 Flash page/CRB 전체 영속성 구현과 구별한다.
- runtime node 20B, 학습 입력 44B, workspace 31,032B로 줄었다. 가상 8채널 arena는
  3,007,936B이며 기존 시작 주소는 유지하고 끝은 0x1c55e240으로 줄었다.
- 모델 핵심 9개 배치 × 256 offset, 8B golden vector/roundtrip, 큰 주소·L=255·불가 절편 분할,
  Figure 13/CRB/계수 보존/실패 처리 38,912회 조회·7회 compaction 및 매핑·RAM 경계 검사 통과.
- 대규모 무작위 재검증 없이 최소 검증만 진행했다. 실제 ARM SDK 빌드·보드 업로드는 미수행이다.

상세한 필드 배치·uint32 I 선택의 한계·메모리 변화는 [#2단계 - 8바이트 세그먼트 표현.md](%232%EB%8B%A8%EA%B3%84%20-%208%EB%B0%94%EC%9D%B4%ED%8A%B8%20%EC%84%B8%EA%B7%B8%EB%A8%BC%ED%8A%B8%20%ED%91%9C%ED%98%84.md)에 기록했다.
이 수정은 앞선 기록의 16-byte model/32-byte node/46,392-byte workspace 수치를 대체한다.


## 이후 단계 기록 형식

각 단계 완료 시 아래 항목을 추가하고 진행 현황을 갱신한다.

- 단계 번호·제목·기록일
- 목적과 생성·수정 파일
- 주요 구현 내용과 설계 결정
- 실행한 검증 및 결과, 미실행 검증
- 남은 작업과 다음 단계


<a id="step-7-8"></a>

## 7·8번: 정렬 writeback 및 실제 할당 연결 (2026-11-12)

- 추가: `leaftl_writeback.c/h`(수집·ID 정렬·학습·완료 후 발행), `leaftl_cosmos.c/h`(OpenSSD 연결), `tests/test_leaftl_writeback.c`.
- 수정: `data_buffer.c/h`, `request_transform.c`, `request_allocation.c`, `address_translation.c`, `ftl_config.c`, `leaftl_memory.h`, `leaftl_mapping.h`, `leaftl_config.h`.
- DMA/NAND 기존 작업을 drain한 뒤 dirty 버퍼 ID를 LPN 오름차순으로 정렬한다. payload는 이동하지 않는다. 단일 펌웨어 스레드에서 동기 flush가 버퍼를 독점하므로 별도 pin 비트 없이 재사용을 차단한다. 같은 LPN의 host overwrite는 기존 hash entry에 덮어써 최신 데이터를 유지한다. 중복 cache entry 입력은 할당 전에 거절한다.
- **정렬 → 새 die/block의 연속 usable page 예약 → 실제 PPN으로 그룹별 학습/인덱스 용량 사전 확인 → NAND program 완료 확인 → 학습 매핑 발행 → old reverse map 무효화 → clean 전환** 순서. 그룹 경계에서 물리 주소를 재할당하지 않는다. block 크기를 넘으면 다음 run으로 분할한다. partial flush도 같은 API를 사용한다.
- PPN은 `die * USER_PAGES_PER_DIE + block * USER_PAGES_PER_BLOCK + page`로 변경했다. 기존 interleaved VSA는 `die + USER_DIES * (block * USER_PAGES_PER_BLOCK + page)`로 변환한다. raw NAND row가 아니며 기존 bad-block/SLC 변환은 이후 단계에서 적용된다. 초기 1번의 PPN=VSA 설명은 이 쓰기 경로에 대해 대체한다.
- 실패한 그룹은 매핑을 발행하지 않고 dirty와 기존 매핑을 보존한다. 앞서 성공한 그룹은 유지한다(전체 flush 원자성 아님). 미발행 예약은 재사용하지 않고 해당 block을 seal한다. 버퍼를 재사용할 수 없으면 slice 요청을 큐 head에 그대로 두고 반환한다. grown-bad 복구/기존 같은 block 데이터 보호는 9번의 후속 오류 처리 대상이다.
- 논문 §3의 sorted buffered write 및 공식 `FlashMetadata.update_block()`의 `sorted(extents) → next_free_block → 연속 PPN → mapping update` 원칙 반영. 데이터 program 성공 확인 후 매핑 공개는 실제 장치용 완료 순서를 명시한 것이다. 원본: https://github.com/platformxlab/LeaFTL/blob/main/wiscsee/wiscsim/learnedftl.py . 현재 program은 page별 동기 실행이라 논문 성능/병렬성과 동일하다고 주장하지 않는다. 부분 run의 block 잔여 공간은 재사용하지 않아 WAF/공간 효율은 후속 조정 대상이다.
- 실행 연결: InitFTL → CosmosInit, buffer miss/eviction 직전 flush, NAND queue 완료 hook, AddrTransRead의 learned lookup. **`LEAFTL_FIRMWARE_WRITEBACK=0` 기본값 유지**: GC·Flash backing 연결 전이므로 기존 DFTL이 기본 실행 경로다. 1로 빌드하면 새 경로가 연결되며, 공간 부족 시 legacy GC로 우회하지 않고 중단/재시도한다. 전환 시 기존 DFTL 매핑을 이관하지 않는다. 빈 실험 namespace에서만 활성화할 것. 최종 기본 전환은 13번.
- host FLUSH/FUA의 명령 완료·영속성 계약은 아직 구현하지 않았다(기존 핸들러 동작). CosmosFlush는 dirty data writeback 함수이며 NVMe 영속 flush 완성을 의미하지 않는다. 18번에서 metadata checkpoint와 함께 연결 필요.
- 최소 검증 통과: ID 정렬 후 할당, 그룹 경계 연속 PPN, pending read 비노출, 그룹 중간 program 실패 시 old map/dirty 유지, 재시도, busy/empty/중복 거절, 기존 mapping/arena 검사. 핵심 모듈 MSVC `/W4 /WX`; 연결부 5개 C 파일 host BSP stub syntax 검사 통과(기존 32-bit 주소의 x64 pointer 변환 경고만 존재). Xilinx ARM 링크/실제 NAND·보드 테스트 미실행.
- scratch 5,124 bytes를 고정 RAM arena에 추가(large stack 없음). synthetic 8ch/16die 전체 예약 3,013,120 bytes, end-exclusive `0x1c55f680`. 메모리 문서의 이전 총량은 이 값으로 대체. 기존 raw flush-ID 예약 1,024 bytes는 남겨 두었고 실제 정렬은 scratch의 ID/LPN 레코드를 사용한다.


<a id="step-9"></a>

## 9번 — 완료·오류 처리 (2026-11-12)

`leaftl_cosmos.c/h`·`request_transform.c`에 NAND 완료 tag/code 검사, 읽기 실패 시 DMA 차단·남은 command slice 취소·DMA 완료 후 단일 NVMe 오류 완료, 미매핑 페이지 0 초기화, 물리 주소 범위 검사·실패 block 격리를 구현했고 실제 adapter의 완료/실패/재시도 최소 PC 테스트를 통과했으며, 성능보다 정확성을 우선한 slice별 동기 DMA 방식이고 ARM SDK·NVMe 하드웨어 동작 및 grown-bad 데이터 복구·영속성 검증은 미실행이다.

<a id="step-10"></a>

## 10번 — GC 연결 (2026-11-12)

`leaftl_gc.c/h`·`garbage_collection.c`·`address_translation.c`·`leaftl_cosmos.c/h`·`leaftl_memory.h`에 최신 learned map으로 valid 판정→LPN 정렬→실제 relocation PPN 재학습→복사 성공 후 발행→erase 성공 후 free 반환을 연결하고 reserve 부족 시 learned GC를 호출하도록 했으며 stale reverse·read/program/erase 실패·부분 성공 후 재시도 최소 PC 테스트와 연결부 문법 검사를 통과했고, 8번의 매번 새 block 할당/실패 예약 seal 방식을 연속 tail 재사용/미발행 예약 회수·실제 발행 page count 보존으로 보강했으며 host flush와 별도 GC scratch 5,124 bytes를 추가해 synthetic 8ch/16die arena가 3,018,304 bytes(end-exclusive `0x1c560ac0`)이고 기본 활성화는 여전히 13번·실제 보드 검증은 미실행이다.


<a id="step-11"></a>

## 11번 — segment·CRB Flash 형식·디렉터리 (축소 반영)

초기 48B 헤더·16B 디렉터리·전용 staging 설계는 아래 내용으로 대체한다: segment는 8B를 유지하고 그룹별 중복 magic/version/generation/gamma/PPN mode/CRC/전체 길이를 제거해 헤더를 **12B**(그룹 ID u32 + 레벨/segment/CRB offset/CRB record 개수 u16씩)로, RAM 디렉터리를 **8B**(`VSA u32 + page offset u16 + length u16`)로 줄였으며, 복원에 필요한 레벨별 count·CRB 경계·범위/membership 검사·실패 시 기존 디렉터리 보존·resident 그룹 stale-load 차단은 유지했다.

체크섬은 손상 검증을 위해 **페이지당 CRC32 4B 하나**만 유지하고 `StoreMany`로 여러 그룹을 같은 페이지에 묶어 저장하도록 했으며(개별 `Store` 호출은 여전히 한 페이지이므로 12번 캐시에서 묶음 API 사용 필요), 별도 16KiB staging은 기존 `RESERVED_DATA_BUFFER_BASE_ADDR` 공용 uncached 버퍼로 대체하고 사용하지 않던 1KiB flush-ID 예약을 제거했다; 공용 버퍼는 NAND/DMA가 참조하는 동안 독점해야 하고 DFTL metadata I/O와 동시 사용하면 안 된다.

synthetic x64 8ch/16die 고정 arena는 **3,034,752 → 2,755,200 bytes**, 즉 **279,552 bytes(273KiB) 절감**, 디렉터리는 512→256KiB, end-exclusive는 `0x1c520700`; 헤더 48→12B는 Flash record 감소분으로 RAM 절감에 중복 합산하지 않았으며 segment node의 필수 연결/트리 정보(8B model+12B index=20B), 그룹/레벨/CRB와 transaction scratch는 유지했다.

최소 PC 검증: 두 그룹의 한 페이지 packing·offset 복원·512개 LPN 조회 일치, 독립 CRC 대조·손상/잘림/잘못된 그룹/CRB 거절, read/program 실패 시 상태 보존, 두 페이지 중 두 번째 실패 시 성공한 prefix만 발행, busy/pending/capacity 거절 통과; 실제 NAND metadata pool/I/O·cache eviction/GC는 12번, checkpoint/형식 식별/복구 정책은 18번이며 이전 초기 Flash 형식과 호환되지 않고 현재 실험은 RAM-backed 모의 Flash라 실제 데이터 이관은 없으며 ARM SDK·보드·성능 검증은 미실행이다.


<a id="step-12"></a>

## 12번 — Flash-backed 캐시·지연 merge·metadata GC (2026-11-12)

`leaftl_cache.c/h`·`leaftl_metadata.c/h`를 추가하고 매핑 API·Cosmos NAND 완료 경로에 연결했다: 공개 `FrameLogPLR`처럼 **쓰기 miss는 기존 Flash를 선복원하지 않고 RAM delta만 생성**, RAM hit은 I/O 없이 조회하며 RAM miss 또는 dirty eviction에서 이전 Flash snapshot과 RAM의 새 레벨을 합쳐 최신 값/기존 다른 LPN/CRB 소속을 보존한다; byte 예산(기본 1MiB, 실제 group+level+node 크기 기준)의 그룹 LRU·dirty/complete 상태를 추가하고 dirty victim은 최대 8개씩 묶어 저장한 뒤 성공한 그룹만 제거하며 clean victim은 재쓰기 없이 제거하고 pending 중 eviction/Flash merge는 금지한다.

메타데이터는 기존 DFTL 예약 translation block 목록을 재사용하되 DFTL의 고정 page 형식·GC 함수는 사용하지 않고, 전용 풀에서 한 erase block을 남겨 둔 채 유효 page만 복사→해당 page를 공유하는 모든 directory VSA 갱신→원본 erase 순서로 회수하며 read/program/erase 실패 시 원본 참조를 잃거나 실패 page를 재사용하지 않는다; 유효성 판정은 작은 pool과 8B/group directory의 bounded scan을 사용한다.

논문 §3.8의 GMD 기반 on-demand fetch와 공개 `FrameLogPLR.update/lookup/flush`의 256-LPN 그룹 LRU·RAM delta/Flash 지연 merge를 반영했으며 앞서 제안했던 write-miss 선복원은 채택하지 않았다; 실제 NAND 완료 후 발행, clean eviction의 불필요한 재쓰기 생략, 최대 8개 victim 묶음, merge 시 기존 seg_merge로 중복 정리는 장치 이식용 선택이며 공개 simulator와 byte-for-byte 동일한 캐시 구현이라고 주장하지 않는다 (https://github.com/platformxlab/LeaFTL/blob/main/wiscsee/wiscsim/learnedftl.py).

추가 상태는 group당 4B(cache flags/LRU), 캐시 작업 ID 목록·작은 pool descriptor이며 synthetic x64 8ch/16die arena는 **2,755,200 → 2,758,336 bytes(+3,136B)**, mapping context 72B, end-exclusive `0x1c521340`; segment 8B·directory 8B는 유지하고 DMA read/metadata GC/merge 보호용 작업 버퍼는 기존 2MiB reserved uncached 영역의 고정 offset을 독점 차용한다(전용 대형 arena 배열 추가 없음, 공용 영역 안에서 실제 사용하는 작업 공간은 늘어나므로 전체 메모리 사용 집계에 숨기지 않음).

최소 검증은 2-group RAM 캐시·3-block×2-page 모의 NAND로 write miss/RAM hit 무읽기, 지연 merge·hole·최신값, eviction merge, pending pin, packing, 반복 metadata GC와 program 실패 시 dirty RAM 보존을 확인했고 기존 Flash/arena/실제 Cosmos adapter 완료 테스트 및 연결부 host 문법 검사를 통과했다; 실제 OpenSSD NAND·ARM SDK 링크와 성능·복구 검증은 미실행이며 기본 활성화는 여전히 13번, checkpoint/전원 복구 및 영속 FLUSH/FUA는 18번이다.


## SDK Import 이름 충돌 수정

.project의 앱 이름을 LeaFTL-Work로 변경하고 .cproject의 Debug/Release buildPath 및 앱 project ID를 맞췄으며 기존 run-gftl3_bsp 참조·include/lib 경로는 유지했고 XML 파싱과 이름/경로 검사를 통과했다.


<a id="step-13"></a>

## 13번 — gamma=0 통합·기본 LeaFTL 전환 (보드 검증 대기)

`LEAFTL_FIRMWARE_WRITEBACK=1`을 기본값으로 전환하고 초기화 실패 시 namespace를 시작하지 않도록 해 DFTL로 조용히 fallback하는 경우를 막았으며, 시작 로그에 LeaFTL gamma=0/DFTL 모드를 표시하고 learned 모드의 legacy `AddrTransWrite` 진입을 차단했다; DFTL CMT 초기화는 baseline 모드에서만 수행하되 기존 translation block 목록은 learned metadata pool에 재사용하고, 잘못된 주소의 반환값을 명시했으며 SDK의 BSP include/lib 경로를 workspace의 `run-gftl3_bsp` 기준으로 정리했다.

`tests/test_leaftl_firmware_path.c`는 실제 request_transform/request_allocation/data_buffer/address_translation/garbage_collection/Cosmos/learned 모듈을 함께 실행하고 DMA·NAND 장치만 모의했다: 다중 slice 명령의 단일 완료, full/partial/unmapped RMW 데이터 비교, 257개 host page로 자동 dirty eviction, NAND readback, valid page를 포함한 data GC, cache/metadata GC, 다중 slice read 실패 시 DMA 차단·단일 오류 완료, program 실패 시 old map 보존/재시도와 요청 슬롯 전체 반환을 확인했다; 마지막 독립 phase에서 NAND read 406·write 561·data erase 1·metadata erase 4를 수행했고 기존 cache/Flash/arena/Cosmos 완료 최소 회귀도 통과했다.

**실제 Xilinx SDK 2019.1 ARM compiler + 기존 Micron OpenSSD BSP**(`E:/OpenSSD_Work/CosmosPlus_Micron_19_1/cosm-plus-sys.sdk/run-gftl3_bsp/ps7_cortexa9_0`)로 전체 C 27개 파일을 Cortex-A9/VFPv3/hard-float 옵션으로 컴파일·링크해 `C:/Users/kimeu/Documents/LeaFTL-validation/step13/arm/LeaFTL-Work.elf`를 생성했다; 기존 NAND/host 드라이버 경고는 남아 있으며 경고 없는 전체 빌드라고 주장하지 않는다. 실제 BSP/ARM ABI에서 8ch/16die, segment 8B, index node 20B, group 312B, mapping context 48B, learned arena **2,758,144B**, base `0x1c27fc80`, end-exclusive `0x1c521280`을 산출했다(기존 x64 2,758,336B와 구분).

실제 보드 다운로드·NAND 타이밍/오류 복구·성능 검증은 아직 하지 않았으므로 13번 전체 완료가 아닌 **보드 1회차 대기** 상태이며, 기존 DFTL 데이터를 이관하지 않는 빈 namespace 실험용이고 checkpoint/재부팅 복구·영속 FLUSH/FUA는 18번 범위 그대로다. 보드 첫 실행에서는 부팅 로그의 `[ FTL: LeaFTL gamma=0; ... ]`를 확인해야 한다.


## 13번 실험용 보관본

`C:/Users/kimeu/Desktop/SSS-Lab/LeaFTL-Step13-gamma0`와 동일 이름 ZIP에 13번 코드·테스트·문서 및 검증된 ARM ELF/map/log를 복제했으며, 보관본 SDK 앱 이름만 `LeaFTL-Step13-gamma0`로 구분하고 source/test 파일 및 ELF SHA-256 일치와 ZIP 무결성을 확인했다. 이후 구현은 이 작업본에서 계속하고 보관본은 실험 기준으로 유지한다.


<a id="step-14"></a>

## 14번 — gamma>0 모델·CRB 확장

`LEAFTL_GAMMA`를 0–16 범위로 확장하고 `ceil(K*x+I)`의 오차 band와 float16 양자화 후 모든 입력의 ±gamma를 재검증하도록 했으며, 비영 오차 또는 불규칙 소속은 반드시 CRB type으로 표시하고 singleton·실제 LPN 소속·계수 유지 규칙을 보존했다; 부분 flush에서 미기록 page를 예측하지 않도록 선택 모델의 예측을 해당 학습 point의 실제 PPN span 안에 두고 물리 PPN이 불연속이면 분할하는 보수적 제약을 추가했다(논문의 full-block 전제에 대한 이식 선택이며 예측값 clamp나 별도 정답 map을 저장하지 않음).

최소 검증: gamma=0/1/4/16에서 각각 2/2/1/1 segments와 0/2/3/5개 비영 오차 사례를 확인했고, ±gamma·CRB hole·SLPA overwrite/rekey·compaction·Flash roundtrip·최대 PPN·물리 gap 분할을 통과했다; segment는 계속 8B이며 gamma=16 모델/OOB 모듈의 ARM 컴파일도 통과했다.

<a id="step-15"></a>

## 15번 — OOB 이웃 reverse mapping 기록

논문 §3.5/Figure 11의 `2*gamma+1`개 full LPN을 `[PPA-gamma..PPA+gamma]` 순서로 사용자 spare에 기록하는 `leaftl_oob.c/h`를 추가하고, host writeback·GC relocation의 전체 정렬 run 정보를 사용해 아직 program하지 않은 이웃/다른 LPN 그룹의 주소까지 기록하도록 연결했으며 원래 NAND 요청의 spare DMA 경로(`GenerateSpareDataBufAddr → V2FProgramPageAsync`)를 재사용해 request struct와 controller/ECC parity 형식은 늘리거나 바꾸지 않았다.

사용자 spare 256B 중 첫 aligned word 4B는 bad-block marker 가능성을 고려해 FF로 유지하고 LPN 배열을 offset 4부터 기록한다: gamma=16에서 총 136B, 나머지는 FF; 물리적인 raw MLC row나 VSA 정수 ±1이 아니라 **같은 die/block의 usable-page PPN 좌표**를 사용하고 block 밖/부분 flush의 알려지지 않은 이웃은 NONE 처리하며 metadata program은 spare 전체를 FF로 초기화하고 GC는 목적지 PPA 기준으로 다시 생성한다. 실제 controller의 user-spare/marker 배치 및 ECC 동작은 보드에서 추가 확인해야 한다.

최소 검증: OOB 보정용 reverse lookup의 LPN/PPN 0·255, block 경계, 다른 LPN 그룹의 미래 이웃, 잘못된 크기/모호한 중복/알 수 없는 center 거절을 확인했고 실제 request/FTL 통합 테스트에서 모든 host·GC data program의 own LPN 및 metadata spare FF를 검사해 통과했다; gamma=0 전체 ARM SDK 빌드(28 C 파일)와 기존 회귀도 통과했다. writeback 작업 구조에 run 정보 12B씩 추가했지만 기존 정렬 여유에 들어가 고정 arena 총량은 변하지 않았고 추가 per-segment RAM/OOB header·대형 배열은 없다.

**기본 firmware gamma=0 유지**: 16번의 실제 read 완료 후 OOB 검증·오예측 재읽기, old-PPA 무효화 및 GC valid 판정 보정을 연결하기 전에는 gamma>0 Cosmos 펌웨어를 compile-time 거절한다(순수 모델/OOB 테스트는 가능). 실제 보드 검증·성능 검증은 미실행이며 13번 별도 보관본의 모든 파일 SHA-256은 그대로임을 확인했다.


<a id="step-16"></a>

## 16번 — OOB 검증·오예측 보정·gamma>0 실행

host 읽기/RMW는 NAND 완료 후 OOB에서 요청 LPN을 검증하고 필요하면 한 번만 정확한 PPA를 재읽은 뒤 **두 번째 페이지 자신의 LPN**까지 확인해야 DMA를 허용하도록 했으며, old-PPA 무효화·GC valid 판정에는 OOB exact resolver를 연결해 예측 주소를 실제 주소로 오인하지 않도록 했다; resolver는 dirty host/GC 버퍼를 덮지 않는 기존 reserved 영역의 16KiB+256B 작업 구간을 차용하며 추가 per-LPN 정답 map·상주 대형 배열은 없고 고정 arena 총량도 그대로다.

실제 firmware C 경로를 모의 NAND/DMA에 연결해 gamma=0/1/4/16을 실행했고 sparse 사례에서 각각 0/2/3/5건의 자연 오예측을 확인해 비영 오차마다 정확히 한 번의 추가 읽기로 payload를 일치시켰으며, 잘못된 OOB·두 번째 페이지의 잘못된 own LPN·재읽기 실패 때 DMA 차단, old-PPA 보정 실패 시 dirty/old map 보존, 정확한 old page 무효화, approximate valid-page GC와 기존 full/partial/RMW/cache/metadata GC 경로를 통과했다.

14·15번의 gamma>0 compile 금지를 해제하고 **SDK Debug/Release compiler define을 `LEAFTL_GAMMA=4`로 설정**했다(라이브러리 헤더 fallback은 0 유지, 다른 gamma는 compiler define 값으로 선택); SDK 2019.1 + 기존 Micron BSP로 C 28개 전체 ARM 컴파일·링크를 통과했으며 실험 ELF는 `C:/Users/kimeu/Documents/LeaFTL-validation/step16/gamma4/arm/LeaFTL-Work.elf`이다. 실제 보드 실행·지연/성능 측정은 하지 않았고 OOB gamma 설정이 다른 namespace 데이터와 혼용하지 않으며 13번 gamma=0 보관본은 변경하지 않았다. 보드 검증은 사용자 일정에 따라 19번까지 구현한 뒤 보관 버전별로 진행한다.

<a id="step-17"></a>

## 17번 — OOB 보정·GC 통합 검증

- OOB가 미기록 페이지를 가리키면 NAND 요청 전에 차단한다. 예측·보정 주소 모두 실제 프로그램 완료 범위를 확인한다.
- GC OOB 검증 실패 시 원본을 보존하고, 아직 프로그램하지 않은 빈 목적지 블록은 반환한다. 부분 기록되거나 불량 처리된 블록은 반환하지 않는다.
- γ=0/1/4/16 실제 펌웨어 경로의 PC 모의 NAND 테스트 통과: 오예측 1회 재읽기, 잘못된 OOB·읽기 실패 시 DMA 차단, 부분 쓰기 RMW, GC 실패·재시도, 블록 말단 분할, LPN 255/256 경계와 Flash 캐시 미스. 기존 writeback/GC/cache/Flash/mapping 회귀 검증도 통과했다.
- SDK 2019.1 실제 Cortex-A9 γ=4 전체 28개 C 소스 컴파일·링크 성공. text/data/bss=149192/6936/28384 bytes. 기존 저수준 코드 경고는 남아 있다. ELF: `C:/Users/kimeu/Documents/LeaFTL-validation/step17/gamma4/arm/LeaFTL-Work.elf`.
- SDK 실행 설정은 γ=4이며, 실제 OpenSSD 보드 검증은 미실시(2회차 대기). 18번은 아래와 같이 실험용 쓰기 완료 연결로 범위를 조정했다.

<a id="step-18"></a>

## 18번 — 실험용 FLUSH/FUA 연결

사용자 요청으로 checkpoint·복구 저널·전원 장애 영속성은 구현 대상에서 제외했다. FLUSH는 앞선 slice/DMA → dirty data → dirty segment/CRB Flash 저장 완료 후 응답하며, FUA도 마지막 slice 완료 전에 같은 경로를 실행한다. 기존 요청의 여유 비트를 사용하므로 요청 구조체·매핑 RAM·Flash 형식은 늘리지 않았다. 데이터/매핑 program 실패는 명령 오류로 응답하고 재시도한다. **디렉터리·할당기는 RAM에 남으며 현재 부팅 경로는 NAND를 초기화하므로 재시작/전원 차단 복구를 지원하지 않는다. NVMe의 전원 장애 영속성을 충족하는 구현으로 해석하면 안 된다.**

<a id="step-19"></a>

## 19번 — 최소 계측·최종 PC 검증·실험 준비

- 고정 96-byte 누적 통계(`LeaFtlStatsGet/Reset/Memory`): host 요청 bytes, data/metadata read·program, GC program, erase, 보정 재읽기, I/O·sync 실패. per-LPN 배열/추적 버퍼/타이머 없음. 메모리 조회는 segment 8B 외 node·directory·arena 및 기존 full forward/reverse map도 보고한다. WAF는 `(data_programs+metadata_programs)*16384/host_write_bytes`; GC program은 data program의 부분집합이며 성공한 I/O 기준이다. 요청 bytes에는 실패한 host 요청도 포함되므로 성능 비교는 오류 없는 측정 구간을 사용한다.
- γ=0/1/4/16 PC 통합 통과: 실제 NVMe FLUSH/FUA, 여러 slice 완료 순서, 데이터/매핑 실패·재시도, OOB/GC/부분 쓰기/경계/cache miss, 카운터와 모의 NAND 실제 I/O 일치. 기존 핵심 회귀 검증 통과. 재실행: `python tests/run_firmware_validation.py` (Windows/MSVC, SSD 접근 없음).
- 실제 SDK 2019.1 ARM 전체 28개 C 빌드·링크 성공: γ=4 ELF `C:/Users/kimeu/Documents/LeaFTL-validation/step19/gamma4/arm/LeaFTL-Work.elf` (text/data/bss=150220/6936/28496), DFTL 선택 ELF `C:/Users/kimeu/Documents/LeaFTL-validation/step19/dftl-baseline/arm/LeaFTL-Work.elf`. 17번 대비 BSS +112B(통계 96B 포함·정렬), arena/segment/CRB 저장 형식 불변. 기존 저수준 경고는 남음.
- 실제 보드 무결성·지연/처리량·WAF·DFTL 비교는 미실시다. 동일 용량·data buffer·workload로 비교하고 mapping DRAM 예산 차이를 별도 기재한다. 보드 실험 전후 idle 상태에서 통계를 조회/초기화하며 fio JSON의 IOPS/bandwidth/latency를 기록한다. host 모의 실행 시간을 SSD 성능으로 사용하지 않는다. 13번 동결본은 유지한다.

### 13번 실제 OpenSSD 보드 기본 검증 결과

사용자 실행 로그 기준: γ=0, mapping cache 1048576B, 표시 용량 117904MB, Cosmos+ OpenSSD 장치 인식·NVMe 큐 생성 및 직접 I/O 열기 성공. 같은 부팅 세션에서 64MiB 순차 쓰기/전체 비교, 전체 덮어쓰기/최신값 비교, 1024페이지 무작위 덮어쓰기/전체 비교, 4KiB 부분 쓰기/나머지 12KiB 보존, LPN 255/256 그룹 경계 32KiB 쓰기/이웃 보존 모두 통과했다. 각 단계는 별도 16MiB 쓰기로 기존 dirty 버퍼의 NAND writeback을 유도했다. GC 발생·매핑 cache eviction/metadata GC·전원 복구·성능 비교까지 검증된 것은 아니다. 13번 동결본 파일은 수정하지 않는다.
### 보드 FLUSH Data Abort 조사·정렬 보강

γ=4 보드 기본 1~6번은 사용자 로그상 통과. 추가 FLUSH에서 host timeout→reset 실패→capacity 0 발생. 실제 Debug ELF의 DataAbortAddr=0x0012ace4는 memcpy 내부 STR 명령으로 확인했다. 비캐시/Strongly-ordered DMA 페이지의 packed CRB에 일반 memcpy를 사용하는 위험을 발견해 leaftl_flash.c의 encode/decode 4개 복사를 volatile byte 접근으로 변경했다. 저장 형식·8-byte segment·arena 크기는 불변. γ=4 PC FLUSH/FUA/OOB/GC 통합은 통과했으나, 실제 fault address/status와 호출자는 미확보이므로 이 수정으로 보드 장애가 해결됐는지는 재검증 필요하다. 13번 동결본 및 장애 당시 Debug ELF는 보존한다.
### 정렬 보강 후 실제 보드 7번 재검증

사용자 실행 로그상 수정 γ=4 Debug ELF에서 용량 117904MiB 정상 인식, 불규칙 LPN 112개 쓰기→명시적 NVMe FLUSH→16MiB 버퍼 교체→전체 비교(7-A), FUA=1 32KiB 쓰기→버퍼 교체→데이터 비교(7-B) 모두 통과했다. 이번 재현 시나리오에서 기존 FLUSH timeout/Data Abort는 재발하지 않았다. 실제 보정 발생 횟수(stats.corrections), GC/cache eviction, 전원 장애 복구는 이 결과로 검증된 것이 아니다.
### 8번 실제 OOB 보정 통계 확인

7번 통과 후 사용자 SDK 조회: corrections=0x29(41), data_reads=0x49b(1179), io_failures=0, syncs=4, sync_failures=0. γ=4 실제 보드에서 보정 재읽기 경로 진입을 확인했고, 앞선 7-A/7-B 데이터 비교가 통과했다. 통계는 host 읽기 외 old-PPA/GC 등 내부 resolver 읽기도 포함하므로 41/1179를 host 오예측률로 해석하지 않는다. 전원 복구·GC 발생·mapping cache eviction은 별도 검증 대상이다.
### 9번 매핑 cache eviction·Flash 재조회 보드 통계

320개 그룹 실험 전후 사용자 SDK 조회: metadata_reads 0→0x144(324), metadata_programs 4→0x0e(14), io_failures=0, sync_failures=0. 256개 resident group 상한을 넘는 workload에서 실제 매핑 Flash 읽기·쓰기가 발생한 것을 확인했다. 사용자가 이번 메시지에 데이터 비교 통과 출력은 별도로 첨부하지 않아, 데이터 무결성 최종 판정은 해당 출력 확인과 함께 한다. data GC·metadata GC 발생은 이 카운터만으로 확정하지 않는다.
### 수동 GC 디버그 기능 철회

디버거 함수 호출 지원 확인 전에 추가했던 수동 GC request/result 변수·main loop 호출부·관련 테스트 변경을 모두 철회했다. 현재 소스는 Flash CRB 바이트 복사 보강이 적용된 기존 검증 상태를 유지한다. step19-gc-debug 산출물은 철회된 실험용이므로 사용하지 않는다. GC 보드 검증은 아직 미완료다.

### 4GiB 자동 GC 보드 실험 통계

SDK Debug/Release 빌드에 EXP_LOGICAL_GB=4만 추가해 기존 실험 모드로 host 용량·할당 블록 범위를 축소했다(γ=4 유지, GC 알고리즘 변경 없음). UART 4096MB 및 host 4GiB 확인. 전체 4GiB 초기 쓰기→앞 1GiB 짝수 페이지 512MiB 덮어쓰기→전체 비교 실험 후 사용자 통계: gc_programs=0x5000(20480페이지, 320MiB), erases=0xa0(160회), io_failures=0, sync_failures=0. 자동 GC 유효 페이지 복사와 erase 발생을 확인했다. erases는 metadata erase도 포함하는 누적값이며 별도 분류 없이 모두 data GC erase로 단정하지 않는다. 최종 전체 데이터 비교 통과 문구는 이번 통계 메시지에 미첨부여서 최종 무결성 판정은 해당 출력 확인과 함께 한다.