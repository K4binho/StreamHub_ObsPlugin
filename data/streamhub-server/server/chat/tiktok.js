const { WebcastPushConnection } = require('tiktok-live-connector');

// Integração experimental baseada no protocolo usado pelo TikTok. Precisa
// ser validada ponta a ponta em uma live real antes de ser anunciada como
// suporte estável.
async function startTiktok(cfg, onMessage, onStatus = () => {}) {
  let connection = null;
  let retryTimer = null;
  let stopped = false;

  const scheduleReconnect = () => {
    if (stopped || retryTimer) return;
    onStatus('reconnecting', 'TikTok desconectado — reconectando');
    retryTimer = setTimeout(() => {
      retryTimer = null;
      connect().catch(() => {});
    }, 10000);
  };

  const connect = async () => {
    if (stopped) return;
    onStatus('connecting', 'TikTok conectando...');
    const current = new WebcastPushConnection(cfg.username);
    connection = current;

    current.on('chat', (data) => {
      onMessage({
        platform: 'tiktok',
        id: data.msgId,
        user: data.nickname || data.uniqueId,
        color: '#FF2D8D',
        message: data.comment,
        badges: data.isModerator ? ['moderator'] : [],
        timestamp: Date.now(),
      });
    });
    current.on('disconnected', scheduleReconnect);
    current.on('streamEnd', scheduleReconnect);

    try {
      await current.connect();
      console.log(`[tiktok] conectado à live de @${cfg.username}`);
      onStatus('connected', 'TikTok conectado');
    } catch (err) {
      console.error('[tiktok] não consegui conectar (a live está no ar?):', err.message);
      scheduleReconnect();
    }
  };

  await connect();
  return {
    stop: () => {
      stopped = true;
      if (retryTimer) clearTimeout(retryTimer);
      if (connection) connection.disconnect();
    },
  };
}

module.exports = { startTiktok };
