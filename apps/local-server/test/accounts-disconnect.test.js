const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { createAccounts } = require('../server/accounts');

test('disconnect removes saved authorization while preserving app credentials', (t) => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'streamhub-accounts-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const file = path.join(directory, 'accounts-private.json');
  fs.writeFileSync(file, JSON.stringify({
    twitch: { access_token: 'twitch-token', name: 'Creator' },
    kick: { access_token: 'kick-token', refresh_token: 'kick-refresh', name: 'Creator', clientId: 'kick-client', clientSecret: 'kick-secret' },
    youtube: { access_token: 'youtube-token', name: 'Creator', clientId: 'youtube-client', clientSecret: 'youtube-secret' },
  }));
  const accounts = createAccounts(directory, async () => {});

  assert.deepEqual(accounts.disconnect('twitch'), { connected: false });
  assert.deepEqual(accounts.disconnect('kick'), { connected: false });
  assert.deepEqual(accounts.disconnect('youtube'), { connected: false });

  const saved = JSON.parse(fs.readFileSync(file, 'utf8'));
  assert.equal(Object.hasOwn(saved, 'twitch'), false);
  assert.deepEqual(saved.kick, { clientId: 'kick-client', clientSecret: 'kick-secret' });
  assert.deepEqual(saved.youtube, { clientId: 'youtube-client', clientSecret: 'youtube-secret' });
  assert.throws(() => accounts.disconnect('tiktok'), /Plataforma inválida/);
});
