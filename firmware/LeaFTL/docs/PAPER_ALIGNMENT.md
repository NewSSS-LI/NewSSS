# 4번 논문 대조 및 보정 결과

기준: 2026-11-12 사용자 요청의 다섯 가지 보정.
논문 [LeaFTL.pdf](../../LeaFTL.pdf) §3.4·3.7, Algorithm 1·2, Figure 9·13을 기준으로
현재 동작을 대조한다. 이 문서는 보정 전 stride·전체 재배치 구현에 대한 완료 판정을 대체한다.

## 다섯 가지 반영표

| 요청 | 논문 절차 | 현재 반영 | 주요 코드 |
|---|---|---|---|
| CRB와 실제 LPN 소속 | exact는 K에서 `ceil(1/K)` 소속 추론, approximate는 그룹 CRB 조회 | 저장 stride 제거. exact 공식/CRB 분기, 실제 CRB 삽입·삭제·키 변경·중복 제거를 학습·조회·merge에 연결 | `leaftl_segment.c`: ExactStep, LeaFtlSegmentMembers/Predict. `leaftl_crb.c` |
| compaction 진행 | 상위 세그먼트를 하위 레벨에 seg_update하고 빈 상위 레벨 제거 | 그룹 전체 survivor 재구성·interval coloring 제거. 레벨 간 seg_merge 후 기존 상위 레벨을 하위로 이동하며 같은 SegUpdate 호출 | `leaftl_index.c`: MergeLevelPairs, LeaFtlIndexCompactGroup |
| merge 의미 통일 | `old_bitmap & ~new_bitmap`, 이전 시작·길이 조정 및 빈 세그먼트 제거 | SegMerge가 해당 의미만 수행. 같은 계수 구간을 합치던 MergeCompatible 삭제 | `leaftl_index.c`: SegMerge, Bounds |
| 삽입 중 merge | Algorithm 1 line 8–16: overlap victim merge·제거·하위 이동 | 삽입 시 즉시 SegUpdate→SegMerge. trim 후 범위가 분리되면 같은 레벨 유지, 남은 겹침은 하위 이동. 하위 충돌 시 새 레벨 | `leaftl_index.c`: LeaFtlIndexInsert, SegUpdate, DownLevel |
| 기존 계수 유지 | 이전 세그먼트 K/I를 바꾸지 않음 | 여러 점뿐 아니라 한 점만 남아도 slope_bits와 intercept를 bit 단위로 유지. singleton 재학습/정규화 삭제 | `leaftl_index.c`: Bounds. 테스트: SameCoefficients, CheckCoefficients |

## CRB에서 특히 검사한 내용

- 같은 그룹의 approximate record는 실제 LPN offset이 중복되지 않는다.
- record 내부 LPN은 연속된 메모리 구간에 정렬해 보관하고 record는 SLPA 순으로 정렬한다.
- 시작 LPN을 덮어쓰면 이전 record의 다음 실제 member를 새 SLPA로 정하고 기존
  segment의 first/last와 CRB 키를 함께 갱신한다. K/I는 바꾸지 않는다.
- 새 exact segment도 이전 approximate record에서 해당 LPN을 삭제한다.
- 빈 CRB record와 대응 세그먼트를 제거한다. 삭제/변경은 모든 레벨에 적용한다.
- CRB는 LPN 소속만 보관하며 PPN 정답 map을 숨겨 두지 않는다.
- 0과 255를 모두 유효 offset으로 쓰기 위해 논문의 null separator를 256-bit
  record-boundary 표식으로 표현했다. 논문과 물리 byte 인코딩이 같다는 뜻은 아니다.
- SLPA key 이진 탐색과 해당 record 내부 이진 탐색을 사용한다. rank/select는
  경계 bitmap을 사용하며, 전체 CRB가 완전히 정렬됐다고 가정하지 않는다.

## Figure 13 대조 테스트

1. T0: 이전 `0..63` 세그먼트 삽입.
2. T1: `200..255` 삽입. 범위 비중첩이므로 같은 레벨 유지.
3. T2: `16..31` 삽입. 기존 `0..63`을 survivor stride로 분할하지 않고 아래 레벨에 유지.
4. T3/T4: CRB membership `[75,78,82]`, `[72,73,80]`을 넣고 서로 다른 레벨의 범위 중첩 처리.
5. T5/T6: LPN 50의 이전 exact 조회와 LPN 78의 올바른 CRB record 조회를 검사.
6. T7: 새 exact `32..90`으로 두 approximate record를 덮어쓰고 CRB 항목 제거.
7. T8: compaction 후 L0가 `[0..15] [16..31] [32..90] [200..255]`, 한 레벨인지 직접 확인.

