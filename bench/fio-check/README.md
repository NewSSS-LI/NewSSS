# FTL 구현 확인 FIO (GC 없음)

각 FTL 구현이 제대로 되었는지 빠르게 확인하기 위한 작은 FIO 측정이다. 교수님 지시(2026-11-16~17)로 LearnedFTL 논문 재현(`../learnedftl-paper/`)의 Filebench·RocksDB는 멈추고, 이 측정으로 먼저 각 FTL을 바로잡는다.

- **목적:** GC가 일어나지 않는 조건에서 FTL의 기본 성능(주소 변환, 요청 처리)을 본다. 기준은 공식 Cosmos+ 튜토리얼 성능이다(아래 4절).
- **비교 대상:** GreedyFTL(기준), DFTL, LearnedFTL, LeaFTL. TPFTL은 지금 측정하지 않는다.
- **진짜 성능 비교가 아니다.** 구현 확인이 목적이라 FTL마다 랜덤 쓰기 양이 다르다(교수님). FTL끼리 같은 조건으로 비교하는 측정은 따로 정한다.

## 1. 측정 순서 (FTL 하나마다)

펌웨어를 **두 번** 올린다. 두 회차 모두 빈 SSD에서 시작한다.

| 회차 | 스크립트 | 단계 | bs | iodepth | numjobs | job당 범위(size) | job당 양(io_size) | 전체 |
|---|---|---|---|---|---|---|---|---|
| a | `fio_a.sh` | seqwrite | 128K | 4 | 1 | 32GB(전 영역) | = size | 32GB |
| | | seqread | 128K | 4 | 4 | 8GB, `offset_increment=8g` | = size | 32GB |
| | | randread | 4K | 32 | 6 | 32GB(전 영역) | 5GB | 30GB |
| b | `fio_b.sh` | randwrite | 4K | 32 | 6 | 32GB(전 영역) | FTL별(아래) | FTL별 |

- 공통: `--direct=1 --ioengine=libaio --group_reporting`, `time_based`/`runtime` 없이 정한 IO 양을 다 하면 끝난다. 단계 사이 20초 휴식.
- seqwrite가 읽기 측정 전의 전 영역 쓰기를 겸한다. randread는 어디를 읽을지 정할 수 없어 전 영역에 데이터가 있어야 한다.
- 32GB를 한 번 순차로 쓰는 것은 플래시(32GB 모드 약 36.5GB) 안이라 GC가 없다.
- randwrite는 꽉 찬 SSD에서 하면 GC가 나므로 펌웨어를 다시 올린 빈 SSD에서 따로 한다.

### 랜덤 쓰기 양 (`fio_b.sh`)

4KB를 써도 플래시에는 16KB 페이지 하나가 쓰여 호스트가 쓴 양의 약 4배가 플래시에 들어간다. 여기에 FTL의 변환 페이지 쓰기가 더해진다. 그래서 FTL마다 GC(데이터 GC와 변환 페이지 GC)가 나지 않는 양으로 정했다.

| FTL | job당 | 전체 | 이유 |
|---|---|---|---|
| greedyftl | 1280MB | 7.5GB | 플래시 약 30GB. 변환 페이지 없음 |
| dftl, tpftl | 512MB | 3GB | 변환 페이지 쓰기(약 6,600번)가 변환 GC 시작점 아래 |
| learnedftl | 256MB | 1.5GB | 변환 페이지 line GC 시작점 아래(실측 6,426번) |
| 그 밖(leaftl 등) | 256MB | 1.5GB | 기본값. GC가 안 났는지 확인할 것 |

다른 양으로 재려면 `RANDWRITE_IO_SIZE=<job당 양> ./fio_b.sh`. 쓴 값은 `meta.txt`의 `randwrite_io_size_per_job`에 남는다.

## 2. 실행

설정과 공통 함수는 `../learnedftl-paper/host/`의 `config.sh`, `common.sh`를 같이 쓴다. 준비(호스트 설정, sudo 규칙)는 [`../learnedftl-paper/README.md`](../learnedftl-paper/README.md) 2절과 같다.

