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
  'moderator:manage:banned_users', 'moderator:manage:chat_messages',
  'moderator:manage:chat_settings', 'moderator:manage:automod',
  'channel:manage:redemptions',
].join(' ');
const TWITCH_DEVICE_URL = 'https://id.twitch.tv/oauth2/device';
const TWITCH_TOKEN_URL = 'https://id.twitch.tv/oauth2/token';
const TWITCH_USERS_URL = 'https://api.twitch.tv/helix/users';

const KICK_AUTHORIZE_URL = 'https://id.kick.com/oauth/authorize';
const KICK_TOKEN_URL = 'https://id.kick.com/oauth/token';
const KICK_API_BASE = 'https://api.kick.com/public/v1';
const KICK_REDIRECT_URI = 'http://localhost:605/api/accounts/kick/callback';
const KICK_SCOPES = [
  'user:read', 'channel:read', 'channel:write', 'streamkey:read', 'chat:write',
].join(' ');

const YOUTUBE_AUTHORIZE_URL = 'https://accounts.google.com/o/oauth2/v2/auth';
const YOUTUBE_TOKEN_URL = 'https://oauth2.googleapis.com/token';
const YOUTUBE_API_BASE = 'https://www.googleapis.com/youtube/v3';
const YOUTUBE_REDIRECT_URI = 'http://localhost:605/api/accounts/youtube/callback';
const YOUTUBE_SCOPE = 'https://www.googleapis.com/auth/youtube.force-ssl';
const SHARED_OAUTH_BROKER_URL = String(
  process.env.STREAMHUB_OAUTH_BROKER_URL || '',
).trim().replace(/\/+$/, '');

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
        detail = result.message || result.error || '';
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

  const kickTransmission = async () => {
    const account = await kickAccess();
    const result = await kickRequest(`/channels?broadcaster_user_id=${encodeURIComponent(account.userId)}`);
    const channel = result.data?.[0];
    if (!channel) throw new Error('A Kick não retornou o canal autorizado.');

    const server = String(channel.stream_url || channel.rtmp_url || channel.server || channel.stream_server || '').trim();
    const streamKey = String(channel.stream_key || channel.streamKey || '').trim();
    if (!server || !streamKey) {
      throw new Error('A Kick não forneceu servidor RTMP e stream key com autorização atual.');
    }
    return { platform: 'kick', name: account.name || '', server, streamKey };
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
        ...options.headers,
      },
    }, 'O YouTube');
  };

  const youtubeTransmission = async () => {
    const account = await youtubeAccess();
    const broadcasts = await youtubeRequest('/liveBroadcasts?part=snippet,contentDetails,status&mine=true&maxResults=50');
    const candidates = (broadcasts.items || []).filter((broadcast) => {
      const lifecycle = String(broadcast.status?.lifeCycleStatus || '').toLowerCase();
      return ['live', 'testing', 'ready', 'created'].includes(lifecycle);
    });
    const broadcast = candidates[0];
    const streamId = String(broadcast?.contentDetails?.boundStreamId || '').trim();
    if (!streamId) {
      throw new Error('O YouTube não encontrou uma transmissão vinculada a uma stream.');
    }

    const streams = await youtubeRequest(`/liveStreams?part=cdn&id=${encodeURIComponent(streamId)}`);
    const stream = streams.items?.[0];
    const ingestion = stream?.cdn?.ingestionInfo || {};
    const server = String(ingestion.ingestionAddress || '').trim();
    const streamKey = String(ingestion.streamName || '').trim();
    if (!server || !streamKey) {
      throw new Error('O YouTube não forneceu servidor RTMP e stream key autorizados.');
    }
    return { platform: 'youtube', name: account.name || '', server, streamKey };
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
      const account = await access();
      const headers = { Authorization: `Bearer ${account.access_token}`, 'Client-Id': TWITCH_CLIENT_ID, 'Content-Type': 'application/json' };
      return requestJson(`https://api.twitch.tv/helix${endpoint}`, { ...(options || {}), headers: { ...headers, ...(options || {}).headers } });
    },

    userId: async (platform = 'twitch') => (platform === 'kick' ? (await kickAccess()).userId : (await access()).userId),
    kickTransmission,
    youtubeTransmission,
  };
}

module.exports = {
  createAccounts,
  KICK_REDIRECT_URI,
  KICK_SCOPES,
  YOUTUBE_REDIRECT_URI,
  YOUTUBE_SCOPE,
};
