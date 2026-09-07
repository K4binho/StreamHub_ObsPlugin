let config = null;
let editingIndex = null; // índice da saída sendo editada no modal (null = criando nova)
let twitchPollTimer = null;
let selectedTwitchGameId = '';
let categorySearchTimer = null;

async function loadConfig() {
  const res = await fetch('/api/config');
  config = await res.json();
  fillChatFields();
  renderOutputs();
  loadTwitchAccount();
}

async function loadTwitchAccount() {
  const status = document.getElementById('twitch-account-status');
  try {
    const res = await fetch('/api/accounts/twitch/status');
    const account = await res.json();
    status.textContent = account.connected ? `Conectada como ${account.name}` : 'Nenhuma conta conectada';
    document.getElementById('twitch-live-fields').classList.toggle('hidden', !account.connected);
    if (account.connected) loadTwitchChannel();
  } catch (_) {
    status.textContent = 'Não foi possível consultar a conta';
  }
}

async function connectTwitch() {
  const button = document.getElementById('twitch-connect-btn');
  const status = document.getElementById('twitch-account-status');
  const help = document.getElementById('twitch-authorize-help');
  clearTimeout(twitchPollTimer);
  button.disabled = true;
  status.textContent = 'Preparando autorização…';
  help.classList.add('hidden');
  try {
    const res = await fetch('/api/accounts/twitch/connect', { method: 'POST' });
    const flow = await res.json();
    if (!res.ok) throw new Error(flow.error || 'Não foi possível iniciar a autorização.');
    status.textContent = 'Aguardando a autorização na Twitch…';
    help.innerHTML = `Se a página não abriu, <a href="${flow.verificationUri}" target="_blank" rel="noopener">abra a Twitch</a> e use o código <strong>${flow.userCode}</strong>.`;
    help.classList.remove('hidden');
    window.open(flow.verificationUri, '_blank', 'noopener');
    pollTwitchAuthorization(flow.flowId, flow.interval);
  } catch (error) {
    status.textContent = error.message;
    button.disabled = false;
  }
}

async function pollTwitchAuthorization(flowId, interval) {
  const status = document.getElementById('twitch-account-status');
  const button = document.getElementById('twitch-connect-btn');
  try {
    const res = await fetch(`/api/accounts/twitch/connect/${encodeURIComponent(flowId)}`);
    const result = await res.json();
    if (!res.ok) throw new Error(result.error || 'A autorização não foi concluída.');
    if (result.state === 'connected') {
      status.textContent = `Conectada como ${result.name}`;
      document.getElementById('twitch-live-fields').classList.remove('hidden');
      document.getElementById('twitch-authorize-help').classList.add('hidden');
      loadTwitchChannel();
      button.disabled = false;
      return;
    }
    twitchPollTimer = setTimeout(() => pollTwitchAuthorization(flowId, result.interval || interval), (result.interval || interval) * 1000);
  } catch (error) {
    status.textContent = error.message;
    button.disabled = false;
  }
}

async function loadTwitchChannel() {
  const response = await fetch('/api/twitch/channel');
  const channel = await response.json();
  if (!response.ok) return;
  document.getElementById('twitch-title').value = channel.title || '';
  document.getElementById('twitch-category').value = channel.gameName || '';
  selectedTwitchGameId = channel.gameId || '';
}

async function searchTwitchCategories() {
  const input = document.getElementById('twitch-category');
  const results = document.getElementById('twitch-category-results');
  const query = input.value.trim();
  selectedTwitchGameId = '';
  if (query.length < 2) { results.classList.add('hidden'); return; }
  const response = await fetch(`/api/twitch/categories?q=${encodeURIComponent(query)}`);
  const payload = await response.json();
  results.replaceChildren();
  for (const category of payload.data || []) {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = category.name;
    button.onclick = () => {
      input.value = category.name;
      selectedTwitchGameId = category.id;
      results.classList.add('hidden');
    };
    results.appendChild(button);
  }
  results.classList.toggle('hidden', !results.children.length);
}

async function updateTwitchChannel() {
  const status = document.getElementById('twitch-live-status');
  status.textContent = 'Atualizando…';
  const response = await fetch('/api/twitch/channel', {
    method: 'PATCH', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ title: document.getElementById('twitch-title').value, gameId: selectedTwitchGameId }),
  });
  const result = await response.json();
  status.textContent = response.ok ? 'Atualizado na Twitch.' : (result.error || 'Não foi possível atualizar.');
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
document.getElementById('twitch-connect-btn').addEventListener('click', connectTwitch);
document.getElementById('twitch-category').addEventListener('input', () => {
  clearTimeout(categorySearchTimer);
  categorySearchTimer = setTimeout(searchTwitchCategories, 250);
});
document.getElementById('twitch-update-btn').addEventListener('click', updateTwitchChannel);

loadConfig();
