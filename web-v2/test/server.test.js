import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from '../server.js';

test('v2 serves the lobby and uses no-cache for app files', async () => {
  const server = createServer();
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address();
  try {
    const page = await fetch(`http://127.0.0.1:${address.port}/`);
    assert.equal(page.status, 200);
    assert.match(await page.text(), /JJUGLE/);
    const app = await fetch(`http://127.0.0.1:${address.port}/app.js`);
    assert.equal(app.headers.get('cache-control'), 'no-store');
    assert.equal((await fetch(`http://127.0.0.1:${address.port}/../server.js`)).status, 404);
  } finally { await new Promise(resolve => server.close(resolve)); }
});
