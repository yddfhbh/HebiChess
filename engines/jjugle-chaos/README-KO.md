# JJUGLE Chaos v4.10.0 — Verified Attack Planning (candidate)

이 패키지는 v4.9.15 r2 및 운영 GitHub main 18d3e462dd49c892d26b40c881b365efa8507cbc 기반입니다.

## 구현

- 루트 CHAOS 후보 평가에 검증된 룩 리프트(Rd3, Rf3/Rh3 진입) 작은 보너스 추가. 여왕 협력·진입로 안전·상대 킹 위치를 확인하고 기존 깊이 검색에 따른 점수 하한 검증을 필수로 둡니다.
- 21~45수 중 객관적 후보를 근거 약한 조용한 퀸 이동으로 바꾸는 것을 제한. 기본 탐색에서 선택한 퀸 이동과 명확한 강제 체크·캡처·희생에는 적용하지 않습니다.
- LOBX9aul 22.Qh5 및 81...Rc1, tckafQbA 20.Rd3/24.Rd3 공격 준비, ZHbRP3f8 31...Nxf3 및 33...Rh2+ 희생 보존 회귀 테스트.
- UCI 결정 정보 `info string v4100_attack_planning` 출력.
- 오프닝 북·시간 관리·리체스 연결 설정은 바꾸지 않았습니다.

## 확인된 사항 및 미해결

- x86-64 릴리스 빌드와 v4100 표적 회귀 테스트 통과, v4915 희생 보존 회귀 테스트 통과.
- 같은 로컬 예제에서 LOBX9aul 22.Qh5는 Ne4로, tckafQbA 20수는 Rd3로 바뀌었으나 CPU/탐색 깊이에 따라 달라질 수 있습니다.
- 19.Qh5·20.Bd3는 기본 탐색의 오판으로, 이번 루트 스타일 변경만으로 해결되었다고 주장하지 않습니다.
- tckafQbA 24.Rd3는 후보로 인식하나 같은 로컬 시간 제한에서는 선택되지 않았습니다.
- v4.10.0 ARM64 전체 19/19 통과와 승률 향상은 아직 확인 전입니다.

## 운영 안전

- `deploy-v4100.ps1`: 서버 격리 실험 디렉터리에 소스와 배포 스크립트 전송, sha256 검증, 일회용 배포 실행.
- 서버 스크립트는 운영 바이너리 SHA-256이 기존 v4.9.15와 일치하는지 확인하고, 대국이 없는지 확인하고, ARM64 빌드·UCI·19/19 회귀 테스트를 완료한 후에만 봇을 교체합니다.
- 교체 전 백업하고 실패 시 롤백; PM2에서 `jjugle-lichess`만 재시작합니다.
- `verify-v4100.ps1`은 read-only 상태 확인.
- `push-v4100-github.ps1`은 운영 성공 확인 후 실행. GitHub main이 지정한 18d3e46 커밋과 다르면 중단합니다.

## 참고

서버가 사전에 설정한 SSH 키 `~/Downloads/arm-ssh-key` 와 경로 `/home/ubuntu/jjugle-lichess`가 필요합니다.
배포 중 `DEPLOY_LAUNCH=PASS`는 완료를 뜻하지 않습니다. `ARM64_FULL_REGRESSION=19/19_PASS`, `V4100_DEPLOYED=YES`, `DEPLOY_EXIT=0`, SHA-256 일치를 확인해야 합니다.
