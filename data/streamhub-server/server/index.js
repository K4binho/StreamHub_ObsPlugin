const path = require('path');
const fs = require('fs');
const crypto = require('crypto');
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const fetch = require('node-fetch');

const { startTwitch } = require('./chat/twitch');
const { startYoutube } = require('./chat/youtube');
const { startKick } = require('./chat/kick');
const { startTiktok } = require('./chat/tiktok');
const { createApiRouter } = require('./routes/api');
const { createAccounts, SHARED_OAUTH_BROKER_URL } = require('./accounts');

const CONFIG_PATH = path.join(__dirname, '..', 'config.json');
const PUBLIC_PATH = path.join(__dirname, '..', 'public');
const OBS_PID = Number.parseInt(process.env.STREAMHUB_OBS_PID || '', 10);
const INSTANCE_TOKEN = String(process.env.STREAMHUB_INSTANCE_TOKEN || '');
const ALLOW_STANDALONE = process.env.STREAMHUB_ALLOW_STANDALONE === '1';
const OBS_MISSING_TIMEOUT_MS = 5000;
const BROKER_HEARTBEAT_INTERVAL_MS = 4 * 60 * 1000;

function validPid(pid) {
  return Number.isSafeInteger(pid) && pid > 0;
}

if (!fs.existsSync(CONFIG_PATH)) {
  console.error('Falta o config.json. Copie config.example.json para config.json e preencha seus dados.');
  process.exit(1);
}

if ((!validPid(OBS_PID) || INSTANCE_TOKEN.length < 32) && !ALLOW_STANDALONE) {
  console.error('[streamhub] ambiente de processo incompleto; inicie pelo plugin OBS.');
  process.exit(1);
}

const config = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf-8'));
const accounts = createAccounts(path.join(__dirname, '..'));

const app = express();
const server = http.createServer(app);
const io = new Server(server);
const connectorStops = [];
let watchdogTimer = null;
let brokerHeartbeatTimer = null;
let obsMissingSince = 0;
let shuttingDown = false;
let shutdownPromise = null;
let exitPromise = null;

