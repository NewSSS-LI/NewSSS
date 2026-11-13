# DFTL → LeaFTL 변경 방향

이 문서는 구현 지침이다. 아래 모듈·동작은 아직 코드에 반영되지 않았다.

> 18번 범위 변경(사용자 요청): 원본 실험에 없는 checkpoint·복구 저널은 추가하지 않는다. FLUSH/FUA는 data·segment/CRB 쓰기 완료만 연결하며 전원 차단/재시작 영속성을 보장하지 않는다. 아래 초기 영속성 계획은 현재 적용 범위에서 제외한다.

## 1. 먼저 주소 체계를 맞추기

논문의 LPA/PPA와 펌웨어의 logicalSliceAddr/VSA가 같은 단위인지 확인한다. 논리 슬라이스·NAND page·host LBA의 크기 및 상호 변환을 기록한다. `FindFreeVirtualSlice`, `FindDieForFreeSliceAllocation`, VSA→die/block/page 계산을 먼저 추적한다.

Cosmos+는 채널/다이에 분산 할당한다. 따라서 정수 VSA의 차이 1을 논문의 물리적으로 인접한 PPA로 바로 해석하면 안 된다. 학습용 물리 순번과 NAND 주소 사이의 변환 및 OOB 이웃 범위를 명시적으로 정의한다. 연속성을 확보할 수 없으면 먼저 die/block별로 나누어 학습하는 방식과 병렬성 손실을 검토한다. 이는 논문 복사가 아닌 플랫폼 이식 결정이다.

## 2. 논문의 핵심 구조

근거: 로컬 논문의 §3.1–3.9, 인쇄면 445–450쪽(PDF 4–9쪽).

- 구간별 선형 모델로 여러 LPA→PPA 매핑을 표현한다. 조회식은 `ceil(K * LPA + I)`이다. singleton은 K=0으로 정확한 주소를 표현할 수 있다.
- 256개 연속 LPA를 한 그룹으로 나누고 그룹 내부 offset을 저장한다. 구현에서는 `group_id = lpn / 256`, `offset = lpn % 256`으로 구분한다. 논문 Algorithm 1의 그룹 접근 표기를 그대로 베끼기보다 본문 정의와 대조한다.
- 새 세그먼트는 최신 레벨에 넣고, 레벨 내 정렬·비중첩을 유지한다. 오래된 매핑을 최신 매핑이 가리고, 남은 유효 주소는 낮은 레벨에서 조회한다.
- approximate segment는 범위 내부라고 모두 유효한 LPN이 아니다. CRB가 실제 포함 주소를 판별한다. 정확한 segment도 stride/membership 판정이 필요하다.
- 정렬된 버퍼 flush로 매핑의 규칙성을 높인다. GC로 이동된 페이지도 새 물리 주소에 맞춰 재학습한다.
- gamma>0은 OOB 역매핑 검증과 오예측 보정이 함께 필요하다. 논문의 이웃 역매핑 메타데이터 준비 없이는 '한 번 추가 읽기' 성질을 주장할 수 없다.
- 학습 인덱스는 DRAM 캐시 외에 flash 저장·조회와 회복 절차가 필요하다.

논문 8바이트 세그먼트 형식은 목표 표현이다. C struct에 float나 포인터를 추가한 후 8바이트라고 계산하면 안 된다. slope 인코딩, type 비트, alignment, serialization 크기를 별도 검증한다. float16 양자화 후 오차 보장과 `ceil` 결과도 다시 검증한다.

## 3. DFTL에서 재사용할 부분과 교체할 부분

현재 읽기 접점은 `DftlGetMapping()`, 갱신 접점은 `DftlSetMapping()`이다. GC도 이 두 함수를 호출한다. 함수 이름만 바꾸는 것으로 LeaFTL이 되지 않는다.

재사용 후보는 NVMe 처리, 요청 큐·스케줄러, NAND 드라이버, bad block 관리, free block 관리다. 주소 변환, 버퍼 flush, 매핑 메타데이터 저장, GC 갱신은 서로 연결해 변경해야 한다.

`dftl_gtd.c`는 4바이트 VSA 배열과 MVPN header를 전제로 한다. 가변 개수 세그먼트와 CRB를 그 배열에 그대로 넣을 수 없다. version, group ID, 길이, generation, checksum을 포함하는 저장 형식과 디렉터리 갱신 순서를 설계한다. translation block GC도 새 형식을 이해해야 한다.

`memory_map.h`에서 기존 CMT/GTD 영역 뒤에 무작정 배열을 추가하지 않는다. 링크 영역·DMA·NAND 버퍼·스택과의 경계, 최대 segment/CRB/level 수를 계산한다. 포인터·allocator overhead와 아직 할당된 기존 full map도 실제 RAM 사용량에 포함한다.

## 4. 구현 순서와 각 단계의 완료 기준

### 단계 0: baseline 확보

- SDK에서 DFTL 그대로 빌드하고 보드에서 읽기/쓰기 검증.
- 실제 보드, SDK/BSP, page/block/die geometry, 실험 용량, 기준 fio workload 기록.
- 기존 코드는 현재 `DFTL_CMT_CAPACITY=65536`이다. README에 적힌 예전 기본값 4096과 구분한다.
- 복사본 `.project`가 아직 `run-gftl3`라는 점을 정리한다. 프로젝트명·artifact·BSP 경로를 각각 확인하고 광역 문자열 치환은 피한다.

