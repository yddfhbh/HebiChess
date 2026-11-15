# JJUGLE Chaos v4.9.14 — Verified Choice / Time Budget

기준: GitHub HebiChess `main` d098464cbfc1c67266bc8d5bd3d46d4830818cf0, 운영 ARM64 v4.9.13 SHA256 `d10157abc91f259a4dd5f0847b2455640fbd05904d6ed4a27025b5b5913e2567`.

## 수정 내용

1. 오프닝 북 **원본 파일·가중치·설정은 그대로 유지**. 단, 첫 수 `1.h3` 또는 `1.a3` 이후 흑의 첫 응수 `...h5`만 배제. 같은 포지션에 다른 북 후보가 있으면 기존 가중치대로 선택하고, `...h5`밖에 없다면 엔진 검색으로 전환. 그 외 수순과 할로윈 갬빗의 의도된 `4.Nxe5`는 유지. Witty_Alien의 정확한 북 출처는 검증하지 않았음.
2. 실전 **시간 제한 탐색**의 초반 단계(16수까지): CHAOS가 객관적 최선 대신 다른 수를 고르면 45cp 이내라는 더 엄격한 하한 검증. 얕고 평가가 흔들리는 열세에서는 상한 점수에만 근거한 불확실한 변칙 수를 포기함. 객관적 최선 희생·체크, 퀸 보존, 불리할 때 무승부 확보, 이미 검증된 왕 공격 대응은 유지. 고정 깊이 분석 동작은 기존대로 둠.
3. 초반 시간 관리: **3+2 / 180초 / 기본 ThinkTimePercent=75**에서 최대 생각 시간 계산치 약 9.05초 → **4.53초**. 중반·저시간 설정은 그대로. 네트워크·UCI 통신 오버헤드 등 실제 시간과 차이 가능.
4. `info string v4914_decision`: 객관적 최선 착수, 실제 착수, 점수, 선택한 수의 경계값, 추가 검증 결과. 기존 `tm`·`qprofile`와 함께 `scripts/profile_v4914.py`로 라이브 엔진을 건드리지 않고 v4.9.13/14의 고정 시간·시계 기반 비교 및 CSV 출력.

## 포함 파일

- `source-v4914.zip`: 전체 소스, 17개 회귀 테스트, `scripts/profile_v4914.py`
- `deploy-v4914.ps1`: Windows PowerShell SSH/SCP 업로드 및 백그라운드 배포 시작
- `remote-deploy-v4914.sh`: 바이너리 SHA 검증 → 진행 중 대국 0 확인 → ARM64 빌드·17개 테스트 → 대국 0 재확인 → 백업·원자적 교체·PM2 재시작·UCI 확인, 실패 시 자동 롤백
- `verify-v4914.ps1`: 배포 로그와 운영 바이너리 SHA 조회
- `profile-v4914.ps1`: 배포 이후 대국이 없을 때만 백업 v4.9.13 대비 별도 UCI 프로파일링, 봇 재시작 안 함
- `push-v4914-github.ps1`: GitHub HEAD 확인 후 엔진 폴더만 커밋·푸시 (별도 수동 실행)

## 실행

PowerShell에서:

```
$zip = "$env:USERPROFILE\Downloads\JJUGLE-Chaos-v4.9.14-verified-choice.zip"
$dest = "$env:USERPROFILE\HebiChessLocal\deploy\v4914"
Expand-Archive -LiteralPath $zip -DestinationPath $dest -Force
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$dest\jjugle-v4914-work\deploy-v4914.ps1"
```

배포 성공 기준: `ARM64_FULL_REGRESSION=17/17_PASS`, `V4914_DEPLOYED=YES`, `JJUGLE_PM2=ONLINE`, `DEPLOY_EXIT=0`. GitHub 푸시는 별도 단계.

### 검증 범위와 한계

동일 포지션 로컬 x86-64 Release에서 기존 `4...Qd4`, `9.Bb5+`, `10.exd6` 대신 `4...Qe6+`, `9.O-O`, `10.O-O` 선택 확인. **ARM64 운영 서버의 NPS, 전반적 승률, 다른 기보 모든 실수까지 해결됐다는 증거는 아님**. ARM64 17/17 테스트 실패 시 운영 엔진은 변경하지 않음.
