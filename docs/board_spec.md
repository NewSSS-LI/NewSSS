# 보드 스펙 (실제 하드웨어 검증 완료)

확인 방법: 실제 물리 보드에 `CosmosPlus_OpenSSD_Micron_8C2W_19.1` 펌웨어를 JTAG로 올리고 UART 부팅 로그로 직접 확인, 해당 프로젝트의 `ftl_config.c` 소스코드 대조. 2026-11-16에 칩 표면 번호(Micron FBGA 디코더), Micron 데이터시트, 판매사 페이지, 보드 사진으로 다시 확인했다. 튜토리얼 문서 추측이 아니라 실측/실증 기반.

## 용어
- **낸드 모듈**: 보드의 SO-DIMM 슬롯에 꽂는 파란색 기판. 보드에 2개 꽂혀 있다.
- **칩**: 낸드 모듈 위의 검은색 NAND 패키지. 모듈 앞면과 뒷면에 4개씩, 모듈당 8개, 보드 전체 16개다.
- **다이(die)**: 칩 안에 들어 있는 실제 플래시 단위. 펌웨어가 "채널 x 웨이"로 하나씩 골라 명령을 보내는 대상이다. 이 칩은 칩 1개 = 다이 1개 = CE 1개다(아래).

## 플랫폼
- **보드**: Cosmos+ OpenSSD (보드 인쇄: "COSMOS REV 2.1 / OPENSSD 2.1", Hanyang University ENC Lab, CRZ Technology)
- **레포/프로젝트**: `CRZ-Technology/OpenSSD-OpenChannelSSD` → `CosmosPlus/OpenSSD/Micron_NAND_64GB/CosmosPlus_OpenSSD_Micron_8C2W_19.1`
  - 레포 README 설명: "OpenSSD project w/ Micron 8Channel 2Way NAND module on CosmosPlus board". 2025년 최신판(`CosmosPlus/2025.1/Micron_NAND/CosmosPlus_OpenSSD_Micron_8C2W`)도 8C2W다.
- **FPGA/SoC**: Xilinx Zynq-7000 (XC7Z045-FFG900-3), FPGA에 NAND 컨트롤러 8개(T4NFC 0~7)
- **CPU**: ARM Cortex-A9 듀얼코어 1GHz (PS 영역)
- **DRAM**: 1GB DDR3
- **개발 툴체인**: Vivado/Xilinx SDK 2019.1
- **구매 페이지**:
  - 보드: https://crz-mart.com/shop/shopdetail.html?branduid=1914663 — 스펙에 "NAND Module"만 있고 채널·웨이·용량은 없다. 낸드 모듈은 별도 옵션(+670,000원).
  - 낸드 모듈: https://crz-mart.com/shop/shopdetail.html?branduid=1981812 — "64GBytes Micron NAND Module for Cosmos+ OpenSSD", "4 x 128Gbit Micron MLC NAND", "Support 4channel 4way NAND Flash Interface", "Compatible Cosmos OpenSSD Main Board". 실제 모듈과 다르다. 아래 "칩 수 확인" 참고.

## NAND Flash
- **칩 표면 표시**: `91E22` / `NQ466` (보드 사진, 2026-11-16)
- **칩 부품 번호**: **MT29F128G08CBCEBJ4-37ITR:E** (Micron FBGA 디코더에서 NQ466 조회, 2026-11-16)
  - 디코더 결과: "MLC 128G X8 VBGA", 분류 obsolete-mlc-nand (단종)
  - 조회 방법: `https://www.micron.com/content/micron/us/en/sales-support/design-tools/fbga-parts-decoder/_jcr_content.products.json/getpartbyfbgacode/-/-/-/en_US/-/-/NQ466`
- **칩 구성** (데이터시트 `CosmosPlus/DOC/L05B_128-256-512Gb_MLC_Fortis_Flash_NAND.pdf` 1~2쪽, Micron "Fortis Flash"):
  - 부품 번호의 분류 문자 `B` = **다이 1개, CE 1개, R/B 1개**. 칩 1개 = 다이 1개다. (같은 계열 256Gb `E`는 다이 2개, 512Gb `M`은 다이 4개)
  - 다이 용량 128Gb(16GiB, MLC), 4 plane x 548 블록 = **2,192 블록**, 블록당 MLC 512 페이지, 페이지 16,384 + 2,208 바이트
  - 수명: MLC 3,000회 / **SLC 25,000회** P/E. 칩이 SLC(pSLC) 모드를 공식 지원한다.
