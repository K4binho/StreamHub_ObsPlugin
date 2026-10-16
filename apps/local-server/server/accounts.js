const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const fetch = require('node-fetch');
const { config: loadEnv } = require('dotenv');

loadEnv({ path: path.join(__dirname, '..', '.env') });

// Client IDs are public. Client Secrets stay in accounts-private.json and never
// belong in source, config.json, logs, responses, or documentation.
const TWITCH_CLIENT_ID = 'qujbqnms1y167wc7dpt4t7v8k0dwtj';
const TWITCH_SCOPES = [
  'user:read:chat', 'user:write:chat', 'channel:manage:broadcast',
  'channel:read:stream_key', 'moderator:manage:banned_users',
  'moderator:manage:chat_messages', 'moderator:manage:chat_settings',
  'moderator:manage:automod',
].join(' ');
const TWITCH_DEVICE_URL = 'https://id.twitch.tv/oauth2/device';
const TWITCH_TOKEN_URL = 'https://id.twitch.tv/oauth2/token';
const TWITCH_USERS_URL = 'https://api.twitch.tv/helix/users';

const KICK_AUTHORIZE_URL = 'https://id.kick.com/oauth/authorize';
const KICK_TOKEN_URL = 'https://id.kick.com/oauth/token';
const KICK_API_BASE = 'https://api.kick.com/public/v1';
const KICK_REDIRECT_URI = 'http://localhost:605/api/accounts/kick/callback';
const KICK_SCOPES = [
  'user:read', 'channel:read', 'channel:write', 'streamkey:read', 'chat:write', 'moderation:ban',
].join(' ');

const YOUTUBE_AUTHORIZE_URL = 'https://accounts.google.com/o/oauth2/v2/auth';
const YOUTUBE_TOKEN_URL = 'https://oauth2.googleapis.com/token';
const YOUTUBE_API_BASE = 'https://www.googleapis.com/youtube/v3';
const YOUTUBE_REDIRECT_URI = 'http://localhost:605/api/accounts/youtube/callback';
const YOUTUBE_SCOPE = 'https://www.googleapis.com/auth/youtube.force-ssl';
const DEFAULT_OAUTH_BROKER_URL =
  'https://streamhub-oauth-broker.onrender.com';

const SHARED_OAUTH_BROKER_URL = String(
  process.env.STREAMHUB_OAUTH_BROKER_URL ||
  DEFAULT_OAUTH_BROKER_URL
)
  .trim()
  .replace(/\/+$/, '');


