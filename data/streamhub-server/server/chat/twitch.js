const tmi = require('tmi.js');

/**
 * Conecta ao chat da Twitch em modo somente leitura (anônimo).
 * Não precisa de token — só de ler mensagens públicas do canal.
 *
 * @param {object} cfg - config.twitch do config.json
 * @param {(msg: object) => void} onMessage - callback chamado a cada mensagem
 * @returns {tmi.Client}
 */
function startTwitch(cfg, onMessage) {
  const client = new tmi.Client({
    channels: [cfg.channel],
  });

  client.connect().catch((err) => {
    console.error('[twitch] erro ao conectar:', err.message);
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
  });

  return client;
}

module.exports = { startTwitch };
