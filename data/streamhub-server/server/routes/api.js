const express = require('express');
const fetch = require('node-fetch');
const { readConfig, writeConfig } = require('../config-store');

function createApiRouter(accounts) {
const router = express.Router();

function localBoxArtUrl(req, remoteUrl) {
  if (!remoteUrl) return '';
  return `${req.protocol}://${req.get('host')}/api/twitch/category-art?url=${encodeURIComponent(remoteUrl)}`;
}

async function activeYoutubeBroadcast() {
  const result = await accounts.youtubeRequest(
    '/liveBroadcasts?part=id,snippet,status,contentDetails&mine=true&maxResults=50'
  );
  const priority = new Map([
    ['live', 0],
    ['testing', 1],
    ['ready', 2],
    ['created', 3],
  ]);
  const broadcasts = (result.items || [])
    .filter((item) => priority.has(item.status?.lifeCycleStatus))
    .sort((left, right) => {
      const rank = priority.get(left.status.lifeCycleStatus) - priority.get(right.status.lifeCycleStatus);
      if (rank !== 0) return rank;
      return String(right.snippet?.scheduledStartTime || '').localeCompare(
        String(left.snippet?.scheduledStartTime || '')
      );
    });
  if (broadcasts[0]) return broadcasts[0];
  throw new Error('Nenhuma live ativa ou agendada foi encontrada no YouTube.');
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

router.post('/accounts/twitch/connect', async (_req, res) => {
  try { res.json(await accounts.start()); }
  catch (err) { res.status(500).json({ error: err.message }); }
});

router.get('/accounts/twitch/connect/:flowId', async (req, res) => {
  try { res.json(await accounts.poll(req.params.flowId)); }
  catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/twitch/channel', async (req, res) => {
  try {
    const broadcasterId = await accounts.userId();
    const result = await accounts.request(`/channels?broadcaster_id=${encodeURIComponent(broadcasterId)}`);
    const channel = result.data?.[0];
    if (!channel) throw new Error('A Twitch não retornou as informações do canal.');
    let boxArtUrl = '';
    if (channel.game_id) {
      try {
        const games = await accounts.request(`/games?id=${encodeURIComponent(channel.game_id)}`);
        const remoteArt = String(games.data?.[0]?.box_art_url || '')
          .replace('{width}', '104').replace('{height}', '144');
        boxArtUrl = localBoxArtUrl(req, remoteArt);
      } catch (error) {
        console.warn('[streamhub] não foi possível carregar a capa da categoria atual:', error.message);
      }
    }
    res.json({
      title: channel.title || '', gameId: channel.game_id || '', gameName: channel.game_name || '',
      boxArtUrl,
      language: channel.broadcaster_language || 'pt', tags: channel.tags || [],
      classificationLabels: (channel.content_classification_labels || []).filter((item) => item.is_enabled).map((item) => item.id),
    });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/accounts/youtube/status', (_req, res) => {
  res.json(accounts.youtubeStatus());
});

router.post('/accounts/youtube/connect', async (req, res) => {
  try {
    const port = readConfig().server?.port || 3000;
    const redirectUri = `http://127.0.0.1:${port}/api/accounts/youtube/callback`;
    res.json(await accounts.startYoutube({ clientId: req.body?.clientId, clientSecret: req.body?.clientSecret, redirectUri }));
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/accounts/youtube/callback', async (req, res) => {
  try {
    if (req.query.error) throw new Error(String(req.query.error_description || req.query.error));
    await accounts.finishYoutube(req.query.state, req.query.code);
    const config = readConfig();
    config.youtube = { ...(config.youtube || {}), enabled: true };
    writeConfig(config);
    res.type('html').send('<!doctype html><meta charset="utf-8"><title>StreamHub</title><body style="background:#080c14;color:#fff;font:16px sans-serif;padding:32px"><h2>YouTube conectado ao StreamHub</h2><p>Você pode fechar esta janela e voltar ao OBS.</p></body>');
  } catch (err) {
    res.status(400).type('html').send(`<!doctype html><meta charset="utf-8"><title>StreamHub</title><body style="font:16px sans-serif;padding:32px"><h2>Não foi possível conectar</h2><p>${String(err.message).replace(/[<>&"]/g, '')}</p></body>`);
  }
});

router.get('/twitch/category-art', async (req, res) => {
  try {
    const remoteUrl = new URL(String(req.query.url || ''));
    if (remoteUrl.protocol !== 'https:' || remoteUrl.hostname !== 'static-cdn.jtvnw.net')
      throw new Error('URL de capa inválida.');
    const response = await fetch(remoteUrl.toString(), { timeout: 10000 });
    if (!response.ok) throw new Error(`A Twitch não retornou a capa (HTTP ${response.status}).`);
    res.set('Content-Type', response.headers.get('content-type') || 'image/jpeg');
    res.set('Cache-Control', 'public, max-age=86400');
    response.body.pipe(res);
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
      boxArtUrl: localBoxArtUrl(req, String(item.box_art_url || '').replace('{width}', '52').replace('{height}', '72')),
    })) });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/broadcast/current', async (req, res) => {
  try {
    const broadcasterId = await accounts.userId();
    const result = await accounts.request(`/channels?broadcaster_id=${encodeURIComponent(broadcasterId)}`);
    const channel = result.data?.[0];
    if (!channel) throw new Error('A Twitch não retornou as informações do canal.');
    let boxArtUrl = '';
    if (channel.game_id) {
      try {
        const games = await accounts.request(`/games?id=${encodeURIComponent(channel.game_id)}`);
        const remoteArt = String(games.data?.[0]?.box_art_url || '').replace('{width}', '104').replace('{height}', '144');
        boxArtUrl = localBoxArtUrl(req, remoteArt);
      } catch (_) {}
    }
    return res.json({
      platform: 'twitch', title: channel.title || '', gameId: channel.game_id || '', gameName: channel.game_name || '', boxArtUrl,
      language: channel.broadcaster_language || 'pt', tags: channel.tags || [],
      classificationLabels: (channel.content_classification_labels || []).filter((item) => item.is_enabled).map((item) => item.id),
    });
  } catch (twitchError) {
    if (!accounts.youtubeStatus().connected) return res.status(400).json({ error: twitchError.message });
    try {
      const broadcast = await activeYoutubeBroadcast();
      const current = await accounts.youtubeRequest(`/videos?part=snippet,status&id=${encodeURIComponent(broadcast.id)}`);
      const video = current.items?.[0];
      if (!video) throw new Error('A live do YouTube não foi encontrada.');
      return res.json({
        platform: 'youtube', title: video.snippet?.title || '', gameId: '',
        gameName: video.snippet?.categoryId === '20' ? 'Jogos' : '', boxArtUrl: '',
        language: video.snippet?.defaultLanguage || 'pt', tags: video.snippet?.tags || [],
        classificationLabels: [], visibility: video.status?.privacyStatus || 'public',
      });
    } catch (youtubeError) { return res.status(400).json({ error: youtubeError.message }); }
  }
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

router.post('/broadcast/apply', async (req, res) => {
  const title = String(req.body?.title || '').trim();
  const category = String(req.body?.category || '').trim();
  const categoryId = String(req.body?.categoryId || '').trim();
  if (!title) return res.status(400).json({ error: 'Informe o título da transmissão.' });
  if (title.length > 140) return res.status(400).json({ error: 'O título pode ter no máximo 140 caracteres.' });
  const results = [];
  try {
    const broadcasterId = await accounts.userId();
    const body = { title };
    if (categoryId) {
      body.game_id = categoryId;
    } else if (category) {
      const found = await accounts.request(`/search/categories?query=${encodeURIComponent(category)}&first=10`);
      const exact = (found.data || []).find((item) => item.name.toLowerCase() === category.toLowerCase());
      if (!exact) throw new Error('Selecione uma categoria existente da Twitch.');
      body.game_id = exact.id;
    }
    if (Array.isArray(req.body?.tags)) body.tags = req.body.tags.map((tag) => String(tag).trim()).filter(Boolean).slice(0, 10);
    if (typeof req.body?.language === 'string' && req.body.language) body.broadcaster_language = req.body.language;
    const classificationIds = ['ProfanityVulgarity', 'ViolentGraphic', 'DebatedSocialIssuesAndPolitics', 'Gambling'];
    if (typeof req.body?.classification === 'string') {
      body.content_classification_labels = classificationIds.map((id) => ({ id, is_enabled: id === req.body.classification }));
    }
    await accounts.request(`/channels?broadcaster_id=${encodeURIComponent(broadcasterId)}`, { method: 'PATCH', body: JSON.stringify(body) });
    const ignored = [];
    if (req.body?.notification) ignored.push('notification');
    if (Number(req.body?.visibility) !== 0) ignored.push('visibility');
    const message = ignored.length
      ? `Informações atualizadas. Campos ignorados pela API da Twitch: ${ignored.join(', ')}.`
      : 'Informações atualizadas.';
    results.push({ platform: 'Twitch', ok: true, message, ignored });
  } catch (err) {
    results.push({ platform: 'Twitch', ok: false, message: err.message });
  }
  const config = readConfig();
  if (accounts.youtubeStatus().connected) {
    try {
      const broadcast = await activeYoutubeBroadcast();
      const current = await accounts.youtubeRequest(`/videos?part=snippet,status&id=${encodeURIComponent(broadcast.id)}`);
      const video = current.items?.[0];
      if (!video) throw new Error('A live ativa não foi encontrada como vídeo do YouTube.');
      const snippet = {
        title,
        description: video.snippet?.description || '',
        categoryId: category ? '20' : (video.snippet?.categoryId || '20'),
        tags: Array.isArray(req.body?.tags) ? req.body.tags.map((tag) => String(tag).trim()).filter(Boolean).slice(0, 30) : (video.snippet?.tags || []),
      };
      if (req.body?.language && req.body.language !== 'other') snippet.defaultLanguage = req.body.language;
      if (video.snippet?.defaultAudioLanguage) snippet.defaultAudioLanguage = video.snippet.defaultAudioLanguage;
      const privacy = ['public', 'unlisted', 'private'][Number(req.body?.visibility)] || video.status?.privacyStatus || 'public';
      const preservedStatus = {};
      for (const key of ['license', 'embeddable', 'publicStatsViewable', 'publishAt', 'containsSyntheticMedia']) {
        if (video.status?.[key] !== undefined) preservedStatus[key] = video.status[key];
      }
      await accounts.youtubeRequest('/videos?part=snippet,status', {
        method: 'PUT',
        body: JSON.stringify({ id: broadcast.id, snippet, status: { ...preservedStatus, privacyStatus: privacy, selfDeclaredMadeForKids: Boolean(video.status?.selfDeclaredMadeForKids) } }),
      });
      results.push({ platform: 'YouTube', ok: true, message: 'Título, categoria Gaming, tags, idioma e visibilidade atualizados.' });
    } catch (err) { results.push({ platform: 'YouTube', ok: false, message: err.message }); }
  } else if (config.youtube?.enabled) {
    results.push({ platform: 'YouTube', ok: false, message: 'Conecte a conta do YouTube.' });
  }
  for (const [key, name] of [['kick', 'Kick'], ['tiktok', 'TikTok']]) {
    if (config[key]?.enabled) results.push({ platform: name, ok: false, message: 'OAuth desta plataforma ainda não está conectado.' });
  }
  res.json({ results });
});

router.post('/chat/send', async (req, res) => {
  const message = String(req.body?.message || '').trim();
  const target = String(req.body?.target || 'all').toLowerCase();
  if (!message || message.length > 500) return res.status(400).json({ error: 'Digite uma mensagem com até 500 caracteres.' });
  if (!['all', 'twitch', 'kick', 'youtube', 'tiktok'].includes(target)) return res.status(400).json({ error: 'Chat de destino inválido.' });
  const results = [];
  const config = readConfig();
  if (target === 'all' || target === 'twitch') {
    try {
      const senderId = await accounts.userId();
      let broadcasterId = senderId;
      const channel = String(config.twitch?.channel || '').trim();
      if (channel) {
        const users = await accounts.request(`/users?login=${encodeURIComponent(channel)}`);
        if (!users.data?.[0]) throw new Error('Canal da Twitch não encontrado.');
        broadcasterId = users.data[0].id;
      }
      const result = await accounts.request('/chat/messages', { method: 'POST', body: JSON.stringify({ broadcaster_id: broadcasterId, sender_id: senderId, message }) });
      if (!result.data?.[0]?.is_sent) throw new Error(result.data?.[0]?.drop_reason?.message || 'A Twitch recusou a mensagem.');
      results.push({ platform: 'Twitch', ok: true });
    } catch (err) { results.push({ platform: 'Twitch', ok: false, message: err.message }); }
  }
  if (target === 'all' || target === 'youtube') {
    if (!accounts.youtubeStatus().connected) {
      if (target === 'youtube') results.push({ platform: 'YouTube', ok: false, message: 'Conecte a conta do YouTube.' });
    } else {
      try {
        const broadcast = await activeYoutubeBroadcast();
        const liveChatId = broadcast.snippet?.liveChatId;
        if (!liveChatId) throw new Error('A live do YouTube ainda não possui um chat ativo.');
        await accounts.youtubeRequest('/liveChat/messages?part=snippet', {
          method: 'POST',
          body: JSON.stringify({ snippet: { liveChatId, type: 'textMessageEvent', textMessageDetails: { messageText: message } } }),
        });
        results.push({ platform: 'YouTube', ok: true });
      } catch (err) { results.push({ platform: 'YouTube', ok: false, message: err.message }); }
    }
  }
  if (target === 'kick' || target === 'tiktok') results.push({ platform: target, ok: false, message: `O envio autenticado para ${target} ainda não está conectado.` });
  const successes = results.filter((item) => item.ok);
  if (!successes.length) return res.status(400).json({ error: results.map((item) => `${item.platform}: ${item.message}`).join('\n') || 'Nenhum chat conectado.' });
  res.json({ results });
});

router.post('/twitch/moderation', async (req, res) => {
  const action = String(req.body?.action || 'timeout');
  const login = String(req.body?.user || '').trim().replace(/^@/, '');
  if (!login) return res.status(400).json({ error: 'Informe o usuário.' });
  try {
    const broadcasterId = await accounts.userId();
    const users = await accounts.request(`/users?login=${encodeURIComponent(login)}`);
    const targetId = users.data?.[0]?.id;
    if (!targetId) throw new Error('Usuário não encontrado.');
    const query = `broadcaster_id=${encodeURIComponent(broadcasterId)}&moderator_id=${encodeURIComponent(broadcasterId)}`;
    if (action === 'unban') {
      await accounts.request(`/moderation/bans?${query}&user_id=${encodeURIComponent(targetId)}`, { method: 'DELETE' });
    } else {
      const data = { user_id: targetId };
      if (action === 'timeout') data.duration = Math.min(1209600, Math.max(1, Number(req.body?.duration) || 600));
      await accounts.request(`/moderation/bans?${query}`, { method: 'POST', body: JSON.stringify({ data }) });
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

router.get('/twitch/rewards', async (_req, res) => {
  try {
    const broadcasterId = await accounts.userId();
    const result = await accounts.request(`/channel_points/custom_rewards?broadcaster_id=${encodeURIComponent(broadcasterId)}&only_manageable_rewards=true`);
    res.json({ data: result.data || [] });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.post('/twitch/rewards', async (req, res) => {
  const title = String(req.body?.title || '').trim();
  const cost = Number(req.body?.cost);
  if (!title || title.length > 45 || !Number.isInteger(cost) || cost < 1) return res.status(400).json({ error: 'Informe título e custo válido para a recompensa.' });
  try {
    const broadcasterId = await accounts.userId();
    const body = { title, cost, prompt: String(req.body?.prompt || '').trim().slice(0, 200), is_enabled: true };
    const result = await accounts.request(`/channel_points/custom_rewards?broadcaster_id=${encodeURIComponent(broadcasterId)}`, { method: 'POST', body: JSON.stringify(body) });
    res.json({ data: result.data?.[0] });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.get('/twitch/rewards/:rewardId/redemptions', async (req, res) => {
  const rewardId = String(req.params.rewardId || '').trim();
  if (!rewardId) return res.status(400).json({ error: 'Selecione uma recompensa.' });
  try {
    const broadcasterId = await accounts.userId();
    const query = `broadcaster_id=${encodeURIComponent(broadcasterId)}&reward_id=${encodeURIComponent(rewardId)}&status=UNFULFILLED&sort=OLDEST&first=50`;
    const result = await accounts.request(`/channel_points/custom_rewards/redemptions?${query}`);
    res.json({ data: result.data || [] });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

router.patch('/twitch/rewards/:rewardId/redemptions/:redemptionId', async (req, res) => {
  const rewardId = String(req.params.rewardId || '').trim();
  const redemptionId = String(req.params.redemptionId || '').trim();
  const status = String(req.body?.status || '').toUpperCase();
  if (!rewardId || !redemptionId || !['FULFILLED', 'CANCELED'].includes(status)) {
    return res.status(400).json({ error: 'Resgate ou estado inválido.' });
  }
  try {
    const broadcasterId = await accounts.userId();
    const query = `broadcaster_id=${encodeURIComponent(broadcasterId)}&reward_id=${encodeURIComponent(rewardId)}&id=${encodeURIComponent(redemptionId)}`;
    const result = await accounts.request(`/channel_points/custom_rewards/redemptions?${query}`, {
      method: 'PATCH', body: JSON.stringify({ status }),
    });
    res.json({ data: result.data?.[0] });
  } catch (err) { res.status(400).json({ error: err.message }); }
});

return router;
}

module.exports = { createApiRouter };
