'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const { test } = require('node:test');
const { createKickEventStore, PUBLIC_KEY_URL } = require('./kick-events');

function signedDelivery(privateKey, overrides = {}) {
  const rawBody = Buffer.from(JSON.stringify({
    message_id: 'message-1',
    broadcaster: { user_id: 77 },
    sender: { username: 'viewer', identity: { username_color: '#123456', badges: [{ type: 'subscriber' }] } },
    content: 'Olá chat',
    created_at: '2026-01-02T03:04:05.000Z',
  }));
  const headers = {
    'kick-event-message-id': 'delivery-1',
    'kick-event-message-timestamp': new Date(1_767_321_845_000).toISOString(),
    'kick-event-type': 'chat.message.sent',
    'kick-event-version': '1',
    ...overrides,
  };
  const signed = Buffer.concat([Buffer.from(`${headers['kick-event-message-id']}.${headers['kick-event-message-timestamp']}.`), rawBody]);
  headers['kick-event-signature'] = crypto.sign('RSA-SHA256', signed, privateKey).toString('base64');
  return { headers, rawBody };
}

test('valida a assinatura Kick e entrega mensagens somente ao canal correto', async () => {
  let now = 1_767_321_845_000;
  const { privateKey, publicKey } = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
  const store = createKickEventStore({
    now: () => now,
    fetchImpl: async (url) => {
      assert.equal(url, PUBLIC_KEY_URL);
      return { ok: true, json: async () => ({ data: { public_key: publicKey.export({ type: 'spki', format: 'pem' }) } }) };
    },
  });
  const delivery = signedDelivery(privateKey);

  assert.deepEqual(await store.verifyAndStore(delivery.headers, delivery.rawBody), { ok: true, accepted: true });
  assert.deepEqual(await store.verifyAndStore(delivery.headers, delivery.rawBody), { ok: true, duplicate: true });
  const received = store.read('77', 0);
  assert.equal(received.events.length, 1);
  assert.deepEqual(received.events[0], {
    platform: 'kick', id: 'message-1', user: 'viewer', color: '#123456',
    message: 'Olá chat', badges: ['subscriber'], timestamp: Date.parse('2026-01-02T03:04:05.000Z'),
  });
  assert.equal(store.read('88', 0).events.length, 0);
  assert.equal(store.read('77', received.cursor).events.length, 0);
  now += 3_600_000;
});

test('recusa assinatura inválida e timestamp vencido', async () => {
  const now = 1_767_321_845_000;
  const { privateKey, publicKey } = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
  const store = createKickEventStore({
    now: () => now,
    fetchImpl: async () => ({ ok: true, json: async () => ({ data: { public_key: publicKey.export({ type: 'spki', format: 'pem' }) } }) }),
  });
  const badSignature = signedDelivery(privateKey);
  badSignature.headers['kick-event-signature'] = Buffer.from('forged').toString('base64');
  assert.deepEqual(await store.verifyAndStore(badSignature.headers, badSignature.rawBody), { ok: false, reason: 'invalid_signature' });

  const old = signedDelivery(privateKey, { 'kick-event-message-id': 'old', 'kick-event-message-timestamp': new Date(now - 600_000).toISOString() });
  assert.deepEqual(await store.verifyAndStore(old.headers, old.rawBody), { ok: false, reason: 'invalid_headers' });
});
