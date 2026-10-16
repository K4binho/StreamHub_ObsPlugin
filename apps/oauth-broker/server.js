'use strict';

const crypto = require('crypto');
const express = require('express');

const PLATFORMS = {
  kick: {
    clientIdEnv: 'KICK_CLIENT_ID',
    clientSecretEnv: 'KICK_CLIENT_SECRET',
    authorizeUrl: 'https://id.kick.com/oauth/authorize',
    tokenUrl: 'https://id.kick.com/oauth/token',
    apiBase: 'https://api.kick.com/public/v1',
    scopes: 'user:read channel:read channel:write streamkey:read chat:write moderation:ban',
    identity: 'kick',
  },
  youtube: {
    clientIdEnv: 'GOOGLE_CLIENT_ID',
    clientSecretEnv: 'GOOGLE_CLIENT_SECRET',
    authorizeUrl: 'https://accounts.google.com/o/oauth2/v2/auth',
    tokenUrl: 'https://oauth2.googleapis.com/token',
    apiBase: 'https://www.googleapis.com/youtube/v3',
    scopes: 'https://www.googleapis.com/auth/youtube.force-ssl',
    identity: 'youtube',
  },
};

const TRANSACTION_TTL_MS = 10 * 60 * 1000;
const REQUEST_TIMEOUT_MS = 15000;
const transactions = new Map();
const rateBuckets = new Map();

function requiredHttpsUrl(value, name) {
  const url = String(value || '').trim().replace(/\/+$/, '');
  if (!/^https:\/\//i.test(url)) throw new Error(`${name} precisa usar HTTPS.`);
  return url;
}

function buildConfig(env = process.env) {
  const baseUrl = requiredHttpsUrl(env.BROKER_PUBLIC_BASE_URL, 'BROKER_PUBLIC_BASE_URL');
  const config = {};
  for (const [platform, provider] of Object.entries(PLATFORMS)) {
    const redirectEnv = platform === 'kick' ? 'KICK_REDIRECT_URI' : 'YOUTUBE_REDIRECT_URI';
    const redirectUri = requiredHttpsUrl(
      env[redirectEnv] || `${baseUrl}/v1/oauth/${platform}/callback`,
      redirectEnv,
    );
    if (!redirectUri.startsWith(`${baseUrl}/`)) {
      throw new Error(`${redirectEnv} precisa pertencer ao domínio público do broker.`);
    }
    config[platform] = {
      ...provider,
      clientId: String(env[provider.clientIdEnv] || '').trim(),
      clientSecret: String(env[provider.clientSecretEnv] || '').trim(),
      redirectUri,
    };
  }
  return { baseUrl, config };
}

function randomToken(bytes = 32) {
  return crypto.randomBytes(bytes).toString('base64url');
}

function safeEqual(left, right) {
  const a = Buffer.from(String(left || ''));
  const b = Buffer.from(String(right || ''));
  return a.length === b.length && a.length > 0 && crypto.timingSafeEqual(a, b);
}

function cleanupTransactions(now = Date.now()) {
  for (const [id, transaction] of transactions) {
    if (transaction.expiresAt <= now) transactions.delete(id);
  }
}

function clientIp(req) {
  return String(req.ip || req.socket?.remoteAddress || 'unknown').slice(0, 80);
}

function rateLimit(name, limit, windowMs) {
  return (req, res, next) => {
    const key = `${name}:${clientIp(req)}`;
    const now = Date.now();
    const bucket = rateBuckets.get(key);
    if (!bucket || bucket.expiresAt <= now) {
      rateBuckets.set(key, { count: 1, expiresAt: now + windowMs });
      next();
      return;
    }
    bucket.count += 1;
    if (bucket.count > limit) {
      res.set('Retry-After', String(Math.ceil((bucket.expiresAt - now) / 1000)));
      res.status(429).json({ error: 'Muitas tentativas. Aguarde e tente novamente.' });
      return;
    }
    next();
  };
}

function publicError(res, status, message = 'Não foi possível concluir autorização.') {
  res.status(status).json({ error: message });
}

function providerError(platform) {
  return new Error(`O provedor ${platform} recusou autorização.`);
}

async function providerJson(url, options, platform) {
  let response;
  try {
    response = await fetch(url, {
      ...options,
      signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
    });
  } catch (_) {
    throw providerError(platform);
  }
  let body = {};
  try {
    body = await response.json();
  } catch (_) {
    body = {};
  }
  if (!response.ok) throw providerError(platform);
  return body;
}

function tokenFields(result, platform) {
  const accessToken = String(result.access_token || '').trim();
  if (!accessToken) throw providerError(platform);
  return {
    access_token: accessToken,
    refresh_token: String(result.refresh_token || '').trim(),
    token_type: String(result.token_type || 'Bearer'),
    expires_in: Math.max(60, Number(result.expires_in) || 3600),
    scope: Array.isArray(result.scope) ? result.scope : String(result.scope || '').trim(),
  };
}

async function exchangeCode(provider, platform, code, verifier) {
  const body = new URLSearchParams({
    grant_type: 'authorization_code',
    code,
    client_id: provider.clientId,
    client_secret: provider.clientSecret,
    redirect_uri: provider.redirectUri,
    code_verifier: verifier,
  });
  const result = await providerJson(provider.tokenUrl, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded', Accept: 'application/json' },
    body,
  }, platform);
  return tokenFields(result, platform);
}

