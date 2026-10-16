const fetch = require('node-fetch');
const Pusher = require('pusher-js');

// ATENÇÃO: a Kick não tem API pública de chat. Isso usa o mesmo Pusher que o
// site kick.com usa internamente. Pode quebrar sem aviso se a Kick mudar algo.
// Se a busca automática do chatroomId falhar (a Kick às vezes bloqueia via
// Cloudflare), pegue o "chatroom_id" manualmente em:
//   https://kick.com/api/v2/channels/SEU_CANAL
// e cole em config.json como "kick.chatroomId".
const PUSHER_APP_KEY = '32cbd69e4b950bf97679';
const PUSHER_CLUSTER = 'us2';

async function getChatroomId(channel) {
  const res = await fetch(`https://kick.com/api/v2/channels/${channel}`, {
    headers: { 'User-Agent': 'Mozilla/5.0' },
  });
  if (!res.ok) {
    throw new Error(`Kick API respondeu ${res.status} — pode ser bloqueio da Cloudflare`);
  }
  const data = await res.json();
  return data.chatroom.id;
}

/**
 * @param {object} cfg - config.kick (channel e, opcionalmente, chatroomId fixo)
 * @param {(msg: object) => void} onMessage
 */
async function startKick(cfg, onMessage, onStatus = () => {}) {
  let retryTimer = null;
  let stopped = false;
  let pusher = null;
  let chatroomId = cfg.chatroomId;
  let retryAttempt = 0;

  const scheduleConnect = (reason = '') => {
    if (stopped || retryTimer) return;
    if (reason) console.warn(`[kick] ${reason}; nova tentativa automática agendada`);
    retryTimer = setTimeout(() => {
      retryTimer = null;
      if (pusher) {
        pusher.disconnect();
        pusher = null;
      }
      void connect();
    }, Math.min(60000, 5000 * (2 ** Math.min(retryAttempt++, 4))));
  };

  const connect = async () => {
    if (stopped) return;
    onStatus('connecting', 'Kick conectando...');

    if (!chatroomId) {
      try {
        chatroomId = await getChatroomId(cfg.channel);
      } catch (err) {
        console.error('[kick] não consegui achar o chatroom automaticamente:', err.message);
        onStatus('reconnecting', 'Kick desconectado — reconectando');
        scheduleConnect(err.message);
        return;
      }
    }

    pusher = new Pusher(PUSHER_APP_KEY, { cluster: PUSHER_CLUSTER });
    const channel = pusher.subscribe(`chatrooms.${chatroomId}.v2`);

    channel.bind('App\\Events\\ChatMessageEvent', (data) => {
      onMessage({
        platform: 'kick',
        id: data.id,
        user: data.sender?.username,
        color: data.sender?.identity?.color || '#53FC18',
        message: data.content,
        badges: (data.sender?.identity?.badges || []).map((b) => b.type),
        timestamp: Date.now(),
      });
    });

    pusher.connection.bind('connected', () => {
      retryAttempt = 0;
      console.log(`[kick] conectado ao chat de ${cfg.channel}`);
      onStatus('connected', 'Kick conectado');
    });

    pusher.connection.bind('error', (err) => {
      console.error('[kick] erro de conexão:', err);
      onStatus('reconnecting', 'Kick desconectado — reconectando');
      if (pusher?.connection?.state === 'failed')
        scheduleConnect('conexão Pusher falhou');
    });

    pusher.connection.bind('state_change', ({ current }) => {
      if (['unavailable', 'failed', 'disconnected'].includes(current)) {
        onStatus('reconnecting', 'Kick desconectado — reconectando');
        if (current === 'failed') scheduleConnect('servidor Pusher recusou a conexão');
      }
    });
  };

  await connect();
  return {
    stop: () => {
      stopped = true;
      if (retryTimer) {
        clearTimeout(retryTimer);
        retryTimer = null;
      }
      if (pusher) {
        pusher.disconnect();
        pusher = null;
      }
    },
  };
}

module.exports = { startKick };
