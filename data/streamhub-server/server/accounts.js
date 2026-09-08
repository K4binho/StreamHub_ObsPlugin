const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const fetch = require('node-fetch');

// Client IDs are public by design. Do not add a Client Secret here: this is a
// desktop application and Twitch's public-client Device Code Flow does not use one.
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
const YOUTUBE_AUTH_URL = 'https://accounts.google.com/o/oauth2/v2/auth';
const YOUTUBE_TOKEN_URL = 'https://oauth2.googleapis.com/token';
const YOUTUBE_API_URL = 'https://www.googleapis.com/youtube/v3';
const YOUTUBE_SCOPE = 'https://www.googleapis.com/auth/youtube.force-ssl';

function createAccounts(directory, request = fetch) {
  const file = path.join(directory, 'accounts-private.json');
  const pending = new Map();
  const refreshing = new Map();
  const youtubePending = new Map();
  const read = (name) => fs.existsSync(name) ? JSON.parse(fs.readFileSync(name, 'utf8')) : {};
  let accounts = read(file);

  const save = () => {
    fs.writeFileSync(`${file}.tmp`, JSON.stringify(accounts), { mode: 0o600 });
    fs.renameSync(`${file}.tmp`, file);
    try { fs.chmodSync(file, 0o600); } catch (_) {}
  };

  const youtubeTokenNeedsRefresh = (account) =>
    !account?.access_token || !Number(account.expiresAt) ||
    Number(account.expiresAt) <= Date.now() + 60000;

  const clearYoutubeAccount = () => {
    if (accounts.youtube) {
      delete accounts.youtube;
      save();
    }
  };

  const postForm = async (url, fields) => {
    const response = await request(url, { method: 'POST', body: new URLSearchParams(fields), timeout: 15000 });
    const result = await response.json();
    if (!response.ok) {
      const error = new Error(result.message || result.error_description || `O serviço recusou a operação (HTTP ${response.status}).`);
      error.code = result.message || result.error;
      throw error;
    }
    return result;
  };

  const requestJson = async (url, options = {}) => {
    const response = await request(url, { ...options, timeout: 15000 });
    if (response.status === 204) return {};
    const result = await response.json();
    if (!response.ok) throw new Error(result.error?.message || result.message || `O serviço recusou a operação (HTTP ${response.status}). Reconecte a conta se a autorização expirou.`);
    return result;
  };

  const exchange = async (fields) => {
    const result = await postForm(TWITCH_TOKEN_URL, { ...fields, client_id: TWITCH_CLIENT_ID });
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

  const youtubeAccess = async () => {
    let account = accounts.youtube;
    const app = accounts.youtubeApp;
    if (!account || !app?.clientId) throw new Error('Conecte sua conta do YouTube primeiro.');
    if (youtubeTokenNeedsRefresh(account)) {
      if (!account.refresh_token) throw new Error('A autorização do YouTube expirou. Conecte novamente.');
      if (!refreshing.has('youtube')) {
        refreshing.set('youtube', (async () => {
          const fields = { client_id: app.clientId, refresh_token: account.refresh_token, grant_type: 'refresh_token' };
          if (app.clientSecret) fields.client_secret = app.clientSecret;
          try {
            const token = await postForm(YOUTUBE_TOKEN_URL, fields);
            accounts.youtube = { ...account, ...token, refresh_token: token.refresh_token || account.refresh_token, expiresAt: Date.now() + Number(token.expires_in || 3600) * 1000 };
            save();
          } catch (error) {
            if (error.code === 'invalid_grant' || error.code === 'unauthorized_client' || error.code === 'invalid_client')
              clearYoutubeAccount();
            throw error;
          }
        })().finally(() => refreshing.delete('youtube')));
      }
      await refreshing.get('youtube');
      account = accounts.youtube;
    }
    return account;
  };

  const youtubeRequest = async (endpoint, options = {}) => {
    const account = await youtubeAccess();
    return requestJson(`${YOUTUBE_API_URL}${endpoint}`, {
      ...options,
      headers: { Authorization: `Bearer ${account.access_token}`, 'Content-Type': 'application/json', ...options.headers },
    });
  };

  return {
    status() {
      const account = accounts.twitch;
      const granted = new Set(account?.scope || []);
      const missingScopes = TWITCH_SCOPES.split(' ').filter((scope) => !granted.has(scope));
      return { connected: Boolean(account), name: account?.name || '', needsReconnect: Boolean(account && missingScopes.length), missingScopes };
    },

    youtubeStatus() {
      const account = accounts.youtube;
      const hasAccess = Boolean(account?.access_token);
      const hasRefresh = Boolean(account?.refresh_token);
      return {
        connected: Boolean(hasAccess || hasRefresh),
        configured: Boolean(accounts.youtubeApp?.clientId),
        clientId: accounts.youtubeApp?.clientId || '',
        name: account?.name || '',
        needsReconnect: Boolean(account && !hasRefresh),
      };
    },

    async startYoutube({ clientId, clientSecret, redirectUri }) {
      clientId = String(clientId || accounts.youtubeApp?.clientId || '').trim();
      clientSecret = String(clientSecret || accounts.youtubeApp?.clientSecret || '').trim();
      redirectUri = String(redirectUri || '').trim();
      if (!clientId) throw new Error('Informe o Client ID OAuth do Google para conectar o YouTube.');
      if (!redirectUri || !redirectUri.startsWith('http://127.0.0.1:')) throw new Error('Callback local do YouTube inválido.');
      accounts.youtubeApp = { clientId, clientSecret };
      save();
      const state = crypto.randomBytes(24).toString('hex');
      youtubePending.clear();
      const cleanupTimer = setTimeout(() => youtubePending.delete(state), 10 * 60 * 1000);
      cleanupTimer.unref?.();

      const verifier = crypto.randomBytes(48).toString('base64url');
      const challenge = crypto.createHash('sha256').update(verifier).digest('base64url');
      youtubePending.set(state, { verifier, redirectUri, expiresAt: Date.now() + 10 * 60 * 1000 });
      const params = new URLSearchParams({
        client_id: clientId, redirect_uri: redirectUri, response_type: 'code', scope: YOUTUBE_SCOPE,
        access_type: 'offline', prompt: 'consent', include_granted_scopes: 'true', state,
        code_challenge: challenge, code_challenge_method: 'S256',
      });
      return { authorizationUrl: `${YOUTUBE_AUTH_URL}?${params}`, state };
    },

    async finishYoutube(state, code) {
      const stateKey = String(state || '').trim();
      const authorizationCode = String(code || '').trim();
      const flow = youtubePending.get(stateKey);
      if (!flow || flow.expiresAt < Date.now()) {
        youtubePending.delete(stateKey);
        throw new Error('A autorização do YouTube expirou. Tente conectar novamente.');
      }
      if (!authorizationCode) throw new Error('O Google não retornou um código de autorização.');
      const app = accounts.youtubeApp;
      if (!app?.clientId) throw new Error('Client ID do YouTube não configurado.');
      const fields = { client_id: app.clientId, code: authorizationCode, code_verifier: flow.verifier, redirect_uri: flow.redirectUri, grant_type: 'authorization_code' };
      if (app.clientSecret) fields.client_secret = app.clientSecret;
      const token = await postForm(YOUTUBE_TOKEN_URL, fields);
      const account = { ...token, expiresAt: Date.now() + Number(token.expires_in || 3600) * 1000 };
      const channels = await requestJson(`${YOUTUBE_API_URL}/channels?part=id,snippet&mine=true`, { headers: { Authorization: `Bearer ${account.access_token}` } });
      const channel = channels.items?.[0];
      if (!channel) throw new Error('O Google não retornou um canal do YouTube para esta conta.');
      accounts.youtube = { ...account, channelId: channel.id, name: channel.snippet?.title || '' };
      youtubePending.delete(stateKey);
      save();
      return accounts.youtube;
    },

    async start() {
      const device = await postForm(TWITCH_DEVICE_URL, { client_id: TWITCH_CLIENT_ID, scopes: TWITCH_SCOPES });
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

    async request(endpoint, options = {}) {
      const account = await access();
      const headers = { Authorization: `Bearer ${account.access_token}`, 'Client-Id': TWITCH_CLIENT_ID, 'Content-Type': 'application/json' };
      return requestJson(`https://api.twitch.tv/helix${endpoint}`, { ...options, headers: { ...headers, ...options.headers } });
    },

    userId: async () => (await access()).userId,
    youtubeRequest,
  };
}

module.exports = { createAccounts };