- **Device ID**: `2C 84 44 34 AA 00` (부팅 시 ch0 way0의 `ReadId`와 `ftl_config.c`의 NAND ID 테이블 대조). 펌웨어가 이 칩을 `MT29F128G08CBCEB`로 인식하고 여분 영역 2,208바이트, 예비 블록 144개(2,048 + 144 = 2,192)로 설정한다. 부팅 로그의 불량 블록 번호가 2,191번까지 나와 데이터시트와 맞는다.
- **동작 모드**: **pSLC (pseudo-SLC)**. MLC 셀에 1비트만 쓴다. 용량은 MLC의 절반이고 쓰기가 빠르며 수명이 길다.
  - NAND 명령: `T4NSC_CMD_PROGRAM_PAGE_PSLC`, `T4NSC_CMD_READ_PAGE_TRIGGER_PSLC`, `T4NSC_CMD_READ_TRANSFER_PSLC` (`nsc_driver.c`)
  - 블록당 MLC 512 페이지 중 LSB 페이지 256개만 쓴다(`Vpage2PlsbPageTranslation`).
  - 설정 `BITS_PER_FLASH_CELL = SLC_MODE`. `CheckConfigRestriction`이 SLC_MODE가 아니면 assert로 멈춘다. 즉 Micron 공식 펌웨어(19.1, 2025.1)와 Toshiba 펌웨어는 MLC 모드를 막아 두었다. Hynix 8C8W 펌웨어만 MLC를 허용하지만 기본값은 SLC다. 막아 둔 이유는 문서에 없다.
- **구성**: 8채널 x 2웨이 = **16 다이** (펌웨어 `USER_WAYS 2`, FPGA 채널 8개)
  - 부팅 로그에 ch 0~7 x way 0~1, 16개 다이 각각의 불량 블록 표가 있다.
  - 이 칩은 `way_table`이 그대로(웨이 0 → CE0, 웨이 1 → CE1)라서, 웨이 2개 = 채널마다 CE가 다른 칩 2개다.
  - 32GB 측정에서 16개 다이 모두에 데이터를 쓰고 RocksDB·fio 데이터 검증을 통과했다. 웨이 0과 1이 같은 칩을 가리킨다면 서로 덮어써서 검증이 실패했을 것이다. 따라서 16개 다이는 서로 다른 실제 다이다.
- **펌웨어가 쓰는 블록**: 다이당 2,048 블록(user blocks), 블록당 256 페이지(pSLC)
- **페이지 크기(FTL 기준)**: 16,384 Bytes (16KB) data + 256 Bytes spare
- **매핑 단위(slice)**: 1 slice = 1 page = 16KB (full page-level mapping)

## 용량
| 항목 | 계산 | 값 |
|---|---|---|
| 칩(다이) 수 | 8채널 x 2웨이 | 16 |
| 플래시 실제 용량 (MLC) | 16 x 128Gb(16GiB) | 256GiB |
| pSLC로 쓰는 용량 | 16 x 2,048 블록 x 256 페이지 x 16KB | 128GiB (131,072MB) |
| 호스트에 보이는 용량 | 131,072 − 여유 블록 64 − OP 10% 13,104 − 불량 0 | **117,904 MB** (부팅 로그 `[ storage capacity 117904 MB ]`) |
| MLC로 썼다면 | | 약 236GB |

- 32GB 실험 모드(`-DEXP_LOGICAL_GB=32`): 16개 다이 모두 쓰고 다이당 2,048 블록 중 570 블록만 쓴다(약 36.5GB, 여유 공간 약 11%). 채널·웨이는 줄이지 않는다.

