const express = require('express');
const mqtt = require('mqtt');

const app = express();
const port = Number(process.env.PORT || 3000);
const mqttUrl = process.env.MQTT_URL || 'mqtt://mosquitto:1883';
const topicPrefix = process.env.TOPIC_PREFIX || 'fingerprint';
const deviceId = process.env.DEVICE_ID || 'esp32-fingerprint-01';

const commandsBase = `${topicPrefix}/devices/${deviceId}/commands`;
const eventsWildcard = `${topicPrefix}/devices/${deviceId}/events/#`;
const statusWildcard = `${topicPrefix}/devices/${deviceId}/status/#`;

const commandTopics = {
  register: `${commandsBase}/register/start`,
  checkin: `${commandsBase}/checkin/once`,
  delete: `${commandsBase}/fingerprint/delete`,
  wipeAll: `${commandsBase}/fingerprint/wipe-all`,
  list: `${commandsBase}/fingerprint/list`
};

const recentEvents = [];
const sseClients = new Set();

function rememberEvent(entry) {
  recentEvents.push(entry);
  while (recentEvents.length > 100) {
    recentEvents.shift();
  }

  const packet = `data: ${JSON.stringify(entry)}\n\n`;
  for (const client of sseClients) {
    client.write(packet);
  }
}

function correlationId() {
  return `web-${Date.now()}-${Math.floor(Math.random() * 1000)}`;
}

const mqttClient = mqtt.connect(mqttUrl, {
  reconnectPeriod: 1000,
  clientId: `node-console-${Math.random().toString(16).slice(2, 10)}`
});

mqttClient.on('connect', () => {
  console.log(`[mqtt] connected ${mqttUrl}`);
  mqttClient.subscribe([eventsWildcard, statusWildcard], (err) => {
    if (err) {
      console.error('[mqtt] subscribe error', err);
      return;
    }
    console.log(`[mqtt] subscribed ${eventsWildcard} + ${statusWildcard}`);
  });
});

mqttClient.on('reconnect', () => console.log('[mqtt] reconnecting...'));
mqttClient.on('error', (err) => console.error('[mqtt] error', err));

mqttClient.on('message', (topic, payload) => {
  let parsed = null;

  try {
    parsed = JSON.parse(payload.toString('utf8'));
  } catch {
    parsed = { raw: payload.toString('utf8') };
  }

  rememberEvent({
    ts: new Date().toISOString(),
    topic,
    payload: parsed
  });
});

app.use(express.urlencoded({ extended: false }));
app.use(express.json());

app.get('/', (_req, res) => {
  const items = recentEvents
    .slice()
    .reverse()
    .map((evt) => `<li><strong>${evt.ts}</strong> <code>${evt.topic}</code><pre>${JSON.stringify(evt.payload, null, 2)}</pre></li>`)
    .join('');

  res.send(`<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>ESP32 MQTT Console</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 1rem auto; max-width: 980px; }
    button { margin-right: .5rem; margin-bottom: .5rem; }
    #events { background:#f6f8fa; border:1px solid #ddd; padding:1rem; border-radius:8px; }
    pre { white-space: pre-wrap; margin:.25rem 0 .75rem; }
    code { color:#0d47a1; }
  </style>
</head>
<body>
  <h1>ESP32 MQTT Console</h1>
  <p><b>Broker:</b> ${mqttUrl} · <b>Device:</b> ${deviceId} · <b>Prefix:</b> ${topicPrefix}</p>

  <h2>Commands</h2>
  <form method="post" action="/api/command/register"><button>register/start</button></form>
  <form method="post" action="/api/command/checkin"><button>checkin/once</button></form>
  <form method="post" action="/api/command/list"><button>fingerprint/list</button></form>

  <form method="post" action="/api/command/delete">
    <label>fingerprintId: <input type="number" name="fingerprintId" min="1" required></label>
    <button>fingerprint/delete</button>
  </form>

  <form method="post" action="/api/command/wipeAll" onsubmit="return confirm('¿Borrar todas las huellas del sensor?');">
    <button>fingerprint/wipe-all</button>
  </form>

  <h2>Live events</h2>
  <div id="events"><ul id="events-list">${items || '<li>Esperando eventos...</li>'}</ul></div>

<script>
  const source = new EventSource('/events/stream');
  const list = document.getElementById('events-list');

  source.onmessage = (message) => {
    const event = JSON.parse(message.data);
    const li = document.createElement('li');
    li.innerHTML = '<strong>' + event.ts + '</strong> <code>' + event.topic + '</code><pre>' + JSON.stringify(event.payload, null, 2) + '</pre>';
    list.prepend(li);
    while (list.children.length > 100) {
      list.removeChild(list.lastChild);
    }
  };
</script>
</body>
</html>`);
});

app.get('/events/stream', (req, res) => {
  res.setHeader('Content-Type', 'text/event-stream');
  res.setHeader('Cache-Control', 'no-cache');
  res.setHeader('Connection', 'keep-alive');
  res.flushHeaders();

  sseClients.add(res);

  req.on('close', () => {
    sseClients.delete(res);
    res.end();
  });
});

app.post('/api/command/:command', (req, res) => {
  const commandName = req.params.command;
  const topic = commandTopics[commandName];

  if (!topic) {
    return res.status(404).send('Unknown command');
  }

  const body = {
    correlationId: correlationId(),
    requestedBy: 'node-console',
    timestamp: new Date().toISOString()
  };

  if (commandName === 'delete') {
    const fingerprintId = Number(req.body.fingerprintId || req.body?.fingerprintId || 0);
    if (!Number.isInteger(fingerprintId) || fingerprintId <= 0) {
      return res.status(400).send('fingerprintId inválido');
    }
    body.fingerprintId = fingerprintId;
  }

  mqttClient.publish(topic, JSON.stringify(body), { qos: 1 }, (err) => {
    if (err) {
      return res.status(500).send(`Publish failed: ${err.message}`);
    }

    res.redirect('/');
  });

  return undefined;
});

app.listen(port, () => {
  console.log(`[http] server on http://localhost:${port}`);
});
