const test = require('node:test');
const assert = require('node:assert/strict');
test('terminal browser snapshots preserve draw reasons without server-side rule calculation', () => {
  const {appendCompletedRecord} = require('../server');
  const target = {startedAt:'2026-01-01T00:00:00.000Z', playerColor:'w', snapshot:{moves:['g1f3'], result:'1/2-1/2', termination:'threefold repetition'}};
  const record = appendCompletedRecord([], {
    startTime:target.startedAt, endTime:'2026-01-01T00:01:00.000Z', playerColor:'w',
    moves:target.snapshot.moves, result:target.snapshot.result, termination:target.snapshot.termination
  });
  assert.deepEqual(record[0], {
    startTime:target.startedAt, endTime:'2026-01-01T00:01:00.000Z', playerColor:'w',
    moves:['g1f3'], result:'1/2-1/2', termination:'threefold repetition'
  });
});

test('draw and checkmate metadata are accepted as browser-owned terminal state', () => {
  const {appendCompletedRecord} = require('../server');
  const completed = appendCompletedRecord({games:[]}, {
    startTime:'2026-01-01T00:00:00.000Z', endTime:'2026-01-01T00:02:00.000Z', playerColor:'b',
    moves:['f2f3','e7e5','g2g4','d8h4'], result:'0-1', termination:'checkmate'
  });
  assert.equal(completed.games[0].termination, 'checkmate');
  assert.equal(completed.games[0].result, '0-1');
});
