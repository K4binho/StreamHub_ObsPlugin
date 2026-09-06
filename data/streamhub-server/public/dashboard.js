let config = null;
let editingIndex = null; // índice da saída sendo editada no modal (null = criando nova)

async function loadConfig() {
  const res = await fetch('/api/config');
  config = await res.json();
  fillChatFields();
  renderOutputs();
}

function fillChatFields() {
  const t = config.twitch || {};
  const y = config.youtube || {};
  const k = config.kick || {};
  const tk = config.tiktok || {};
  const r = config.rtmp || {};

  document.getElementById('twitch-enabled').checked = !!t.enabled;
  document.getElementById('twitch-channel').value = t.channel || '';

  document.getElementById('youtube-enabled').checked = !!y.enabled;
  document.getElementById('youtube-apikey').value = y.apiKey || '';
  document.getElementById('youtube-videoid').value = y.videoId || '';

  document.getElementById('kick-enabled').checked = !!k.enabled;
  document.getElementById('kick-channel').value = k.channel || '';
  document.getElementById('kick-chatroomid').value = k.chatroomId || '';

  document.getElementById('tiktok-enabled').checked = !!tk.enabled;
  document.getElementById('tiktok-username').value = tk.username || '';

  document.getElementById('rtmp-port').value = r.port || 1935;
  document.getElementById('rtmp-streamkey').value = r.streamKey || 'obs';
  document.getElementById('rtmp-port-hint').textContent = r.port || 1935;
  document.getElementById('rtmp-key-hint').textContent = r.streamKey || 'obs';
}

function renderOutputs() {
  const list = document.getElementById('outputs-list');
  list.innerHTML = '';

  const destinations = config.rtmp?.destinations || [];

  destinations.forEach((dest, i) => {
    const row = document.createElement('div');
    row.className = 'output-row';

    const configured = dest.key && !dest.key.startsWith('SUA_STREAM_KEY');

    row.innerHTML = `
      <span class="name">${dest.name}</span>
      <span class="status ${dest.enabled ? '' : 'off'}">${dest.enabled ? (configured ? 'configurada' : 'sem chave') : 'desativada'}</span>
      <button class="edit">⚙ Configurações</button>
      <button class="remove">— Remover</button>
    `;

    row.querySelector('.edit').onclick = () => openModal(i);
    row.querySelector('.remove').onclick = () => {
      destinations.splice(i, 1);
      renderOutputs();
    };

    list.appendChild(row);
  });
}

function openModal(index) {
  editingIndex = index;
  const modal = document.getElementById('output-modal');
  const isNew = index === null;

  document.getElementById('modal-title').textContent = isNew ? 'Adicionar Saída' : 'Editar Saída';

  const dest = isNew
    ? { name: '', url: '', key: '', enabled: true }
    : config.rtmp.destinations[index];

  document.getElementById('modal-name').value = dest.name;
  document.getElementById('modal-url').value = dest.url;
  document.getElementById('modal-key').value = dest.key.startsWith('SUA_STREAM_KEY') ? '' : dest.key;
  document.getElementById('modal-enabled').checked = dest.enabled;

  modal.classList.remove('hidden');
}

function closeModal() {
  document.getElementById('output-modal').classList.add('hidden');
  editingIndex = null;
}

function saveModal() {
  if (!config.rtmp.destinations) config.rtmp.destinations = [];

  const dest = {
    name: document.getElementById('modal-name').value.trim() || 'Nova Saída',
    url: document.getElementById('modal-url').value.trim(),
    key: document.getElementById('modal-key').value.trim() || 'SUA_STREAM_KEY',
    enabled: document.getElementById('modal-enabled').checked,
  };

  if (editingIndex === null) {
    config.rtmp.destinations.push(dest);
  } else {
    config.rtmp.destinations[editingIndex] = dest;
  }

  closeModal();
  renderOutputs();
}

function collectChatFields() {
  config.twitch = {
    enabled: document.getElementById('twitch-enabled').checked,
    channel: document.getElementById('twitch-channel').value.trim(),
  };
  config.youtube = {
    enabled: document.getElementById('youtube-enabled').checked,
    apiKey: document.getElementById('youtube-apikey').value.trim(),
    videoId: document.getElementById('youtube-videoid').value.trim(),
  };
  config.kick = {
    enabled: document.getElementById('kick-enabled').checked,
    channel: document.getElementById('kick-channel').value.trim(),
    chatroomId: document.getElementById('kick-chatroomid').value.trim() || undefined,
  };
  config.tiktok = {
    enabled: document.getElementById('tiktok-enabled').checked,
    username: document.getElementById('tiktok-username').value.trim(),
  };
  config.rtmp = config.rtmp || {};
  config.rtmp.port = Number(document.getElementById('rtmp-port').value) || 1935;
  config.rtmp.streamKey = document.getElementById('rtmp-streamkey').value.trim() || 'obs';
}

async function saveAll() {
  collectChatFields();

  const res = await fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(config),
  });

  const status = document.getElementById('save-status');
  if (res.ok) {
    status.textContent = 'Salvo! Reinicie o servidor (npm start) para aplicar.';
  } else {
    status.textContent = 'Erro ao salvar.';
  }
  setTimeout(() => (status.textContent = ''), 5000);
}

// Navegação entre abas
document.querySelectorAll('.nav-item').forEach((btn) => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.nav-item').forEach((b) => b.classList.remove('active'));
    document.querySelectorAll('.tab').forEach((t) => t.classList.add('hidden'));
    btn.classList.add('active');
    document.getElementById(`tab-${btn.dataset.tab}`).classList.remove('hidden');
  });
});

document.getElementById('add-output-btn').addEventListener('click', () => openModal(null));
document.getElementById('modal-close').addEventListener('click', closeModal);
document.getElementById('modal-save').addEventListener('click', saveModal);
document.getElementById('save-btn').addEventListener('click', saveAll);

loadConfig();
