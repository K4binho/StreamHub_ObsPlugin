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
  ];

  for (const scope of requiredScopes) assert.ok(grantedScopes.has(scope), `permissão ausente: ${scope}`);
});

test('mantém termos e política de privacidade públicos', async () => {
  for (const [path, expected] of [['/termos', 'Termos de Uso'], ['/privacidade', 'Política de Privacidade']]) {
    const response = await fetch(`${baseUrl}${path}`, { headers: { 'x-forwarded-proto': 'https' } });
    assert.equal(response.status, 200);
    assert.match(await response.text(), new RegExp(expected));
  }
});

test('publica a verificação de propriedade na página inicial', async () => {
  const response = await fetch(`${baseUrl}/`);
  assert.equal(response.status, 200);
  assert.match(await response.text(), /google-site-verification" content="KwRm3dysSljZzYimU4XSbiEnY9-yCDcD6Vi_Nu8c3aI/);
});
