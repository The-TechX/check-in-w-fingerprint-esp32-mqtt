const logEl = document.getElementById('log');
const socketEl = document.getElementById('socket-indicator');
const counterEl = document.getElementById('event-counter');
const clearBtn = document.getElementById('clear-log');
const deleteForm = document.getElementById('delete-form');
const deleteInput = document.getElementById('fingerprint-id');

function lineClass(direction) {
  if (direction === 'OUT') return 'out';
  if (direction === 'IN') return 'in';
  if (direction === 'ERR') return 'err';
  return 'sys';
}

function addLine(entry) {
  const line = document.createElement('span');
  line.className = `line ${lineClass(entry.direction)}`;
  const payload = entry.payload ? ` ${JSON.stringify(entry.payload)}` : '';
  line.innerHTML = `<span class="time">${entry.ts}</span><span class="dir">${entry.direction}</span><span class="event">${entry.event}</span>${payload}`;
  logEl.appendChild(line);
  logEl.scrollTop = logEl.scrollHeight;
}

function setStatus(status) {
  socketEl.textContent = status;
  socketEl.className = `badge ${status}`;
}

function setCounter(count) {
  counterEl.textContent = `${count} events`;
}

async function postJson(url, body) {
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const json = await res.json();
  if (!res.ok) throw new Error(json.error || `HTTP ${res.status}`);
  return json;
}

async function runCommand(command, payload = {}) {
  try {
    await postJson('/api/command', { command, payload });
  } catch (error) {
    addLine({ ts: new Date().toISOString(), direction: 'ERR', event: 'ui_command_failed', payload: { command, error: error.message } });
  }
}

Array.from(document.querySelectorAll('[data-command]')).forEach((button) => {
  button.addEventListener('click', () => runCommand(button.dataset.command, {}));
});

deleteForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const id = deleteInput.value.trim();
  if (!id) {
    addLine({ ts: new Date().toISOString(), direction: 'ERR', event: 'validation_error', payload: { message: 'fingerprintId is required' } });
    return;
  }
  try {
    await postJson('/api/command/delete', { fingerprintId: Number(id) });
    deleteInput.value = '';
  } catch (error) {
    addLine({ ts: new Date().toISOString(), direction: 'ERR', event: 'ui_delete_failed', payload: { error: error.message } });
  }
});

clearBtn.addEventListener('click', () => {
  logEl.textContent = '';
  addLine({ ts: new Date().toISOString(), direction: 'SYS', event: 'console_cleared' });
});

function connectConsoleSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  const ws = new WebSocket(`${protocol}://${window.location.host}/console`);
  setStatus('connecting');

  ws.addEventListener('open', () => setStatus('open'));
  ws.addEventListener('close', () => setStatus('closed'));
  ws.addEventListener('error', () => setStatus('error'));

  ws.addEventListener('message', (event) => {
    const data = JSON.parse(event.data);
    if (data.kind === 'bootstrap') {
      setStatus(data.socketStatus || 'closed');
      setCounter(data.counters?.totalEvents || 0);
      (data.events || []).slice().reverse().forEach(addLine);
      return;
    }
    if (data.kind === 'socket_state' && data.status) {
      setStatus(data.status);
      return;
    }
    if (data.kind === 'event' && data.event) {
      addLine(data.event);
      setCounter(data.counters?.total ?? Number(counterEl.textContent.replace(/\D/g, '')) + 1);
    }
  });
}

connectConsoleSocket();
