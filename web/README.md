# HebiChess Web

로컬에서 한 명의 플레이어와 관전자들이 HebiChess를 공유하는 독립 서비스입니다.

```sh
cd web
cp .env.example .env  # 필요할 때 값 조정
npm start
```

기본 주소는 `http://127.0.0.1:3400`이며, 현재는 Nginx/DNS/SSL을 변경하지 않습니다. Node 내장 HTTP 서버와 Server-Sent Events(`/events`)를 사용해 `state`, `move`, `engineThinking`, `engineInfo`, `gameOver` 이벤트를 전달합니다. 브라우저는 cookie UUID를 session으로 사용하고, active game의 `playerSessionId`와 일치하는 session만 이동/포기할 수 있습니다. 다른 session은 spectator입니다.

서버가 매 수를 자체 board에서 legal move로 검증하고 UCI history를 `position startpos moves ...`로 엔진에 전달합니다. 기본 탐색 깊이는 7이며, 게임 기록은 `data/games.json`에 저장됩니다. 연결 grace 설정(`GAME_DISCONNECT_GRACE_MS`, 기본 60초)은 환경 변수로 준비되어 있습니다.

```sh
npm test
```
