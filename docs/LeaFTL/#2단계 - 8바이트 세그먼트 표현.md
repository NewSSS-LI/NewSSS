# 세그먼트 8-byte 표현 적용

기록일: 2026-11-12. 논문이 명시한 필드별 크기에 맞춰 런타임과 저장용 표현을 함께 수정했다.

| offset | 필드 | 타입·크기 |
|---|---|---|
| 0 | 시작 LPN의 그룹 내 offset (SLPA) | uint8, 1 byte |
| 1 | 길이 L (마지막 offset − 시작 offset) | uint8, 1 byte |
| 2 | 기울기 K와 type bit | binary16 bits, 2 bytes |
| 4 | 절편 I | uint32, 4 bytes |
| 합계 | LEAFTL_SEGMENT | **8 bytes** |

`sizeof==8`과 각 필드의 `offsetof`를 컴파일 시 검사한다. alignment padding이 들어가
다시 커지면 빌드를 실패시킨다. group ID는 각 세그먼트에서 제거하고 그룹 디렉터리가
관리한다. 학습 입력에만 임시 group ID를 전달하며 node에는 저장하지 않는다.

`LeaFtlSegmentEncode8/Decode8`은 위 순서를 명시적인 little-endian 8 bytes로 저장/복원한다.
메모리 struct를 그대로 덤프해 endian/alignment에 의존하지 않는다. 변환 후 유효 범위를
검사하고 잘못된 입력이면 출력 모델을 바꾸지 않는다. Flash page의 version/checksum,
CRB·directory의 serialization과 실제 NAND I/O는 여전히 11번 이후 범위이다.

## 절편과 학습 정확성

이 구현에서 4-byte I는 **uint32**이다. float32로 바꾼 것이 아니다. uint32 주소를
정확히 저장하므로 2^24보다 큰 singleton 주소와 0xfffffffe도 손실 없이 표현한다.
논문의 필드별 byte 수를 맞춘 것이며, 논문이 I의 세부 signed/float 인코딩까지 동일하게
명시했다고 주장하지 않는다. 이 uint32 선택은 펌웨어의 주소 보존을 위한 구현 결정이다.

학습은 기존처럼 double 부동소수점 PLR을 사용한다. K를 binary16으로 저장한 다음,
모든 입력 점의 `y−1−K*x < I <= y−K*x` 구간을 구한다. 그 안에 uint32 정수가 있으면
선택하고 **저장된 K/I로 모든 입력을 다시 예측**한다.

음수·소수 절편만 가능한 구간이나 uint32 범위를 벗어나는 구간은 반올림해서 잘못
발행하지 않고 세그먼트를 나눈다. 최악에는 K=0, I=해당 PPN인 singleton으로 표현한다.
예를 들어 그룹 offset 100..102를 PPN 0..2에 대응시키려면 K=1의 I가 −100이므로
이 표현으로 하나의 세그먼트에 넣지 않고 분할한다. 따라서 byte 수는 줄지만 일부
패턴에서는 세그먼트 개수가 늘 수 있으며 논문의 압축률을 보장하지 않는다.

읽기는 `ceil(K*group_offset+I)`이고, float16 복원과 계산은 double을 사용한다.
고정소수점으로 전환한 것은 아니다. merge·compaction은 K/I 비트를 그대로 유지하고
SLPA와 L만 바꾼다. CRB와 exact 소속 판정도 유지한다.

## 메모리 변화 (MSVC x64 / 가상 8채널·16die)

| 항목 | 이전 | 현재 |
|---|---:|---:|
| 세그먼트 | 16 B | **8 B** |
| node (세그먼트+트리/목록 링크) | 32 B | 20 B |
| 학습 입력 1개 (임시 group+model+membership) | 48 B | 44 B |
| transaction workspace | 46,392 B | 31,032 B |
| LeaFTL arena (예비 directory 포함) | 3,810,752 B | 3,007,936 B |
| arena end-exclusive | 0x1c622240 | 0x1c55e240 |

arena 시작 0x1c27fc80과 기존 DFTL map/DMA 시작 주소는 유지한다. 모든 크기는 실제
sizeof로 계산하므로 SDK에서는 실제 target ABI·BSP로 다시 검사된다. node 링크와
그룹 CRB(292 B)는 8-byte 모델 외 별도 비용이며, 전체 인덱스가 segment당 8 B만
사용한다고 계산하지 않는다.

## 최소 검증

- MSVC C11 `/W4 /WX /fp:strict` 컴파일 및 32-bit host 문법·크기 guard 통과.
- 세그먼트 핵심 9개 배치 × 256 offset: 순차·stride·CRB hole·큰/음수 기울기 주소·
  큰 singleton·표현 불가능 절편 분할 검사 통과.
- `sizeof`/offset, golden 8-byte vector, encode/decode roundtrip, L=255,
  0xfffffffe 절편, 잘못된 길이 거부와 출력 보존 검사 통과.
- Figure 13/CRB/계수 보존/실패 처리 핵심 index 회귀 38,912회 조회·7회 compaction 통과.
- 매핑 prepare/publish/abort/failure와 RAM 정렬·크기·설정 초과 검사 통과.
- 대규모 무작위 검증은 반복하지 않았다. ARM SDK 전체 빌드와 보드 업로드는 미수행이다.
