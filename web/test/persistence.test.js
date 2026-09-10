const test = require('node:test');
const assert = require('node:assert/strict');
const {appendCompletedRecord} = require('../server.js');

const record = {startTime:'2026-09-10T00:00:00.000Z', endTime:'2026-09-10T00:01:00.000Z', playerColor:'w', moves:['e2e4'], result:'0-1', termination:'resignation'};

test('completed-game persistence preserves the production array schema', () => {
  const existing = [{...record, moves:[]}];
  assert.deepEqual(appendCompletedRecord(existing, record), [...existing, record]);
});

test('completed-game persistence appends to temporary object schemas without calling push on the object', () => {
  const existing = {games:[{...record, moves:[]}]};
  const next = appendCompletedRecord(existing, record);
  assert.equal(next.games.length, 2);
  assert.deepEqual(next.games[1], record);
  assert.deepEqual(existing.games.length, 1);
});

test('completed-game persistence retains an existing single record', () => {
  const existing = {...record, result:'1-0'};
  const next = appendCompletedRecord(existing, record);
  assert.deepEqual(next, [existing, record]);
});
