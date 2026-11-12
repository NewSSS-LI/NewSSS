# 공용 벤치마크: Filebench · RocksDB (LearnedFTL 논문 재현)

보드 3개에서 FTL만 바꿔 같은 조건으로 측정하기 위한 스크립트다. 펌웨어는 이 레포 `firmware/`에 올라간 것을 그대로 쓴다.

| 담당 | FTL | 펌웨어 폴더 |
|---|---|---|
| 보드 1 | LearnedFTL, TPFTL | `firmware/learnedftl`, `firmware/tpftl` |
| 보드 2 | DFTL, LeaFTL | `firmware/dftl`, `firmware/leaftl` |
| 보드 3 | GreedyFTL | `firmware/greedyftl` |

- LearnedFTL은 `firmware/learnedftl`(float16, 논문 설정)을 쓴다. `firmware/learnedftl-fixed`는 비교용이라 이 측정에 쓰지 않는다.
- LeaFTL은 아직 레포에 없다. 담당자가 `firmware/leaftl`에 올린 뒤 측정한다.

## 0. 시작 전에 확인할 것

- **스크립트는 FTL 종류와 상관없이 같다.** 호스트에서 보드(NVMe 장치)에 같은 부하를 줄 뿐이고, 보드에 어떤 FTL이 올라가 있는지는 알 수 없다.
- **`config.sh`의 `FTL=`은 사람이 맞게 적는다.** 결과 폴더 이름과 CSV에 들어가는 이름표다. 펌웨어를 바꿔 올릴 때마다 함께 바꾼다(`learnedftl`, `tpftl`, `dftl`, `leaftl`, `greedyftl` 중 하나).
- **펌웨어는 레포 main의 `firmware/<ftl>/src`로 빌드한다.** 모든 FTL이 GC 복사를 16개 die에 나눠 하는 버전이어야 비교가 공정하다. 다른 브랜치나 개인 사본으로 빌드하지 않는다.
- **빌드 옵션에 `-DEXP_LOGICAL_GB=32`를 넣는다.** 다른 크기로 빌드하면 대상 확인에서 멈춘다.
- **레포 밖에서 가져온 펌웨어(LeaFTL 등)는** 올린 뒤 `./check_board.sh`가 `OK`인지 먼저 본다. 실패하면 스크립트의 확인값을 고치지 말고 펌웨어를 맞춘다(모델 `Cosmos+ OpenSSD`, 크기 32GB).
- **`FW_SHA256`은 올린 ELF로 구한다.** 개발 PC에서 `sha256sum <ELF 경로>`로 구하고, Windows PowerShell이면 `Get-FileHash <ELF 경로>`를 쓴다. `meta.txt`에 기록되어 어떤 펌웨어로 쟀는지 나중에 확인할 수 있다.

## 1. 측정하는 것

FTL 하나마다 `suite.sh`가 아래를 순서대로 한 번에 돌린다. 앞 단계가 SSD에 남긴 상태에서 다음 단계를 재므로 **순서와 구성을 바꾸지 않는다.**

| 순서 | 스크립트 | 내용 | 시간 |
|---|---|---|---|
| 1 | `paper_fio.sh` | 논문 IV-B FIO 절차. Filebench·RocksDB 전의 사전 조건(preconditioning)도 겸한다 | 6~9시간 |
| 2 | `fb_bench.sh` | Filebench fileserver, webserver, varmail (논문 Table I), 각 600초 | 1~3시간 |
| 3 | `rdb_bench.sh` | RocksDB db_bench fillseq → overwrite → readrandom → readseq (논문 IV-D) | 1~2시간 |

시간은 FTL에 따라 다르다. DFTL·TPFTL이 가장 오래 걸린다(랜덤 워밍업만 6시간 넘게).

### 세부 조건

**FIO (`paper_fio.sh`, 펌웨어를 새로 올린 빈 SSD에서 시작)**

| 단계 | fio 옵션 |
|---|---|
| seqwarm | 512KB 순차 쓰기 6회: `--rw=write --bs=512k --ioengine=libaio --iodepth=32 --size=100% --loops=6` |
| seqread | 4KB 순차 읽기 300초: `--rw=read --bs=4k --ioengine=psync --numjobs=64 --size=512M --offset_increment=512M` |
| randwarm | 512KB 랜덤 쓰기 192GB: `--rw=randwrite --bs=512k --ioengine=libaio --iodepth=32 --io_size=192G --norandommap --randrepeat=0` |
| randread | 4KB 랜덤 읽기 300초, psync 64 jobs |
| randwrite | 4KB 랜덤 쓰기 600초, psync 64 jobs |
| seqwrite | 4KB 순차 쓰기 600초, psync 64 jobs, job마다 512MB 구간 |

