const { google } = require('googleapis');

const youtube = google.youtube('v3');

/**
 * Chat ao vivo do YouTube funciona por polling (não é websocket).
 * Passo 1: a partir do videoId da live, descobrir o liveChatId.
 * Passo 2: ficar buscando mensagens novas no intervalo recomendado pela API.
 *
 * @param {object} cfg - config.youtube do config.json (apiKey, videoId)
 * @param {(msg: object) => void} onMessage
 */
async function startYoutube(cfg, onMessage, onStatus = () => {}) {
  const auth = cfg.apiKey;
  let liveChatId = '';
  let nextPageToken;
  let retryTimer = null;
  let pollTimer = null;
  let stopped = false;
  let connecting = false;

  const scheduleConnect = () => {
    if (stopped || retryTimer) return;
    retryTimer = setTimeout(() => {
      retryTimer = null;
      void connect();
    }, 10000);
  };

  const connect = async () => {
    if (stopped || connecting) return;
    connecting = true;
    onStatus('connecting', 'YouTube conectando...');
    try {
      const videoRes = await youtube.videos.list({
        key: auth,
        id: cfg.videoId,
        part: 'liveStreamingDetails',
      });

      const video = videoRes.data.items?.[0];
      liveChatId = video?.liveStreamingDetails?.activeLiveChatId || '';
      nextPageToken = undefined;

      if (!liveChatId) {
        console.error('[youtube] não achei um chat ativo para esse videoId. Confira se a live está no ar.');
        onStatus('reconnecting', 'YouTube sem chat ativo — tentando novamente');
        scheduleConnect();
        return;
      }

      console.log('[youtube] conectado ao chat da live');
      onStatus('connected', 'YouTube conectado');
      void poll();
    } catch (err) {
      console.error('[youtube] erro ao buscar a live:', err.message);
      onStatus('reconnecting', 'YouTube desconectado — reconectando');
      scheduleConnect();
    } finally {
      connecting = false;
    }
  };

  const poll = async () => {
    if (stopped || !liveChatId) return;

    try {
      const res = await youtube.liveChatMessages.list({
        key: auth,
        liveChatId,
        part: 'snippet,authorDetails',
        pageToken: nextPageToken,
      });

      onStatus('connected', 'YouTube conectado');
      nextPageToken = res.data.nextPageToken;

      for (const item of res.data.items || []) {
        onMessage({
          platform: 'youtube',
          id: item.id,
          user: item.authorDetails.displayName,
          color: '#FF0000',
          message: item.snippet.displayMessage,
          badges: item.authorDetails.isChatModerator ? ['moderator'] : [],
          timestamp: Date.now(),
        });
      }

      pollTimer = setTimeout(() => {
        pollTimer = null;
        void poll();
      }, res.data.pollingIntervalMillis || 5000);
    } catch (err) {
      console.error('[youtube] erro no polling do chat:', err.message);
      liveChatId = '';
      onStatus('reconnecting', 'YouTube desconectado — reconectando');
      scheduleConnect();
    }
  };

  await connect();
  return {
    stop: () => {
      stopped = true;
      if (retryTimer) {
        clearTimeout(retryTimer);
        retryTimer = null;
      }
      if (pollTimer) {
        clearTimeout(pollTimer);
        pollTimer = null;
      }
    },
  };
}

module.exports = { startYoutube };
