'use strict';

const assert = require('node:assert/strict');
const { test } = require('node:test');
const { startKick } = require('../server/chat/kick');

test('consulta eventos oficiais da Kick e publica mensagens recebidas', async () => {
  const messages = [];
  const statuses = [];
  let calls = 0;
  const connector = await startKick(
    { enabled: true, channel: 'canal' },
    (message) => messages.push(message),
    (state, detail) => statuses.push({ state, detail }),
    {
      async kickWebhookEvents(since) {
        calls += 1;
        assert.equal(since, 0);
        return {
          events: [{ platform: 'kick', id: 'm1', user: 'viewer', message: 'oi', badges: [], timestamp: 1 }],
          cursor: 7,
        };
      },
    },
  );
  connector.stop();

  assert.equal(calls, 1);
  assert.equal(messages.length, 1);
  assert.equal(messages[0].platform, 'kick');
  assert.ok(statuses.some((status) => status.state === 'connected'));
});