async function refreshToken(provider, platform, refreshTokenValue) {
  const body = new URLSearchParams({
    grant_type: 'refresh_token',
    refresh_token: refreshTokenValue,
    client_id: provider.clientId,
    client_secret: provider.clientSecret,
  });
  const result = await providerJson(provider.tokenUrl, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded', Accept: 'application/json' },
    body,
  }, platform);
  return tokenFields(result, platform);
}

async function identify(provider, platform, token) {
  if (platform === 'kick') {
    const result = await providerJson(`${provider.apiBase}/users`, {
      headers: { Authorization: `Bearer ${token.access_token}`, Accept: 'application/json' },
    }, platform);
    const user = result.data?.[0];
    const userId = user?.user_id ?? user?.id;
    if (!userId) throw providerError(platform);
    return { userId: String(userId), name: String(user.name || user.username || userId) };
  }
  const result = await providerJson(`${provider.apiBase}/channels?part=snippet&mine=true`, {
    headers: { Authorization: `Bearer ${token.access_token}`, Accept: 'application/json' },
  }, platform);
  const channel = result.items?.[0];
  if (!channel?.id) throw providerError(platform);
  return { userId: String(channel.id), name: String(channel.snippet?.title || channel.id) };
}

function authorizationUrl(provider, platform, state, challenge) {
  const url = new URL(provider.authorizeUrl);
  url.search = new URLSearchParams({
    client_id: provider.clientId,
    redirect_uri: provider.redirectUri,
    response_type: 'code',
    scope: provider.scopes,
    state,
    code_challenge: challenge,
    code_challenge_method: 'S256',
    ...(platform === 'youtube' ? { access_type: 'offline', prompt: 'consent' } : {}),
  }).toString();
  return url.toString();
}

function callbackPage(connected) {
  const heading = connected ? 'Conta conectada com sucesso' : 'Não foi possível conectar conta';
  const message = connected
    ? 'Autorização concluída. Volte ao OBS para continuar.'
    : 'Autorização não concluída. Feche esta janela e tente novamente no OBS.';
  return `<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="referrer" content="no-referrer"><meta name="robots" content="noindex"><title>StreamHub</title><style>:root{color-scheme:dark;font-family:Segoe UI,sans-serif;background:#080c14;color:#f2f7ff}body{min-height:100vh;margin:0;display:grid;place-items:center}main{width:min(430px,calc(100vw - 32px));box-sizing:border-box;padding:28px;border:1px solid ${connected ? '#16d86a' : '#d94155'};border-radius:14px;background:#0d1726;text-align:center}h1{margin:0 0 10px;font-size:22px}p{margin:0;color:#b8c8dc;line-height:1.5}.icon{width:48px;height:48px;margin:0 auto 14px;border-radius:50%;display:grid;place-items:center;background:${connected ? '#16d86a' : '#d94155'};color:#080c14;font-size:28px;font-weight:800}</style></head><body><main><div class="icon">${connected ? '✓' : '!'}</div><h1>${heading}</h1><p>${message}</p></main><script>history.replaceState({},document.title,window.location.pathname)</script></body></html>`;
}

