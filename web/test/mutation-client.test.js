const test = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../public/mutation-client.js');

function captureMutations(state) {
  const requests = [];
  const api = async (path, body) => {
    requests.push({path, body});
    return {ok:true};
  };
  return {requests, mutations:create({api, getState:() => state})};
}

test('production player move path sends the canonical revisioned body', async () => {
  const state = {gameId:'game-123', revision:7};
  const {requests, mutations} = captureMutations(state);

  await mutations.move('e2e4');

  assert.deepEqual(requests, [{
    path:'/api/move',
    body:{gameId:'game-123', revision:7, move:'e2e4'}
  }]);
});

test('production resign path sends gameId and current revision', async () => {
  const state = {gameId:'game-123', revision:7};
  const {requests, mutations} = captureMutations(state);

  await mutations.resign();

  assert.deepEqual(requests, [{
    path:'/api/resign',
    body:{gameId:'game-123', revision:7}
  }]);
});

test('production engine path sends the search snapshot revision', async () => {
  const currentState = {gameId:'game-123', revision:8};
  const searchSnapshot = {gameId:'game-123', revision:7};
  const {requests, mutations} = captureMutations(currentState);

  await mutations.engineMove('e7e5', searchSnapshot);

  assert.deepEqual(requests, [{
    path:'/api/engine-move',
    body:{gameId:'game-123', revision:7, move:'e7e5'}
  }]);
});