## 칩 수 확인 (2026-11-16)
- 낸드 모듈 **앞면 4개 + 뒷면 4개 = 모듈당 칩 8개, 보드 전체 16개** (사용자가 뒷면 직접 확인). 칩 1개 = 다이 1개이므로 다이 16개 = 8채널 x 2웨이와 맞는다. 낸드 모듈 1개가 4채널 x 2웨이를 맡는다.
- 판매 페이지의 "4 x 128Gbit, 64GB"는 모듈 한 면 기준이거나 이전 세대 Cosmos 보드용 문구로 보인다("Compatible Cosmos OpenSSD Main Board"). 실제 모듈은 칩 8개, 128GB(MLC)다.
- "4channel 4way"는 낸드 모듈이 지원하는 최대 인터페이스로 보인다. 이 보드는 모듈당 4채널 x 2웨이만 쓴다.

## 용어 주의: "다이"가 문서마다 뜻이 다르다
- **Micron 데이터시트의 die**: 칩 안의 실리콘 조각 1개(= LUN). 이 칩은 칩 1개 = die 1개 = LUN 1개 = CE 1개.
- **Cosmos+ 펌웨어의 die**: `USER_DIES = 채널 x 웨이`. 웨이는 CE(칩 선택 신호) 하나다. 즉 펌웨어의 die는 "CE 하나로 고르는 대상"이고, 그 안에 LUN이 `LUNS_PER_DIE`개 있다.
  - Micron 8C2W 펌웨어: `LUNS_PER_DIE 1` → 펌웨어 die 1개 = 칩 1개 = 실리콘 die 1개.
  - Hanyang 레포(Hynix 8Ch8Way) 펌웨어: `LUNS_PER_DIE 2` → 펌웨어 die 1개 안에 LUN 2개. Hynix 칩 기준이고 우리 칩과 무관하다.

## 참고: 공식 튜토리얼 성능 그래프와의 관계
- `Cosmos+ OpenSSD 2017 Tutorial.pdf` 슬라이드 164~167의 성능 그래프는 **Hynix 8채널 x 8웨이 모듈**(64 다이) 기준이다. 우리 낸드 모듈(Micron, 16 다이)과 칩이 다르다.
- 같은 시기 공식 레포 https://github.com/Cosmos-OpenSSD/Cosmos-plus-OpenSSD (한양대, 마지막 갱신 2022-07)의 GreedyFTL 2.7.0/3.0.0도 Hynix 8Ch8Way 기준이다: 페이지 16,384 + 1,664 바이트, 블록당 MLC 256 페이지, LUN당 4,096 블록, 다이당 LUN 2개, `USER_WAYS 8`. 기본값은 `SLC_MODE`이고 MLC도 허용한다. Micron 낸드 모듈용 코드는 이 레포에 없다.
- 비교 기준: 8ch-8way 그래프를 1/4(다이 64 → 16)로 본다. 칩 종류가 달라 대략적인 기준이다.

## 호스트 인터페이스
- NVMe over PCIe (PCIe Gen2 x8, Tiger4 컨트롤러)
- NVMe 블록 크기: 4096 Bytes

## 현재 올라가 있는 펌웨어
- **GreedyFTL 3.0.0** (pure page-level mapping + greedy GC victim 선정 + LRU data buffer)
- 소스 위치: `C:\Users\user\Documents\GitHub\CosmosPlus_OpenSSD\CosmosPlus\OpenSSD\Micron_NAND_64GB\CosmosPlus_OpenSSD_Micron_8C2W_19.1\cosm-plus-sys.sdk\run-gftl3\src\`

## 개발 환경
- Dev PC(노트북, Windows): JTAG + UART(COM3, 115200 baud)로 보드 연결
- Host PC(Linux, 보드 1 호스트): PCIe로 연결, `/mnt/ssd`에 마운트해서 측정 (예전 기록: SSSLab-Com4, `/mnt/openssd`)

---
*작성: 2026-09-30, Claude Code 세션에서 소스코드+부팅로그 직접 대조 확인*
*갱신: 2026-11-16, 칩 부품 번호(FBGA 디코더)·데이터시트·판매 페이지·보드 사진·Hanyang 레포 확인, pSLC와 칩/다이 구성 추가*
