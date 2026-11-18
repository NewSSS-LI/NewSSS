# 실험 스크립트 (bench)

실험마다 폴더를 하나씩 둔다. 각 폴더의 README에 목적, 조건, 실행 방법이 있다.

| 폴더 | 실험 | 상태 |
|---|---|---|
| [`fio-check/`](fio-check/README.md) | FTL 구현 확인: GC 없는 작은 FIO(순차 쓰기·읽기, 랜덤 읽기·쓰기). 공식 Cosmos+ 튜토리얼 성능과 비교 | 사용 중 |
| [`learnedftl-paper/`](learnedftl-paper/README.md) | LearnedFTL 논문(HPCA'24) 실험 재현: FIO, Filebench, RocksDB. 보드 3개에서 FTL만 바꿔 같은 조건으로 측정 | 멈춤(2026-11-16, FTL 구현을 먼저 바로잡기로). `fio-check`가 이 폴더의 `host/`(config.sh, common.sh 등)를 같이 쓴다 |

## 새 실험을 추가할 때

- `bench/<실험 이름>/`을 만들고 README에 목적, 비교할 FTL, 조건, 실행 방법을 적는다.
- 위 표에 한 줄 추가한다.
- 호스트마다 다른 설정 파일은 `<실험 이름>/host/config.sh`에 두면 git에 올라가지 않는다(`.gitignore`).
- `*.sh`, `*.py`는 LF 줄바꿈으로 저장된다(`.gitattributes`).
