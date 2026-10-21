const test = require('node:test');
const assert = require('node:assert/strict');
const { resolveChatConfigurations } = require('../server/chat/resolve-config');

test('enables Kick chat only when the connected account has the required authorization', () => {
  const result = resolveChatConfigurations({
    twitch: { enabled: false, channel: 'old-twitch-channel' },
    kick: { enabled: false, channel: 'old-kick-channel' },
    youtube: { enabled: false },
  }, {
    twitch: { connected: true, name: '@K4binho' },
    kick: { connected: true, needsReconnect: true, oauthMode: 'shared', name: '#K4binho' },
    youtube: { connected: true, name: 'Creator' },
  });

  assert.deepEqual(result.twitch, { enabled: true, channel: 'K4binho' });
  assert.deepEqual(result.kick, { enabled: false, channel: 'K4binho' });
  assert.equal(result.youtube.enabled, true);
});

test('preserves local fallback for supported platforms but disables TikTok chat', () => {
  const tiktok = { enabled: true, username: 'creator' };
  const result = resolveChatConfigurations({
    twitch: { enabled: true, channel: '@legacy' },
    kick: { enabled: true, channel: '#legacy-kick' },
    youtube: { enabled: true, apiKey: 'legacy-key', videoId: 'live-id' },
    tiktok,
  }, {
    twitch: { connected: false },
    kick: { connected: false },
    youtube: { connected: false },
  });

  assert.equal(result.twitch.channel, 'legacy');
  assert.equal(result.kick.channel, 'legacy-kick');
  assert.equal(result.youtube.apiKey, 'legacy-key');
  assert.deepEqual(result.tiktok, { ...tiktok, enabled: false });
});

test('enables official Kick chat for a connected shared-OAuth account with current scopes', () => {
  const result = resolveChatConfigurations({}, {
    kick: { connected: true, needsReconnect: false, oauthMode: 'shared', name: 'K4binho' },
  });
  assert.deepEqual(result.kick, { enabled: true, channel: 'K4binho' });
});