### 단계 1: gamma=0 모델·인덱스를 호스트에서 검증

- 단위 테스트 가능한 순수 C 모듈로 학습/조회부터 구현.
- sequential, stride, singleton, sparse/hole, overwrite, group 경계, ceil 경계, 최대 주소 시험.
- 전 구간 OLS 한 번으로 끝내지 않는다. 모든 입력 점에 대한 허용 오차를 만족하지 못하면 분할한다.
- gamma=0이어도 불규칙한 LPN 소속을 범위로 오인하면 안 된다. exact 표현 가능성을 판정하고 필요하면 singleton으로 분할한다.
- 정답 map과 조회 결과가 모두 일치해야 다음 단계로 진행. 정답 map은 테스트 전용이며 메모리 절감 수치에 숨겨서 제외하지 않는다.

### 단계 2: 버퍼 flush·실제 할당 연결

- 현재 `request_transform.c`의 버퍼 eviction→`AddrTransWrite` 경로 추적.
- buffer entry ID만 LPN 기준으로 정렬할 수 있는지 검토. DMA 참조 중인 데이터를 직접 이동하지 않는다.
- PPA 할당 전에 정렬하고 실제 배정 주소를 수집해 학습. 이미 할당된 주소를 정렬만 해서는 같은 효과가 아니다.
- overwrite된 버퍼 항목 정리, partial flush, host FLUSH/FUA, 요청 순서, program 실패 처리 정의.
- pending write와 완료된 write의 가시성, 이전 VSA 무효화, 새 매핑 발행 순서를 정의. 실패 시 학습된 주소가 읽기에 노출되지 않아야 한다.

### 단계 3: GC·index flash backing

- 기존 reverse map과 최신 학습 map을 대조해 유효 페이지 판별.
- GC relocation의 실제 새 주소를 학습하고 매핑 최신성을 유지.
- metadata cache miss/eviction, segment 저장·복구, translation block 공간 부족·GC 시험.
- compaction은 최신 값만 남기고 다른 유효 LPN을 잃지 않는지 정답 map과 전수 비교.
- 이 시점도 gamma=0 정확 조회 버전이다. CRB/OOB 미구현 상태를 전체 LeaFTL 완료로 표현하지 않는다.

### 단계 4: gamma>0, CRB, OOB

- CRB insert/delete/membership, 시작 LPN 충돌, 새 exact segment가 기존 approximate 값을 덮는 경우 시험.
- NAND spare 영역에서 ECC/컨트롤러 사용 바이트를 제외한 실제 여유 공간을 확인.
- 쓰기 시 역매핑·이웃 정보 기록, 읽기 완료 후 OOB 검증, 오예측 시 정확 주소 재조회/재읽기.
- 보정 전 데이터를 host DMA로 반환하지 않는다. invalid neighbor, block 경계, GC 이동 후 이웃 관계를 처리한다.
- 주소 예측 실패가 무관한 사용자 데이터를 반환하는 경우가 없어야 한다.

### 단계 5: 영속성·회복·실험

- mapping/index 디렉터리와 데이터의 일관성, checkpoint generation, NAND 오류, power-loss 회복 순서 구현.
- baseline 자체의 전원 장애 지원 수준부터 확인. 논문의 배터리 백업 가정을 자동으로 보드에 적용하지 않는다.
- sequential/random/mixed/rewrite/GC/partial flush/재시작에서 데이터 무결성 검증.
- 동등한 데이터 캐시·DRAM 예산·실험 용량에서 DFTL과 비교. 세그먼트·CRB·디렉터리·임시 정렬 버퍼·기존 잔존 배열 모두 집계.
- 통계: 읽기/쓰기 지연, 처리량, model hit/miss, 레벨 수, segment 수, compaction 시간, 오예측/추가 read, metadata I/O, GC copy, WAF.

## 5. 공개 저장소를 읽는 순서

1. 저장소 README: WiscSim 실험 환경과 실행 구조 파악.
2. `wiscsee/wiscsim/learnedftl.py`: 학습 FTL·매핑 자료구조 흐름 확인. 시뮬레이터의 reference mapping table은 correctness oracle 역할과 제품 매핑 경로를 구별한다.
3. 같은 디렉터리의 `datacache.py`, `crb_standalone.py`, `dftldes.py`, `sftl.py`: 버퍼/CRB/기준 FTL 역할 확인. 일부 파일은 웹에서 목록만 확인했으므로 실제 내용을 내려받은 뒤 검토한다.

저장소는 웹에서 참조했지만 전체 소스를 로컬에 다운로드하지는 않았다. 필요하면 사용자가 터미널에서 별도 `git clone https://github.com/platformxlab/LeaFTL.git`을 실행한다. 현재 작업 폴더를 clone 대상으로 지정해 덮어쓰지 않는다.

## 참고

- 논문: `C:/Users/kimeu/Desktop/SSS-Lab/LeaFTL.pdf`
- https://github.com/platformxlab/LeaFTL
- https://github.com/platformxlab/LeaFTL/blob/main/wiscsee/wiscsim/learnedftl.py
- 현 DFTL `src/` 및 README. 원본의 설명/과거 성공 기록은 이번에 재현한 결과가 아니다.
