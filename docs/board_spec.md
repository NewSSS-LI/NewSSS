# 보드 스펙 (실제 하드웨어 검증 완료)

확인 방법: 실제 물리 보드에 `CosmosPlus_OpenSSD_Micron_8C2W_19.1` 펌웨어를 JTAG로 프로그래밍하고 UART 부팅 로그로 직접 확인 + 해당 프로젝트의 `ftl_config.c` 소스코드 대조. 튜토리얼 문서 추측이 아니라 실측/실증 기반.

## 플랫폼
- **보드**: Cosmos+ OpenSSD (Hanyang University ENC Lab)
- **레포/프로젝트**: `CRZ-Technology/OpenSSD-OpenChannelSSD` → `CosmosPlus/OpenSSD/Micron_NAND_64GB/CosmosPlus_OpenSSD_Micron_8C2W_19.1`
- **FPGA/SoC**: Xilinx Zynq-7000 (XC7Z045-FFG900-3)
- **CPU**: ARM Cortex-A9 듀얼코어 (PS 영역)
- **개발 툴체인**: Vivado/Xilinx SDK 2019.1

## NAND Flash
- **제조사/모델**: Micron **MT29F128G08CBCEB**
  - Device ID: `2C 84 44 34 AA 00` (부팅 로그 `ReadId`와 `ftl_config.c`의 NAND ID 테이블 대조로 확정)
- **동작 모드**: SLC 모드
- **구성**: 8채널 × 2웨이 = **16 다이(die)**
- **다이(LUN)당 블록 수**: 2048 (user blocks)
- **블록당 페이지 수**: 256
- **페이지 크기**: 16,384 Bytes (16KB) data + 256 Bytes spare
- **매핑 단위(slice)**: 1 slice = 1 page = 16KB (full page-level mapping)

## 용량
- Raw 용량: 약 128GB (32,768 블록 × 4MB/블록)
- 실사용 가능 용량: **117,904 MB (약 117.9GB)** — over-provisioning + 메타데이터 영역 제외 후, 실제 부팅 로그에서 확인된 값과 일치

## 호스트 인터페이스
- NVMe over PCIe (PCIe Gen2, Tiger4 컨트롤러)
- NVMe 블록 크기: 4096 Bytes

## 현재 올라가 있는 펌웨어
- **GreedyFTL 3.0.0** (pure page-level mapping + greedy GC victim 선정 + LRU data buffer)
- 소스 위치: `C:\Users\user\Documents\GitHub\CosmosPlus_OpenSSD\CosmosPlus\OpenSSD\Micron_NAND_64GB\CosmosPlus_OpenSSD_Micron_8C2W_19.1\cosm-plus-sys.sdk\run-gftl3\src\`

## 개발 환경
- Dev PC(노트북, Windows): JTAG + UART(COM3, 115200 baud)로 보드 연결
- Host PC(SSSLab-Com4, Linux): PCIe로 연결, `/mnt/openssd`에 ext4 마운트해서 FIO 테스트

---
*작성: 2026-09-30, Claude Code 세션에서 소스코드+부팅로그 직접 대조 확인*
