const { WebcastPushConnection } = require('tiktok-live-connector');

// ATENÇÃO: o TikTok também não tem API pública de chat. A lib abaixo é
// mantida pela comunidade e conecta ao mesmo protocolo interno que o app
// TikTok usa. Só funciona enquanto a live estiver realmente ao vivo.
async function startTiktok(cfg, onMessage) {
  const connection = new WebcastPushConnection(cfg.username);

  try {
    await connection.connect();
    console.log(`[tiktok] conectado à live de @${cfg.username}`);
  } catch (err) {
    console.error('[tiktok] não consegui conectar (a live está no ar?):', err.message);
    return null;
  }

  connection.on('chat', (data) => {
    onMessage({
      platform: 'tiktok',
      id: data.msgId,
      user: data.nickname || data.uniqueId,
      color: '#000000',
      message: data.comment,
      badges: data.isModerator ? ['moderator'] : [],
      timestamp: Date.now(),
    });
  });

  connection.on('disconnected', () => {
    console.log('[tiktok] desconectado');
  });

  return {
    stop: () => connection.disconnect(),
  };
}

module.exports = { startTiktok };