공통: `--direct=1 --group_reporting`, 단계 사이 20초 휴식.

**Filebench (`fb_bench.sh`, 1번 직후 펌웨어를 다시 올리지 않음)**

| 부하 | 파일 수 × 평균 크기 | 스레드 |
|---|---|---|
| fileserver | 225,000 × 128KB | 50 |
| webserver | 825,000 × 16KB | 64 |
| varmail | 475,000 × 16KB | 64 |

- Filebench 1.5-alpha3 기본 워크로드 파일에서 위 값과 `run 600`만 바꾼다.
- 부하마다 `mkfs.ext4 -F` → mount → 페이지 캐시 비우기 → 20초 대기 후 실행한다.

**RocksDB (`rdb_bench.sh`, 2번 직후)**

- 설정: db_bench v8.11.4, 1스레드, 키 1,000만 개(키 16B, 값 1,000B), 압축 없음, direct I/O(읽기와 flush·compaction 모두)
- 실행: `mkfs.ext4` 1회 후 fillseq → overwrite → readrandom 600초 → readseq 600초

**공통**
- Filebench와 db_bench는 메모리 2GB cgroup 안에서 돈다(`systemd-run -p MemoryMax=2G`). 페이지 캐시가 SSD 성능을 가리지 않게 하려는 것이다.
- 펌웨어는 32GB 모드로 빌드한다(`-DEXP_LOGICAL_GB=32`). 대상 확인이 이 크기를 기준으로 한다.

## 2. 준비 (처음 한 번)

### 개발 PC (펌웨어 빌드·적재)

1. `firmware/<ftl>/src`를 Xilinx SDK 2019.1의 Cosmos+ 프로젝트(`cosm-plus-sys.sdk`)에 넣고 빌드한다.
   - 컴파일러 옵션: `-O0 -g3 -mfp16-format=ieee -DEXP_LOGICAL_GB=32`. `-mfp16-format=ieee`는 LearnedFTL에만 필요하지만 모두 같게 둔다.
   - 소스의 설정값(CMT 크기 등)은 바꾸지 않는다.
2. 보드에 올리는 방법은 평소처럼 SDK의 "Program FPGA" → "Run As > Launch on Hardware"를 쓴다.
3. UART(115200 8N1)에 `[ storage capacity 32768 MB ]`와 `Turn on the host PC`가 보이면 정상이다.

### 호스트 PC (보드가 PCIe에 꽂힌 Linux PC, Ubuntu 22.04 기준)

```bash
git clone https://github.com/NewSSS-LI/NewSSS.git ~/NewSSS
cd ~/NewSSS/bench/host
cp config.example.sh config.sh     # FTL, BOARD_ID 등을 채운다
./setup_host.sh                    # fio, Filebench, db_bench 설치 + sudo 규칙 안내
```

`setup_host.sh` 마지막에 나오는 sudo 규칙을 `sudo visudo -f /etc/sudoers.d/newsss-bench`로 넣는다. 스크립트는 `sudo -n`으로 정해진 명령만 쓴다. 보드 장치가 `nvme0n1`이 아니면 규칙의 장치 이름을 바꾼다.

## 3. 측정 순서 (FTL 하나마다)

1. **호스트:** `config.sh`의 `FTL=`을 이번 펌웨어 이름으로 바꾼다. 결과 폴더 이름과 기록에 들어간다.
2. **호스트:** `./pci_rescan.sh remove`로 보드를 떼어 낸다. 펌웨어를 새로 올리면 보드가 리셋되기 때문이다.
3. **개발 PC:** 펌웨어를 새로 올린다. 이전 데이터가 없는 빈 SSD로 시작해야 한다.
4. **호스트:** `./pci_rescan.sh rescan`으로 보드를 다시 잡는다. `OK /dev/nvme0n1 ...`이 나와야 한다. 실패하면 호스트를 재부팅한 뒤 `./check_board.sh`로 확인한다.
5. **호스트:** 아래처럼 측정을 시작한다. 터미널이나 SSH가 끊겨도 계속 돈다.
   ```bash
   FW_SHA256=<올린 ELF의 sha256> setsid -f nohup ./suite.sh > ~/suite_$(date +%m%d_%H%M).out 2>&1 < /dev/null
   ```