async function pingBroker() {
  if (!/^https:\/\//i.test(SHARED_OAUTH_BROKER_URL)) return;
  try {
    const response = await fetch(`${SHARED_OAUTH_BROKER_URL}/healthz`, {
      headers: { Accept: 'application/json', 'User-Agent': 'StreamHub-OBS-Heartbeat' },
      timeout: 10000,
    });
    await response.text();
    if (!response.ok) {
      console.warn(`[streamhub] broker heartbeat HTTP ${response.status}`);
    }
  } catch (err) {
    console.warn(`[streamhub] broker heartbeat falhou: ${err.message}`);
  }
}

function startBrokerHeartbeat() {
  if (!/^https:\/\//i.test(SHARED_OAUTH_BROKER_URL)) return;
  void pingBroker();
  brokerHeartbeatTimer = setInterval(() => void pingBroker(), BROKER_HEARTBEAT_INTERVAL_MS);
  brokerHeartbeatTimer.unref?.();
}

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
const internalSyncNonces = new Map();

function issueInternalSyncNonce() {
  const now = Date.now();
  for (const [nonce, expiresAt] of internalSyncNonces) {
    if (expiresAt <= now) internalSyncNonces.delete(nonce);
  }
  const nonce = crypto.randomBytes(32).toString('base64url');
  internalSyncNonces.set(nonce, Date.now() + 30000);
  return nonce;
}

function consumeInternalSyncNonce(nonce) {
  const expiresAt = internalSyncNonces.get(nonce);
  internalSyncNonces.delete(nonce);
  return Boolean(expiresAt && expiresAt > Date.now());
}

app.post('/internal/twitch-sync/nonce', authorizeInternal, (_req, res) => {
  res.json({ nonce: issueInternalSyncNonce(), pid: process.pid, obsPid: validPid(OBS_PID) ? OBS_PID : null });
});

app.post('/internal/twitch-sync', authorizeInternal, async (req, res) => {
  const nonce = String(req.body?.nonce || '');
  if (!consumeInternalSyncNonce(nonce)) {
    res.status(401).json({ error: 'Nonce inválido ou expirado.' });
    return;
  }
  try {
    const transmission = await accounts.twitchTransmission();
    res.json({ ...transmission, pid: process.pid, obsPid: validPid(OBS_PID) ? OBS_PID : null });
  } catch (err) {
    res.status(400).json({ error: err.message });
  }
});

app.post('/internal/kick-sync/nonce', authorizeInternal, (_req, res) => {
  res.json({ nonce: issueInternalSyncNonce(), pid: process.pid, obsPid: validPid(OBS_PID) ? OBS_PID : null });
});

app.post('/internal/kick-sync', authorizeInternal, async (req, res) => {
  const nonce = String(req.body?.nonce || '');
  if (!consumeInternalSyncNonce(nonce)) {
    res.status(401).json({ error: 'Nonce inválido ou expirado.' });
    return;
  }
  try {
    const transmission = await accounts.kickTransmission();
    res.json({ ...transmission, pid: process.pid, obsPid: validPid(OBS_PID) ? OBS_PID : null });
  } catch (err) {
    res.status(400).json({ error: err.message });
  }
});

app.post('/internal/youtube-sync/nonce', authorizeInternal, (_req, res) => {
  res.json({ nonce: issueInternalSyncNonce(), pid: process.pid, obsPid: validPid(OBS_PID) ? OBS_PID : null });
});

app.post('/internal/youtube-sync', authorizeInternal, async (req, res) => {
  const nonce = String(req.body?.nonce || '');
  if (!consumeInternalSyncNonce(nonce)) {
    res.status(401).json({ error: 'Nonce inválido ou expirado.' });
    return;
  }
  try {
    const transmission = await accounts.youtubeTransmission();
    res.json({ ...transmission, pid: process.pid, obsPid: validPid(OBS_PID) ? OBS_PID : null });
  } catch (err) {
    res.status(400).json({ error: err.message });
  }
});

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

function registerConnector(handle) {
  if (!handle) return;
  const stop = typeof handle.stop === 'function'
    ? () => handle.stop()
    : typeof handle.disconnect === 'function'
      ? () => handle.disconnect()
      : null;
  if (!stop) return;
  if (shuttingDown) {
    void Promise.resolve().then(stop).catch((err) => {
      console.error('[streamhub] erro ao encerrar conector:', err.message);
    });
    return;
  }
  connectorStops.push(stop);
}

function isLoopback(req) {
  const address = String(req.socket.remoteAddress || '').replace(/^::ffff:/, '');
  return address === '127.0.0.1' || address === '::1' || address === 'localhost';
}

function hasInternalToken(req) {
  if (!INSTANCE_TOKEN) return false;
  const supplied = String(req.get('X-StreamHub-Token') || '');
  return supplied.length === INSTANCE_TOKEN.length && supplied === INSTANCE_TOKEN;
}

function authorizeInternal(req, res, next) {
  if (!isLoopback(req) || !hasInternalToken(req)) {
    res.status(401).json({ error: 'Não autorizado.' });
    return;
  }
  next();
}

app.get('/internal/status', authorizeInternal, (_req, res) => {
  res.json({ ok: true, pid: process.pid, obsPid: validPid(OBS_PID) ? OBS_PID : null, port: server.address()?.port || null });
});

app.post('/internal/shutdown', authorizeInternal, (_req, res) => {
  res.json({ ok: true, pid: process.pid, obsPid: validPid(OBS_PID) ? OBS_PID : null });
  setImmediate(() => {
    void exitAfterShutdown('internal request');
  });
});

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

function obsProcessExists() {
  if (!validPid(OBS_PID)) return false;
  try {
    process.kill(OBS_PID, 0);
    return true;
  } catch (err) {
    return err.code === 'EPERM';
  }
}

async function closeServer() {
  for (const pending of Array.from(pendingPolls)) {
    clearTimeout(pending.timer);
    pendingPolls.delete(pending);
    pending.res.end();
  }

  await new Promise((resolve) => {
    io.close(() => {
      if (!server.listening) {
        resolve();
        return;
      }
      server.close(() => resolve());
    });
  });
}

async function shutdown(reason = 'requested') {
  if (shutdownPromise) return shutdownPromise;
  shuttingDown = true;
  if (watchdogTimer) {
    clearInterval(watchdogTimer);
    watchdogTimer = null;
  }
  if (brokerHeartbeatTimer) {
    clearInterval(brokerHeartbeatTimer);
    brokerHeartbeatTimer = null;
  }

  shutdownPromise = (async () => {
    for (const stop of connectorStops.splice(0)) {
      try {
        await stop();
      } catch (err) {
        console.error('[streamhub] erro ao encerrar conector:', err.message);
      }
    }
    await closeServer();
    console.log(`[streamhub] encerrado (${reason})`);
  })();
  return shutdownPromise;
}

function exitAfterShutdown(reason, exitCode = 0) {
  if (exitPromise) return exitPromise;
  exitPromise = shutdown(reason).then(() => {
    process.exit(exitCode);
  });
  return exitPromise;
}

async function startWatchdog() {
  if (!validPid(OBS_PID)) return;
  const check = () => {
    if (obsProcessExists()) {
      obsMissingSince = 0;
      return;
    }
    if (!obsMissingSince) obsMissingSince = Date.now();
    if (Date.now() - obsMissingSince >= OBS_MISSING_TIMEOUT_MS) {
      void exitAfterShutdown('OBS não encontrado');
    }
  };
  watchdogTimer = setInterval(check, 2000);
  watchdogTimer.unref?.();
  check();
}

async function main() {
  try {
    startBrokerHeartbeat();
    void startWatchdog();

    if (config.twitch?.enabled) {
      registerConnector(startTwitch(config.twitch, broadcastMessage, (state, detail) => updateConnectionStatus('twitch', state, detail)));
    }

    if (config.youtube?.enabled) {
      registerConnector(await startYoutube(config.youtube, broadcastMessage, (state, detail) => updateConnectionStatus('youtube', state, detail)));
    }

    if (config.kick?.enabled) {
      registerConnector(await startKick(config.kick, broadcastMessage, (state, detail) => updateConnectionStatus('kick', state, detail)));
    }

    if (config.tiktok?.enabled) {
      registerConnector(await startTiktok(config.tiktok, broadcastMessage, (state, detail) => updateConnectionStatus('tiktok', state, detail)));
    }

    if (shuttingDown) return;

    const port = Number(process.env.PORT) || config.server?.port || 605;
    server.listen(port, '127.0.0.1', () => {
      console.log(`\n[streamhub] painel de configuração em http://localhost:${port}/dashboard.html`);
      console.log(`[streamhub] overlay de chat em http://localhost:${port}/overlay.html`);
      console.log('[streamhub] no OBS: View > Docks > Custom Browser Docks para encaixar o dashboard dentro do OBS\n');
    });
  } catch (err) {
    console.error('[streamhub] falha ao iniciar:', err.message);
    await exitAfterShutdown('startup failure', 1);
  }
}
process.on('SIGTERM', () => {
  void exitAfterShutdown('SIGTERM');
});
process.on('SIGINT', () => {
  void exitAfterShutdown('SIGINT');
});

main();
