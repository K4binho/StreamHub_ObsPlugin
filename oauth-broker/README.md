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

Broker uses in-memory transactions with 10-minute TTL. Authorization result and claim token exist only during one short transaction. Restart or multi-instance deployment needs shared ephemeral storage before production scale-out.

Render can host this service as Node web service. Service must bind `0.0.0.0` through `PORT`; Render terminates TLS and forwards HTTPS requests. Neon is DB storage, not HTTP hosting; no DB is needed for single-instance transaction storage.

## API

- `POST /v1/oauth/kick/start`
- `GET /v1/oauth/kick/callback`
- `GET /v1/oauth/kick/poll/:transactionId`
- `POST /v1/oauth/kick/refresh`
- Same paths for `youtube`.
- `GET /healthz`

`start` may receive public `clientId` for allowlist validation; broker provider configuration remains authoritative. `poll` needs `Authorization: Bearer <claimToken>`. Tokens return only once from `poll`, then transaction becomes claimed.

## Security

HTTPS required. Broker rejects non-HTTPS requests outside health checks. State, PKCE verifier, claim token and provider tokens never enter logs. Callback uses exact redirect URI, state validation, PKCE S256, TTL and replay protection. Rate limits protect start, callback, poll and refresh.
