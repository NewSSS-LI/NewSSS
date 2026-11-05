# LearnedFTL on Cosmos+ OpenSSD

LearnedFTL(Wang et al., HPCA'24)을 Cosmos+ OpenSSD Micron 8C2W 보드용으로 이식한 펌웨어입니다. GreedyFTL(`run-gftl3`)을 복제해 주소 변환, 할당, GC를 교체했습니다. 설계와 논문·레포 분석은 [`docs/learnedftl_analysis.md`](../../docs/learnedftl_analysis.md)를 참고하세요.

**현재 상태:** PC 시뮬레이터 검증과 ARM 빌드까지 완료했고, **실제 보드에서는 아직 실행하지 않았습니다.**

## 폴더 구성

- `src/`: 펌웨어 소스 (`lftl_*`가 새 코드)
- `test/sim_main.c`: 실제 코어 코드를 시뮬레이션 NAND 위에서 돌리는 검증 프로그램
- `test/results/`: 시뮬레이션 결과
- `build_arm.sh`: SDK 워크스페이스를 열지 않고 빌드 여부를 확인하는 스크립트

## 보드에 올리기

작업 원본은 `cosm-plus-sys.sdk/run-lftl`에 있습니다. 한글 경로에서는 SDK가 깨지므로 영문 경로를 유지하세요.

1. SDK에서 `File → Import → Existing Projects into Workspace`로 `run-lftl`을 가져와 빌드합니다. 워크스페이스에 `run-gftl3_bsp`가 있어야 합니다.
2. **첫 실행은 검증 모드로 합니다.** `src/lftl_config.h`에서 `LFTL_SHADOW_CHECK`를 `1`로 바꾸고 빌드합니다. 모든 변환을 전체 매핑표와 비교하고, 불일치하면 UART에 `[LFTL FATAL]`을 출력한 뒤 멈춥니다.
3. `Xilinx Tools → Program FPGA`(기존 비트스트림)를 실행한 뒤 `Run As → Launch on Hardware`로 실행합니다.
4. UART 부팅 로그를 확인합니다.
   ```
   [ LearnedFTL: 1999 groups, 2152 KB metadata, model on, CMT 8192 entries ]
   [ storage capacity 117904 MB ]
   ```
   불량 블록이 있으면 그룹 수와 용량이 조금 줄 수 있습니다.

## 첫 동작 확인

아래 명령은 지정한 장치의 데이터를 덮어씁니다. **먼저 `/dev/nvme0n1`이 보드인지 반드시 확인하세요.** 호스트 내장 SSD로 장치명이 넘어간 사고가 있었고, 그 상태로 실행하면 내장 SSD의 데이터가 지워집니다.

```bash
lspci | grep -i non-volatile     # Xilinx Device 7028 확인
sudo nvme list                   # 용량 약 118GB의 장치가 어느 이름인지 확인

# 데이터 무결성 확인 (GC가 돌도록 작은 범위를 반복해서 덮어씀)
sudo fio --name=verify --filename=/dev/nvme0n1 --rw=randwrite --bs=16k --size=2G --loops=8 \
    --ioengine=libaio --direct=1 --iodepth=4 --verify=crc32c --do_verify=1
```

FIO가 끝나고 2초가 지나면 UART에 통계가 출력됩니다.

| 항목 | 확인할 내용 |
|---|---|
| `modelRejected` | **항상 0**이어야 함. 0이 아니면 버그 |
| `readModelHits`, `readCmtHits`, `readTpMisses` | 읽기 경로 비율. `readTpMisses`가 double read |
| `gcGroups`, `gcCopiedPages`, `tpWrites` | WA = (hostWrites + gcCopiedPages + tpWrites) / hostWrites |
| `gcMs`, `trainUs` | GC 전체 시간과 학습 시간 (논문 Fig. 17 비교용) |

문제가 없으면 `LFTL_SHADOW_CHECK`를 `0`으로 되돌리고 다시 빌드한 뒤 성능을 측정하세요. Shadow 검사가 켜져 있으면 측정값이 왜곡됩니다.

## 측정 권장안

기존 조건(`bs=128k`, 2GB)은 요청 하나가 8개 슬라이스라 모델 효과가 잘 드러나지 않습니다. 논문의 효과를 보려면 작은 블록과 넓은 범위가 필요합니다.

```bash
# 1. 전체 용량 순차 채우기 (약 118GB)
sudo fio --name=fill --filename=/dev/nvme0n1 --rw=write --bs=128k --ioengine=libaio --direct=1 --iodepth=32
# 2. 랜덤 읽기
sudo fio --name=rr --filename=/dev/nvme0n1 --rw=randread --bs=16k --runtime=60 --time_based \
    --ioengine=libaio --direct=1 --iodepth=32
```

비교 대상은 `lftl_config.h`로 전환합니다.

| 구성 | 설정 | 용도 |
|---|---|---|
| LearnedFTL | 기본값 | |
| TPFTL | `LFTL_ENABLE_MODEL=0` | **모델 효과만 비교** |
| TPFTL, 같은 메모리 | `LFTL_ENABLE_MODEL=0`, `LFTL_CMT_CAPACITY=65536` 또는 `131072` | 같은 DRAM을 캐시에 썼을 때와 비교 |
| GreedyFTL | `run-gftl3` | 상한선 |

`LFTL_CMT_CAPACITY`는 2의 거듭제곱이어야 합니다.

## 시뮬레이터 실행

Vivado에 포함된 mingw gcc를 사용합니다.

```bash
export PATH=/c/Xilinx/Vivado/2019.1/msys64/mingw64/bin:$PATH
cd test && mkdir -p build
gcc -std=gnu99 -O2 -DLFTL_HOST_TEST -DLFTL_SHADOW_CHECK=1 -DUSER_BLOCKS_PER_DIE=256 -DLFTL_GROUP_SPAN=3840 -I../src \
    sim_main.c ../src/lftl_core.c ../src/lftl_model.c ../src/lftl_cmt.c -o build/sim.exe
./build/sim.exe 1
```

라인 수를 줄이면 빠르게 돌지만, 여유 라인 수는 줄어듭니다. 256라인에서는 `LFTL_GROUP_SPAN=3776`이 라인 부족으로 시작되지 않을 수 있으니 3840을 쓰세요. 실제 보드 규모는 `USER_BLOCKS_PER_DIE=2048`이며 수 분 걸립니다.