1. **호스트:** `../learnedftl-paper/host/config.sh`의 `FTL=`을 이번 펌웨어 이름으로 바꾼다.
2. **호스트:** `../learnedftl-paper/host/pci_rescan.sh remove`
3. **개발 PC:** 펌웨어를 새로 올린다(빌드는 `-DEXP_LOGICAL_GB=32`).
4. **호스트:** `../learnedftl-paper/host/pci_rescan.sh rescan` → `OK /dev/nvme0n1 ...`
5. **호스트:** 회차 a
   ```bash
   cd ~/NewSSS/bench/fio-check/host
   FW_SHA256=<ELF sha256> setsid -f nohup ./fio_a.sh > ~/fio_a_$(date +%m%d_%H%M).out 2>&1 < /dev/null
   ```
6. 2~4를 다시 한다(펌웨어를 다시 올려 SSD를 비운다).
7. **호스트:** 회차 b — 위 5의 `fio_a`를 `fio_b`로 바꿔 실행한다.
8. CSV:
   ```bash
   ../../learnedftl-paper/host/summarize.py ~/newsss_results/<날짜>_<ftl>_<board>_fio_a ~/newsss_results/<날짜>_<ftl>_<board>_fio_b > <ftl>_<board>_fiocheck.csv
   ```

결과 폴더: `$RESULTS_DIR/<날짜>_<ftl>_<board>_fio_a`, `..._fio_b` (`meta.txt`, `phases.log`, 단계별 fio 출력).

## 3. GC가 안 났는지 확인

- **LearnedFTL:** 측정이 끝나고 2초 쉬면 UART에 통계가 찍힌다. `gcGroups 0`, `tpGcCount 0`이어야 한다(`modelRejected`는 항상 0).
- **그 밖의 FTL:** 펌웨어에 GC 카운터가 없다. 1절의 양을 지키면 GC가 나지 않는 것으로 계산했다. 의심되면 GC 함수에 UART 출력을 넣은 디버그 빌드로 확인한다.

## 4. 기준: 공식 Cosmos+ 튜토리얼

`Cosmos+ OpenSSD 2017 Tutorial.pdf` 슬라이드 164~167의 8채널 × 8웨이(64 다이) 결과를 1/4(우리 보드 16 다이)로 본다. 튜토리얼은 Hynix 모듈 기준이라 우리 Micron 칩(pSLC)과 다르므로 대략적인 기준이다(`docs/board_spec.md`).

| 측정 | 기준(÷4, 그래프 눈금으로 읽은 근사) |
|---|---|
| 128K 순차 읽기 | 약 345 MB/s |
| 128K 순차 쓰기 | SLC 약 312 / MLC 약 154 MB/s |
| 4K 랜덤 읽기 | 약 18k IOPS |
| 4K 랜덤 쓰기 | SLC 약 15k / MLC 약 7k IOPS (튜토리얼은 빈 SSD, read-modify-write 없음) |

## 5. 보드 1 결과 (2026-11-17, 같은 절차를 로컬 스크립트로 측정)

| 측정 | GreedyFTL | DFTL | LearnedFTL |
|---|---|---|---|
| seqwrite 128K (MB/s) | 389 | 375 | 372 |
| seqread 128K (MB/s) | 570 | 65 | 560 |
| randread 4K (IOPS) | 33.5k | 4,026 | 32.5k (모델 적중 98.4%) |
| randwrite 4K (IOPS) | 21.8k (7.5GB) | 3,567 (3GB) | 3,738 (1.5GB) |

- GreedyFTL은 네 측정 모두 기준 이상이다.
- DFTL과 LearnedFTL은 매핑 캐시 miss 때 변환 페이지를 플래시에서 동기식으로 읽는 동안 다른 요청을 처리하지 못해 4k IOPS 수준에서 막힌다. 고치는 중이다.
- LearnedFTL은 순차 쓰기로 채운 뒤라 모델이 거의 모든 주소를 맞힌다. 랜덤으로 쓴 데이터의 모델은 GC 때 학습되므로 이 측정(GC 없음)으로는 볼 수 없다.
