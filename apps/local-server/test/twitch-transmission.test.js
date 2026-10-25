const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { createAccounts } = require('../server/accounts');

function fixture(t, request) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'streamhub-twitch-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  fs.writeFileSync(path.join(directory, 'accounts-private.json'), JSON.stringify({
    twitch: { access_token: 'fixture-token', userId: '123', name: 'fixture-channel', expiresAt: Date.now() + 3600000 },
  }));
  return createAccounts(directory, request);
}

test('Twitch transmission reads the authorized key and channel without writes', async (t) => {
  const calls = [];
  const accounts = fixture(t, async (url, options) => {
    calls.push(url);
    assert.equal(options.method, undefined);
    assert.equal(options.headers.Authorization, 'Bearer fixture-token');
    assert.equal(new URL(url).searchParams.get('broadcaster_id'), '123');
    if (new URL(url).pathname === '/helix/streams/key') {
      return { ok: true, status: 200, json: async () => ({ data: [{ stream_key: 'fixture-key' }] }) };
    }
    assert.equal(new URL(url).pathname, '/helix/channels');
    return { ok: true, status: 200, json: async () => ({ data: [{
      title: 'Live de teste', game_id: '42', game_name: 'Jogo', tags: ['Português'],
      broadcaster_language: 'pt', content_classification_labels: [{ id: 'Gambling', is_enabled: false }],
    }] }) };
  });
  const result = await accounts.twitchTransmission();
  assert.equal(result.server, 'rtmps://live.twitch.tv/app');
  assert.equal(result.streamKey, 'fixture-key');
  assert.equal(result.title, 'Live de teste');
  assert.deepEqual(result.tags, ['Português']);
  assert.deepEqual(result.classificationLabels, []);
  assert.equal(calls.length, 2);
});

test('Twitch transmission rejects a missing key instead of returning success', async (t) => {
  const accounts = fixture(t, async () => ({ ok: true, status: 200, json: async () => ({ data: [] }) }));
  await assert.rejects(accounts.twitchTransmission(), /não forneceu servidor RTMP e stream key/);
});

test('Twitch transmission preserves permission failure', async (t) => {
  const accounts = fixture(t, async () => ({
    ok: false, status: 403, json: async () => ({ message: 'Missing scope channel:read:stream_key' }),
  }));
  await assert.rejects(accounts.twitchTransmission(), /HTTP 403.*channel:read:stream_key/);
});

test('Twitch metadata rejects a missing channel', async (t) => {
  const accounts = fixture(t, async () => ({ ok: true, status: 200, json: async () => ({ data: [] }) }));
  await assert.rejects(accounts.twitchBroadcastContent(), /não retornou os dados do canal/);
});