function createAccounts(directory, request = fetch) {
  const file = path.join(directory, 'accounts-private.json');
  const pending = new Map();
  const refreshing = new Map();
  const read = (name) => fs.existsSync(name) ? JSON.parse(fs.readFileSync(name, 'utf8')) : {};
  let accounts = read(file);

  const save = () => {
    fs.writeFileSync(`${file}.tmp`, JSON.stringify(accounts), { mode: 0o600 });
    fs.renameSync(`${file}.tmp`, file);
  };

  const postForm = async (url, fields, platform) => {
    const response = await request(url, { method: 'POST', body: new URLSearchParams(fields), timeout: 15000 });
    const result = await response.json();
    if (!response.ok) {
      const error = new Error(result.message || result.error_description || `${platform} recusou a operação (HTTP ${response.status}).`);
      error.code = result.message || result.error;
      throw error;
    }
    return result;
  };

  const requestJson = async (url, options = {}, platform = 'Twitch') => {
    const response = await request(url, { ...options, timeout: 15000 });
    if (!response.ok) {
      let detail = '';
      try {
        const result = await response.json();
        detail = result.message || result.error?.message || result.error?.errors?.[0]?.reason || result.error || '';
      } catch (_) {
        // Keep platform errors safe when response is not JSON.
      }
      throw new Error(`${platform} recusou a operação (HTTP ${response.status})${detail ? `: ${detail}` : ''}.`);
    }
    return response.status === 204 ? {} : response.json();
  };

  const exchange = async (fields) => {
    const result = await postForm(TWITCH_TOKEN_URL, { ...fields, client_id: TWITCH_CLIENT_ID }, 'A Twitch');
    if (!result.access_token) throw new Error('A Twitch não retornou uma autorização válida.');
    return { ...result, expiresAt: Date.now() + Number(result.expires_in || 3600) * 1000 };
  };

  const identify = async (token) => {
    const profile = await requestJson(TWITCH_USERS_URL, { headers: { Authorization: `Bearer ${token.access_token}`, 'Client-Id': TWITCH_CLIENT_ID } });
    const user = profile.data?.[0];
    if (!user) throw new Error('Não foi possível identificar a conta autorizada na Twitch.');
    return { ...token, userId: String(user.id), name: user.login || user.display_name };
  };

  const access = async () => {
    let account = accounts.twitch;
    if (!account) throw new Error('Conecte sua conta da Twitch primeiro.');
    if (account.expiresAt <= Date.now() + 60000) {
      if (!refreshing.has('twitch')) {
        refreshing.set('twitch', (async () => {
          const token = await exchange({ grant_type: 'refresh_token', refresh_token: account.refresh_token });
          accounts.twitch = { ...account, ...token, refresh_token: token.refresh_token || account.refresh_token };
          save();
        })().finally(() => refreshing.delete('twitch')));
      }
      await refreshing.get('twitch');
      account = accounts.twitch;
    }
    return account;
  };

  const twitchBroadcastContent = async () => {
    const account = await access();
    const headers = {
      Authorization: `Bearer ${account.access_token}`,
      'Client-Id': TWITCH_CLIENT_ID,
      Accept: 'application/json',
    };
    const channelResult = await requestJson(
      `https://api.twitch.tv/helix/channels?broadcaster_id=${encodeURIComponent(account.userId)}`,
      { headers },
      'A Twitch',
    );
    const channel = channelResult.data?.[0];
    if (!channel) throw new Error('A Twitch não retornou os dados do canal autorizado.');
    const has = (object, key) => Object.prototype.hasOwnProperty.call(object, key);
    return {
      platform: 'twitch', name: account.name || '',
      title: String(channel.title || ''),
      categoryId: String(channel.game_id || ''),
      category: String(channel.game_name || ''),
      tags: Array.isArray(channel.tags) ? channel.tags : [],
      language: String(channel.broadcaster_language || ''),
      classificationLabels: (channel.content_classification_labels || [])
        .filter((item) => typeof item === 'string' || item?.is_enabled)
        .map((item) => typeof item === 'string' ? item : item.id).filter(Boolean),
      fieldAvailability: {
        title: has(channel, 'title'),
        category: has(channel, 'game_name'),
        tags: has(channel, 'tags'),
        language: has(channel, 'broadcaster_language'),
      },
    };
  };

  const twitchTransmission = async () => {
    const account = await access();
    const headers = {
      Authorization: `Bearer ${account.access_token}`,
      'Client-Id': TWITCH_CLIENT_ID,
      Accept: 'application/json',
    };
    const result = await requestJson(
      `https://api.twitch.tv/helix/streams/key?broadcaster_id=${encodeURIComponent(account.userId)}`,
      { headers },
      'A Twitch',
    );
    const streamKey = String(result.data?.[0]?.stream_key || '').trim();
    const server = 'rtmps://live.twitch.tv/app';
    if (!server || !streamKey) {
      throw new Error('A Twitch não forneceu servidor RTMP e stream key autorizados.');
    }
    return { ...(await twitchBroadcastContent()), server, streamKey };
  };

  const envCredentials = {
    kick: {
      clientId: String(process.env.KICK_CLIENT_ID || '').trim(),
      clientSecret: String(process.env.KICK_CLIENT_SECRET || '').trim(),
    },
    youtube: {
      clientId: String(process.env.YOUTUBE_CLIENT_ID || '').trim(),
      clientSecret: String(process.env.YOUTUBE_CLIENT_SECRET || '').trim(),
    },
  };

  const platformConfig = (platform) => {
    const account = accounts[platform] || {};
    const env = envCredentials[platform] || {};
    const manualClientId = String(account.clientId || '').trim();
    const manualClientSecret = String(account.clientSecret || '').trim();
    const clientId = manualClientId || env.clientId;
    const clientSecret = manualClientSecret || env.clientSecret;
    const hasManual = Boolean(manualClientId || manualClientSecret);
    const source = !clientId || !clientSecret
      ? 'none'
      : (hasManual ? (manualClientId && manualClientSecret ? 'manual' : 'mixed') : 'env');
    return { clientId, clientSecret, source };
  };

  const storedCredentials = (platform) => {
    const account = accounts[platform] || {};
    const credentials = {};
    if (String(account.clientId || '').trim()) credentials.clientId = String(account.clientId).trim();
    if (String(account.clientSecret || '').trim()) credentials.clientSecret = String(account.clientSecret).trim();
    return credentials;
  };

  const sharedClientId = (platform) => String(
    process.env[platform === 'kick' ? 'STREAMHUB_KICK_CLIENT_ID' : 'STREAMHUB_YOUTUBE_CLIENT_ID'] || '',
  ).trim();

  const sharedConfigured = () => /^https:\/\//i.test(SHARED_OAUTH_BROKER_URL);

  const brokerRequest = async (method, pathName, body, platform, headers = {}) => {
    if (!sharedConfigured()) throw new Error(`OAuth compartilhado ${platform} indisponível.`);
    let response;
    try {
      response = await request(`${SHARED_OAUTH_BROKER_URL}${pathName}`, {
        method,
        headers: { Accept: 'application/json', ...headers },
        ...(method === 'GET' ? {} : {
          headers: { 'Content-Type': 'application/json', Accept: 'application/json', ...headers },
          body: JSON.stringify(body || {}),
        }),
        timeout: 15000,
      });
    } catch (_) {
      throw new Error(`OAuth compartilhado ${platform} indisponível.`);
    }
    let result = {};
    try {
      result = await response.json();
    } catch (_) {
      result = {};
    }
    if (!response.ok) {
      throw new Error(String(result.error || `OAuth compartilhado ${platform} recusou a operação.`));
    }
    return result;
  };

  const sharedStart = async (platform) => {
    const result = await brokerRequest(
      'POST',
      `/v1/oauth/${platform}/start`,
      {},
      platform,
    );
    const transactionId = String(result.transactionId || '').trim();
    const claimToken = String(result.claimToken || '').trim();
    const authorizationUri = String(result.authorizationUri || '').trim();
    if (!transactionId || !claimToken || !/^https:\/\//i.test(authorizationUri)) {
      throw new Error(`OAuth compartilhado ${platform} retornou resposta inválida.`);
    }
    const flowId = crypto.randomBytes(24).toString('hex');
    pending.set(`${platform}:${flowId}`, {
      flowId,
      mode: 'shared',
      platform,
      transactionId,
      claimToken,
      expiresAt: Date.now() + Math.min(10 * 60 * 1000, Math.max(60000, Number(result.expiresIn || 600) * 1000)),
    });
    return { flowId, authorizationUri };
  };

  const sharedPoll = async (platform, flowId) => {
    const key = `${platform}:${flowId}`;
    const flow = pending.get(key);
    if (!flow || flow.mode !== 'shared' || flow.expiresAt <= Date.now()) {
      pending.delete(key);
      throw new Error(`Esta autorização ${platform} expirou. Clique em Conectar novamente.`);
    }
    if (flow.stateResult) {
      pending.delete(key);
      return flow.stateResult;
    }
    const result = await brokerRequest(
      'GET',
      `/v1/oauth/${platform}/poll/${encodeURIComponent(flow.transactionId)}`,
      {},
      platform,
      { Authorization: `Bearer ${flow.claimToken}` },
    );
    const state = String(result.state || 'pending');
    if (state === 'pending') return { state: 'pending' };
    if (state !== 'connected') {
      pending.delete(key);
      throw new Error(String(result.error || `Autorização ${platform} não concluída.`));
    }
    const token = {
      access_token: String(result.access_token || '').trim(),
      refresh_token: String(result.refresh_token || '').trim(),
      token_type: String(result.token_type || 'Bearer'),
      scope: result.scope,
      expiresAt: Date.now() + Math.max(60, Number(result.expires_in) || 3600) * 1000,
    };
    if (!token.access_token) {
      pending.delete(key);
      throw new Error(`OAuth compartilhado ${platform} retornou autorização inválida.`);
    }
    accounts[platform] = {
      ...storedCredentials(platform),
      ...token,
      refresh_token: token.refresh_token || accounts[platform]?.refresh_token || '',
      userId: String(result.userId || '').trim(),
      name: String(result.name || '').trim(),
      oauthMode: 'shared',
    };
    save();
    pending.delete(key);
    return { state: 'connected', name: accounts[platform].name };
  };

  const sharedRefresh = async (platform, refreshTokenValue) => {
    const result = await brokerRequest(
      'POST',
      `/v1/oauth/${platform}/refresh`,
      { refreshToken: refreshTokenValue },
      platform,
    );
    return {
      ...result,
      expiresAt: Date.now() + Math.max(60, Number(result.expires_in) || 3600) * 1000,
    };
  };

  const kickConfig = () => platformConfig('kick');
  const kickScopes = (account) => Array.isArray(account?.scope)
    ? account.scope
    : String(account?.scope || '').split(/[ ,]+/).filter(Boolean);

  const kickConfigured = () => Boolean(
    kickConfig().clientId && kickConfig().clientSecret,
  );

  const kickExchange = async (fields) => {
    const config = kickConfig();
    if (!String(config.clientId || '').trim() || !String(config.clientSecret || '').trim()) {
      throw new Error('Configure Client ID e Client Secret da Kick antes de conectar.');
    }
    const result = await postForm(KICK_TOKEN_URL, {
      ...fields,
      client_id: config.clientId,
      client_secret: config.clientSecret,
    }, 'A Kick');
    if (!result.access_token) throw new Error('A Kick não retornou uma autorização válida.');
    return { ...result, expiresAt: Date.now() + Number(result.expires_in || 3600) * 1000 };
  };

  const identifyKick = async (token) => {
    const result = await requestJson(`${KICK_API_BASE}/users`, {
      headers: { Authorization: `Bearer ${token.access_token}`, Accept: 'application/json' },
    }, 'A Kick');
    const user = result.data?.[0];
    const userId = user?.user_id ?? user?.id;
    if (!userId) throw new Error('Não foi possível identificar a conta autorizada na Kick.');
    return { ...token, userId: String(userId), name: user.name || user.username || user.email || String(userId) };
  };

  const kickAccess = async () => {
    let account = accounts.kick;
    if (!account?.access_token) throw new Error('Conecte sua conta da Kick primeiro.');
    if (account.expiresAt <= Date.now() + 60000) {
      if (!account.refresh_token) throw new Error('A autorização da Kick expirou. Reconecte a conta.');
      if (!refreshing.has('kick')) {
        refreshing.set('kick', (async () => {
          const token = account.oauthMode === 'shared'
            ? await sharedRefresh('kick', account.refresh_token)
            : await kickExchange({ grant_type: 'refresh_token', refresh_token: account.refresh_token });
          accounts.kick = { ...account, ...token, refresh_token: token.refresh_token || account.refresh_token };
          save();
        })().finally(() => refreshing.delete('kick')));
      }
      await refreshing.get('kick');
      account = accounts.kick;
    }
    return account;
  };

  const kickRequest = async (endpoint, options = {}) => {
    const account = await kickAccess();
    return requestJson(`${KICK_API_BASE}${endpoint}`, {
      ...options,
      headers: {
        Authorization: `Bearer ${account.access_token}`,
        Accept: 'application/json',
        'Content-Type': 'application/json',
        ...options.headers,
      },
    }, 'A Kick');
  };

  const kickContentFromChannel = (account, channel) => {
    const stream = channel.stream || {};
    const category = channel.category || {};
    const has = (object, key) => Object.prototype.hasOwnProperty.call(object, key);
    return {
      platform: 'kick', name: account.name || '',
      title: String(channel.stream_title || channel.title || ''),
      categoryId: String(category.id || ''),
      category: String(category.name || ''),
      tags: Array.isArray(channel.tags) ? channel.tags : [],
      language: String(channel.language || ''),
      isLive: stream.is_live === true,
      viewerCount: Number(stream.viewer_count || 0),
      fieldAvailability: {
        title: has(channel, 'stream_title') || has(channel, 'title'),
        category: has(channel, 'category') && (channel.category == null || has(category, 'name')),
        tags: has(channel, 'tags'),
        language: has(channel, 'language'),
      },
    };
  };

  const kickBroadcastContent = async () => {
    const account = await kickAccess();
    // Kick documents an unfiltered /channels request as the authenticated
    // broadcaster's own channel. Use that identity-bound form for read-back.
    const result = await kickRequest('/channels');
    const channel = result.data?.[0];
    if (!channel) throw new Error('A Kick não retornou o canal autorizado.');
    return kickContentFromChannel(account, channel);
  };

  const kickTransmission = async () => {
    const account = await kickAccess();
    const result = await kickRequest('/channels');
    const channel = result.data?.[0];
    if (!channel) throw new Error('A Kick não retornou o canal autorizado.');

    const stream = channel.stream || {};
    const server = String(stream.url || '').trim();
    const streamKey = String(stream.key || '').trim();
    if (!server || !streamKey) {
      throw new Error('A Kick não forneceu servidor RTMP e stream key com autorização atual.');
    }
    return { ...kickContentFromChannel(account, channel), server, streamKey };
  };

  const youtubeConfig = () => platformConfig('youtube');
  const youtubeScopes = (account) => String(account?.scope || '').split(/[ ,]+/).filter(Boolean);
  const youtubeConfigured = () => Boolean(
    youtubeConfig().clientId && youtubeConfig().clientSecret,
  );

  const youtubeExchange = async (fields) => {
    const config = youtubeConfig();
    if (!String(config.clientId || '').trim() || !String(config.clientSecret || '').trim()) {
      throw new Error('Configure Client ID e Client Secret do YouTube antes de conectar.');
    }
    const result = await postForm(YOUTUBE_TOKEN_URL, {
      ...fields,
      client_id: config.clientId,
      client_secret: config.clientSecret,
    }, 'O YouTube');
    if (!result.access_token) throw new Error('O YouTube não retornou uma autorização válida.');
    return {
      ...result,
      expiresAt: Date.now() + Number(result.expires_in || 3600) * 1000,
    };
  };

  const identifyYoutube = async (token) => {
    const result = await requestJson(`${YOUTUBE_API_BASE}/channels?part=snippet&mine=true`, {
      headers: { Authorization: `Bearer ${token.access_token}`, Accept: 'application/json' },
    }, 'O YouTube');
    const channel = result.items?.[0];
    if (!channel?.id) throw new Error('Não foi possível identificar o canal autorizado do YouTube.');
    return {
      ...token,
      userId: String(channel.id),
      name: channel.snippet?.title || String(channel.id),
    };
  };

  const youtubeAccess = async () => {
    let account = accounts.youtube;
    if (!account?.access_token) throw new Error('Conecte sua conta do YouTube primeiro.');
    if (account.expiresAt <= Date.now() + 60000) {
      if (!account.refresh_token) throw new Error('A autorização do YouTube expirou. Reconecte a conta.');
      if (!refreshing.has('youtube')) {
        refreshing.set('youtube', (async () => {
          const token = account.oauthMode === 'shared'
            ? await sharedRefresh('youtube', account.refresh_token)
            : await youtubeExchange({
              grant_type: 'refresh_token',
              refresh_token: account.refresh_token,
            });
          accounts.youtube = {
            ...account,
            ...token,
            refresh_token: token.refresh_token || account.refresh_token,
          };
          save();
        })().finally(() => refreshing.delete('youtube')));
      }
      await refreshing.get('youtube');
      account = accounts.youtube;
    }
    return account;
  };

  const youtubeRequest = async (endpoint, options = {}) => {
    const account = await youtubeAccess();
    return requestJson(`${YOUTUBE_API_BASE}${endpoint}`, {
      ...options,
      headers: {
        Authorization: `Bearer ${account.access_token}`,
        Accept: 'application/json',
        'Content-Type': 'application/json',
        ...options.headers,
      },
    }, 'O YouTube');
  };

  const youtubeBroadcastContent = async (requestedBroadcastId = '') => {
    const account = await youtubeAccess();
    const broadcasts = await youtubeRequest(
      '/liveBroadcasts?part=snippet,contentDetails,status&mine=true&maxResults=50',
    );
    const ownedBroadcasts = broadcasts.items || [];
    const requestedId = String(requestedBroadcastId || '').trim();
    if (requestedId && !ownedBroadcasts.some((broadcast) => String(broadcast.id || '') === requestedId)) {
      throw new Error('A transmissão YouTube carregada não está mais disponível. Atualize os dados antes de aplicar.');
    }
    let candidates = requestedId
      ? ownedBroadcasts.filter((broadcast) => String(broadcast.id || '') === requestedId)
      : ownedBroadcasts.filter((broadcast) => {
      const lifecycle = String(broadcast.status?.lifeCycleStatus || '').toLowerCase();
      return ['live', 'testing'].includes(lifecycle);
      });
    if (candidates.length) {
      candidates.sort((left, right) => {
        const leftTime = Date.parse(left.snippet?.actualStartTime || left.snippet?.scheduledStartTime || '') || 0;
        const rightTime = Date.parse(right.snippet?.actualStartTime || right.snippet?.scheduledStartTime || '') || 0;
        return rightTime - leftTime;
      });
    } else {
      candidates = ownedBroadcasts.filter((broadcast) => {
        const lifecycle = String(broadcast.status?.lifeCycleStatus || '').toLowerCase();
        return ['ready', 'created'].includes(lifecycle);
      });
      // When multiple events are scheduled, edit the next one rather than a
      // farther future event returned earlier by the API.
      candidates.sort((left, right) => {
        const leftTime = Date.parse(left.snippet?.scheduledStartTime || '') || Number.MAX_SAFE_INTEGER;
        const rightTime = Date.parse(right.snippet?.scheduledStartTime || '') || Number.MAX_SAFE_INTEGER;
        return leftTime - rightTime;
      });
    }
    const broadcast = candidates[0];
    if (!broadcast) throw new Error('O YouTube não encontrou uma transmissão atual ou agendada.');
    const lifecycle = String(broadcast?.status?.lifeCycleStatus || '').toLowerCase();
    const isLive = lifecycle === 'live' || lifecycle === 'testing';
    const streamId = String(broadcast?.contentDetails?.boundStreamId || '').trim();

    const broadcastSnippet = broadcast.snippet || {};
    const broadcastStatus = broadcast.status || {};
    const videos = await youtubeRequest(
      `/videos?part=snippet,status&id=${encodeURIComponent(broadcast.id)}`,
    );
    const video = videos.items?.[0] || null;
    const videoSnippet = video?.snippet || {};
    const videoStatus = video?.status || {};
    const has = (object, key) => Object.prototype.hasOwnProperty.call(object, key);
    const snippet = video ? videoSnippet : broadcastSnippet;
    const status = video ? videoStatus : broadcastStatus;
    const broadcastTitle = String(broadcastSnippet.title || '').trim();
    const broadcastDescription = String(broadcastSnippet.description || '');
    const broadcastCategoryId = String(broadcastSnippet.categoryId || '').trim();
    const videoTitle = String(videoSnippet.title || '').trim();
    const videoDescription = String(videoSnippet.description || '');
    const videoCategoryId = String(videoSnippet.categoryId || '').trim();
    const broadcastTitleAvailable = has(broadcastSnippet, 'title');
    const broadcastDescriptionAvailable = has(broadcastSnippet, 'description');
    const broadcastCategoryAvailable = has(broadcastSnippet, 'categoryId');
    const videoTitleAvailable = Boolean(video && has(videoSnippet, 'title'));
    const videoDescriptionAvailable = Boolean(video && has(videoSnippet, 'description'));
    const videoCategoryAvailable = Boolean(video && has(videoSnippet, 'categoryId'));
    const categoryId = broadcastCategoryId || videoCategoryId;
    let category = '';
    if (categoryId) {
      const categories = await youtubeRequest(
        `/videoCategories?part=snippet&id=${encodeURIComponent(categoryId)}`,
      );
      category = String(categories.items?.[0]?.snippet?.title || '');
    }
    return {
      platform: 'youtube', name: account.name || '', broadcastId: String(broadcast.id || ''),
      streamId,
      // YouTube Studio shows the liveBroadcast resource. Keep the linked video
      // values alongside it so a split update cannot appear as successful.
      title: broadcastTitleAvailable ? broadcastTitle : videoTitle,
      description: videoDescriptionAvailable ? videoDescription : broadcastDescription,
      broadcastTitle, broadcastDescription, broadcastCategoryId,
      videoTitle, videoDescription, videoCategoryId,
      metadataMismatch: (broadcastTitleAvailable && videoTitleAvailable && broadcastTitle !== videoTitle) ||
        (broadcastDescriptionAvailable && videoDescriptionAvailable && broadcastDescription !== videoDescription) ||
        (broadcastCategoryAvailable && videoCategoryAvailable && broadcastCategoryId !== videoCategoryId),
      fieldAvailability: {
        title: broadcastTitleAvailable || videoTitleAvailable,
        category: Boolean(categoryId),
        description: broadcastDescriptionAvailable || videoDescriptionAvailable,
        broadcastTitle: broadcastTitleAvailable,
        videoTitle: videoTitleAvailable,
        broadcastDescription: broadcastDescriptionAvailable,
        videoDescription: videoDescriptionAvailable,
        broadcastCategory: broadcastCategoryAvailable,
        videoCategory: videoCategoryAvailable,
        tags: Boolean(video && has(videoSnippet, 'tags')),
        language: Boolean(video && (has(videoSnippet, 'defaultLanguage') || has(videoSnippet, 'defaultAudioLanguage'))),
      },
      categoryId, category,
      tags: Array.isArray(snippet.tags) ? snippet.tags : [],
      language: String(snippet.defaultLanguage || snippet.defaultAudioLanguage || ''),
      visibility: String(status.privacyStatus || ''),
      lifecycle,
      isLive,
    };
  };

  const youtubeTransmission = async () => {
    const content = await youtubeBroadcastContent();
    if (!content.streamId) {
      throw new Error('O YouTube não encontrou uma transmissão vinculada a uma stream.');
    }
    const streams = await youtubeRequest(`/liveStreams?part=cdn&id=${encodeURIComponent(content.streamId)}`);
    const stream = streams.items?.[0];
    const ingestion = stream?.cdn?.ingestionInfo || {};
    const server = String(ingestion.rtmpsIngestionAddress || ingestion.ingestionAddress || '').trim();
    const streamKey = String(ingestion.streamName || '').trim();
    if (!server) throw new Error('O YouTube não retornou um endereço RTMP/RTMPS para a transmissão vinculada.');
    if (!streamKey) throw new Error('O YouTube não retornou a chave da transmissão vinculada.');
    const { streamId, ...transmission } = content;
    return { ...transmission, server, streamKey };
  };

  const youtubeBroadcastEvents = async () => {
    await youtubeAccess();
    const broadcasts = await youtubeRequest(
      '/liveBroadcasts?part=snippet,status&mine=true&maxResults=50',
    );
    const editable = (broadcasts.items || []).filter((broadcast) => {
      const lifecycle = String(broadcast.status?.lifeCycleStatus || '').toLowerCase();
      return broadcast.id && ['live', 'testing', 'ready', 'created'].includes(lifecycle);
    });
    editable.sort((left, right) => {
      const rank = (broadcast) => ['live', 'testing'].includes(String(broadcast.status?.lifeCycleStatus || '').toLowerCase()) ? 0 : 1;
      const rankDelta = rank(left) - rank(right);
      if (rankDelta) return rankDelta;
      const leftTime = Date.parse(left.snippet?.actualStartTime || left.snippet?.scheduledStartTime || '') || 0;
      const rightTime = Date.parse(right.snippet?.actualStartTime || right.snippet?.scheduledStartTime || '') || 0;
      return rank(left) === 0 ? rightTime - leftTime : leftTime - rightTime;
    });
    return editable.map((broadcast) => ({
      id: String(broadcast.id),
      title: String(broadcast.snippet?.title || '').trim(),
      state: String(broadcast.status?.lifeCycleStatus || ''),
      scheduledStartTime: String(broadcast.snippet?.scheduledStartTime || ''),
    }));
  };

  const updateTransmission = async (platform, input = {}) => {
    const globalMetadata = input.global_metadata && typeof input.global_metadata === 'object'
      ? input.global_metadata
      : {};
    const allPlatforms = input.platforms && typeof input.platforms === 'object'
      ? input.platforms
      : {};
    const platformMetadata = input.platformSettings && typeof input.platformSettings === 'object'
      ? input.platformSettings
      : (allPlatforms[platform] && typeof allPlatforms[platform] === 'object' ? allPlatforms[platform] : {});
    const title = String(globalMetadata.title ?? input.title ?? '').trim();
    const descriptionProvided = platform === 'youtube'
      && (Object.prototype.hasOwnProperty.call(platformMetadata, 'description')
        || Object.prototype.hasOwnProperty.call(globalMetadata, 'description')
        || Object.prototype.hasOwnProperty.call(input, 'description')
        || Object.prototype.hasOwnProperty.call(input, 'notification'));
    const description = platform === 'youtube'
      ? String(platformMetadata.description ?? globalMetadata.description ?? input.description ?? input.notification ?? '').trim()
      : '';
    const platformLanguage = Object.prototype.hasOwnProperty.call(platformMetadata, 'language')
      ? platformMetadata.language
      : input.language;
    const language = String(platformLanguage || '').trim();
    const isMature = typeof globalMetadata.is_mature === 'boolean'
      ? globalMetadata.is_mature
      : Boolean(input.is_mature);
    const categoryField = platform === 'twitch'
      ? 'gameName'
      : platform === 'kick'
        ? 'categoryName'
        : 'categoryName';
    const category = String(Object.prototype.hasOwnProperty.call(platformMetadata, categoryField)
      ? platformMetadata[categoryField]
      : input.category || '').trim();
    const categoryId = String(Object.prototype.hasOwnProperty.call(platformMetadata, 'categoryId')
      ? platformMetadata.categoryId
      : platform === 'twitch' && Object.prototype.hasOwnProperty.call(platformMetadata, 'gameId')
        ? platformMetadata.gameId
        : '').trim();
    const normalizeTags = (value, limit, maxCharacters) => {
      if (!Array.isArray(value)) return null;
      const result = [];
      let used = 0;
      for (const raw of value) {
        const tag = String(raw || '').trim();
        if (!tag || result.length >= limit || used + tag.length > maxCharacters) continue;
        result.push(tag);
        used += tag.length;
      }
      return result;
    };
    const tags = normalizeTags(
      Object.prototype.hasOwnProperty.call(platformMetadata, 'tags') ? platformMetadata.tags : input.tags,
      platform === 'twitch' ? 5 : 50,
      platform === 'youtube' ? 500 : 1000,
    );
    const privacyStatus = String(
      platformMetadata.privacyStatus
      || input.privacyStatus
      || (Number(input.visibility) === 0 ? 'public' : Number(input.visibility) === 1 ? 'unlisted' : Number(input.visibility) === 2 ? 'private' : ''),
    ).trim().toLowerCase();
    const classificationIds = Array.isArray(platformMetadata.contentClassificationIds)
      ? platformMetadata.contentClassificationIds.map((id) => String(id).trim()).filter(Boolean)
      : Array.isArray(input.contentClassificationIds)
        ? input.contentClassificationIds.map((id) => String(id).trim()).filter(Boolean)
        : typeof input.classification === 'string' && input.classification.trim()
          ? [input.classification.trim()]
          : [];
    const classificationProvided = Object.prototype.hasOwnProperty.call(platformMetadata, 'contentClassificationIds')
      || Array.isArray(input.contentClassificationIds)
      || typeof input.classification === 'string'
      || typeof globalMetadata.is_mature === 'boolean'
      || typeof input.is_mature === 'boolean';
    const madeForKids = typeof platformMetadata.madeForKids === 'boolean'
      ? platformMetadata.madeForKids
      : (typeof input.madeForKids === 'boolean' ? input.madeForKids : undefined);
    const selfDeclaredMadeForKids = typeof platformMetadata.selfDeclaredMadeForKids === 'boolean'
      ? platformMetadata.selfDeclaredMadeForKids
      : (typeof input.selfDeclaredMadeForKids === 'boolean' ? input.selfDeclaredMadeForKids : undefined);
    if (!title) throw new Error('Informe o título da transmissão.');
    if (title.length > 140) throw new Error('O título pode ter no máximo 140 caracteres.');

    if (platform === 'twitch') {
      const account = await access();
      const body = { title };
      let gameId = categoryId;
      if (!gameId && category) {
        const found = await requestJson(
          `https://api.twitch.tv/helix/search/categories?query=${encodeURIComponent(category)}&first=10`,
          { headers: { Authorization: `Bearer ${account.access_token}`, 'Client-Id': TWITCH_CLIENT_ID, Accept: 'application/json' } },
          'A Twitch',
        );
        const exact = (found.data || []).find((item) => String(item.name || '').toLowerCase() === category.toLowerCase());
        if (!exact) throw new Error('Selecione uma categoria existente da Twitch.');
        gameId = String(exact.id || '');
      }
      if (gameId) body.game_id = gameId;
      if (tags) body.tags = tags;
      if (language) body.broadcaster_language = language;
      if (classificationProvided) {
        const supported = ['ProfanityVulgarity', 'ViolentGraphic', 'DebatedSocialIssuesAndPolitics', 'Gambling'];
        const effective = classificationIds.length ? classificationIds : (isMature ? ['ProfanityVulgarity'] : []);
        body.content_classification_labels = supported.map((id) => ({ id, is_enabled: effective.includes(id) }));
      }
      await requestJson(
        `https://api.twitch.tv/helix/channels?broadcaster_id=${encodeURIComponent(account.userId)}`,
        { method: 'PATCH', body: JSON.stringify(body), headers: { Authorization: `Bearer ${account.access_token}`, 'Client-Id': TWITCH_CLIENT_ID, 'Content-Type': 'application/json' } },
        'A Twitch',
      );
      const saved = await twitchBroadcastContent();
      const mismatches = [];
      if (saved.title.trim() !== title) mismatches.push('título');
      if (gameId && saved.categoryId !== String(gameId)) mismatches.push('jogo');
      if (language && saved.language !== language) mismatches.push('idioma');
      if (tags && JSON.stringify(saved.tags) !== JSON.stringify(tags)) mismatches.push('tags');
      if (classificationProvided && classificationIds.length &&
          classificationIds.some((id) => !saved.classificationLabels.includes(id))) {
        mismatches.push('classificação');
      }
      if (mismatches.length) {
        throw new Error(`A Twitch recebeu a alteração, mas não confirmou: ${mismatches.join(', ')}.`);
      }
      return { platform: 'twitch', message: 'Título e jogo confirmados pela Twitch.' };
    }

    if (platform === 'kick') {
      const account = await kickAccess();
      const body = { stream_title: title };
      let kickCategoryId = categoryId;
      if (!kickCategoryId && category) {
        const found = await kickRequest(`/categories?q=${encodeURIComponent(category)}`);
        const exact = (found.data || []).find((item) => String(item.name || '').toLowerCase() === category.toLowerCase());
        if (!exact) throw new Error('Selecione uma categoria existente da Kick.');
        kickCategoryId = String(exact.id || '');
      }
      if (kickCategoryId) {
        const numericCategoryId = Number(kickCategoryId);
        if (!Number.isInteger(numericCategoryId) || numericCategoryId < 1) {
          throw new Error('A Kick retornou uma categoria inválida.');
        }
        body.category_id = numericCategoryId;
      }
      await kickRequest('/channels', { method: 'PATCH', body: JSON.stringify(body) });
      // Kick acknowledges channel updates with HTTP 204 (no response body).
      // Its channel read can also return empty stream_title/category while the
      // channel is offline, so only compare values the API actually exposes.
      let saved = null;
      try {
        saved = await kickBroadcastContent();
      } catch (_) {
        // The successful PATCH remains authoritative for acceptance; a failed
        // follow-up GET means the result cannot be independently confirmed.
      }
      const mismatches = [];
      const returnedTitle = String(saved?.title || '').trim();
      const returnedCategoryId = String(saved?.categoryId || '').trim();
      if (returnedTitle && returnedTitle !== title) mismatches.push('título');
      if (kickCategoryId && returnedCategoryId && returnedCategoryId !== String(kickCategoryId))
        mismatches.push('jogo');
      if (mismatches.length) {
        throw new Error(`A Kick recebeu a alteração, mas não confirmou: ${mismatches.join(', ')}.`);
      }
      const fullyConfirmed = returnedTitle === title &&
        (!kickCategoryId || returnedCategoryId === String(kickCategoryId));
      return {
        platform: 'kick',
        broadcasterId: String(account.userId || ''),
        message: fullyConfirmed
          ? 'Título e jogo confirmados pela Kick.'
          : saved?.isLive === false
            ? 'A Kick aceitou a atualização; não expôs título e jogo para conferência enquanto o canal está offline.'
            : 'A Kick aceitou a atualização; a leitura não retornou dados suficientes para confirmar título e jogo.',
      };
    }

    if (platform === 'youtube') {
      // The broadcast and its video represent the same YouTube live. Persistent
      // broadcasts may have no scheduled start time; in that case YouTube
      // forbids changing that field, so write through the linked video only.
      if (title.length > 100) throw new Error('O título do YouTube pode ter no máximo 100 caracteres.');
      if (descriptionProvided && Buffer.byteLength(description, 'utf8') > 5000) {
        throw new Error('A descrição do YouTube pode ter no máximo 5.000 bytes.');
      }
      const requestedBroadcastId = String(platformMetadata.broadcastId || '').trim();
      if (!requestedBroadcastId) throw new Error('Selecione o evento do YouTube antes de aplicar as alterações.');
      const current = await youtubeBroadcastContent(requestedBroadcastId);
      if (!current.broadcastId) throw new Error('O YouTube não encontrou uma transmissão atualizável.');
      let youtubeCategoryId = categoryId;
      if (!youtubeCategoryId && category) {
        const found = await youtubeRequest('/videoCategories?part=snippet&regionCode=US&maxResults=50');
        const exact = (found.items || []).find((item) => String(item.snippet?.title || '').toLowerCase() === category.toLowerCase());
        if (!exact) throw new Error('Selecione uma categoria existente do YouTube.');
        youtubeCategoryId = String(exact.id || '').trim();
      }
      if (!youtubeCategoryId) youtubeCategoryId = current.categoryId;
      if (!youtubeCategoryId) throw new Error('Selecione uma categoria existente do YouTube.');
      const videos = await youtubeRequest(`/videos?part=snippet,status&id=${encodeURIComponent(current.broadcastId)}`);
      const video = videos.items?.[0];
      if (!video) throw new Error('O YouTube não encontrou o vídeo associado à live.');
      const existingVideoSnippet = video.snippet || {};
      const existingVideoStatus = video.status || {};
      const broadcasts = await youtubeRequest(
        `/liveBroadcasts?part=snippet,contentDetails,status&id=${encodeURIComponent(current.broadcastId)}`,
      );
      const broadcast = broadcasts.items?.[0];
      if (!broadcast) throw new Error('O YouTube não encontrou o evento associado ao vídeo.');
      const existingBroadcastSnippet = broadcast.snippet || {};
      const scheduledStartTime = String(existingBroadcastSnippet.scheduledStartTime || '').trim();
      const parsedStartTime = Date.parse(scheduledStartTime);
      const canUpdateBroadcast = Number.isFinite(parsedStartTime) && parsedStartTime > 0;
      const effectiveDescription = descriptionProvided
        ? description
        : String(current.description || existingVideoSnippet.description || '');
      if (canUpdateBroadcast) {
        const broadcastSnippet = {
          title,
          description: effectiveDescription,
          categoryId: youtubeCategoryId,
          scheduledStartTime,
        };
        if (existingBroadcastSnippet.scheduledEndTime)
          broadcastSnippet.scheduledEndTime = existingBroadcastSnippet.scheduledEndTime;
        await youtubeRequest('/liveBroadcasts?part=snippet', {
          method: 'PUT',
          body: JSON.stringify({
            id: current.broadcastId,
            snippet: broadcastSnippet,
          }),
        });
      }
      const videoSnippet = {
        title,
        description: effectiveDescription,
        categoryId: youtubeCategoryId,
        tags: tags || (Array.isArray(existingVideoSnippet.tags) ? existingVideoSnippet.tags : []),
      };
      const effectiveLanguage = language || String(existingVideoSnippet.defaultLanguage || '').trim();
      if (effectiveLanguage) videoSnippet.defaultLanguage = effectiveLanguage;
      const videoStatus = {};
      for (const field of ['privacyStatus', 'license', 'embeddable', 'publicStatsViewable', 'selfDeclaredMadeForKids', 'containsSyntheticMedia']) {
        if (Object.prototype.hasOwnProperty.call(existingVideoStatus, field))
          videoStatus[field] = existingVideoStatus[field];
      }
      if (privacyStatus) videoStatus.privacyStatus = privacyStatus;
      if (typeof madeForKids === 'boolean') videoStatus.selfDeclaredMadeForKids = madeForKids;
      if (typeof selfDeclaredMadeForKids === 'boolean') videoStatus.selfDeclaredMadeForKids = selfDeclaredMadeForKids;
      await youtubeRequest('/videos?part=snippet,status', {
        method: 'PUT',
        body: JSON.stringify({ id: current.broadcastId, snippet: videoSnippet, status: videoStatus }),
      });
      const updatedVideos = await youtubeRequest(
        `/videos?part=snippet,status&id=${encodeURIComponent(current.broadcastId)}`,
      );
      const updatedBroadcasts = await youtubeRequest(
        `/liveBroadcasts?part=snippet&id=${encodeURIComponent(current.broadcastId)}`,
      );
      const savedVideo = updatedVideos.items?.[0];
      const savedBroadcast = updatedBroadcasts.items?.[0];
      if (!savedVideo) throw new Error('O YouTube não retornou o vídeo da transmissão depois de salvar.');
      if (canUpdateBroadcast && !savedBroadcast) throw new Error('O YouTube não retornou o evento depois de salvar.');
      const savedSnippet = savedVideo.snippet || {};
      const savedStatus = savedVideo.status || {};
      const savedBroadcastSnippet = savedBroadcast?.snippet || {};
      const mismatches = [];
      if (String(savedSnippet.title || '').trim() !== title ||
          (canUpdateBroadcast && String(savedBroadcastSnippet.title || '').trim() !== title))
        mismatches.push(canUpdateBroadcast ? 'título do evento/vídeo' : 'título do vídeo');
      if (String(savedSnippet.description || '') !== effectiveDescription ||
          (canUpdateBroadcast && String(savedBroadcastSnippet.description || '') !== effectiveDescription))
        mismatches.push(canUpdateBroadcast ? 'descrição do evento/vídeo' : 'descrição do vídeo');
      if (String(savedSnippet.categoryId || '').trim() !== youtubeCategoryId ||
          (canUpdateBroadcast && String(savedBroadcastSnippet.categoryId || '').trim() !== youtubeCategoryId))
        mismatches.push(canUpdateBroadcast ? 'categoria do evento/vídeo' : 'categoria do vídeo');
      const normalizeTags = (values) => (Array.isArray(values) ? values : [])
        .map((tag) => String(tag || '').normalize('NFC').trim().toLowerCase())
        .filter(Boolean).sort();
      if (tags && JSON.stringify(normalizeTags(savedSnippet.tags)) !== JSON.stringify(normalizeTags(tags))) {
        mismatches.push('tags');
      }
      if (effectiveLanguage && String(savedSnippet.defaultLanguage || '').trim() !== effectiveLanguage) mismatches.push('idioma');
      if (privacyStatus && String(savedStatus.privacyStatus || '').trim().toLowerCase() !== privacyStatus) mismatches.push('visibilidade');
      if (mismatches.length) {
        throw new Error(`O YouTube recebeu a alteração, mas não confirmou: ${mismatches.join(', ')}.`);
      }
      return {
        platform: 'youtube',
        broadcastId: current.broadcastId,
        message: canUpdateBroadcast
          ? 'Evento e vídeo atualizados e confirmados.'
          : 'Vídeo da transmissão atualizado e confirmado; evento sem horário agendado.',
      };
    }

    throw new Error('Plataforma de transmissão inválida.');
  };

  const completeYoutube = async (flow, query) => {
    if (query.error) {
      throw new Error(`Autorização YouTube recusada: ${String(query.error_description || query.error)}`);
    }
    const code = String(query.code || '').trim();
    if (!code || String(query.state || '') !== flow.state) {
      throw new Error('Resposta OAuth YouTube inválida.');
    }
    const token = await youtubeExchange({
      grant_type: 'authorization_code',
      code,
      redirect_uri: YOUTUBE_REDIRECT_URI,
      code_verifier: flow.verifier,
    });
    const identified = await identifyYoutube(token);
    accounts.youtube = {
      ...storedCredentials('youtube'),
      ...identified,
      refresh_token: identified.refresh_token || accounts.youtube?.refresh_token,
    };
    save();
    flow.stateResult = { state: 'connected', name: accounts.youtube.name };
    return flow.stateResult;
  };

  const completeKick = async (flow, query) => {
    if (query.error) throw new Error(`Autorização Kick recusada: ${String(query.error_description || query.error)}`);
    const code = String(query.code || '').trim();
    if (!code || String(query.state || '') !== flow.state) throw new Error('Resposta OAuth Kick inválida.');
    const token = await kickExchange({
      grant_type: 'authorization_code',
      code,
      redirect_uri: KICK_REDIRECT_URI,
      code_verifier: flow.verifier,
    });
    accounts.kick = {
      ...storedCredentials('kick'),
      ...await identifyKick(token),
    };
    save();
    flow.stateResult = { state: 'connected', name: accounts.kick.name };
    return flow.stateResult;
  };

  return {
    status() {
      const account = accounts.twitch;
      const granted = new Set(account?.scope || []);
      const missingScopes = TWITCH_SCOPES.split(' ').filter((scope) => !granted.has(scope));
      return { connected: Boolean(account), name: account?.name || '', needsReconnect: Boolean(account && missingScopes.length), missingScopes };
    },

    kickStatus() {
      const account = accounts.kick;
      const granted = new Set(kickScopes(account));
      const missingScopes = KICK_SCOPES.split(' ').filter((scope) => !granted.has(scope));
      return {
        configured: sharedConfigured() || kickConfigured(),
        credentialSource: sharedConfigured() ? 'shared' : kickConfig().source,
        clientId: String(account?.clientId || sharedClientId('kick') || kickConfig().clientId || ''),
        connected: Boolean(account?.access_token),
        name: account?.name || '',
        needsReconnect: Boolean(account?.access_token && missingScopes.length),
        missingScopes,
      };
    },

    youtubeStatus() {
      const account = accounts.youtube;
      const granted = new Set(youtubeScopes(account));
      return {
        configured: sharedConfigured() || youtubeConfigured(),
        credentialSource: sharedConfigured() ? 'shared' : youtubeConfig().source,
        clientId: String(account?.clientId || sharedClientId('youtube') || youtubeConfig().clientId || ''),
        connected: Boolean(account?.access_token),
        name: account?.name || '',
        needsReconnect: Boolean(account?.access_token && !granted.has(YOUTUBE_SCOPE)),
        missingScopes: granted.has(YOUTUBE_SCOPE) ? [] : [YOUTUBE_SCOPE],
      };
    },

    async youtubeActiveChat() {
      const account = await youtubeAccess();
      const broadcasts = await youtubeRequest(
        '/liveBroadcasts?part=snippet,status&mine=true&maxResults=50',
      );
      const broadcast = (broadcasts.items || []).find((item) =>
        ['live', 'testing'].includes(String(item.status?.lifeCycleStatus || '').toLowerCase()) &&
        item.snippet?.liveChatId,
      );
      return {
        name: account.name || '',
        liveChatId: String(broadcast?.snippet?.liveChatId || ''),
      };
    },

    async youtubeChatMessages(liveChatId, pageToken) {
      const query = new URLSearchParams({ part: 'snippet,authorDetails', liveChatId: String(liveChatId || '') });
      if (pageToken) query.set('pageToken', String(pageToken));
      return youtubeRequest(`/liveChat/messages?${query.toString()}`);
    },

    configureYoutube(clientId, clientSecret) {
      const id = String(clientId || '').trim();
      const secret = String(clientSecret || '').trim();
      accounts.youtube = { ...accounts.youtube };
      if (id) accounts.youtube.clientId = id;
      else delete accounts.youtube.clientId;
      if (secret) accounts.youtube.clientSecret = secret;
      else delete accounts.youtube.clientSecret;
      if (!youtubeConfigured()) throw new Error('Informe credenciais YouTube ou configure `.env`.');
      save();
      return this.youtubeStatus();
    },

    configureKick(clientId, clientSecret) {
      const id = String(clientId || '').trim();
      const secret = String(clientSecret || '').trim();
      accounts.kick = { ...accounts.kick };
      if (id) accounts.kick.clientId = id;
      else delete accounts.kick.clientId;
      if (secret) accounts.kick.clientSecret = secret;
      else delete accounts.kick.clientSecret;
      if (!kickConfigured()) throw new Error('Informe credenciais Kick ou configure `.env`.');
      save();
      return this.kickStatus();
    },

    async start() {
      const device = await postForm(TWITCH_DEVICE_URL, { client_id: TWITCH_CLIENT_ID, scopes: TWITCH_SCOPES }, 'A Twitch');
      if (!device.device_code || !device.verification_uri) throw new Error('A Twitch não iniciou a autorização. Tente novamente.');
      const flowId = crypto.randomBytes(24).toString('hex');
      const interval = Math.max(3, Number(device.interval) || 5);
      pending.set(flowId, { deviceCode: device.device_code, expiresAt: Date.now() + Number(device.expires_in || 1800) * 1000, interval });
      return { flowId, verificationUri: device.verification_uri, userCode: device.user_code, interval };
    },

    async poll(flowId) {
      const flow = pending.get(flowId);
      if (!flow || flow.expiresAt <= Date.now()) {
        pending.delete(flowId);
        throw new Error('Esta autorização expirou. Clique em Conectar à Twitch novamente.');
      }
      try {
        const token = await exchange({ grant_type: 'urn:ietf:params:oauth:grant-type:device_code', device_code: flow.deviceCode });
        accounts.twitch = await identify(token);
        save();
        pending.delete(flowId);
        return { state: 'connected', name: accounts.twitch.name };
      } catch (error) {
        if (error.code === 'authorization_pending') return { state: 'pending', interval: flow.interval };
        if (error.code === 'slow_down') {
          flow.interval += 5;
          return { state: 'pending', interval: flow.interval };
        }
        pending.delete(flowId);
        throw error;
      }
    },

    async startKick() {
      if (sharedConfigured()) return sharedStart('kick');
      return this.startKickAdvanced();
    },

    async startKickAdvanced() {
      if (!kickConfigured()) throw new Error('Configure Client ID e Client Secret da Kick antes de conectar.');
      const state = crypto.randomBytes(32).toString('base64url');
      const verifier = crypto.randomBytes(48).toString('base64url');
      const challenge = crypto.createHash('sha256').update(verifier).digest('base64url');
      const flowId = crypto.randomBytes(24).toString('hex');
      pending.set(`kick:${flowId}`, { flowId, mode: 'local', state, verifier, expiresAt: Date.now() + 10 * 60 * 1000 });
      const url = new URL(KICK_AUTHORIZE_URL);
      url.search = new URLSearchParams({
        client_id: kickConfig().clientId,
        redirect_uri: KICK_REDIRECT_URI,
        response_type: 'code',
        scope: KICK_SCOPES,
        state,
        code_challenge: challenge,
        code_challenge_method: 'S256',
      }).toString();
      return { flowId, authorizationUri: url.toString() };
    },

    async pollKick(flowId) {
      const key = `kick:${flowId}`;
      const flow = pending.get(key);
      if (flow?.mode === 'shared') return sharedPoll('kick', flowId);
      if (!flow || flow.expiresAt <= Date.now()) {
        pending.delete(key);
        throw new Error('Esta autorização Kick expirou. Clique em Conectar novamente.');
      }
      if (flow.stateResult) {
        pending.delete(key);
        return flow.stateResult;
      }
      return { state: 'pending' };
    },

    async startYoutube() {
      if (sharedConfigured()) return sharedStart('youtube');
      return this.startYoutubeAdvanced();
    },

    async startYoutubeAdvanced() {
      if (!youtubeConfigured()) throw new Error('Configure Client ID e Client Secret do YouTube antes de conectar.');
      const state = crypto.randomBytes(32).toString('base64url');
      const verifier = crypto.randomBytes(48).toString('base64url');
      const challenge = crypto.createHash('sha256').update(verifier).digest('base64url');
      const flowId = crypto.randomBytes(24).toString('hex');
      pending.set(`youtube:${flowId}`, { flowId, mode: 'local', state, verifier, expiresAt: Date.now() + 10 * 60 * 1000 });
      const url = new URL(YOUTUBE_AUTHORIZE_URL);
      url.search = new URLSearchParams({
        client_id: youtubeConfig().clientId,
        redirect_uri: YOUTUBE_REDIRECT_URI,
        response_type: 'code',
        scope: YOUTUBE_SCOPE,
        access_type: 'offline',
        prompt: 'consent',
        state,
        code_challenge: challenge,
        code_challenge_method: 'S256',
      }).toString();
      return { flowId, authorizationUri: url.toString() };
    },

    async pollYoutube(flowId) {
      const key = `youtube:${flowId}`;
      const flow = pending.get(key);
      if (flow?.mode === 'shared') return sharedPoll('youtube', flowId);
      if (!flow || flow.expiresAt <= Date.now()) {
        pending.delete(key);
        throw new Error('Esta autorização YouTube expirou. Clique em Conectar novamente.');
      }
      if (flow.stateResult) {
        pending.delete(key);
        return flow.stateResult;
      }
      return { state: 'pending' };
    },

    async completeYoutubeCallback(query) {
      const state = String(query.state || '');
      const flow = Array.from(pending.values()).find((item) => item.state === state && item.flowId);
      if (!flow || flow.expiresAt <= Date.now()) throw new Error('Estado OAuth YouTube expirado ou inválido.');
      const result = await completeYoutube(flow, query);
      return { flowId: flow.flowId, ...result };
    },

    async completeKickCallback(query) {
      const state = String(query.state || '');
      const flow = Array.from(pending.values()).find((item) => item.state === state);
      if (!flow || flow.expiresAt <= Date.now()) throw new Error('Estado OAuth Kick expirado ou inválido.');
      const result = await completeKick(flow, query);
      return { flowId: flow.flowId, ...result };
    },

    async request(platformOrEndpoint, endpointOrOptions = {}, maybeOptions = {}) {
      let platform = 'twitch';
      let endpoint = platformOrEndpoint;
      let options = endpointOrOptions;
      if (!String(platformOrEndpoint).startsWith('/')) {
        platform = String(platformOrEndpoint).toLowerCase();
        endpoint = endpointOrOptions;
        options = maybeOptions;
      }
      if (platform === 'kick') return kickRequest(endpoint, options || {});
      if (platform === 'youtube') return youtubeRequest(endpoint, options || {});
      const account = await access();
      const headers = { Authorization: `Bearer ${account.access_token}`, 'Client-Id': TWITCH_CLIENT_ID, 'Content-Type': 'application/json' };
      return requestJson(`https://api.twitch.tv/helix${endpoint}`, { ...(options || {}), headers: { ...headers, ...(options || {}).headers } });
    },

    userId: async (platform = 'twitch') => (platform === 'kick' ? (await kickAccess()).userId : (await access()).userId),
    twitchBroadcastContent,
    kickBroadcastContent,
    youtubeBroadcastContent,
    youtubeBroadcastEvents,
    twitchTransmission,
    kickTransmission,
    youtubeTransmission,
    updateTransmission,
  };
}

module.exports = {
  createAccounts,
  KICK_REDIRECT_URI,
  KICK_SCOPES,
  YOUTUBE_REDIRECT_URI,
  YOUTUBE_SCOPE,
  SHARED_OAUTH_BROKER_URL,
};
