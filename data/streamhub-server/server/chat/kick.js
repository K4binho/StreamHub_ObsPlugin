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
async function startKick(cfg, onMessage) {
  let chatroomId = cfg.chatroomId;

  if (!chatroomId) {
    try {
      chatroomId = await getChatroomId(cfg.channel);
    } catch (err) {
      console.error('[kick] não consegui achar o chatroom automaticamente:', err.message);
      console.error('[kick] pegue o chatroom_id manualmente e cole em config.kick.chatroomId');
      return null;
    }
  }

  const pusher = new Pusher(PUSHER_APP_KEY, { cluster: PUSHER_CLUSTER });
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
    console.log(`[kick] conectado ao chat de ${cfg.channel}`);
  });

  pusher.connection.bind('error', (err) => {
    console.error('[kick] erro de conexão:', err);
  });

  return {
    stop: () => pusher.disconnect(),
  };
}

module.exports = { startKick };
