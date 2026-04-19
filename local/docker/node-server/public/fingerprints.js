const table = document.getElementById('fingerprints-table');
const tbody = table.querySelector('tbody');
const emptyState = document.getElementById('empty-state');
const refreshBtn = document.getElementById('refresh-table');

function renderRows(rows) {
  tbody.innerHTML = '';
  if (!rows.length) {
    table.classList.add('hidden');
    emptyState.classList.remove('hidden');
    return;
  }

  rows.forEach((row) => {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${row.fingerprintId}</td><td>${row.source || '-'}</td><td>${row.lastSeenAt || '-'}</td>`;
    tbody.appendChild(tr);
  });
  emptyState.classList.add('hidden');
  table.classList.remove('hidden');
}

async function loadFingerprints() {
  const res = await fetch('/api/fingerprints/refresh', { method: 'POST' });
  if (!res.ok) {
    const fallback = await fetch('/api/fingerprints');
    const fallbackData = await fallback.json();
    renderRows(fallbackData.fingerprints || []);
    return;
  }
  const data = await res.json();
  renderRows(data.fingerprints || []);
}

refreshBtn.addEventListener('click', loadFingerprints);
loadFingerprints();
