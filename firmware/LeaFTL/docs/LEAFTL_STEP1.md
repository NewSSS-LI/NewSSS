# 1번: LeaFTL 설정과 주소 계약

완료 범위: `src/leaftl_config.h` 추가. 기존 펌웨어에는 아직 include하거나
연결하지 않는다. DFTL 설정, 할당 정책, geometry와 메모리 주소는 변경하지 않는다.

## 현재 소스에서 확인한 단위

| 항목 | 현재 값 또는 규칙 | 근거 |
|---|---|---|
| host LBA | 4,096 bytes | `ftl_config.h`: BYTES_PER_NVME_BLOCK |
| LPN/LPA | logicalSliceAddr, 16,384 bytes | BYTES_PER_DATA_REGION_OF_SLICE |
| NAND page data | 16,384 bytes, slice/page = 1 | BYTES_PER_DATA_REGION_OF_PAGE, SLICES_PER_PAGE |
| host → LPN | LBA / 4, slice 내부 offset = LBA % 4 | `request_transform.c`: ReqTransNvmeToSlice |
| block | SLC mode, virtual page 256개 | BITS_PER_FLASH_CELL, USER_PAGES_PER_BLOCK |
| die | USER_CHANNELS * 2 | USER_DIES, USER_WAYS |
| channel 수 | BSP의 연결된 NSC 개수 | NUMBER_OF_CONNECTED_CHANNEL; 보드/BSP 없이 8로 단정하지 않음 |
| spare | 컨트롤러 page/slice 영역 256 bytes | 실제 LeaFTL OOB 사용 가능량은 16번에서 확인 |

주소 상한은 `SLICES_PER_SSD`라는 물리 geometry 상한과 실제 host 노출 용량을
구분해야 한다. `storageCapacity_L`은 host LBA 개수이고 EXP_LOGICAL_GB, OP,
bad block 등에 영향을 받는다. 인덱스 그룹 수는 실제 관리할 LPN 개수로 계산한다.
마지막 그룹은 256개보다 작을 수 있다. geometry 값은 새 헤더에 중복 고정하지 않는다.

## 초기 학습 좌표: PPN = VSA (gamma=0)

`address_translation.h`의 기존 변환을 그대로 따른다. D=USER_DIES,
B=SLICES_PER_BLOCK이라 하면:

```
VSA   = die + D * (block * B + page)
die   = VSA % D
block = (VSA / D) / B
page  = (VSA / D) % B
```

학습 모델의 출력 PPN은 이 VSA 정수이고, 실제 NAND row가 아니다. 조회 결과는
기존 VSA 요청 경로로 전달한다. `request_schedule.c:GenerateNandRowAddr`가
virtual block을 physical block으로 변환하고 bad-block remap을 적용한다.
NAND 종류와 SLC 모드에 따라 page-to-row 변환도 추가될 수 있다.

모델의 x 좌표는 그룹 내부 offset(0..255), y 좌표는 VSA로 정한다.
`group_id = LPN / 256`, `offset = LPN % 256`이다. 이후 intercept는 이 x 좌표를
기준으로 정의하며, 전역 LPN과 혼용하지 않는다. 모든 조회는 실제 LPN 소속을
확인해야 한다. 출력이 정확해도 hole을 포함하는 것으로 판정하면 안 된다.

`FindFreeVirtualSlice`는 die별 current block/page를 사용하고
`FindDieForFreeSliceAllocation`은 channel/way를 순환한다. 따라서 배치 내 VSA가
항상 단조 증가한다고 가정하지 않는다. 8번에서는 LPN을 할당 전에 정렬하고 실제
할당된 VSA를 수집한다. 모델이 표현할 수 없는 부분은 분할/singleton으로 처리한다.
이 초기 정책은 기존 병렬 할당을 유지하지만 논문의 압축률을 보장하지 않는다.

### 이후 gamma>0을 위한 경계

