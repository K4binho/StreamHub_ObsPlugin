const tmi = require('tmi.js');

/**
 * Conecta ao chat da Twitch em modo somente leitura (anônimo).
 * Não precisa de token — só de ler mensagens públicas do canal.
 *
 * @param {object} cfg - config.twitch do config.json
 * @param {(msg: object) => void} onMessage - callback chamado a cada mensagem
 * @param {(state: string, detail?: string) => void} onStatus
 * @returns {tmi.Client}
 */
function startTwitch(cfg, onMessage, onStatus = () => {}) {
  onStatus('connecting', 'Twitch conectando...');
  const client = new tmi.Client({
    connection: { reconnect: true, secure: true },
    channels: [cfg.channel],
  });

  client.connect().catch((err) => {
    console.error('[twitch] erro ao conectar:', err.message);
    onStatus('reconnecting', 'Twitch desconectado — reconectando');
  });

  client.on('message', (channel, tags, message, self) => {
    if (self) return;

    onMessage({
      platform: 'twitch',
      id: tags.id,
      user: tags['display-name'] || tags.username,
      color: tags.color || '#9146FF',
      message,
      badges: Object.keys(tags.badges || {}),
      timestamp: Date.now(),
    });
  });

  client.on('connected', () => {
    console.log(`[twitch] conectado ao canal #${cfg.channel}`);
    onStatus('connected', 'Twitch conectado');
  });

  client.on('disconnected', () => onStatus('reconnecting', 'Twitch desconectado — reconectando'));
  client.on('reconnect', () => onStatus('reconnecting', 'Twitch desconectado — reconectando'));

  return client;
}

module.exports = { startTwitch };
