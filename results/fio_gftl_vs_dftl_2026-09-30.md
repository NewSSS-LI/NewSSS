# Cosmos+ OpenSSD — GreedyFTL vs DFTL 정식 비교 (2026-09-30)

**공통 파라미터**: bs=128k, iodepth=4, size=128M, raw device(`/dev/nvme0n1`), `--ioengine=libaio --direct=1`
**주의**: 이 세션 도중 호스트 PC(SSSLab-Com4) 재부팅/rescan 과정에서 보드 PCIe 인식이 끊겨 `/dev/nvme0n1`이 호스트 내장 SSD를 가리켰던 사고가 있었음. 아래 GreedyFTL 결과는 `lspci`로 Xilinx Device 7028 인식 재확인 후 측정한 값. DFTL 결과(`fio_dftl_v1_2026-09-30.md`)는 이 사고 이전에 측정되어 유효한 것으로 판단됨(수치가 물리적으로 타당하고 CMT 캐싱 패턴이 명확했음).

## 결과 비교

| 테스트 | GreedyFTL | DFTL | GreedyFTL/DFTL 배수 |
|---|---|---|---|
| Sequential Write | 294 MiB/s (308 MB/s), IOPS 2348 | 10.2 MiB/s (10.7 MB/s), IOPS 81 | **29배** |
| Sequential Read (1회차, 콜드) | 367 MiB/s (385 MB/s), IOPS 2934 | 32.3 MiB/s (33.8 MB/s), IOPS 258 | **11배** |
| Sequential Read (2회차) | 476 MiB/s (499 MB/s), IOPS 3806 | 76.5 MiB/s (80.2 MB/s), IOPS 611 | 6.2배 |
| Sequential Read (3회차) | 417 MiB/s (437 MB/s), IOPS 3335 | 61.1 MiB/s (64.0 MB/s), IOPS 488 | 6.8배 |
| Random Write | 427 MiB/s (447 MB/s), IOPS 3413 | 14.1 MiB/s (14.8 MB/s), IOPS 112 | **30배** |
| Random Read (1회차, 콜드) | 529 MiB/s (555 MB/s), IOPS 4231 | 32.3 MiB/s (33.9 MB/s), IOPS 258 | **16배** |
| Random Read (2회차) | 559 MiB/s (586 MB/s), IOPS 4471 | 77.4 MiB/s (81.2 MB/s), IOPS 619 | 7.2배 |
| Random Read (3회차) | 554 MiB/s (581 MB/s), IOPS 4432 | 77.4 MiB/s (81.2 MB/s), IOPS 619 | 7.2배 |

## 반복 실행 시 변동 패턴 — 가장 중요한 대조

| | 1회차 | 2회차 | 3회차 | 변동폭 |
|---|---|---|---|---|
| GreedyFTL Sequential Read | 367 | 476 | 417 | ±15% (잡음 수준) |
| DFTL Sequential Read | 32.3 | 76.5 | 61.1 | 콜드→웜업(+137%)→간섭으로 하락 |
| GreedyFTL Random Read | 529 | 559 | 554 | ±5% (거의 평평) |
| DFTL Random Read | 32.3 | 77.4 | 77.4 | 콜드→웜업(+140%)→plateau(수렴) |

## 분석

**GreedyFTL이 모든 항목에서 압도적으로 빠름 (7~30배)** — 예상했던 대로. DRAM에 전체 매핑 테이블(~32MB)이 항상 있어서 주소변환이 순수 배열 인덱싱(수십~수백 ns)인 반면, DFTL은 CMT miss마다 실제 NAND I/O(translation page read-modify-write, 수십 μs~ms)가 추가로 발생하기 때문.

**반복 실행 패턴이 완전히 다름**: GreedyFTL은 몇 번을 반복해도 성능이 항상 비슷한 범위(±5~15%, 이건 그냥 시스템 잡음)에 머무는 반면, DFTL은 반복할 때마다 CMT가 데워지면서 뚜렷하게(2배 이상) 빨라지다가 CMT 용량(4096) 한계에서 수렴(plateau)하거나, 다른 워크로드가 끼어들면 다시 느려짐(오염). 이건 "GreedyFTL은 이력 의존성이 없는 결정적(deterministic) 성능, DFTL은 캐시 상태에 따라 달라지는 비결정적 성능"이라는 걸 실측으로 보여주는 핵심 증거.

**메모리 대비 성능 트레이드오프 관점**: DFTL은 forward mapping 메모리를 32MB→~104KB(CMT+GTD)로 300배 줄이는 대신, 이 정도 성능 손실을 감수한 것. 이게 이후 Learned Index FTL이 넘어야 할 기준선(baseline) — "같은 메모리 절감폭에서 DFTL보다 성능 손실이 적어야 한다"가 다음 단계의 성공 기준이 됨.

## 참고 — 오늘 겪은 사고 (교훈)

호스트 PC를 여러 번 재부팅/rescan하다가 보드 PCIe 인식이 끊겼는데, `/dev/nvme0n1`이라는 장치명이 호스트 내장 SSD로 넘어가 있어서 몇 시간 동안 "성능이 GB/s대로 비정상적으로 빠르다"며 원인을 잘못 짚었음(write-back 버퍼링 코드까지 분석했지만 헛다리). `--verify=crc32c`로도 통과해서 더 헷갈렸음(진짜 상용 SSD라 당연히 통과). **교훈: FIO 테스트 전 항상 `lspci | grep -i non-volatile`로 보드 인식부터 확인할 것.**
