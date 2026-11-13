# LeaFTL on Cosmos+ OpenSSD

DFTL을 기반으로 LeaFTL의 학습 매핑을 구현한 **Cosmos+ OpenSSD용 C 펌웨어**다. PC 통합 검증, Xilinx SDK 2019.1 ARM 빌드 및 실제 보드의 기본 I/O·OOB 보정·매핑 Flash 재조회 검증을 수행했다. 4GiB 실험에서는 자동 GC의 유효 페이지 복사와 블록 erase 발생도 확인했다.

## 구현 범위

- 256-LPN 그룹, 그룹별 다중 level 인덱스, 최신 매핑 우선 조회, merge·compaction
- 8-byte segment와 CRB를 이용한 학습·주소 예측·실제 LPN 소속 판별
- LPN 순서로 정렬한 writeback, 실제 할당 PPN 학습, GC 이동 후 매핑 재학습
- Flash-backed 매핑 캐시, segment·CRB 저장/재조회, 매핑 메타데이터 GC
- OOB 역매핑 검증, 오예측 보정 재읽기, 검증 실패 시 host DMA 차단
- NVMe FLUSH/FUA의 데이터·매핑 쓰기 완료 연결과 I/O·GC·메모리 계측

기본 SDK 설정은 **`LEAFTL_GAMMA=4`**, 매핑 캐시 예산은 **1MiB**, resident group 상한은 **256개**다. `EXP_LOGICAL_GB`는 기본 0으로 전체 보드 용량을 사용한다. 4GiB 자동 GC 실험은 별도로 `-DEXP_LOGICAL_GB=4`를 사용했으며, 이 제한은 현재 Debug/Release 설정에서 제거했다.

## 폴더 구조

```text
LeaFTL/
├── .project
├── .cproject
├── src/                 # 펌웨어와 링크 스크립트
├── docs/                # 구현 단계·메모리·보드 검증 기록
├── tests/               # PC 테스트와 모의 NAND/DMA 통합 검증
└── README.md
```

## 빌드·실행

1. Xilinx SDK 2019.1에 기존 Micron 8C2W 하드웨어 플랫폼과 `run-gftl3_bsp`를 준비한다. BSP·`system.hdf`·bitstream은 이 폴더에 포함하지 않는다.
2. **이 `LeaFTL` 폴더 자체**를 Existing Projects into Workspace로 Import한다. SDK 프로젝트 이름은 `LeaFTL-Work`다.
3. Refresh → Clean → Build 후 `Debug/LeaFTL-Work.elf`를 실행한다. FPGA에는 기존 Micron 8C2W 19.1의 `sys_top_wrapper.bit`를 사용한다.
4. UART에서 `FTL: LeaFTL gamma=4`, 용량 및 `ftl configuration complete`를 확인한다.

PC 검증은 Windows의 MSVC Build Tools와 Windows SDK를 사용한다. 이 폴더에서 실행한다.

```bash
python tests/run_firmware_validation.py --gammas 4
```

γ=0·1·4·16 통합 검증을 수행했으며, 위 명령은 γ=4만 검사한다. PC 검증은 모의 NAND/DMA를 사용하고 실제 SSD에 접근하지 않는다. 전체 ARM 펌웨어 28개 C 소스도 컴파일·링크를 확인했다.

## 실제 보드 검증 결과

- γ=0/4: 순차 쓰기, 덮어쓰기, 무작위 갱신, 4KiB 부분 쓰기, LPN 255/256 그룹 경계의 데이터 비교 통과
- γ=4: 명시적 NVMe FLUSH/FUA 후 데이터 버퍼 교체·읽기 비교 통과
- OOB 보정 경로 41회, 매핑 Flash read 0→324회 / program 4→14회 확인; 해당 실험 I/O·sync 실패 0회
- 4GiB 자동 GC 실험: 유효 페이지 20,480개(320MiB) 복사, erase 160회, I/O·sync 실패 0회 확인. erase 통계는 메타데이터 erase도 포함한다.

통계에는 내부 매핑 확인용 I/O도 포함되므로 그대로 host 오예측률이나 성능 비교값으로 사용하지 않는다. 상세 조건·확인 범위는 [구현 보고서](docs/IMPLEMENTATION_REPORT.md) 후반의 보드 실험 기록을 따른다.

## 현재 한계

- **부팅 시 NAND를 초기화한다.** 기존 데이터 이관과 전원 차단·재시작 복구를 지원하지 않는다. FLUSH/FUA 연결은 전원 장애 영속성 보장을 의미하지 않는다.
- 8-byte segment 외에 node·CRB·directory·작업 버퍼와 기존 full forward/reverse map 예약 영역이 남아 있다. segment 크기를 전체 RAM 사용량으로 해석하지 않는다.
- 동일 조건의 DFTL 대비 성능·지연·WAF 비교와 장시간 안정성 실험은 후속 작업이다.

## 문서·참고 자료

- [구현 순서·보고서·보드 검증 결과](docs/IMPLEMENTATION_REPORT.md)
- [메모리 배치](docs/MEMORY_LAYOUT.md), [8-byte segment 형식](docs/SEGMENT_8BYTE.md), [논문 반영 기록](docs/PAPER_ALIGNMENT.md)
- 초기 준비 문서와 단계별 기록의 “미구현/미검증” 표현은 해당 기록 시점의 상태다. 현재 상태는 이 README와 구현 보고서 후반의 최신 기록을 기준으로 확인한다.
- LeaFTL 논문: *LeaFTL: A Learning-based Flash Translation Layer for Solid-State Drives*, ASPLOS 2023
- [LeaFTL 공개 구현](https://github.com/platformxlab/LeaFTL), [기반 DFTL](https://github.com/NewSSS-LI/NewSSS/tree/main/firmware/dftl)