6. 끝나면 `.out` 마지막 줄이 `SUITE DONE`이다. 진행 상황은 결과 폴더의 `phases.log`에서 본다.
7. 결과를 CSV로 정리한다.
   ```bash
   ./summarize.py ~/newsss_results/<날짜>_<ftl>_<board>_fio ~/newsss_results/<…>_fb ~/newsss_results/<…>_rdb > <ftl>_<board>.csv
   ```
   CSV를 공유하면 FTL끼리 바로 합쳐 비교할 수 있다. 컬럼은 `ftl, board_id, run, benchmark, workload, metric, value, unit, run_dir`이다. MB는 10⁶ 바이트(논문과 같음)이고, db_bench의 MB/s(2²⁰ 바이트)는 변환해서 적는다.

측정 중에는 같은 보드에 다른 I/O를 내지 않는다. 한 번에 한 측정만 돌린다.

## 4. 결과 폴더

`$RESULTS_DIR/<날짜>_<ftl>_<board>_{fio,fb,rdb}/`

- `meta.txt`: FTL, 보드, ELF sha256, 레포 커밋, 커널, CPU, 메모리, 도구 버전
- `phases.log`: 단계별 시작·끝 시각과 요약
- 원본 출력: fio `*.txt`(텍스트 + JSON), `filebench_*.txt`, `dbbench_*.txt`

## 5. 안전장치

- 모든 스크립트는 SSD에 쓰기 전에 대상을 확인한다. 모델이 `Cosmos+ OpenSSD`이고 크기가 정확히 34,359,738,368 바이트인 NVMe 장치가 **딱 하나**여야 한다.
  - 아니면 아무것도 쓰지 않고 멈춘다. 호스트의 다른 디스크는 건드리지 않는다.
  - PCI 주소까지 고정하려면 `config.sh`의 `EXPECT_PCI`를 채운다.
- `fio`와 `mkfs.ext4`는 장치 전체를 덮어쓴다. 대상 확인을 끄거나 우회하지 않는다.

## 6. 알려진 점

- **TRIM 없음:** 펌웨어가 TRIM을 처리하지 않는다. `mkfs` 뒤에도 SSD 안은 이전 단계 데이터가 유효한 상태로 남는다. 그래서 1 → 2 → 3 순서가 결과에 영향을 준다.
- **readseq 처리 개수:** 2026-11 측정에서 FTL마다 readseq 처리 개수가 달랐다(1,000만 / 749만 / 90만). `operations` 값을 함께 보고, 1,000만이 아니면 따로 표시한다.
- **반복 측정:** 논문은 3회 이상 평균이다. 시간이 되면 FTL마다 `suite.sh`를 반복하되, 매번 펌웨어를 새로 올린다.

## 7. Claude에게 맡길 때

각자 쓰는 Claude에게 아래를 그대로 주면 된다(`<…>`만 채운다).

> 레포 `NewSSS`의 `bench/README.md`를 처음부터 끝까지 읽고 그대로 따라 해 줘.
> - 측정할 FTL: `<learnedftl | tpftl | dftl | leaftl | greedyftl>`
> - 보드 이름(BOARD_ID): `<예: board-yujung>`
> - 호스트 접속: `<ssh 별칭 또는 이 PC>`
>
> 지켜야 할 것:
> - "0. 시작 전에 확인할 것"부터 하나씩 확인하고 시작해.
> - 스크립트와 펌웨어의 조건(옵션, 순서, 시간, CMT 크기, 메모리 제한)을 바꾸지 마.
> - 바꿔야 할 것 같으면 먼저 나에게 물어봐.
> - 대상 확인(`check_board.sh`)이 실패하면 멈추고 알려줘.
> - 펌웨어는 측정 직전에 새로 올려. 이전 측정에 이어서 돌리지 마.
> - 단계(FIO, Filebench, RocksDB)가 끝날 때마다 결과를 요약해서 알려줘.
> - 끝나면 `summarize.py`로 CSV를 만들어 줘.