VSA+1은 보통 다른 die이며, 같은 die의 다음 virtual page는 VSA+D이다.
이때도 동일 block 범위를 확인해야 한다. `PPN±gamma`를 물리 이웃으로 간주하는
논문의 OOB 기법을 VSA 좌표에 그대로 적용하지 않는다.

15~17번에서 물리 run 좌표/변환, 동일 die·block 범위, 이웃 메타데이터를
쓰기 전에 확보하는 방식, GC와 block 끝의 처리까지 확정해야 한다.
필요하면 좌표 모드와 저장 형식을 버전 변경한다. 그 전까지 헤더는 nonzero gamma를
컴파일 오류로 차단한다. CRB/OOB 읽기 검증 없는 근사 주소를 발행하지 않는다.

## 초기 자원 상한

| 설정 | 기본값 | 의미 |
|---|---:|---|
| GROUP_SIZE | 256 | 고정 그룹 크기 |
| GAMMA | 0 | 정확 조회만 허용 |
| MAX_CACHED_GROUPS | 256 | 전체 그룹 수가 아닌 동시 상주 그룹 상한 |
| MAX_LEVELS_PER_GROUP | 256 | 그룹별 레벨 상한 |
| MAX_SEGMENTS_PER_GROUP | 1,024 | 오래된 레벨을 포함한 그룹별 세그먼트 상한 |
| MAX_RESIDENT_SEGMENTS | 65,536 | 모든 상주 그룹이 공유하는 총 세그먼트 상한 |
| MAX_TRAINING_POINTS | 256 | 그룹 단위 학습 scratch 입력 상한 |
| MAX_CRB_LPNS_PER_GROUP | 256 | 그룹 내 고유 offset 수 상한, 아직 미구현 |
| MAX_CRB_BYTES_PER_GROUP | 512 | separator 포함 예산, 저장 형식은 14번에서 설계 |
| METADATA_RAM_BUDGET_BYTES | 8 MiB | 추가 LeaFTL 메타데이터 전체의 임시 예산 |
| TARGET_SEGMENT_BYTES | 8 | 논문 형식 목표; 런타임 구조체 크기가 아님 |

이 값들은 실측 튜닝 결과가 아니다. 그룹별 상한을 cached group 수와 곱해서
모두 선할당하는 설계를 뜻하지도 않는다. 6번에서 실제 sizeof, 디렉터리,
allocator·포인터·scratch overhead를 계산하고 예산 및 링크/DMA/스택 경계를 검사한다.
남아 있는 기존 full map/CMT/GTD/reverse map도 총 RAM 사용량에 포함한다.

한도 초과 시 compaction/안전한 writeback·eviction 또는 명시적 실패가 필요하다.
유효 매핑을 버리거나 오래된 매핑을 대신 반환하면 안 된다. Flash backing이 없는
호스트 단계에서도 용량 부족을 오류로 반환한다. 현재 헤더는 한도만 정의하며,
이 동작이나 메모리 예산 준수를 구현했다고 의미하지 않는다.

## 다음 작업과 검증 일정

다음은 2번 `leaftl_segment.c/h` 및 호스트 테스트다. BSP 없는 순수 C 모듈로
만들고 sequential/stride/singleton/hole/ceil 경계/최대 주소를 검사한다.
헤더는 BSP를 include하지 않으므로 해당 테스트에서 그대로 사용할 수 있다.

보드 테스트는 합의대로 13번, 17번, 19번 완료 후 총 3회로 계획한다.
1번에서는 보드 업로드를 하지 않는다.

### 1번 PC 검증 결과

MSVC C 모드 `/W4 /WX /Zs`로 BSP 없이 헤더 중복 include와 컴파일 시간 조건을
검증했다. 그룹 경계 255/256, offset 0/255, 빈/부분/전체 그룹과 UINT_MAX에서의
그룹 수 계산이 통과했다. nonzero gamma, 그룹 캐시 0, 레벨 257,
그룹별 세그먼트 255, 부족한 총 세그먼트 pool, RAM 예산 0은 의도한 컴파일
오류로 차단됨을 확인했다. 이는 헤더 검증이며 SDK 펌웨어 빌드/보드 검증은 아니다.
