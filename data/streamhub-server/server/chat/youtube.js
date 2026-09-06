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
async function startYoutube(cfg, onMessage) {
  const auth = cfg.apiKey;

  let liveChatId;
  try {
    const videoRes = await youtube.videos.list({
      key: auth,
      id: cfg.videoId,
      part: 'liveStreamingDetails',
    });

    const video = videoRes.data.items?.[0];
    liveChatId = video?.liveStreamingDetails?.activeLiveChatId;

    if (!liveChatId) {
      console.error('[youtube] não achei um chat ativo para esse videoId. Confira se a live está no ar.');
      return null;
    }
  } catch (err) {
    console.error('[youtube] erro ao buscar a live:', err.message);
    return null;
  }

  console.log('[youtube] conectado ao chat da live');

  let nextPageToken = undefined;
  let stopped = false;

  const poll = async () => {
    if (stopped) return;

    try {
      const res = await youtube.liveChatMessages.list({
        key: auth,
        liveChatId,
        part: 'snippet,authorDetails',
        pageToken: nextPageToken,
      });

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

      const delay = res.data.pollingIntervalMillis || 5000;
      setTimeout(poll, delay);
    } catch (err) {
      console.error('[youtube] erro no polling do chat:', err.message);
      setTimeout(poll, 10000);
    }
  };

  poll();

  return {
    stop: () => {
      stopped = true;
    },
  };
}

module.exports = { startYoutube };
