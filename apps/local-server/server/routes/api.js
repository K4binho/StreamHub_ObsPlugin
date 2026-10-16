const express = require('express');
const fetch = require('node-fetch');
const { readConfig, writeConfig } = require('../config-store');

function createApiRouter(accounts, getChatStatus = () => null) {
const router = express.Router();

function escapeHtml(value) {
  return String(value).replace(/[&<>\"']/g, (char) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '\"': '&quot;', "'": '&#39;',
  })[char]);
}

function oauthResultPage({ platform, connected, detail = '' }) {
  const name = escapeHtml(platform);
  const title = connected ? `${name} conectado ao StreamHub` : `Falha ao conectar ${name}`;
  const heading = connected ? 'Conta conectada com sucesso' : 'Não foi possível conectar conta';
  const message = connected
    ? `${name} foi autorizado. Volte ao OBS para continuar.`
    : `${name} não foi autorizado. Feche esta janela e tente novamente no OBS.`;
  const safeDetail = detail ? `<p class=\"detail\">${escapeHtml(detail)}</p>` : '';
  return `<!doctype html>
<html lang=\"pt-BR\"><head><meta charset=\"utf-8\"><title>${title}</title>
<style>
:root { color-scheme: dark; font-family: Segoe UI, sans-serif; background: #080c14; color: #f2f7ff; }
body { min-height: 100vh; margin: 0; display: grid; place-items: center; }
main { width: min(560px, calc(100vw - 40px)); box-sizing: border-box; padding: 36px; border: 1px solid ${connected ? '#16d86a' : '#d94155'}; border-radius: 16px; background: #0d1726; box-shadow: 0 18px 60px #0008; text-align: center; }
.icon { width: 58px; height: 58px; margin: 0 auto 18px; display: grid; place-items: center; border-radius: 50%; background: ${connected ? '#16d86a' : '#d94155'}; color: #080c14; font-size: 34px; font-weight: 800; }
h1 { margin: 0 0 12px; font-size: 26px; }
p { margin: 8px 0; color: #b8c8dc; line-height: 1.5; }
.detail { margin-top: 20px; padding: 12px; border-radius: 8px; background: #080c14; color: #ffb9c2; overflow-wrap: anywhere; }
small { display: block; margin-top: 24px; color: #7187a2; }
</style></head><body><main><div class=\"icon\">${connected ? '✓' : '!'}</div><h1>${heading}</h1><p>${message}</p>${safeDetail}<small>Você pode fechar esta janela.</small></main>
<script>history.replaceState({}, document.title, window.location.pathname);</script></body></html>`;
}

