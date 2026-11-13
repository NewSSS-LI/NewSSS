# LeaFTL 이식 준비용 작업 폴더

현재 상태: **준비만 완료. LeaFTL은 아직 구현하지 않았다.**

`firmware/leaftl/`은 `../NewSSS-DFTL/firmware/dftl/`을 숨김 SDK 설정 파일까지 복사한 DFTL 원본이다. 폴더 이름만 LeaFTL 작업 대상으로 정했으며, C 코드와 SDK 프로젝트 설정은 변경하지 않았다. 원본과 복사본 46개 파일의 SHA-256이 일치하는 것을 확인했다.

- `docs/PORTING_PLAN.md`: 논문 기반 설계, 기존 함수와 수정 지점, 구현 순서, 검증 기준
- `docs/FILE_PREPARATION.md`: 기존 파일의 역할 및 앞으로 만들 파일 목록
- `docs/baseline-sha256.json`: 원본 파일 해시와 출처

## 먼저 알아둘 점

1. 공개 LeaFTL 저장소는 Python/WiscSim 실험 코드다. Cosmos+용 C 펌웨어에 그대로 복사해서 빌드할 수 없다.
2. DFTL 프로젝트는 Cosmos+ OpenSSD 및 Xilinx SDK 2019.1 환경을 전제로 한다. 보드/BSP/툴체인은 실제 보유 환경을 확인해야 한다.
3. 복사한 `.project`의 실제 프로젝트 이름은 `run-gftl3`이다. 원본 README의 명칭과 다르므로 SDK Import 전에 복사본에서 프로젝트 이름을 별도로 정해야 한다. `run-gftl3_bsp` 참조는 실제 BSP 프로젝트가 있어야 유효하다.
4. 필요한 BSP, 하드웨어 플랫폼, `system.hdf`, FPGA bitstream은 이 복사본에 포함되지 않았다. 원본 README도 별도 준비를 요구한다.
5. 보드 빌드/플래싱/성능 실험은 하지 않았다. 원본 README의 빌드 성공 기록을 이 복사본의 검증 결과로 간주하면 안 된다.

## 참고 자료

- 로컬 논문: `C:/Users/kimeu/Desktop/SSS-Lab/LeaFTL.pdf`, ASPLOS 2023
- 공개 구현: https://github.com/platformxlab/LeaFTL
- 핵심 코드: https://github.com/platformxlab/LeaFTL/blob/main/wiscsee/wiscsim/learnedftl.py
- 원본 DFTL: https://github.com/NewSSS-LI/NewSSS/tree/main/firmware/dftl

공개 구현의 클래스/구조와 논문 설계는 참고 대상으로 사용한다. Python 소스를 C 파일로 단순 변환하는 작업은 아니다. 논문에 등장하는 별도 C 프로토타입과 공개 Python 시뮬레이터도 구별해야 한다.