function legalPage(kind) {
  const privacy = kind === 'privacy';
  const title = privacy ? 'Política de Privacidade' : 'Termos de Uso';
  const intro = privacy
    ? 'Esta página explica como o StreamHub trata as informações usadas para conectar contas de transmissão.'
    : 'Estes termos descrevem as condições básicas para usar o StreamHub e o serviço de autorização OAuth.';
  const body = privacy ? `
    <h2>Dados acessados</h2>
    <p>Quando você conecta o YouTube ou a Kick, o StreamHub solicita somente as permissões necessárias para os recursos escolhidos, como identificar o canal, ler dados da transmissão, atualizar informações e usar o chat quando a plataforma permitir.</p>
    <h2>Como os dados são tratados</h2>
    <p>O broker recebe a autorização da plataforma, troca o código por tokens e entrega o resultado ao servidor local do StreamHub por uma transação temporária. A transação expira automaticamente e o broker não mantém uma cópia permanente dos tokens.</p>
    <p>O servidor local do StreamHub armazena os tokens no computador do usuário, em arquivo privado da instalação, para renovar a autorização. O StreamHub não vende dados nem usa as informações da conta para publicidade.</p>
    <h2>Compartilhamento e segurança</h2>
    <p>As credenciais não são exibidas em telas, logs ou URLs comuns. O fluxo usa HTTPS, estado de autorização, PKCE, expiração e proteção contra reutilização da transação. Os dados são enviados apenas ao provedor escolhido pelo usuário e ao próprio StreamHub.</p>
    <h2>Revogação e exclusão</h2>
    <p>Você pode revogar o acesso nas configurações da sua conta Google, Kick ou YouTube. Também pode remover os dados locais da conta pela instalação do StreamHub. Ao revogar o acesso, os recursos que dependem daquela conta deixam de funcionar até uma nova autorização.</p>
    <h2>Contato</h2>
    <p>Para dúvidas, sugestões ou solicitações relacionadas a dados, use o <a href="https://github.com/K4binho/StreamHub_ObsPlugin/issues">repositório oficial do projeto</a>.</p>
  ` : `
    <h2>Uso permitido</h2>
    <p>O StreamHub é uma ferramenta para gerenciar transmissões, chats e informações de contas nas plataformas compatíveis. Você deve usar o aplicativo de acordo com as regras do OBS, do YouTube, da Kick, do Google e das demais plataformas conectadas.</p>
    <h2>Autorização de contas</h2>
    <p>O usuário escolhe quais contas conectar e quais permissões conceder. O StreamHub não solicita a senha da plataforma. A autorização acontece na página oficial do provedor e pode ser revogada a qualquer momento.</p>
    <h2>Disponibilidade</h2>
    <p>Os recursos dependem da disponibilidade, das APIs, das permissões e das regras de cada plataforma. O serviço pode ser alterado, interrompido ou limitado sem garantia de disponibilidade contínua.</p>
    <h2>Responsabilidade</h2>
    <p>Você é responsável pelo uso das suas contas, chaves de transmissão, mensagens e configurações. Não compartilhe tokens, chaves ou outras credenciais. O StreamHub não é afiliado ao Google, YouTube, Kick ou OBS Studio.</p>
    <h2>Contato</h2>
    <p>Para dúvidas ou sugestões, use o <a href="https://github.com/K4binho/StreamHub_ObsPlugin/issues">repositório oficial do projeto</a>.</p>
  `;
  return `<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="robots" content="index,follow"><title>${title} — StreamHub</title><style>:root{color-scheme:dark;font-family:Segoe UI,system-ui,sans-serif;background:#080c14;color:#f2f7ff}body{margin:0;padding:32px 16px;line-height:1.6}main{max-width:760px;margin:auto;padding:32px;border:1px solid #1c2b40;border-radius:14px;background:#0d1726}h1{margin-top:0;color:#fff}h2{margin-top:28px;color:#8fc7ff;font-size:18px}p{color:#c2cfdf}a{color:#67b7ff}footer{margin-top:32px;padding-top:16px;border-top:1px solid #1c2b40;color:#8394aa;font-size:13px}</style></head><body><main><h1>${title}</h1><p>${intro}</p>${body}<footer>StreamHub · Última atualização: ${new Date().toISOString().slice(0, 10)}</footer></main></body></html>`;
}