// Retorna o config.json inteiro pro dashboard preencher os campos.
router.get('/config', (req, res) => {
  try {
    res.json(readConfig());
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Sobrescreve o config.json com o que veio do dashboard.
// Validação é propositalmente simples: isso roda só localmente, pra você mesmo.
router.post('/config', (req, res) => {
  const incoming = req.body;

  if (!incoming || typeof incoming !== 'object') {
    return res.status(400).json({ error: 'Corpo inválido' });
  }

  try {
    writeConfig(incoming);
    res.json({ ok: true });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

router.get('/accounts/twitch/status', (_req, res) => {
  res.json(accounts.status());
});

router.get('/accounts/kick/status', (_req, res) => {
  res.json(accounts.kickStatus());
});

router.get('/accounts/youtube/status', (_req, res) => {
  res.json(accounts.youtubeStatus());
});

router.get('/accounts/chat-identities', (_req, res) => {
  const twitch = accounts.status();
  const kick = accounts.kickStatus();
  const youtube = accounts.youtubeStatus();
  res.json({
    twitch: { connected: twitch.connected, name: twitch.name, chat: getChatStatus('twitch') },
    kick: { connected: kick.connected, name: kick.name, chat: getChatStatus('kick') },
    youtube: { connected: youtube.connected, name: youtube.name, chat: getChatStatus('youtube') },
  });
});

router.post('/accounts/youtube/configure', (req, res) => {
  const address = String(req.socket?.remoteAddress || '').replace(/^::ffff:/, '');
  if (!['127.0.0.1', '::1', 'localhost'].includes(address)) {
    return res.status(403).json({ error: 'Configuração YouTube aceita somente por loopback.' });
  }
  try {
    res.json(accounts.configureYoutube(req.body?.clientId, req.body?.clientSecret));
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.post('/accounts/youtube/connect', async (_req, res) => {
  try { res.json(await accounts.startYoutube()); }
  catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/accounts/youtube/connect/:flowId', async (req, res) => {
  try { res.json(await accounts.pollYoutube(req.params.flowId)); }
  catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/accounts/youtube/callback', async (req, res) => {
  try {
    await accounts.completeYoutubeCallback(req.query);
    res.type('html').send(oauthResultPage({ platform: 'YouTube', connected: true }));
  } catch (err) {
    res.status(400).type('html').send(oauthResultPage({ platform: 'YouTube', connected: false, detail: err.message }));
  }
});

router.post('/accounts/kick/configure', (req, res) => {
  const address = String(req.socket?.remoteAddress || '').replace(/^::ffff:/, '');
  if (!['127.0.0.1', '::1', 'localhost'].includes(address)) {
    return res.status(403).json({ error: 'Configuração Kick aceita somente por loopback.' });
  }
  try {
    res.json(accounts.configureKick(req.body?.clientId, req.body?.clientSecret));
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.post('/accounts/kick/connect', async (_req, res) => {
  try { res.json(await accounts.startKick()); }
  catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/accounts/kick/connect/:flowId', async (req, res) => {
  try { res.json(await accounts.pollKick(req.params.flowId)); }
  catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/accounts/kick/callback', async (req, res) => {
  try {
    const result = await accounts.completeKickCallback(req.query);
    res.type('html').send(oauthResultPage({ platform: 'Kick', connected: true }));
    return result;
  } catch (err) {
    res.status(400).type('html').send(oauthResultPage({ platform: 'Kick', connected: false, detail: err.message }));
  }
});

router.post('/accounts/twitch/connect', async (_req, res) => {
  try { res.json(await accounts.start()); }
  catch (err) { res.status(500).json({ error: err.message }); }
});

router.get('/accounts/twitch/connect/:flowId', async (req, res) => {
  try { res.json(await accounts.poll(req.params.flowId)); }
  catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/twitch/channel', async (_req, res) => {
  try {
    const broadcasterId = await accounts.userId();
    const result = await accounts.request(`/channels?broadcaster_id=${encodeURIComponent(broadcasterId)}`);
    const channel = result.data?.[0];
    if (!channel) throw new Error('A Twitch não retornou as informações do canal.');
    let boxArtUrl = '';
    if (channel.game_id) {
      const games = await accounts.request(`/games?id=${encodeURIComponent(channel.game_id)}`);
      const game = games.data?.[0];
      boxArtUrl = String(game?.box_art_url || '')
        .replace('{width}', '104')
        .replace('{height}', '144');
    }
    res.json({
      title: channel.title || '', gameId: channel.game_id || '', gameName: channel.game_name || '', boxArtUrl,
      language: channel.broadcaster_language || 'pt', tags: channel.tags || [],
      classificationLabels: (channel.content_classification_labels || []).filter((item) => item.is_enabled).map((item) => item.id),
    });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/twitch/category-cover', async (req, res) => {
  const gameId = String(req.query.id || '').trim();
  if (!/^\d+$/.test(gameId)) return res.status(400).json({ error: 'ID de categoria Twitch inválido.' });
  try {
    const result = await accounts.request(`/games?id=${encodeURIComponent(gameId)}`);
    const boxArtUrl = String(result.data?.[0]?.box_art_url || '')
      .replace('{width}', '104')
      .replace('{height}', '144');
    if (!boxArtUrl) return res.status(404).json({ error: 'Capa da categoria não encontrada.' });
    const image = await fetch(boxArtUrl);
    if (!image.ok) return res.status(502).json({ error: 'CDN da Twitch recusou a capa.' });
    res.type(image.headers.get('content-type') || 'image/jpeg').send(await image.buffer());
  } catch (err) {
    res.status(400).json({ error: err.message });
  }
});

router.get('/twitch/categories', async (req, res) => {
  const query = String(req.query.q || '').trim();
  if (query.length < 2) return res.json({ data: [] });
  try {
    const result = await accounts.request(`/search/categories?query=${encodeURIComponent(query)}&first=10`);
    res.json({ data: (result.data || []).map((item) => ({
      id: item.id, name: item.name,
      boxArtUrl: String(item.box_art_url || '').replace('{width}', '52').replace('{height}', '72'),
    })) });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.patch('/twitch/channel', async (req, res) => {
  const title = typeof req.body?.title === 'string' ? req.body.title.trim() : '';
  const gameId = typeof req.body?.gameId === 'string' ? req.body.gameId.trim() : '';
  if (!title && !gameId) return res.status(400).json({ error: 'Informe um título ou selecione uma categoria.' });
  if (title.length > 140) return res.status(400).json({ error: 'O título pode ter no máximo 140 caracteres.' });
  try {
    const broadcasterId = await accounts.userId();
    const body = {};
    if (title) body.title = title;
    if (gameId) body.game_id = gameId;
    await accounts.request(`/channels?broadcaster_id=${encodeURIComponent(broadcasterId)}`, {
      method: 'PATCH', body: JSON.stringify(body),
    });
    res.json({ ok: true });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/broadcast/current', async (req, res) => {
  const platform = String(req.query.platform || '').trim().toLowerCase();
  const loaders = {
    twitch: () => accounts.twitchBroadcastContent(),
    kick: () => accounts.kickBroadcastContent(),
    youtube: () => accounts.youtubeBroadcastContent(String(req.query.broadcastId || '').trim()),
  };
  if (!loaders[platform]) return res.status(400).json({ error: 'Plataforma primária inválida.' });
  try {
    const transmission = await loaders[platform]();
    res.json({
      platform,
      broadcastId: String(transmission.broadcastId || ''),
      title: String(transmission.title || ''),
      description: String(transmission.description || ''),
      category: String(transmission.category || ''),
      categoryId: String(transmission.categoryId || ''),
      broadcastTitle: String(transmission.broadcastTitle || ''),
      broadcastDescription: String(transmission.broadcastDescription || ''),
      broadcastCategoryId: String(transmission.broadcastCategoryId || ''),
      videoTitle: String(transmission.videoTitle || ''),
      videoDescription: String(transmission.videoDescription || ''),
      videoCategoryId: String(transmission.videoCategoryId || ''),
      metadataMismatch: Boolean(transmission.metadataMismatch),
      fieldAvailability: transmission.fieldAvailability || {},
      tags: Array.isArray(transmission.tags) ? transmission.tags : [],
      language: String(transmission.language || ''),
      visibility: String(transmission.visibility || ''),
      classificationLabels: Array.isArray(transmission.classificationLabels)
        ? transmission.classificationLabels
        : [],
      isLive: Boolean(transmission.isLive),
    });
  } catch (err) {
    res.status(400).json({ error: err.message });
  }
});

router.get('/broadcast/youtube/events', async (_req, res) => {
  try {
    res.json({ items: await accounts.youtubeBroadcastEvents() });
  } catch (err) {
    res.status(400).json({ error: err.message });
  }
});

router.post('/broadcast/apply', async (req, res) => {
  const submittedGlobal = req.body?.global_metadata;
  const globalMetadata = submittedGlobal && typeof submittedGlobal === 'object'
    ? { ...submittedGlobal }
    : {
      title: String(req.body?.title || '').trim(),
      description: String(req.body?.description || req.body?.notification || '').trim(),
      language: String(req.body?.language || '').trim(),
      is_mature: Boolean(req.body?.is_mature),
    };
  const title = String(globalMetadata.title || req.body?.title || '').trim();
  globalMetadata.title = title;
  if (!title) return res.status(400).json({ error: 'Informe o título da transmissão.' });
  if (title.length > 140) return res.status(400).json({ error: 'O título pode ter no máximo 140 caracteres.' });

  const submittedPlatforms = req.body?.platforms;
  const platformSettings = submittedPlatforms && typeof submittedPlatforms === 'object'
    ? submittedPlatforms
    : {};
  const input = {
    ...req.body,
    global_metadata: { ...globalMetadata, title },
    platforms: platformSettings,
    title,
    sourcePlatform: String(req.body?.sourcePlatform || '').trim().toLowerCase(),
    categoryIdPlatform: String(req.body?.categoryIdPlatform || '').trim().toLowerCase(),
  };
  const platforms = [
    ['twitch', 'Twitch'],
    ['kick', 'Kick'],
    ['youtube', 'YouTube'],
  ];
  const results = [];
  for (const [platform, name] of platforms) {
    const settings = platformSettings[platform];
    if (settings && settings.enabled === false) {
      results.push({ platform: name, ok: true, skipped: true, message: 'Plataforma desativada.' });
      continue;
    }
    try {
      const update = await accounts.updateTransmission(platform, { ...input, platformSettings: settings });
      results.push({
        platform: name,
        ok: true,
        message: String(update?.message || 'Informações atualizadas.'),
        ...(update?.broadcastId ? { broadcastId: String(update.broadcastId) } : {}),
      });
    } catch (err) {
      results.push({
        platform: name,
        ok: false,
        message: err.message,
        ...(platform === 'youtube' && settings?.broadcastId
          ? { broadcastId: String(settings.broadcastId) }
          : {}),
      });
    }
  }
  res.json({ results });
});

router.post('/chat/send', async (req, res) => {
  const message = String(req.body?.message || '').trim();
  const target = String(req.body?.target || 'all').toLowerCase();
  if (!message || message.length > 500) return res.status(400).json({ error: 'Digite uma mensagem com até 500 caracteres.' });
  if (!['all', 'twitch', 'kick', 'youtube', 'tiktok', 'facebook', 'custom'].includes(target)) return res.status(400).json({ error: 'Chat de destino inválido.' });
  const config = readConfig();
  const targets = target === 'all' ? ['twitch', 'kick', 'youtube'] : [target];
  if (target === 'tiktok') return res.status(400).json({ error: 'O envio autenticado para TikTok ainda não está disponível.' });

  const results = await Promise.all(targets.map(async (platform) => {
    try {
      if (platform === 'twitch') {
        const senderId = await accounts.userId('twitch');
        let broadcasterId = senderId;
        const channel = String(config.twitch?.channel || '').trim();
        if (channel) {
          const users = await accounts.request('twitch', `/users?login=${encodeURIComponent(channel)}`);
          if (!users.data?.[0]) throw new Error('Canal da Twitch não encontrado.');
          broadcasterId = users.data[0].id;
        }
        const result = await accounts.request('twitch', '/chat/messages', {
          method: 'POST',
          body: JSON.stringify({ broadcaster_id: broadcasterId, sender_id: senderId, message }),
        });
        if (!result.data?.[0]?.is_sent) throw new Error(result.data?.[0]?.drop_reason?.message || 'A Twitch recusou a mensagem.');
      } else if (platform === 'kick') {
        const channel = String(config.kick?.channel || '').trim();
        if (!channel) throw new Error('Configure o canal da Kick antes de enviar.');
        const channels = await accounts.request('kick', `/channels?slug=${encodeURIComponent(channel)}`);
        const broadcasterId = channels.data?.[0]?.broadcaster_user_id;
        if (!broadcasterId) throw new Error('Canal da Kick não encontrado.');
        const result = await accounts.request('kick', '/chat', {
          method: 'POST',
          body: JSON.stringify({ broadcaster_user_id: broadcasterId, content: message, type: 'user' }),
        });
        if (result.data?.is_sent === false) throw new Error('A Kick recusou a mensagem.');
      } else if (platform === 'youtube') {
        const { liveChatId } = await accounts.youtubeActiveChat();
        if (!liveChatId) throw new Error('Não há transmissão ao vivo com chat ativo no YouTube.');
        await accounts.request('youtube', '/liveChat/messages?part=snippet', {
          method: 'POST',
          body: JSON.stringify({
            snippet: {
              liveChatId,
              type: 'textMessageEvent',
              textMessageDetails: { messageText: message },
            },
          }),
        });
      } else {
        const platformName = platform === 'facebook' ? 'Facebook' : platform === 'custom' ? 'RTMP personalizado' : 'TikTok';
        throw new Error(`O envio autenticado para ${platformName} ainda não está disponível.`);
      }
      return { platform, ok: true, message: 'Mensagem enviada.' };
    } catch (error) {
      return { platform, ok: false, message: error.message };
    }
  }));
  res.json({ results });
});

router.get('/chat/viewers', async (_req, res) => {
  const viewers = { twitch: null, kick: null, youtube: null };
  const requests = [
    (async () => {
      try {
        if (!accounts.status().connected) return;
        const userId = await accounts.userId('twitch');
        const result = await accounts.request('twitch', `/streams?user_id=${encodeURIComponent(userId)}`);
        viewers.twitch = Number(result.data?.[0]?.viewer_count || 0);
      } catch (_) {}
    })(),
    (async () => {
      try {
        if (!accounts.kickStatus().connected) return;
        const result = await accounts.request('kick', '/channels');
        const stream = result.data?.[0]?.stream;
        viewers.kick = Number(stream?.viewer_count || 0);
      } catch (_) {}
    })(),
    (async () => {
      try {
        if (!accounts.youtubeStatus().connected) return;
        const result = await accounts.request(
          'youtube', '/liveBroadcasts?part=status&broadcastStatus=active&mine=true&maxResults=50',
        );
        const active = (result.items || []).find((broadcast) =>
          ['live', 'testing'].includes(String(broadcast.status?.lifeCycleStatus || '').toLowerCase()),
        );
        const count = active?.status?.concurrentViewers;
        viewers.youtube = count != null && Number.isFinite(Number(count)) ? Number(count) : null;
      } catch (_) {}
    })(),
  ];
  await Promise.all(requests);
  res.json({ viewers });
});

router.post('/moderation/action', async (req, res) => {
  const platform = String(req.body?.platform || '').toLowerCase();
  const action = String(req.body?.action || 'timeout').toLowerCase();
  const user = String(req.body?.user || '').trim();
  const duration = Math.min(1209600, Math.max(1, Number(req.body?.duration) || 600));
  if (!['twitch', 'kick', 'youtube'].includes(platform)) return res.status(400).json({ error: 'Plataforma não suportada para moderação.' });
  if (!['timeout', 'ban', 'unban'].includes(action)) return res.status(400).json({ error: 'Ação de moderação inválida.' });
  if (!user) return res.status(400).json({ error: 'Informe o identificador do usuário.' });

  try {
    if (platform === 'twitch') {
      const broadcasterId = await accounts.userId();
      const users = await accounts.request(`/users?login=${encodeURIComponent(user.replace(/^@/, ''))}`);
      const targetId = users.data?.[0]?.id;
      if (!targetId) throw new Error('Usuário não encontrado na Twitch.');
      const query = `broadcaster_id=${encodeURIComponent(broadcasterId)}&moderator_id=${encodeURIComponent(broadcasterId)}`;
      if (action === 'unban') {
        await accounts.request(`/moderation/bans?${query}&user_id=${encodeURIComponent(targetId)}`, { method: 'DELETE' });
      } else {
        const data = { user_id: targetId };
        if (action === 'timeout') data.duration = duration;
        await accounts.request(`/moderation/bans?${query}`, { method: 'POST', body: JSON.stringify({ data }) });
      }
    } else if (platform === 'kick') {
      if (!/^\d+$/.test(user)) throw new Error('A Kick exige o ID numérico do usuário; o nome não pode ser convertido pela API oficial.');
      const broadcasterId = await accounts.userId('kick');
      const body = { broadcaster_user_id: Number(broadcasterId), user_id: Number(user) };
      if (!Number.isSafeInteger(body.user_id) || body.user_id < 1) throw new Error('ID de usuário da Kick inválido.');
      if (action === 'unban') {
        await accounts.request('kick', '/moderation/bans', { method: 'DELETE', body: JSON.stringify(body) });
      } else {
        if (action === 'timeout') body.duration = Math.min(10080, Math.max(1, Math.ceil(duration / 60)));
        await accounts.request('kick', '/moderation/bans', { method: 'POST', body: JSON.stringify(body) });
      }
    } else {
      const activeChat = await accounts.youtubeActiveChat();
      if (!activeChat.liveChatId) throw new Error('Não há uma live do YouTube ativa com chat para moderar.');
      let channelId = user.trim();
      if (!/^UC[\w-]{20,}$/.test(channelId)) {
        const handle = channelId.replace(/^@/, '');
        const found = await accounts.request('youtube', `/channels?part=id&forHandle=${encodeURIComponent(handle)}`);
        channelId = String(found.items?.[0]?.id || '');
      }
      if (!/^UC[\w-]{20,}$/.test(channelId)) throw new Error('Não encontrei o canal do YouTube. Informe um @handle válido ou o ID do canal (UC...).');
      if (action === 'unban') {
        let pageToken = '';
        let banId = '';
        do {
          const query = new URLSearchParams({ part: 'id,snippet', liveChatId: activeChat.liveChatId, maxResults: '50' });
          if (pageToken) query.set('pageToken', pageToken);
          const bans = await accounts.request('youtube', `/liveChat/bans?${query.toString()}`);
          banId = (bans.items || []).find((ban) => ban.snippet?.bannedUserDetails?.channelId === channelId)?.id || '';
          pageToken = bans.nextPageToken || '';
        } while (!banId && pageToken);
        if (!banId) throw new Error('Não encontrei um ban ativo desse canal no chat do YouTube.');
        await accounts.request('youtube', `/liveChat/bans?id=${encodeURIComponent(banId)}`, { method: 'DELETE' });
      } else {
        const temporary = action === 'timeout';
        await accounts.request('youtube', '/liveChat/bans?part=snippet', {
          method: 'POST',
          body: JSON.stringify({
            snippet: {
              liveChatId: activeChat.liveChatId,
              type: temporary ? 'temporary' : 'permanent',
              ...(temporary ? { banDurationSeconds: duration } : {}),
              bannedUserDetails: { channelId },
            },
          }),
        });
      }
    }
    res.json({ ok: true });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/twitch/chat-settings', async (_req, res) => {
  try {
    const broadcasterId = await accounts.userId();
    const query = `broadcaster_id=${encodeURIComponent(broadcasterId)}&moderator_id=${encodeURIComponent(broadcasterId)}`;
    const result = await accounts.request(`/chat/settings?${query}`);
    res.json({ data: result.data?.[0] || {} });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.patch('/twitch/chat-settings', async (req, res) => {
  try {
    const broadcasterId = await accounts.userId();
    const query = `broadcaster_id=${encodeURIComponent(broadcasterId)}&moderator_id=${encodeURIComponent(broadcasterId)}`;
    const slowSeconds = Math.min(120, Math.max(3, Number(req.body?.slowModeWaitTime) || 30));
    const body = {
      slow_mode: Boolean(req.body?.slowMode),
      follower_mode: Boolean(req.body?.followerMode),
      subscriber_mode: Boolean(req.body?.subscriberMode),
      emote_mode: Boolean(req.body?.emoteMode),
    };
    if (body.slow_mode) body.slow_mode_wait_time = slowSeconds;
    if (body.follower_mode) body.follower_mode_duration = 0;
    const result = await accounts.request(`/chat/settings?${query}`, { method: 'PATCH', body: JSON.stringify(body) });
    res.json({ data: result.data?.[0] || body });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

return router;
}

module.exports = { createApiRouter };
