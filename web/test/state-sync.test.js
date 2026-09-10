const test = require('node:test');
const assert = require('node:assert/strict');
const {shouldApplyServerState} = require('../public/state-sync.js');

const snapshot = (gameId, revision, extra = {}) => ({gameId, revision, active:true, ...extra});

function flowHarness() {
  let state = {active:false}, renders = 0, searches = 0, cancels = 0, started = new Set(), activeKey = null;
  const controller = require('../public/state-sync.js').createController({
    getState:() => state,
    setState:next => { state = next },
    render:() => { renders++ },
    reconcile:() => {
      const key = state.engineTurn ? `${state.gameId}:${state.revision}` : null;
      if (key && !started.has(key)) { started.add(key); searches++ }
      if (!key && activeKey) { cancels++; activeKey = null }
      if (key) activeKey = key;
    }
  });
  return {controller, get state(){return state}, get renders(){return renders}, get searches(){return searches}, get cancels(){return cancels}};
}

test('move response alone starts one engine search and equal SSE reuses it', () => {
  const flow = flowHarness();
  const start = snapshot('game-flow', 0, {engineTurn:false});
  const move = snapshot('game-flow', 1, {engineTurn:true});
  assert.equal(flow.controller.apply(start, 'start-response'), true);
  assert.equal(flow.controller.apply(move, 'move-response'), true);
  assert.equal(flow.searches, 1);
  assert.equal(flow.controller.apply({...move}, 'sse:engineThinking'), true);
  assert.equal(flow.searches, 1);
  assert.equal(flow.renders, 3);
});

test('black start response alone starts the first engine search', () => {
  const flow = flowHarness();
  assert.equal(flow.controller.apply(snapshot('black-game', 0, {engineTurn:true}), 'start-response'), true);
  assert.equal(flow.searches, 1);
});

test('resign response renders terminal state immediately and late active push is ignored', () => {
  const flow = flowHarness();
  const active = snapshot('resign-game', 3, {engineTurn:true});
  const terminal = snapshot('resign-game', 4, {active:false, result:'0-1', termination:'resignation', engineTurn:false});
  assert.equal(flow.controller.apply(active), true);
  assert.equal(flow.controller.apply(terminal, 'resign-response'), true);
  assert.equal(flow.state.result, '0-1');
  assert.equal(flow.cancels, 1);
  assert.equal(flow.controller.apply({...active}, 'sse:late'), false);
  assert.equal(flow.state.result, '0-1');
});

test('ignored old push is a normal no-op and does not throw', () => {
  const flow = flowHarness();
  flow.controller.apply(snapshot('old-push', 5));
  assert.doesNotThrow(() => flow.controller.apply(snapshot('old-push', 4), 'sse:old'));
  assert.equal(flow.state.revision, 5);
});

test('engine bestmove transition clears the thinking reconcile state', () => {
  const flow = flowHarness();
  flow.controller.apply(snapshot('engine-game', 1, {engineTurn:true}));
  flow.controller.apply(snapshot('engine-game', 2, {engineTurn:false, result:null}));
  assert.equal(flow.searches, 1);
  assert.equal(flow.cancels, 1);
  assert.equal(flow.state.engineTurn, false);
});

test('reload engine-turn state starts exactly one search', () => {
  const flow = flowHarness();
  flow.controller.apply(snapshot('reload-game', 8, {engineTurn:true}), 'bootstrap');
  assert.equal(flow.searches, 1);
  flow.controller.apply(snapshot('reload-game', 8, {engineTurn:true}), 'sse:state');
  assert.equal(flow.searches, 1);
});

test('start response revision is the revision used by the next player mutation', () => {
  const started = snapshot('game-a', 0);
  assert.equal(shouldApplyServerState({active:false}, started), true);
  assert.equal(shouldApplyServerState(started, snapshot('game-a', 1, {moves:['e2e4']})), true);
});
test('start then resign uses the same canonical game and revision', () => {
  const started = snapshot('game-b', 0);
  assert.equal(shouldApplyServerState(started, {...started, active:false, result:'0-1'}), true);
});
test('higher SSE state wins over a late start response', () => {
  const newer = snapshot('game-c', 1, {moves:['e2e4']});
  assert.equal(shouldApplyServerState(newer, snapshot('game-c', 0)), false);
});
test('move response becomes the source for the next mutation', () => {
  const afterMove = snapshot('game-d', 1, {moves:['e2e4']});
  assert.equal(shouldApplyServerState(snapshot('game-d', 0), afterMove), true);
  assert.equal(shouldApplyServerState(afterMove, snapshot('game-d', 0)), false);
});
test('stale SSE snapshot cannot move the canonical revision backwards', () => {
  assert.equal(shouldApplyServerState(snapshot('game-e', 3), snapshot('game-e', 2)), false);
});
