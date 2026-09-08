const path = require('path');
const fs = require('fs');
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');

const { startTwitch } = require('./chat/twitch');
const { startYoutube } = require('./chat/youtube');
const { startKick } = require('./chat/kick');
const { startTiktok } = require('./chat/tiktok');
const { createApiRouter } = require('./routes/api');
const { createAccounts } = require('./accounts');

const CONFIG_PATH = path.join(__dirname, '..', 'config.json');
const PUBLIC_PATH = path.join(__dirname, '..', 'public');

if (!fs.existsSync(CONFIG_PATH)) {
  console.error('Falta o config.json. Copie config.example.json para config.json e preencha seus dados.');
  process.exit(1);
}

const config = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf-8'));
const accounts = createAccounts(path.join(__dirname, '..'));

const app = express();
const server = http.createServer(app);
const io = new Server(server);

app.use(express.json());
app.get('/overlay.html', (_req, res) => res.sendFile(path.join(PUBLIC_PATH, 'overlay.html')));
app.use(express.static(PUBLIC_PATH));
app.use('/api', createApiRouter(accounts));

const overlayConfig = {
  messageDurationSeconds: Math.max(3, Number(config.overlay?.messageDurationSeconds) || 20),
  channelName: String(config.overlay?.channelName || config.twitch?.channel || config.kick?.channel || '').replace(/^[@#]+/, ''),
  highlightMentions: config.overlay?.highlightMentions !== false,
  hideCommands: {
    twitch: Boolean(config.overlay?.hideCommands?.twitch),
    kick: Boolean(config.overlay?.hideCommands?.kick),
    youtube: Boolean(config.overlay?.hideCommands?.youtube),
    tiktok: Boolean(config.overlay?.hideCommands?.tiktok),
  },
};

app.get('/api/overlay-config', (_req, res) => {
  res.json(overlayConfig);
});

// Mantém as últimas mensagens em memória, pra quem abrir a overlay depois
// do início da live já ver algo (em vez de uma tela vazia).
const recentEvents = [];
const MAX_HISTORY = 100;
let seq = 0;
const connectionStates = new Map();

// Requisições de long-poll do dock nativo (ver /api/chat/poll) que estão
// seguradas esperando mensagem nova chegar.
const pendingPolls = new Set();

function resolvePendingPolls() {
  for (const pending of Array.from(pendingPolls)) {
    const newMessages = recentEvents.filter((m) => m.seq > pending.since);
    if (newMessages.length > 0) {
      clearTimeout(pending.timer);
      pendingPolls.delete(pending);
      pending.res.json({ next: seq, messages: newMessages });
    }
  }
}

function publishEvent(event) {
  seq += 1;
  const stamped = { ...event, seq };
  recentEvents.push(stamped);
  if (recentEvents.length > MAX_HISTORY) recentEvents.shift();
  io.emit(event.kind === 'status' ? 'connection-status' : 'chat-message', stamped);
  resolvePendingPolls();
}

function broadcastMessage(msg) {
  const platform = String(msg.platform || 'custom').toLowerCase();
  if (overlayConfig.hideCommands[platform] && String(msg.message || '').trimStart().startsWith('!')) {
    return;
  }
  publishEvent({ ...msg, platform, kind: 'chat' });
}

function updateConnectionStatus(platform, state, detail) {
  const status = {
    kind: 'status',
    platform,
    state,
    connected: state === 'connected',
    message: detail || (state === 'connected'
      ? `${platform} conectado`
      : `${platform} desconectado — reconectando`),
    timestamp: Date.now(),
  };
  const previous = connectionStates.get(platform);
  if (previous?.state === status.state && previous?.message === status.message) return;
  connectionStates.set(platform, status);
  publishEvent(status);
}

io.on('connection', (socket) => {
  socket.emit('chat-history', recentEvents.filter((event) => event.kind === 'chat'));
  socket.emit('connection-statuses', Array.from(connectionStates.values()));
});

// Endpoint HTTP de long-poll, pensado pro dock nativo em C++ do plugin do
// OBS: usa só QNetworkAccessManager (módulo Qt já presente no OBS), sem
// precisar do módulo Qt6WebSockets (que exigia compilação manual à parte).
// O cliente manda o "seq" da última mensagem que já tem (?since=N); se já
// houver mensagem mais nova, responde na hora. Senão, segura a resposta
// até ~25s ou até chegar mensagem nova — o que der primeiro — e o cliente
// já dispara a próxima consulta assim que a resposta chegar.
app.get('/api/chat/poll', (req, res) => {
  const since = Number.parseInt(req.query.since, 10) || 0;

  const newMessages = recentEvents.filter((m) => m.seq > since);
  if (newMessages.length > 0) {
    res.json({ next: seq, messages: newMessages });
    return;
  }

  const pending = { res, since };
  pending.timer = setTimeout(() => {
    pendingPolls.delete(pending);
    res.json({ next: since, messages: [] });
  }, 25000);

  pendingPolls.add(pending);

  req.on('close', () => {
    clearTimeout(pending.timer);
    pendingPolls.delete(pending);
  });
});

async function main() {
  if (config.twitch?.enabled) {
    startTwitch(config.twitch, broadcastMessage, (state, detail) => updateConnectionStatus('twitch', state, detail));
  }

  await startYoutube(config.youtube || {}, broadcastMessage, (state, detail) => updateConnectionStatus('youtube', state, detail), accounts);

  if (config.kick?.enabled) {
    await startKick(config.kick, broadcastMessage, (state, detail) => updateConnectionStatus('kick', state, detail));
  }

  if (config.tiktok?.enabled) {
    await startTiktok(config.tiktok, broadcastMessage, (state, detail) => updateConnectionStatus('tiktok', state, detail));
  }

  const port = config.server?.port || 3000;
  server.listen(port, () => {
    console.log(`\n[streamhub] painel de configuração em http://localhost:${port}/dashboard.html`);
    console.log(`[streamhub] overlay de chat em http://localhost:${port}/overlay.html`);
    console.log('[streamhub] no OBS: View > Docks > Custom Browser Docks para encaixar o dashboard dentro do OBS\n');
  });
}

main();
