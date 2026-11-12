# JJUGLE Chaos v4.9.10 — King Threat Triage

**기준 버전:** v4.9.9 Development First (`yddfhbh/HebiChess` main `0e3ad38cccf828c936cf7b198ab2e3fcafefeca4`). 기존 v4.9.9 운영 바이너리의 SHA-256: `a27187b25a29bfca11e499f5ed6ffe4ff6300978362e515db2e251298549c38b`.

## 무엇을 고쳤나

- 실제 5+3 실전 [`0GcfVZbE`](https://lichess.org/game/0GcfVZbE)에서 37...Kh7 이후 **38.Rxa2??** 대신 **38.Qf8**, 38...Qxa2?? 이후 **39.g4??** 대신 **39.Qf8**을 선택하도록 탐색 후 최종 후보 판단을 개선.
- Qf8은 곧바로 체크가 아니지만 상대가 대응하지 않으면 Qg7#를 위협. 따라서 방어 측의 **실제 모든 합법 응수**를 조사해, 대다수가 즉시 메이트를 허용하고 유효한 방어가 5개 이하인 경우에만 '강한 공격 위협'으로 인정.
- 단순 위협만 보고 퀸을 버리지 않도록, 상대가 퀸을 즉시 잡았을 때 정적 교환 평가(SEE)가 250cp 이상 유리하면 후보에서 제외(예: **Qf6? Rxf6**).
- 이미 패배가 큰 상황(객관 점수 -200cp 이하)에서만 작동. 검증된 100cp 이내의 객관 수가 있으면 정상 탐색 검증으로 선택.
- 예외적으로 현재 객관 수가 -350cp 이하이며 **단순 폰을 잡으려다 룩/퀸을 250cp 이상 손해 보는 수**이면, 일반 탐색이 수읽기를 못한 강한 메이트 위협으로 대체하는 실전형 방어 선택을 허용. 강제 메이트 증명은 아니며, 얕은 객관 점수보다 나쁠 수도 있으므로 실전 테스트가 필요.
- 이미 확인된 메이트 / 확정 즉시 무승부 / 이기고 있는 포지션을 변경하지 않으며, v4.9.9의 전개 강화, v4.9.8의 승세 보존, v4.9.7의 불균형 교환·기물 협력·방어를 모두 유지.
- UCI 진단: `info string v4910_mate_threat_candidates ... selected ... reason ...`. 기본 탐색, NNUE/HCE 평가 함수와 TT 점수 자체에는 인위적 가산점 없음.

## 재현 검증 (Linux x86-64 Release)

- 문제 38수 FEN: `2bQ4/2p2p1k/1p2p3/5rpN/8/8/p1PP1PPK/Rq6 w - - 0 38`
- 문제 39수 FEN: `2bQ4/2p2p1k/1p2p3/5rpN/8/8/q1PP1PPK/8 w - - 0 39`
- 고정 Depth 10/11 재현에서 두 위치 모두 `d8f8`, 오류 `a1a2/g2g4` 회피.
- 같은 5+3 시계 조건 x86-64 비교: v4.9.9 38수 `a1a2`, v4.9.10 `d8f8`; 새 버전은 Depth 12, 소요 시간 약 +0.1초(하드웨어·상황에 따라 달라짐).
- 전체 기존 12개 + 신규 1개 = **CTest 13/13 통과**. 다만 **12수 메이트를 엔진이 완전히 수읽기한 것은 아닙니다.** Stockfish 깊은 분석의 결과와, 얕은 엔진이 올바른 공격 후보를 선택했다는 결과를 혼동하지 마세요.
- UCI ID: `JJUGLE Chaos v4.9.10 King Threat Triage`.

## 운영 배포 절차 (Windows PowerShell 5.1)

1. 바깥 ZIP을 `Downloads`에 넣고 압축 풀기 → `jjugle-v4910-work/deploy-v4910.ps1` 실행.
2. 스크립트는 서버의 새 `labs/chaos-v4910-*` 에만 소스를 업로드하고 백그라운드 ARM64 빌드/13개 테스트/UCI 검사 수행.
3. v4.9.9 바이너리 체크섬을 확인하며 `/api/account/playing` 을 **배포 전과 교체 직전 두 번** 조회. 진행 중인 대국이 있거나 검증이 안 되면 엔진을 교체하지 않음.
4. 정상일 때만 기존 실행파일을 백업하고 PM2 ID 36의 JJUGLE 봇만 재시작. 실패하면 기존 바이너리로 롤백 시도. 한국어 인사, 기존 북, 다른 PM2 서비스, 설정은 그대로 둠.
5. 백그라운드 시작 출력만으로 성공이 아님. `verify-v4910.ps1`로 로그 확인. 성공 기준 세 줄:

```
ARM64_FULL_REGRESSION=13/13_PASS
V4910_DEPLOYED=YES
JJUGLE_PM2=ONLINE
DEPLOY_EXIT=0
```

## GitHub

**운영 서버 적용 확인 후에만** `push-v4910-github.ps1` 실행. GitHub main이 정확히 `0e3ad38cccf828c936cf7b198ab2e3fcafefeca4`인지 검사하고 `engines/jjugle-chaos/`만 갱신, 기존 웹 체스·다른 프로젝트 변경 금지. 사용자 PC에 Git 로그인 설정이 필요. Windows PowerShell 5.1을 위해 `.ps1` 파일은 **UTF-8 BOM**으로 제공.
