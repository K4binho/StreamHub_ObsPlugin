const socket = io({ autoConnect: false });
const messages = document.getElementById('messages');
const connectionStatus = document.getElementById('connection-status');

const PLATFORM_NAMES = { twitch: 'Twitch', kick: 'Kick', youtube: 'YouTube', tiktok: 'TikTok' };
const BADGE_NAMES = {
  broadcaster: 'LIVE', moderator: 'MOD', mod: 'MOD', subscriber: 'SUB', sub: 'SUB', vip: 'VIP',
};
const params = new URLSearchParams(location.search);
let settings = { messageDurationSeconds: 20, channelName: '', highlightMentions: true };
const statuses = new Map();
const seen = new Set();

function normalizedBoolean(value, fallback) {
  if (value == null) return fallback;
  return !['0', 'false', 'off', 'no'].includes(String(value).toLowerCase());
}

async function loadSettings() {
  try {
    const response = await fetch('/api/overlay-config', { cache: 'no-store' });
    if (response.ok) settings = { ...settings, ...(await response.json()) };
  } catch (_error) {
    // Continua com os padrões se a configuração não responder.
  }
  const duration = Number(params.get('duration'));
  if (Number.isFinite(duration) && duration >= 3) settings.messageDurationSeconds = duration;
  if (params.has('channel')) settings.channelName = params.get('channel').replace(/^[@#]+/, '');
  settings.highlightMentions = normalizedBoolean(params.get('mentions'), settings.highlightMentions);
}

function isMentioned(text) {
  if (!settings.highlightMentions) return false;
  const terms = ['k4binho', settings.channelName]
    .filter(Boolean)
    .map((term) => term.toLocaleLowerCase('pt-BR'));
  const haystack = String(text || '').toLocaleLowerCase('pt-BR');
  return terms.some((term) => haystack.includes(`@${term}`) || haystack.includes(term));
}

function badgeElement(badge) {
  const normalized = String(badge || '').toLowerCase();
  const label = BADGE_NAMES[normalized];
  if (!label) return null;
  const element = document.createElement('span');
  element.className = 'badge';
  element.textContent = label;
  element.title = normalized;
  return element;
}

function removeMessage(element) {
  if (!element.isConnected) return;
  element.classList.add('is-leaving');
  element.addEventListener('animationend', () => element.remove(), { once: true });
  setTimeout(() => element.remove(), 400);
}

function renderMessage(msg) {
  const id = `${msg.platform}:${msg.id || msg.seq || `${msg.timestamp}:${msg.user}:${msg.message}`}`;
  if (seen.has(id)) return;
  seen.add(id);

  const platform = String(msg.platform || 'custom').toLowerCase();
  const item = document.createElement('li');
  item.className = 'message';
  item.dataset.platform = platform;
  item.classList.toggle('is-mentioned', isMentioned(msg.message));

  const icon = document.createElement('img');
  icon.className = 'platform-icon';
  icon.src = `/icons/${platform}.svg`;
  icon.alt = PLATFORM_NAMES[platform] || platform;
  icon.width = 20;
  icon.height = 20;

  const identity = document.createElement('span');
  identity.className = 'identity';
  const user = document.createElement('span');
  user.className = 'username';
  user.textContent = `${msg.user || 'Usuário'}:`;
  identity.appendChild(user);
  for (const badge of Array.isArray(msg.badges) ? msg.badges : []) {
    const element = badgeElement(badge);
    if (element) identity.appendChild(element);
  }

  const text = document.createElement('span');
  text.className = 'message-text';
  text.textContent = msg.message || '';
  item.append(icon, identity, text);
  messages.appendChild(item);

  while (messages.children.length > 25) messages.firstElementChild.remove();
  const age = Math.max(0, Date.now() - Number(msg.timestamp || Date.now()));
  const remaining = Math.max(0, settings.messageDurationSeconds * 1000 - age);
  if (remaining === 0) item.remove();
  else setTimeout(() => removeMessage(item), remaining);
}

function renderStatuses() {
  connectionStatus.replaceChildren();
}

function updateStatus(status) {
  statuses.set(status.platform, status);
  renderStatuses();
}

socket.on('chat-history', (history) => history.forEach(renderMessage));
socket.on('chat-message', renderMessage);
socket.on('connection-statuses', (items) => items.forEach(updateStatus));
socket.on('connection-status', updateStatus);
loadSettings().finally(() => {
  if (normalizedBoolean(params.get('preview'), false)) {
    const now = Date.now();
    renderMessage({ platform: 'twitch', id: 'preview-twitch', user: 'k4binho', message: 'Salve, chat! Overlay ao vivo 💜', badges: ['broadcaster'], timestamp: now });
    renderMessage({ platform: 'kick', id: 'preview-kick', user: 'comunidade', message: '@k4binho essa mensagem ficou destacada', badges: ['subscriber'], timestamp: now });
  }
  socket.connect();
});
