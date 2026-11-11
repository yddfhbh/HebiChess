# 쮸글(JJUGLE) Chaos v4.9.7 — Material Imbalance

v4.9.7은 운영 v4.9.6 Fighting Spirit 소스 스냅샷을 기반으로 만든 실험 빌드입니다. 기존 운영 바이너리나 서버 파일은 수정하지 않았습니다.

## 핵심 변경

### 1. Material Imbalance

일반 기물 점수는 그대로 유지하면서, 다음과 같은 기물 구성의 전략적 가치를 CHAOS 평가에 제한적으로 반영합니다.

- 퀸 대 비숍·나이트·나이트 등 3개 마이너피스
- 퀸 대 룩+마이너피스
- 룩 대 비숍+나이트

단순히 퀸을 희생했다는 이유로 보너스를 주지 않습니다. 기본 재료 점수는 기존과 동일하고, 실제 마이너피스 연계가 뒷받침될 때만 작은 추가 보너스를 줍니다. 물질 불균형 보너스는 최대 28cp로 제한했습니다.

### 2. Minor-piece Coordination

마이너피스의 숫자뿐 아니라 다음 요소를 평가합니다.

- 안전한 이동 가능 칸
- 중앙 및 상대 진영 통제
- 서로 보호하는 기물 수
- 둘 이상의 마이너피스가 겹쳐 통제하는 칸
- 공격받고 보호받지 못하는 마이너피스
- 자기 킹 주변의 위험

조정 보너스는 최대 60cp로 제한합니다. 아직 어떤 비폰 기물도 교환되지 않은 초기 만재료 국면(phase 24)에서는 추가 계산을 생략해 초반 평가 비용을 줄입니다.

### 3. Desperation Defense

다음 두 조건을 모두 만족할 때 발동합니다.

- 현재 정적 평가가 -350cp 이하
- 완료된 탐색의 최선 점수가 +100cp 이하

이때 공격적인 스타일 선택이 객관적으로 더 나은 방어 수를 밀어내지 못하도록 합니다. 한 수 안에 가능한 3회 반복이나 스테일메이트가 있으면 이를 우선하고, 없다면 탐색이 선택한 객관적 최선 수를 유지합니다.

진단 출력 예시:

```text
info string v497_desperation_defense 1 reason threefold root_static_cp -917 objective_cp 0
```

`reason`은 `threefold`, `stalemate`, `best_defense` 중 하나입니다. 이 정책은 탐색 점수나 TT 값을 직접 수정하지 않습니다.

### 4. 기존 기능 보존

- v4.9.6 Fighting Spirit
- v4.9.5 King Cache
- v4.9.4 Win Conversion
- 기존 오프닝 북, 시간 관리, UCI 설정
- HCE 평가 점수 및 NNUE 인터페이스

새 평가는 CHAOS 모드에만 추가됩니다. HCE는 기존 회귀 점수를 유지합니다.

## 검증 결과

Linux x86-64 Release 빌드:

- `HebiChess` 빌드: PASS
- UCI `uci` / `isready` / `go depth 2`: PASS
- v4.9.7 전략 테스트: PASS
- 전체 CTest: **10/10 PASS**
- 실제 반복 이력으로 구성한 큰 열세 포지션에서 `b1c3` 3회 반복 무승부 선택: PASS
- `v497_desperation_defense` UCI 진단 출력: PASS
- 기존 HCE 기준 점수 및 v4.8~v4.6 회귀 테스트: PASS

ARM64 서버 빌드와 실전 봇 대국은 아직 검증 전입니다. 배포 스크립트는 ARM64에서 전체 10개 CTest를 실행한 후에만 운영 파일을 교체하도록 구성했습니다.

## 배포 방법

1. ZIP을 Windows에서 압축 해제합니다.
2. 압축 해제한 폴더 안에 `deploy-v497.ps1`, `source-v497.zip`, `remote-deploy-v497.sh`, `start-v497.sh`가 있는지 확인합니다.
3. PowerShell에서 해당 폴더로 이동한 뒤 실행합니다.

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\deploy-v497.ps1
```

배포 스크립트는 다음을 확인합니다.

- 현재 운영 바이너리 SHA-256이 v4.9.6 기준값과 일치
- Lichess 계정이 JJUGLE인지 확인
- 활성 대국 0판 확인 (빌드 전과 교체 직전)
- PM2 `jjugle-lichess` 단일 프로세스 online
- ARM64 빌드, UCI smoke test, 전체 10개 CTest 통과
- 운영 바이너리 백업 후 교체 및 해당 PM2 프로세스만 재시작
- 교체 후 실패 시 기존 바이너리 자동 복구 시도

배포 시작 메시지는 성공을 의미하지 않습니다. 서버 로그에서 아래 세 항목을 모두 확인해야 완료입니다.

```text
V497_DEPLOYED=YES
JJUGLE_PM2=ONLINE
DEPLOY_EXIT=0
```

로그는 PowerShell 마지막 출력의 `DEPLOY_LOG` 경로에서 확인할 수 있습니다. SSH 연결이 끊겨도 배포는 서버 백그라운드에서 진행됩니다.

## 운영 기준

- 기준 운영 버전: v4.9.6
- 기준 바이너리 SHA-256: `02eb92afed1178ac796cfa6b13104d8b8d79e8b1e3ee08226765c71584feebea`
- 기준 엔진 경로: `/home/ubuntu/jjugle-lichess/engine/JJUGLE`
- v4.9.6 복구 백업: `/home/ubuntu/jjugle-lichess/deploy-backups/JJUGLE-before-v496-20261110-143258`

운영 설정, 토큰, 오프닝 북, 자동 매칭, 다른 PM2 서비스는 변경하지 않습니다.
