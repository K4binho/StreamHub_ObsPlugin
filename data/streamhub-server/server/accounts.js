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

  const postForm = async (url, fields) => {
    const response = await request(url, { method: 'POST', body: new URLSearchParams(fields), timeout: 15000 });
    const result = await response.json();
    if (!response.ok) {
      const error = new Error(result.message || result.error_description || `A Twitch recusou a operação (HTTP ${response.status}).`);
      error.code = result.message || result.error;
      throw error;
    }
    return result;
  };

  const requestJson = async (url, options = {}) => {
    const response = await request(url, { ...options, timeout: 15000 });
    if (!response.ok) throw new Error(`A Twitch recusou a operação (HTTP ${response.status}). Reconecte a conta se a autorização expirou.`);
    return response.status === 204 ? {} : response.json();
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

  return {
    status() {
      const account = accounts.twitch;
      const granted = new Set(account?.scope || []);
      const missingScopes = TWITCH_SCOPES.split(' ').filter((scope) => !granted.has(scope));
      return { connected: Boolean(account), name: account?.name || '', needsReconnect: Boolean(account && missingScopes.length), missingScopes };
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
  };
}

module.exports = { createAccounts };
