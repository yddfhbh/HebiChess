# HebiChess Web

로컬에서 여러 플레이어가 각자 HebiChess와 대국할 수 있는 독립 서비스입니다.

```sh
cd web
cp .env.example .env  # 필요할 때 값 조정
npm start
```

기본 주소는 `http://127.0.0.1:3400`이며, 현재는 Nginx/DNS/SSL을 변경하지 않습니다. Node 내장 HTTP 서버와 game별 Server-Sent Events(`/events?gameId=...`)를 사용해 `state`, `move`, `engineThinking`, `engineInfo`, `gameOver` 이벤트를 전달합니다. 브라우저는 cookie UUID를 session으로 사용하고, `gameId`별 `revision`을 함께 제출합니다. 서버는 `Map<gameId, Game>`의 authoritative board에서 player/engine move를 모두 검증합니다.

실제 수 계산은 `/public/engine/hebichess.js`와 `/public/engine/hebichess.wasm`을 로드한 browser Web Worker가 수행합니다. 서버는 native HebiChess process를 실행하지 않습니다. engine move는 브라우저가 제출하므로 서버는 합법성, owner session, gameId/revision만 신뢰 경계로 검증하며 bestmove가 특정 엔진 실행 결과였음을 암호학적으로 증명하지는 않습니다. 관전자는 `/?game=<gameId>`로 특정 게임을 볼 수 있지만 수 제출/기권은 할 수 없습니다. 완료 기록은 기존 `data/games.json` 배열 schema를 유지합니다.

WASM artifact는 현재 Phase 1의 Emscripten 산출물을 `web/public/engine/`에 배치하는 방식입니다. production 배포 전에는 해당 artifact가 배포 대상 C++ SHA에서 재빌드되었는지 확인하고, Agent-Hebi fixed deploy operation이 Emscripten build와 artifact 복사를 지원하는지 별도로 확인해야 합니다. 이 단계에서는 production deploy를 수행하지 않습니다.

```sh
npm test
```