function brokerStatusPage(settings) {
  const kickReady = Boolean(settings.config.kick.clientId && settings.config.kick.clientSecret);
  const youtubeReady = Boolean(settings.config.youtube.clientId && settings.config.youtube.clientSecret);
  const row = (name, ready) => `<tr><td>${name}</td><td><span class="dot ${ready ? 'ok' : 'off'}"></span>${ready ? 'Online' : 'Configuração pendente'}</td></tr>`;
  return `<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="referrer" content="no-referrer"><meta name="robots" content="index,follow"><title>StreamHub OAuth Broker</title><style>:root{color-scheme:dark;font-family:Segoe UI,sans-serif;background:#080c14;color:#f2f7ff}body{min-height:100vh;margin:0;display:grid;place-items:center;padding:24px;box-sizing:border-box}main{width:min(560px,100%);box-sizing:border-box;padding:30px;border:1px solid #1c2b40;border-radius:14px;background:#0d1726}h1{margin:0 0 8px;font-size:24px}p{margin:0 0 24px;color:#b8c8dc;line-height:1.5}table{width:100%;border-collapse:collapse}td{padding:12px 8px;border-bottom:1px solid #1c2b40}td:last-child{text-align:right}.dot{display:inline-block;width:9px;height:9px;margin-right:8px;border-radius:50%}.ok{background:#16d86a}.off{background:#d94155}footer{margin-top:22px;color:#72839a;font-size:12px}nav{margin-top:18px;display:flex;gap:16px;flex-wrap:wrap}</style></head><body><main><h1>StreamHub OAuth Broker</h1><p>Serviço compartilhado de autorização OAuth para StreamHub.</p><table><tbody><tr><td>Broker</td><td><span class="dot ok"></span>Online</td></tr>${row('Kick OAuth', kickReady)}${row('YouTube OAuth', youtubeReady)}</tbody></table><nav><a href="/privacidade">Política de Privacidade</a><a href="/termos">Termos de Uso</a></nav><footer>Health check: <a href="/healthz">/healthz</a></footer></main></body></html>`;
}

