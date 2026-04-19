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

  let outbound;
  while (true) {
    const packet = JSON.parse((await once(consoleWs, 'message')).toString());
    if (packet.kind === 'event' && packet.event.direction === 'OUT') {
      outbound = packet;
      break;
    }
  }
  assert.equal(outbound.kind, 'event');
  assert.equal(outbound.event.direction, 'OUT');

  deviceWs.send(JSON.stringify({ type: 'event', event: 'pong', requestId: 'abc', payload: { online: true } }));

  let inbound;
  while (true) {
    const packet = JSON.parse((await once(consoleWs, 'message')).toString());
    if (packet.kind === 'event' && packet.event.direction === 'IN') {
      inbound = packet;
      break;
    }
  }
  assert.equal(inbound.kind, 'event');
  assert.equal(inbound.event.direction, 'IN');
  assert.equal(inbound.event.event, 'pong');

  consoleWs.close();
  deviceWs.close();
  server.close();
});

test('fingerprints refresh sends list command and uses response ids', async () => {
  const { server } = createServerApp();
  await new Promise((resolve) => server.listen(0, resolve));
  const { port } = server.address();

  const deviceWs = new WebSocket(`ws://127.0.0.1:${port}/device`);
  await once(deviceWs, 'open');

  deviceWs.on('message', (raw) => {
    const msg = JSON.parse(raw.toString());
    if (msg.command === 'list') {
      deviceWs.send(JSON.stringify({
        type: 'response',
        event: 'fingerprints_list',
        requestId: msg.requestId,
        payload: { count: 2, ids: [3, 9], truncated: false },
      }));
    }
  });

  const response = await fetch(`http://127.0.0.1:${port}/api/fingerprints/refresh`, { method: 'POST' });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.count, 2);
  assert.deepEqual(body.fingerprints.map((f) => f.fingerprintId), [3, 9]);

  deviceWs.close();
  server.close();
});
