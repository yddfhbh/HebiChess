# JJUGLE Chaos v4.9.15 — Attack Formation

기준 운영 엔진: v4.9.14 Verified Choice, ARM64 SHA256 `39d2fe3f371f18aad1b8c9f0fe62338e2ec084a11a5345f51759cd4a279492dd`.
기존 오프닝 북·가중치·설정·할로윈 갬빗 4.Nxe5는 그대로 유지합니다.

## 수정

1. 약한 퀸 공격의 안전검증: 초반에 객관적 최선 수와 다른 '상한 평가' 퀸 조용한 수가 별다른 킹 압박·체크·캡처를 만들지 않는다면 우선 객관적 최선을 유지합니다. 실전 `DE9Q4KMj`의 12...Qe5?? 대응. 객관적 최선 자체의 퀸 수는 바꾸지 않습니다.
2. 검증된 퀸 공격 템포: 상대 퀸이 이전에 공격받지 않았지만, 합법적인 폰 전진으로 직접 공격받게 된다면 다른 최종 수 대신 35cp 손실 이내인지 추가 탐색으로 확인합니다. 시간 한도와 근거 없는 희생을 방치하지 않습니다. 실전 `6CGQ7z5B`의 35.g4 대응. 강제 오프닝이나 특정 FEN 하드코딩이 아닙니다.
3. 희생을 위한 공격 대형: 룩/퀸과 비숍/나이트가 함께 상대 왕 주변을 실질적으로 압박하도록 준비하는 조용한 수에 최대 13점의 작은 스타일 보너스. 전술적 근거 없는 희생을 강요하지 않으며 기존 점수 하한 검증을 그대로 적용합니다.
4. 진단: `info string v4915_attack_formation`에 퀸 우회 선택 거부, 추가 검증 결과, 공격 대형 보너스가 출력됩니다. 이전 `tm`, `qprofile`, `v4914_decision` 유지.

## x86-64 로컬 테스트

- 전체 CTest 18/18 통과. 신규 `chaos_v4915_attack_formation`은 12...Qe5/35.Bd4 회귀와 11월 9일 31...Nxf3·33...Rh2+ 희생 유지를 다룹니다.
- 동일 포지션 `go movetime 1400`, LTO 빌드 비교: v4.9.14 → v4.9.15
  - DE9Q4KMj 12...: Qe5 → Nxe5
  - 6CGQ7z5B 35.: Bd4 → g4
  - ZHbRP3f8 31...: Nxf3 → Nxf3
  - ZHbRP3f8 33...: Rh2+ → Rh2+
- 성능 및 선택 결과는 환경·실행 시간에 따라 달라질 수 있고, 실전 승률/탁월한 수 증가가 증명된 것은 아닙니다.
- 운영 ARM64 배포 전 x86-64 테스트만 완료된 상태입니다. 설치 스크립트는 ARM64 18/18 PASS 전까지 운영 엔진을 바꾸지 않습니다.

## 내용

- `source-v4915.zip`: C++ 전체 소스, 18개 회귀 테스트, 검색 비교 스크립트
- `deploy-v4915.ps1`: Windows PowerShell 5.1로 SSH/SCP 업로드 후 서버에서 분리된 배포 작업 실행
- `remote-deploy-v4915.sh`: 기준 SHA 검증→진행 중 대국 없음 확인→ARM64 컴파일과 18개 테스트→대국 없음 재확인→백업/원자적 교체→PM2 재시작/UCI 확인. 실패 시 롤백
- `verify-v4915.ps1`: 조회 전용 배포 검증
- `profile-v4915.ps1`: 활성 게임이 없을 때 별도 프로세스로 운영 바이너리와 백업 v4.9.14 비교 (운영 PM2 재시작 없음)
- `push-v4915-github.ps1`: 별도 실행, GitHub main의 엔진 버전을 확인하고 해당 폴더만 업데이트

## 사용

아카이브를 Windows Downloads에 저장해 압축을 푼 후 다음을 실행합니다.

```powershell
$work = "$env:USERPROFILE\HebiChessLocal\deploy\v4915"
Expand-Archive "$env:USERPROFILE\Downloads\JJUGLE-Chaos-v4.9.15-attack-formation.zip" -DestinationPath $work -Force
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$work\jjugle-v4915-work\deploy-v4915.ps1"
```

배포 작업은 백그라운드 시작만 확인하고 종료됩니다. 성공 완료는 로그에서 `ARM64_FULL_REGRESSION=18/18_PASS`, `V4915_DEPLOYED=YES`, `JJUGLE_PM2=ONLINE`, `DEPLOY_EXIT=0`, live SHA=새 후보 SHA를 모두 확인해야 합니다.

운영 서버가 이미 v4.9.15이거나 다른 바이너리인 경우 **변경하지 않고 중단**합니다. GitHub push는 자동 실행되지 않습니다.


### 검증 테스트 보정판 (r2, 엔진 코드 동일)
- 버전은 v4.9.15 그대로이며, 실제 엔진 C++ 소스는 기존 패키지와 동일합니다.
- `tests/v4915_attack_formation_test.cpp`: 각 사례에서 `ucinewgame` 호출로 전역 탐색 테이블을 초기화합니다.
- `35.g4` 재현 테스트: `go depth 9 movetime 10000`으로 충분한 깊이에서 재현하며, `b6d4` 금지 및 `g2g4` 필수 조건을 그대로 유지합니다.
- ARM64 회귀 테스트가 모두 통과하고 활성 대국이 0개일 때만 운영 바이너리 교체를 진행합니다.
- 로컬 x86-64 전체 18/18 통과. ARM64는 배포 과정에서 별도 확인 필요.
