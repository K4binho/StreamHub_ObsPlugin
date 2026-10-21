'use strict';

const crypto = require('node:crypto');

const PUBLIC_KEY_URL = 'https://api.kick.com/public/v1/public-key';
const MAX_AGE_MS = 5 * 60 * 1000;
const MAX_EVENTS = 1000;
const EVENT_TTL_MS = 5 * 60 * 1000;
const MAX_EVENTS_PER_POLL = 100;

function createKickEventStore({ fetchImpl = fetch, now = Date.now } = {}) {
  let publicKey;
  let publicKeyExpiresAt = 0;
  let nextCursor = 1;
  const events = [];
  const seen = new Map();

  function pruneEvents() {
    const cutoff = now() - EVENT_TTL_MS;
    while (events.length && events[0].receivedAt <= cutoff) events.shift();
  }

  async function getPublicKey(forceRefresh = false) {
    if (!forceRefresh && publicKey && publicKeyExpiresAt > now()) return publicKey;
    const response = await fetchImpl(PUBLIC_KEY_URL, { signal: AbortSignal.timeout(10000) });
    if (!response.ok) throw new Error('Não foi possível consultar a chave pública da Kick.');
    const result = await response.json();
    const pem = String(result?.data?.public_key || '').trim();
    if (!pem) throw new Error('A Kick não retornou a chave pública.');
    publicKey = crypto.createPublicKey(pem);
    publicKeyExpiresAt = now() + 60 * 60 * 1000;
    return publicKey;
  }

  async function verifyAndStore(headers, rawBody) {
    const messageId = String(headers['kick-event-message-id'] || '').trim();
    const timestamp = String(headers['kick-event-message-timestamp'] || '').trim();
    const signature = String(headers['kick-event-signature'] || '').trim();
    const type = String(headers['kick-event-type'] || '').trim();
    const time = Date.parse(timestamp);
    if (!messageId || !signature || !Number.isFinite(time) || Math.abs(now() - time) > MAX_AGE_MS) {
      return { ok: false, reason: 'invalid_headers' };
    }
    const signed = Buffer.concat([
      Buffer.from(`${messageId}.${timestamp}.`, 'utf8'),
      Buffer.isBuffer(rawBody) ? rawBody : Buffer.from(rawBody),
    ]);
    let valid = false;
    try { valid = crypto.verify('RSA-SHA256', signed, await getPublicKey(), Buffer.from(signature, 'base64')); }
    catch (_) { return { ok: false, reason: 'invalid_signature' }; }
    if (!valid) {
      try { valid = crypto.verify('RSA-SHA256', signed, await getPublicKey(true), Buffer.from(signature, 'base64')); }
      catch (_) { return { ok: false, reason: 'invalid_signature' }; }
    }
    if (!valid) return { ok: false, reason: 'invalid_signature' };
    if (seen.has(messageId)) return { ok: true, duplicate: true };
    seen.set(messageId, now() + 10 * 60 * 1000);
    for (const [id, expiry] of seen) if (expiry <= now()) seen.delete(id);
    if (type !== 'chat.message.sent') return { ok: true, ignored: true };

    let payload;
    try { payload = JSON.parse(Buffer.isBuffer(rawBody) ? rawBody.toString('utf8') : String(rawBody)); }
    catch (_) { return { ok: false, reason: 'invalid_json' }; }
    const broadcasterId = String(payload?.broadcaster?.user_id || '');
    const sender = payload?.sender || {};
    if (!broadcasterId || !payload?.message_id || typeof payload?.content !== 'string' || !sender?.username) {
      return { ok: false, reason: 'invalid_payload' };
    }
    pruneEvents();
    events.push({
      cursor: nextCursor++, broadcasterId,
      message: {
        platform: 'kick', id: String(payload.message_id), user: String(sender.username),
        color: String(sender.identity?.username_color || '#53FC18'),
        message: payload.content,
        badges: Array.isArray(sender.identity?.badges) ? sender.identity.badges.map((badge) => String(badge.type || '')).filter(Boolean) : [],
        timestamp: Date.parse(payload.created_at) || now(),
      },
      receivedAt: now(),
    });
    while (events.length > MAX_EVENTS) events.shift();
    return { ok: true, accepted: true };
  }

  function read(broadcasterId, since) {
    const cursor = Number.isSafeInteger(Number(since)) && Number(since) >= 0 ? Number(since) : 0;
    pruneEvents();
    const matching = events.filter((item) => item.broadcasterId === String(broadcasterId) && item.cursor > cursor);
    const batch = matching.slice(0, MAX_EVENTS_PER_POLL);
    return {
      events: batch.map((item) => item.message),
      cursor: batch.at(-1)?.cursor || events.at(-1)?.cursor || cursor,
    };
  }

  return { verifyAndStore, read };
}

module.exports = { createKickEventStore, PUBLIC_KEY_URL, MAX_AGE_MS, EVENT_TTL_MS, MAX_EVENTS_PER_POLL };