Figure 13의 두 approximate record는 현재 gamma=0 조건에 맞춰 예측값이 정확한
K/I로 구성한다. membership·충돌·레벨 배치 동작을 재현하는 테스트이며, 논문의
gamma>0 오예측·OOB 비용을 재현한 테스트는 아니다.

## 범위·구현 선택과 남은 차이

- **현재 gamma=0:** CRB-backed type은 membership 규칙으로 구분한다. irregular LPN이나
  현재 Cosmos VSA 기울기가 exact 공식에 맞지 않으면 CRB를 사용하되, 실제 모든 입력
  PPN 예측이 정확해야 학습을 허용한다. gamma>0/OOB 보정은 아직 활성화하지 않는다.
- **계수 보존:** exact 모델의 중간 LPN을 덮어쓴 경우, 경계만 줄이는 논문 merge 특성상
  그 내부 주소가 오래된 segment의 수식상 소속에 남을 수 있다. 최신 상위 segment가
  가려서 조회는 정확하다. 이전 구현의 '모든 레벨에서 각 LPN 소유자가 하나뿐'이라는
  조건은 exact에 적용하지 않는다. CRB actual membership의 유일성은 유지한다.
- **compaction 순서:** 모든 상·하위 쌍에 bounds-only seg_merge를 적용한 뒤,
  그 시점의 원래 레벨을 아래쪽부터 유한 순회하여 상위→하위 SegUpdate를 수행한다.
  충돌로 만든 레벨은 같은 pass에서 재귀적으로 재방문하지 않는다. 이는 exact 범위가
  계속 겹치는 Figure 13 T2 형태에서 무한 이동을 막기 위한 명시적인 순회 선택이다.
  임의 구간 재분할·K/I refit·새 모델 coalescing으로 이를 대체하지 않는다.
- **8-byte representation:** SLPA 1B, L 1B, binary16 K 2B, uint32 I 4B, now implemented. Unrepresentable intercepts split safely; see SEGMENT_8BYTE.md.
- **메모리:** CRB 292 bytes/group, index node 20 bytes, workspace 31,032 bytes
  (MSVC x64 실측). 6번에서 target DRAM·스택과 함께 계산한다.
- **플랫폼 좌표:** 기존 VSA 할당을 유지하므로 음수/큰 기울기나 constant PPN의
  host 검증 입력도 CRB 경로로 안전하게 처리한다. 논문의 단조 물리 할당·압축률을
  달성했다고 주장하지 않는다. 물리 할당 연결은 후속 단계이다.
- **트리거:** 기본 100만 write 주기와 firmware 호출 시점은 아직 연결하지 않았다.
- **전원·NAND:** Flash backing·GC·OOB·복구와 보드 검증은 후속 범위이다.
- **고정소수점 가능성:** target FPU/BSP 제약 확인 후 연산부가 바뀔 수 있으며,
  그 경우 동일한 CRB·계수 보존·주소 오차 테스트를 다시 통과해야 한다.

## 검증 요약

| 검증 | 결과 |
|---|---|
| CRB 3,000회 갱신 및 owner 대조 | 768,000회 통과, 전체 256 record와 0/255 포함 |
| 모델 exact·CRB 경로 | 6,371개 배치 × 그룹 256 offset 전수 대조 |
| 인덱스 무작위 갱신 | 4,000회, exact·CRB·음수/큰 주소 혼합 |
| compaction | 1,888회, K/I bit 보존과 전체 그룹 조회 대조 |
| 인덱스 정답 조회 | 12,083,200회 일치 |
| Figure 13 | T0–T8 조회 및 최종 범위·레벨 일치 |
| 원자적 실패 | CRB 키를 변경한 뒤 공간 부족인 batch도 전체 기존 상태 보존 |
| binary16 변환 | Python 독립 oracle와 321,548건 일치 |

MSVC C11 `/W4 /WX /fp:strict`, debug와 optimized를 사용한다.
최종 optimized는 전체 무작위 suite를 통과했다. 사용자의 검증 시간 축소 요청에 따라
최종 debug는 핵심 사례 `--targeted`로 38,912회 조회와 7회 compaction을 확인했다.
SDK 전체 빌드와 보드 검증은 수행하지 않는다.

The large-suite results above belong to the previous paper-alignment revision. Current 8-byte revision was checked with the minimal segment/index/mapping/memory tests in [SEGMENT_8BYTE.md](SEGMENT_8BYTE.md).
