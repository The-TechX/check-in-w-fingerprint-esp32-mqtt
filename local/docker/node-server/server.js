const http = require('http');
const express = require('express');
const { WebSocketServer } = require('ws');

const app = express();
app.use(express.urlencoded({ extended: false }));
app.use(express.json());

const port = Number(process.env.PORT || 8080);
const events = [];
const pending = new Map();
let deviceSocket = null;

function nowIso() { return new Date().toISOString(); }
function addEvent(direction, data) {
  events.unshift({ ts: nowIso(), direction, data });
  if (events.length > 50) events.pop();
}

function sendToDevice(message) {
  if (!deviceSocket || deviceSocket.readyState !== 1) return false;
  deviceSocket.send(JSON.stringify(message));
  return true;
}

app.get('/health', (_req, res) => res.json({ ok: true, deviceConnected: !!deviceSocket }));

app.post('/api/command', (req, res) => {
  const command = (req.body.command || '').trim();
  const requestId = (req.body.requestId || `req-${Date.now()}`).trim();
  const payload = req.body.payload || {};
  if (!command) return res.status(400).json({ ok: false, error: 'command is required' });

  const msg = { type: 'command', command, requestId, timestamp: nowIso(), payload };
  if (!sendToDevice(msg)) return res.status(409).json({ ok: false, error: 'device is not connected' });

  pending.set(requestId, { command, sentAt: Date.now() });
  addEvent('server->device', msg);
  return res.json({ ok: true, requestId });
});

app.get('/', (_req, res) => {
  const rows = events.map((e) => `<li><strong>${e.ts}</strong> <em>${e.direction}</em><pre>${JSON.stringify(e.data, null, 2)}</pre></li>`).join('');
  res.send(`<!doctype html><html><body><h1>WebSocket test console</h1>
  <p>Device connected: <b>${deviceSocket ? 'yes' : 'no'}</b></p>
  <form method="post" action="/send"><label>Command</label><input name="command" value="ping" />
  <label>Request ID</label><input name="requestId" value="req-${Date.now()}" /><button type="submit">Send</button></form>
  <p>Quick actions:</p>
  <form method="post" action="/send"><input type="hidden" name="command" value="enroll_fingerprint"/><button>Enroll</button></form>
  <form method="post" action="/send"><input type="hidden" name="command" value="identify_fingerprint"/><button>Identify</button></form>
  <form method="post" action="/send"><input type="hidden" name="command" value="healthcheck"/><button>Healthcheck</button></form>
  <h2>Events</h2><ul>${rows}</ul></body></html>`);
});

app.post('/send', (req, res) => {
  req.body.payload = {};
  req.url = '/api/command';
  app._router.handle(req, res);
});

const server = http.createServer(app);
const wss = new WebSocketServer({ server, path: '/device' });

wss.on('connection', (ws) => {
  deviceSocket = ws;
  addEvent('system', { event: 'device_connected' });
  ws.on('message', (raw) => {
    let msg;
    try { msg = JSON.parse(raw.toString()); }
    catch { msg = { type: 'invalid_json', raw: raw.toString() }; }
    if (msg.requestId && pending.has(msg.requestId)) pending.delete(msg.requestId);
    addEvent('device->server', msg);
  });
  ws.on('close', () => {
    if (deviceSocket === ws) deviceSocket = null;
    addEvent('system', { event: 'device_disconnected' });
  });
});

server.listen(port, () => console.log(`WebSocket console on http://localhost:${port}`));
