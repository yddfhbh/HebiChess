# JJUGLE Chaos v4.9.11 — Defensive Pressure Triage

이 패키지는 정상 운영 중인 v4.9.10을 기준으로 만든 **독립 실험/배포용 전체 소스 스냅샷**입니다.

## 실전 기보 재현 (2026-11-12, JJUGLE vs sseh-c, FnEBrkoY)

- 31수, FEN: `3rq1k1/p2r4/1pQp4/1P1P1p2/R3p1p1/6R1/1PP2PP1/6K1 w - - 0 31`
  - v4.9.10 objective depth 9: `Rc3??` (g3c3)
  - v4.9.11 objective depth 9: `Qc3` (c6c3)
- 35수, FEN: `3r2k1/p6r/1pQp4/1P1P1p2/R3p1p1/R7/1PP1KPq1/8 w - - 0 35`
  - v4.9.10 objective depth 9: `Rxa7??` (a4a7)
  - v4.9.11 objective depth 9 remains `Rxa7`; but final CHAOS choice is `Rg3` (a3g3) after a bounded search proof.
- Local Linux 3+2 time-budget tests: 31 `c6c3` depth 9; 35 `a3g3` depth 10, defense candidates 1 / selected 1.
- 14/14 CTest regression tests passed on Linux x86_64 (Release, production macros). **ARM64 production has not yet been verified.**

## 변경 범위

1. CHAOS 전용 퀸+룩의 킹 침투 위험 평가. 중반 이후(`fullmove > 16`), 주요 기물이 충분히 남은 포지션(`phase >= 14`)에서만 적용하며 HCE와 NNUE는 불변.
2. 상대 퀸이 킹에서 3칸 이내이고, 룩이 원거리 폰 획득을 선택할 때: 직접 퀸을 공격하는 안전한 룩 수의 점수를 *별도 탐색으로 검증*하고 최대 90cp 범위에서 선택 가능.
3. 즉시 무승부 방어를 덮어쓰지 않으며, 최선 수의 객관적 탐색 점수와 TT는 변경하지 않음.
4. v4.9.10 메이트 위협·v4.9.9 초반 전개 등 기존 테스트 보존.

수정된 위치: `src/chess/eval.cpp`, `src/chess/search.cpp`, `src/chess/search.hpp`, `src/chess/uci_engine.cpp`, `tests/v4911_defensive_pressure_test.cpp`, `CMakeLists.txt`, 이전 버전명 검사 테스트.

## 파일 사용법

`source-v4911.zip`: `V4911/` 안에 전체 C++ 소스 및 테스트.

`deploy-v4911.ps1`: Windows PowerShell 5.1/7에서 실행. 먼저 소스 ZIP 및 두 셸 스크립트를 SSH/SCP로 업로드하고, 서버 `labs/chaos-v4911-YYYYMMDD-HHMMSS/`에서 백그라운드 빌드/전체 ARM64 테스트를 실행.
업로드 체크섬 확인 및 3회 재시도가 들어 있습니다.

서버의 `remote-deploy-v4911.sh`는 다음을 *배포 전* 확인합니다:
- Lichess 계정 JJUGLE 및 활동 중 대국 0개 (실패시 배포 중단)
- 운영 바이너리 v4.9.10 SHA256 (기준 불일치 시 중단)
- 유일한 온라인 `jjugle-lichess` PM2 프로세스, CHAOS 설정
- ARM64 빌드, UCI smoke, **14/14** 테스트
- 교체 직전 두 번째 활동 대국 확인
- 백업 후 바이너리만 원자 교체, 프로세스 재시작, 실패시 백업 복구 시도

성공 표식: `ARM64_FULL_REGRESSION=14/14_PASS`, `V4911_DEPLOYED=YES`, `JJUGLE_PM2=ONLINE`, `DEPLOY_EXIT=0`.

`verify-v4911.ps1`: 읽기 전용 검증. 실패/중간 상태의 로그만으로 성공을 단정하지 말 것.

`push-v4911-github.ps1`: **운영 검증 후에만** 실행. `yddfhbh/HebiChess` main 이 `1dbaa4430367709ee72d173c4ee9a2ea6e726ec6`인 경우에만 `engines/jjugle-chaos/` 디렉터리를 교체하여 커밋/푸시. 그 외 경로는 보존. 변경 시 자동 중단.

## 제한 및 주의사항

- 이 수정은 제한된 기보에 대한 전술적 방어 개선이며 일반적인 Elo 향상을 증명하지 않습니다.
- 루트 방어 대안은 메이트 증명이나 정확한 점수가 아닌, 안전성 **하한값**의 완결된 검색으로 검증됩니다.
- 일반적인 31/35수 결정에서 추가 시간/노드를 요구할 수 있으므로 새 실전 로그에서 Depth/Nodes/Time을 비교하십시오.
- 현재 운영 v4.9.10을 바로 지우지 말고, 배포 로그에서 모든 성공 표식을 확인하십시오.
