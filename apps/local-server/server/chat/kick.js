'use strict';

const POLL_INTERVAL_MS = 2000;
const MAX_RETRY_MS = 30000;

async function startKick(config, onMessage, onStatus = () => {}, accounts) {
  let stopped = false;
  let timer = null;
  let cursor = 0;
  let retryMs = POLL_INTERVAL_MS;
  onStatus('connecting', 'Kick conectando pelo webhook oficial...');

  const poll = async () => {
    if (stopped) return;
    try {
      const result = await accounts.kickWebhookEvents(cursor);
      const events = Array.isArray(result.events) ? result.events : [];
      for (const message of events) onMessage(message);
      cursor = Math.max(cursor, Number(result.cursor) || cursor);
      retryMs = POLL_INTERVAL_MS;
      onStatus('connected', 'Kick conectado');
    } catch (error) {
      onStatus('reconnecting', `Kick: ${error.message || 'falha ao consultar o webhook'}`);
      retryMs = Math.min(MAX_RETRY_MS, Math.max(POLL_INTERVAL_MS, retryMs * 2));
    }
    if (!stopped) timer = setTimeout(poll, retryMs);
  };

  if (!config?.enabled || !config?.channel) {
    onStatus('disconnected', 'Conecte a conta Kick para ler o chat.');
    return { stop() { stopped = true; } };
  }
  if (!accounts || typeof accounts.kickWebhookEvents !== 'function') {
    throw new Error('O conector oficial da Kick não está disponível.');
  }
  await poll();
  return {
    stop() {
      stopped = true;
      if (timer) clearTimeout(timer);
      timer = null;
    },
  };
}

module.exports = { startKick, POLL_INTERVAL_MS, MAX_RETRY_MS };
