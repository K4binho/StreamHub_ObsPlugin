# StreamHub OAuth Broker

Shared OAuth backend for distributed StreamHub DLL clients. Broker owns Kick and Google Client Secrets. DLL and local Node use broker transaction IDs and one-time claim tokens; users enter no OAuth credentials.

## Run

```bash
copy .env.example .env
npm install
npm start
```

Set `BROKER_PUBLIC_BASE_URL` to real HTTPS domain. Register exact callback URLs:

```text
https://<domain>/v1/oauth/kick/callback
https://<domain>/v1/oauth/youtube/callback
```

Set `KICK_CLIENT_ID`, `KICK_CLIENT_SECRET`, `GOOGLE_CLIENT_ID` and `GOOGLE_CLIENT_SECRET` through deployment secret storage. Never commit `.env` or print values.

For official Kick chat ingestion, enable webhooks in the Kick Developer app and set its webhook URL to `https://<domain>/v1/webhooks/kick` (for the production Render broker: `https://streamhub-oauth-broker.onrender.com/v1/webhooks/kick`). Kick users must reconnect through this shared OAuth app to grant `events:subscribe`; after authorization the broker subscribes to `chat.message.sent`. The broker verifies the current Kick public key signature and serves each broadcaster's recent messages to the local server through `GET /v1/kick/events` authenticated with that user's Kick access token. The in-memory queue is limited to 1,000 events and five minutes; restarts can discard queued messages. Run a real live-chat test after deployment because Kick webhook delivery depends on Kick's service and app approval/configuration.

TikTok's public developer product list currently has no official LIVE chat-reading product suitable for this integration. The old experimental protocol reader stays removed; RTMP output is unaffected.

Broker uses in-memory transactions with 10-minute TTL. Authorization result and claim token exist only during one short transaction. Restart or multi-instance deployment needs shared ephemeral storage before production scale-out.

Render can host this service as Node web service. Service must bind `0.0.0.0` through `PORT`; Render terminates TLS and forwards HTTPS requests. Neon is DB storage, not HTTP hosting; no DB is needed for single-instance transaction storage.

## API

- `POST /v1/oauth/kick/start`
- `GET /v1/oauth/kick/callback`
- `GET /v1/oauth/kick/poll/:transactionId`
- `POST /v1/oauth/kick/refresh`
- Same paths for `youtube`.
- `POST /v1/webhooks/kick` — signed Kick event receiver.
- `GET /v1/kick/events?since=<cursor>` — recent Kick chat events for the OAuth-authorized broadcaster.
- `GET /` — página pública de status do broker e das configurações OAuth
- `GET /healthz`

O servidor local do plugin envia um heartbeat sem credenciais para `/healthz` a cada quatro minutos enquanto o OBS estiver aberto. Isso mantém o serviço Render ativo durante uso normal; não impede reinícios, deploys ou falhas da plataforma.

`start` receives no OAuth credentials from desktop clients; broker provider configuration remains authoritative. `poll` needs `Authorization: Bearer <claimToken>`. Tokens return only once from `poll`, then transaction becomes claimed.

## Security

HTTPS required. Broker rejects non-HTTPS requests outside health checks. State, PKCE verifier, claim token and provider tokens never enter logs. Callback uses exact redirect URI, state validation, PKCE S256, TTL and replay protection. Rate limits protect start, callback, poll and refresh.
