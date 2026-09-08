const { google } = require('googleapis');

const youtube = google.youtube('v3');

/**
 * Chat ao vivo do YouTube funciona por polling (não é websocket).
 * Passo 1: descobrir o liveChatId da live ativa.
 * Passo 2: buscar mensagens novas no intervalo recomendado pela API.
 *
 * @param {object} cfg - config.youtube do config.json (apiKey, videoId)
 * @param {(msg: object) => void} onMessage
 */
async function startYoutube(cfg, onMessage, onStatus = () => {}, accounts = null) {
  let stopped = false;
  let connecting = false;
  let polling = false;
  let liveChatId = '';
  let nextPageToken;
  let retryTimer = null;
  let pollTimer = null;

  const legacyConfigured = () => Boolean(
    cfg.enabled && /^AIza[\w-]{20,}$/.test(String(cfg.apiKey || '')) &&
    String(cfg.videoId || '').trim()
  );

  const oauthAvailable = () => {
    const status = accounts?.youtubeStatus();
    return Boolean(status?.connected && !status?.needsReconnect);
  };

  const clearRetry = () => {
    if (retryTimer) {
      clearTimeout(retryTimer);
      retryTimer = null;
    }
  };

  const clearPoll = () => {
    if (pollTimer) {
      clearTimeout(pollTimer);
      pollTimer = null;
    }
  };

  const scheduleConnect = (delay) => {
    if (stopped || connecting || polling) return;
    clearRetry();
    retryTimer = setTimeout(() => {
      retryTimer = null;
      connect();
    }, delay);
  };


  const failAndRetry = (message, delay = 10000) => {
    liveChatId = '';
    nextPageToken = undefined;
    clearPoll();
    onStatus('reconnecting', message);
    scheduleConnect(delay);
  };

  const poll = async () => {
    if (stopped || !liveChatId || polling) return;
    polling = true;

    try {
      const response = oauthAvailable()
        ? await accounts.youtubeRequest(
          `/liveChat/messages?part=snippet,authorDetails&liveChatId=${encodeURIComponent(liveChatId)}${nextPageToken ? `&pageToken=${encodeURIComponent(nextPageToken)}` : ''}`
        )
        : (await youtube.liveChatMessages.list({
          key: cfg.apiKey,
          liveChatId,
          part: 'snippet,authorDetails',
          pageToken: nextPageToken,
        })).data;

      if (stopped) return;
      nextPageToken = response.nextPageToken;
      onStatus('connected', 'YouTube conectado');

      for (const item of response.items || []) {
        onMessage({
          platform: 'youtube',
          id: item.id,
          user: item.authorDetails?.displayName || 'YouTube',
          userId: item.authorDetails?.channelId || '',
          color: '#FF0000',
          message: item.snippet?.displayMessage || '',
          badges: item.authorDetails?.isChatModerator ? ['moderator'] : [],
          timestamp: Date.now(),
        });
      }

      polling = false;
      pollTimer = setTimeout(() => {
        pollTimer = null;
        poll();
      }, Math.max(1000, Number(response.pollingIntervalMillis) || 5000));
    } catch (err) {
      polling = false;
      console.error('[youtube] erro no polling do chat:', err.message);
      failAndRetry('YouTube desconectado — reconectando');
    }
  };

  const connect = async () => {
    if (stopped || connecting || polling) return;
    connecting = true;
    clearRetry();

    const oauth = oauthAvailable();
    if (!oauth && !legacyConfigured()) {
      connecting = false;
      onStatus('disconnected', 'YouTube não configurado');
      scheduleConnect(3000);
      return;
    }

    onStatus('connecting', 'YouTube conectando...');
    try {
      if (oauth) {
        const broadcasts = await accounts.youtubeRequest(
          '/liveBroadcasts?part=snippet,status&mine=true&maxResults=50'
        );
        const active = (broadcasts.items || [])
          .filter((item) => ['live', 'testing'].includes(item.status?.lifeCycleStatus))
          .sort((left, right) => String(right.snippet?.actualStartTime || '').localeCompare(
            String(left.snippet?.actualStartTime || '')
          ))[0];
        liveChatId = active?.snippet?.liveChatId || '';
      } else {
        const videoRes = await youtube.videos.list({
          key: cfg.apiKey,
          id: cfg.videoId,
          part: 'liveStreamingDetails',
        });
        liveChatId = videoRes.data.items?.[0]?.liveStreamingDetails?.activeLiveChatId || '';
      }

      if (!liveChatId) {
        connecting = false;
        console.error('[youtube] não achei um chat ativo. Confira se a live está no ar.');
        failAndRetry('YouTube sem chat ativo — tentando novamente');
        return;
      }

      nextPageToken = undefined;
      connecting = false;
      console.log('[youtube] conectado ao chat da live');
      onStatus('connected', 'YouTube conectado');
      poll();
    } catch (err) {
      connecting = false;
      console.error('[youtube] erro ao buscar a live:', err.message);
      failAndRetry('YouTube desconectado — reconectando');
    }
  };

  connect();

  return {
    stop: () => {
      stopped = true;
      clearRetry();
      clearPoll();
      liveChatId = '';
    },
  };
}

module.exports = { startYoutube };