function createBroker(options = {}) {
  const env = options.env || process.env;
  const settings = options.settings || buildConfig(env);
  const app = express();
  app.disable('x-powered-by');
  app.set('trust proxy', 1);
  app.use(express.json({ limit: '8kb' }));
  app.use((req, res, next) => {
    res.set({
      'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff',
      'Referrer-Policy': 'no-referrer',
    });
    if (req.path !== '/healthz' && String(req.get('x-forwarded-proto') || 'https').toLowerCase() !== 'https') {
      res.status(400).json({ error: 'HTTPS obrigatório.' });
      return;
    }
    cleanupTransactions();
    next();
  });

  app.get('/', (_req, res) => {
    res.type('html').send(brokerStatusPage(settings));
  });

  app.get('/privacidade', (_req, res) => res.type('html').send(legalPage('privacy')));
  app.get('/termos', (_req, res) => res.type('html').send(legalPage('terms')));

  app.get('/healthz', (_req, res) => res.json({ ok: true }));

  for (const platform of Object.keys(PLATFORMS)) {
    const provider = settings.config[platform];
    const prefix = `/v1/oauth/${platform}`;
    const configured = () => Boolean(provider.clientId && provider.clientSecret);

    app.post(`${prefix}/start`, rateLimit(`${platform}:start`, 10, 60000), (req, res) => {
      if (!configured()) return publicError(res, 503, 'OAuth compartilhado indisponível.');
      // Broker credentials stay authoritative; desktop clients send no client secret
      // and do not need to match a locally stored Client ID.
      const verifier = randomToken(48);
      const state = randomToken(32);
      const transactionId = randomToken(24);
      const claimToken = randomToken(32);
      const challenge = crypto.createHash('sha256').update(verifier).digest('base64url');
      transactions.set(transactionId, {
        platform,
        state,
        verifier,
        claimToken,
        expiresAt: Date.now() + TRANSACTION_TTL_MS,
        status: 'pending',
        claimed: false,
      });
      res.json({
        transactionId,
        claimToken,
        authorizationUri: authorizationUrl(provider, platform, state, challenge),
        expiresIn: Math.floor(TRANSACTION_TTL_MS / 1000),
        interval: 3,
      });
    });

    app.get(`${prefix}/callback`, rateLimit(`${platform}:callback`, 30, 60000), async (req, res) => {
      const state = String(req.query?.state || '');
      const transaction = Array.from(transactions.values()).find((item) => item.platform === platform && safeEqual(item.state, state));
      if (!transaction || transaction.expiresAt <= Date.now() || transaction.status !== 'pending') {
        res.status(400).type('html').send(callbackPage(false));
        return;
      }
      if (req.query?.error || !req.query?.code) {
        transaction.status = 'error';
        transaction.error = 'Autorização recusada ou incompleta.';
        res.status(400).type('html').send(callbackPage(false));
        return;
      }
      try {
        const token = await exchangeCode(provider, platform, String(req.query.code), transaction.verifier);
        const identity = await identify(provider, platform, token);
        transaction.status = 'connected';
        transaction.payload = { ...token, ...identity };
        res.type('html').send(callbackPage(true));
      } catch (_) {
        transaction.status = 'error';
        transaction.error = 'O provedor recusou autorização.';
        res.status(502).type('html').send(callbackPage(false));
      }
    });

    app.get(`${prefix}/poll/:transactionId`, rateLimit(`${platform}:poll`, 120, 60000), (req, res) => {
      const transaction = transactions.get(String(req.params.transactionId || ''));
      const supplied = String(req.get('authorization') || '').replace(/^Bearer\s+/i, '');
      if (!transaction || transaction.platform !== platform || !safeEqual(transaction.claimToken, supplied)) {
        publicError(res, 401, 'Autorização de consulta inválida.');
        return;
      }
      if (transaction.expiresAt <= Date.now()) {
        transactions.delete(String(req.params.transactionId));
        publicError(res, 410, 'Autorização expirada.');
        return;
      }
      if (transaction.claimed) {
        publicError(res, 410, 'Resultado de autorização já entregue.');
        return;
      }
      if (transaction.status === 'pending') {
        res.json({ state: 'pending' });
        return;
      }
      if (transaction.status === 'error') {
        transaction.claimed = true;
        res.status(400).json({ state: 'error', error: transaction.error || 'Autorização não concluída.' });
        return;
      }
      transaction.claimed = true;
      res.json({ state: 'connected', ...transaction.payload });
    });

    app.post(`${prefix}/refresh`, rateLimit(`${platform}:refresh`, 30, 60000), async (req, res) => {
      if (!configured()) return publicError(res, 503, 'OAuth compartilhado indisponível.');
      const refreshTokenValue = String(req.body?.refreshToken || '').trim();
      if (!refreshTokenValue || refreshTokenValue.length > 4096) {
        return publicError(res, 400, 'Refresh token inválido.');
      }
      try {
        res.json(await refreshToken(provider, platform, refreshTokenValue));
      } catch (_) {
        publicError(res, 401, 'Autorização expirada. Conecte a conta novamente.');
      }
    });
  }

  return app;
}

function start() {
  const app = createBroker();
  const port = Number(process.env.PORT) || 10000;
  app.listen(port, '0.0.0.0', () => {
    process.stdout.write(`[streamhub-oauth] ouvindo na porta ${port}\n`);
  });
}

if (require.main === module) {
  try {
    start();
  } catch (error) {
    process.stderr.write(`[streamhub-oauth] ${error.message}\n`);
    process.exitCode = 1;
  }
}

module.exports = { createBroker, buildConfig, PLATFORMS };
