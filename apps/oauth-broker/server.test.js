'use strict';

const assert = require('node:assert/strict');
const { after, before, test } = require('node:test');
const { createBroker, buildConfig } = require('./server');

const env = {
  BROKER_PUBLIC_BASE_URL: 'https://broker.example',
  KICK_CLIENT_ID: 'kick-client',
  KICK_CLIENT_SECRET: 'kick-secret',
  GOOGLE_CLIENT_ID: 'google-client',
  GOOGLE_CLIENT_SECRET: 'google-secret',
};

let server;
let baseUrl;

before(async () => {
  server = createBroker({ settings: buildConfig(env) }).listen(0, '127.0.0.1');
  await new Promise((resolve) => server.once('listening', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  if (server) await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
});

test('solicita todas as permissões que o plugin valida para a Kick', async () => {
  const response = await fetch(`${baseUrl}/v1/oauth/kick/start`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  });

  assert.equal(response.status, 200);
  const result = await response.json();
  const authorization = new URL(result.authorizationUri);
  const grantedScopes = new Set(authorization.searchParams.get('scope').split(' '));
  const requiredScopes = [
    'user:read',
    'channel:read',
    'channel:write',
    'streamkey:read',
    'chat:write',
    'moderation:ban',
    'events:subscribe',
  ];

  for (const scope of requiredScopes) assert.ok(grantedScopes.has(scope), `permissão ausente: ${scope}`);
});

test('recebe webhook Kick com corpo bruto antes do parser JSON', async () => {
  let captured;
  const webhookServer = createBroker({
    settings: buildConfig(env),
    kickEvents: {
      verifyAndStore: async (headers, rawBody) => {
        captured = { headers, rawBody };
        return { ok: true, accepted: true };
      },
      read: () => ({ events: [], cursor: 0 }),
    },
  }).listen(0, '127.0.0.1');
  await new Promise((resolve) => webhookServer.once('listening', resolve));
  try {
    const rawBody = '{"message":"texto exato"}';
    const response = await fetch(`http://127.0.0.1:${webhookServer.address().port}/v1/webhooks/kick`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'x-forwarded-proto': 'https', 'Kick-Event-Message-Id': 'event-1' },
      body: rawBody,
    });
    assert.equal(response.status, 200);
    assert.equal(captured.rawBody.toString('utf8'), rawBody);
    assert.equal(captured.headers['kick-event-message-id'], 'event-1');
  } finally {
    await new Promise((resolve, reject) => webhookServer.close((error) => error ? reject(error) : resolve()));
  }
});

test('mantém termos e política de privacidade públicos', async () => {
  for (const [path, expected] of [['/termos', 'Termos de Uso'], ['/privacidade', 'Política de Privacidade']]) {
    const response = await fetch(`${baseUrl}${path}`, { headers: { 'x-forwarded-proto': 'https' } });
    assert.equal(response.status, 200);
    assert.match(await response.text(), new RegExp(expected));
  }
});

test('publica avisos e links requeridos para uso da API do YouTube', async () => {
  const privacy = await fetch(`${baseUrl}/privacidade`, { headers: { 'x-forwarded-proto': 'https' } });
  const privacyHtml = await privacy.text();
  assert.match(privacyHtml, /accounts-private\.json/);
  assert.match(privacyHtml, /youtube\.com\/t\/terms/);
  assert.match(privacyHtml, /policies\.google\.com\/privacy/);
  assert.match(privacyHtml, /Render/);
  assert.match(privacyHtml, /webhook do Kick envia mensagens ao broker/);
  assert.match(privacyHtml, /até 100 eventos recentes/);

  const terms = await fetch(`${baseUrl}/termos`, { headers: { 'x-forwarded-proto': 'https' } });
  const termsHtml = await terms.text();
  assert.match(termsHtml, /concorda em cumprir os/);
  assert.match(termsHtml, /developers\.google\.com\/youtube\/terms\/developer-policies/);
});

test('publica a verificação de propriedade na página inicial', async () => {
  const response = await fetch(`${baseUrl}/`);
  assert.equal(response.status, 200);
  assert.match(await response.text(), /google-site-verification" content="KwRm3dysSljZzYimU4XSbiEnY9-yCDcD6Vi_Nu8c3aI/);
});
