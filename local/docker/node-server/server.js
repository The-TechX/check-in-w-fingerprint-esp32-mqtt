const http = require('http');
const path = require('path');
const express = require('express');
const { WebSocketServer, WebSocket } = require('ws');

const MAX_EVENTS = 400;

function nowIso() {
  return new Date().toISOString();
}

function createServerApp() {
  const app = express();
  app.use(express.urlencoded({ extended: false }));
  app.use(express.json());
  app.use('/assets', express.static(path.join(__dirname, 'public')));

  const events = [];
  const pending = new Map();
  const fingerprints = new Map();
  const consoleClients = new Set();
  let deviceSocket = null;

  function socketState() {
    if (!deviceSocket) return 'closed';
    if (deviceSocket.readyState === WebSocket.CONNECTING) return 'connecting';
    if (deviceSocket.readyState === WebSocket.OPEN) return 'open';
    if (deviceSocket.readyState === WebSocket.CLOSING || deviceSocket.readyState === WebSocket.CLOSED) return 'closed';
    return 'error';
  }

  function serializePayload(value) {
    if (value === undefined || value === null) return null;
    return value;
  }

  function updateFingerprintCacheFromMessage(msg) {
    const payload = msg && typeof msg === 'object' ? msg.payload : null;
    const fingerprintId = Number(payload && payload.fingerprintId);
    if (!Number.isFinite(fingerprintId) || fingerprintId <= 0) return;

    if (msg.type === 'response' && msg.event === 'operation_result') {
      const success = Boolean(payload && payload.success);
      const code = (payload && payload.code) || '';
      if (success && code === 'DELETE_OK') {
        fingerprints.delete(fingerprintId);
        return;
      }
      if (success && (code === 'ENROLL_OK' || code === 'CHECKIN_OK' || code === 'CHECKIN_FOUND')) {
        fingerprints.set(fingerprintId, {
          fingerprintId,
          source: code,
          lastSeenAt: nowIso(),
        });
      }
      return;
    }

    if (msg.type === 'event' && (msg.event === 'fingerprint_enrolled' || msg.event === 'fingerprint_match')) {
      fingerprints.set(fingerprintId, {
        fingerprintId,
        source: msg.event,
        lastSeenAt: nowIso(),
      });
    }
  }

  function publishConsole(message) {
    const body = JSON.stringify(message);
    for (const client of consoleClients) {
      if (client.readyState === WebSocket.OPEN) client.send(body);
    }
  }

  function addEvent(direction, type, eventName, payload) {
    const event = {
      id: `${Date.now()}-${Math.random().toString(16).slice(2, 8)}`,
      ts: nowIso(),
      direction,
      type,
      event: eventName || 'unknown',
      payload: serializePayload(payload),
    };
    events.unshift(event);
    if (events.length > MAX_EVENTS) events.pop();
    publishConsole({ kind: 'event', event, counters: { total: events.length } });
    return event;
  }

  function publishSocketState(extra = {}) {
    publishConsole({
      kind: 'socket_state',
      status: socketState(),
      deviceConnected: socketState() === 'open',
      ...extra,
    });
  }

  function sendToDevice(message) {
    if (!deviceSocket || deviceSocket.readyState !== WebSocket.OPEN) return false;
    deviceSocket.send(JSON.stringify(message));
    return true;
  }

  function buildCommand(body = {}) {
    const command = (body.command || '').trim();
    const requestId = (body.requestId || `req-${Date.now()}`).trim();
    const payload = body.payload && typeof body.payload === 'object' ? body.payload : {};
    return { command, requestId, payload };
  }

  function dispatchCommand(commandBody) {
    const { command, requestId, payload } = buildCommand(commandBody);
    if (!command) return { ok: false, status: 400, error: 'command is required' };

    const msg = { type: 'command', command, requestId, timestamp: nowIso(), payload };
    if (!sendToDevice(msg)) return { ok: false, status: 409, error: 'device is not connected' };

    pending.set(requestId, { command, sentAt: Date.now(), waiters: [] });
    addEvent('OUT', 'command', command, msg);
    return { ok: true, status: 200, requestId };
  }

  function awaitCommandResponse(requestId, timeoutMs = 5000) {
    const entry = pending.get(requestId);
    if (!entry) return Promise.reject(new Error('request is not pending'));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('response timeout')), timeoutMs);
      entry.waiters.push((message) => {
        clearTimeout(timer);
        resolve(message);
      });
    });
  }

  app.get('/health', (_req, res) => res.json({ ok: true, socketStatus: socketState(), deviceConnected: socketState() === 'open' }));

  app.get('/api/state', (_req, res) => {
    res.json({
      ok: true,
      socketStatus: socketState(),
      deviceConnected: socketState() === 'open',
      counters: {
        totalEvents: events.length,
        pendingRequests: pending.size,
        knownFingerprints: fingerprints.size,
      },
      recentEvents: events.slice(0, 150),
    });
  });

  app.get('/api/fingerprints', (_req, res) => {
    const rows = Array.from(fingerprints.values()).sort((a, b) => a.fingerprintId - b.fingerprintId);
    res.json({ ok: true, count: rows.length, fingerprints: rows });
  });

  app.post('/api/fingerprints/refresh', async (_req, res) => {
    const result = dispatchCommand({ command: 'list', payload: {} });
    if (!result.ok) return res.status(result.status).json(result);

    try {
      const message = await awaitCommandResponse(result.requestId, 6000);
      const payload = message.payload && typeof message.payload === 'object' ? message.payload : {};
      const ids = Array.isArray(payload.ids) ? payload.ids : [];
      const rows = ids
        .map((id) => Number(id))
        .filter((id) => Number.isFinite(id) && id > 0)
        .map((fingerprintId) => ({ fingerprintId, source: 'list', lastSeenAt: nowIso() }));
      fingerprints.clear();
      rows.forEach((row) => fingerprints.set(row.fingerprintId, row));
      return res.json({
        ok: true,
        count: Number(payload.count ?? rows.length),
        fingerprints: rows,
        truncated: Boolean(payload.truncated),
      });
    } catch (error) {
      addEvent('ERR', 'error', 'fingerprints_refresh_failed', { message: error.message });
      return res.status(504).json({ ok: false, error: error.message });
    }
  });

  app.post('/api/command', (req, res) => {
    const result = dispatchCommand(req.body || {});
    return res.status(result.status).json(result);
  });

  app.post('/api/command/delete', (req, res) => {
    const fingerprintId = Number(req.body?.fingerprintId || 0);
    if (!fingerprintId) return res.status(400).json({ ok: false, error: 'fingerprintId is required' });
    const result = dispatchCommand({
      command: 'delete_fingerprint',
      requestId: req.body?.requestId,
      payload: { fingerprintId },
    });
    return res.status(result.status).json(result);
  });

  app.post('/api/command/wipe', (req, res) => {
    const result = dispatchCommand({
      command: 'wipe_all_fingerprints',
      requestId: req.body?.requestId,
      payload: {},
    });
    return res.status(result.status).json(result);
  });

  // Legacy compatibility endpoints.
  app.post('/send', (req, res) => {
    const result = dispatchCommand({ command: req.body?.command, requestId: req.body?.requestId, payload: {} });
    if (req.accepts('html')) return res.redirect('/');
    return res.status(result.status).json(result);
  });

  app.post('/send-delete', (req, res) => {
    const fingerprintId = Number(req.body?.fingerprintId || 0);
    const result = dispatchCommand({ command: 'delete_fingerprint', requestId: req.body?.requestId, payload: { fingerprintId } });
    if (req.accepts('html')) return res.redirect('/');
    return res.status(result.status).json(result);
  });

  app.post('/send-wipe', (req, res) => {
    const result = dispatchCommand({ command: 'wipe_all_fingerprints', requestId: req.body?.requestId, payload: {} });
    if (req.accepts('html')) return res.redirect('/');
    return res.status(result.status).json(result);
  });

  app.get('/', (_req, res) => res.sendFile(path.join(__dirname, 'public', 'index.html')));
  app.get('/fingerprints', (_req, res) => res.sendFile(path.join(__dirname, 'public', 'fingerprints.html')));

  const server = http.createServer(app);
  const deviceWss = new WebSocketServer({ noServer: true });
  const consoleWss = new WebSocketServer({ noServer: true });

  server.on('upgrade', (request, socket, head) => {
    const parsed = new URL(request.url, 'http://localhost');
    if (parsed.pathname === '/device') {
      deviceWss.handleUpgrade(request, socket, head, (ws) => deviceWss.emit('connection', ws, request));
      return;
    }
    if (parsed.pathname === '/console') {
      consoleWss.handleUpgrade(request, socket, head, (ws) => consoleWss.emit('connection', ws, request));
      return;
    }
    socket.destroy();
  });

  deviceWss.on('connection', (ws) => {
    deviceSocket = ws;
    addEvent('SYS', 'system', 'device_connected', { socket: 'device' });
    publishSocketState();

    ws.on('message', (raw) => {
      let msg;
      try {
        msg = JSON.parse(raw.toString());
      } catch {
        msg = { type: 'invalid_json', raw: raw.toString() };
      }

      if (msg.requestId && pending.has(msg.requestId)) {
        const entry = pending.get(msg.requestId);
        const waiters = entry?.waiters || [];
        waiters.forEach((notify) => notify(msg));
        pending.delete(msg.requestId);
      }
      updateFingerprintCacheFromMessage(msg);
      const direction = msg.type === 'error' ? 'ERR' : 'IN';
      const eventName = msg.event || msg.type || 'device_message';
      addEvent(direction, msg.type || 'event', eventName, msg);
      publishSocketState({ pendingRequests: pending.size });
    });

    ws.on('error', (error) => {
      addEvent('ERR', 'error', 'device_socket_error', { message: error.message });
      publishSocketState();
    });

    ws.on('close', () => {
      if (deviceSocket === ws) deviceSocket = null;
      addEvent('SYS', 'system', 'device_disconnected', { socket: 'device' });
      publishSocketState();
    });
  });

  consoleWss.on('connection', (ws) => {
    consoleClients.add(ws);
    ws.send(JSON.stringify({
      kind: 'bootstrap',
      socketStatus: socketState(),
      counters: { totalEvents: events.length, pendingRequests: pending.size, knownFingerprints: fingerprints.size },
      events: events.slice(0, 200),
    }));
    ws.on('close', () => {
      consoleClients.delete(ws);
    });
  });

  return { app, server, state: { events, pending, fingerprints } };
}

if (require.main === module) {
  const port = Number(process.env.PORT || 8080);
  const { server } = createServerApp();
  server.listen(port, () => console.log(`WebSocket console on http://localhost:${port}`));
}

module.exports = { createServerApp };
