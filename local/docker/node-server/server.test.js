const test = require('node:test');
const assert = require('node:assert/strict');
const { WebSocket } = require('ws');
const { createServerApp } = require('./server');

function once(emitter, event) {
  return new Promise((resolve) => emitter.once(event, resolve));
}

test('api command fails if device is disconnected', async () => {
  const { server } = createServerApp();
  await new Promise((resolve) => server.listen(0, resolve));
  const { port } = server.address();

  const response = await fetch(`http://127.0.0.1:${port}/api/command`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ command: 'enroll_fingerprint', payload: {} }),
  });

  assert.equal(response.status, 409);
  const body = await response.json();
  assert.equal(body.ok, false);

  server.close();
});

test('console websocket receives OUT and IN events', async () => {
  const { server } = createServerApp();
  await new Promise((resolve) => server.listen(0, resolve));
  const { port } = server.address();

  const consoleWs = new WebSocket(`ws://127.0.0.1:${port}/console`);
  const bootstrapRaw = await once(consoleWs, 'message');
  const bootstrap = JSON.parse(bootstrapRaw.toString());
  assert.equal(bootstrap.kind, 'bootstrap');

  const deviceWs = new WebSocket(`ws://127.0.0.1:${port}/device`);
  await once(deviceWs, 'open');

  const cmdResponse = await fetch(`http://127.0.0.1:${port}/api/command`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ command: 'healthcheck', payload: {} }),
  });
  assert.equal(cmdResponse.status, 200);

  const outbound = JSON.parse((await once(consoleWs, 'message')).toString());
  assert.equal(outbound.kind, 'event');
  assert.equal(outbound.event.direction, 'OUT');

  deviceWs.send(JSON.stringify({ type: 'event', event: 'pong', requestId: 'abc', payload: { online: true } }));

  const inbound = JSON.parse((await once(consoleWs, 'message')).toString());
  assert.equal(inbound.kind, 'event');
  assert.equal(inbound.event.direction, 'IN');
  assert.equal(inbound.event.event, 'pong');

  consoleWs.close();
  deviceWs.close();
  server.close();
});
