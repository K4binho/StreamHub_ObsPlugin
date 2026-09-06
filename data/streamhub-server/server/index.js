const path = require('path');
const fs = require('fs');
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');

const { startTwitch } = require('./chat/twitch');
const { startYoutube } = require('./chat/youtube');
const { startKick } = require('./chat/kick');
const { startTiktok } = require('./chat/tiktok');
const apiRouter = require('./routes/api');

const CONFIG_PATH = path.join(__dirname, '..', 'config.json');

if (!fs.existsSync(CONFIG_PATH)) {
  console.error('Falta o config.json. Copie config.example.json para config.json e preencha seus dados.');
  process.exit(1);
}

const config = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf-8'));

const app = express();
const server = http.createServer(app);
const io = new Server(server);

app.use(express.json());
app.use(express.static(path.join(__dirname, '..', 'public')));
app.use('/api', apiRouter);

// Mantém as últimas mensagens em memória, pra quem abrir a overlay depois
// do início da live já ver algo (em vez de uma tela vazia).
const recentMessages = [];
const MAX_HISTORY = 100;
let seq = 0;

// Requisições de long-poll do dock nativo (ver /api/chat/poll) que estão
// seguradas esperando mensagem nova chegar.
const pendingPolls = new Set();

function resolvePendingPolls() {
  for (const pending of Array.from(pendingPolls)) {
    const newMessages = recentMessages.filter((m) => m.seq > pending.since);
    if (newMessages.length > 0) {
      clearTimeout(pending.timer);
      pendingPolls.delete(pending);
      pending.res.json({ next: seq, messages: newMessages });
    }
  }
}

function broadcastMessage(msg) {
  seq += 1;
  const stamped = { ...msg, seq };
  recentMessages.push(stamped);
  if (recentMessages.length > MAX_HISTORY) recentMessages.shift();
  io.emit('chat-message', msg);
  resolvePendingPolls();
}

io.on('connection', (socket) => {
  socket.emit('chat-history', recentMessages);
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

  const newMessages = recentMessages.filter((m) => m.seq > since);
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
    startTwitch(config.twitch, broadcastMessage);
  }

  if (config.youtube?.enabled) {
    await startYoutube(config.youtube, broadcastMessage);
  }

  if (config.kick?.enabled) {
    await startKick(config.kick, broadcastMessage);
  }

  if (config.tiktok?.enabled) {
    await startTiktok(config.tiktok, broadcastMessage);
  }

  const port = config.server?.port || 3000;
  server.listen(port, () => {
    console.log(`\n[streamhub] painel de configuração em http://localhost:${port}/dashboard.html`);
    console.log(`[streamhub] overlay de chat em http://localhost:${port}/overlay.html`);
    console.log('[streamhub] no OBS: View > Docks > Custom Browser Docks para encaixar o dashboard dentro do OBS\n');
  });
}

main();
