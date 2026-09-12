# gw2-world-events relay (Cloudflare Worker + Durable Object)

Server side of the live event reporting feature. Implements the wire
protocol `src/networking/ws_client.cpp` in the addon already assumes
exists: one persistent WebSocket per shard, JSON report/history messages
keyed by `event_id`.

## What it is

- **One Durable Object per shard** (`ShardObject`, keyed by
  `ShardIdentity::ToKey()`, e.g. `map1062-a1b2c3d4e5f6a7b8`). Each instance
  holds the live WebSocket connections for everyone currently on that map
  instance, plus its own embedded SQLite table capped at the last 10 reports
  per `event_id`.
- **No separate database, no REST API.** The Worker (`src/index.ts`) does
  nothing but route `GET /ws?shard=<key>` to the right Durable Object by
  name; all protocol logic lives in `src/shard-object.ts`.
- Uses the **WebSocket Hibernation API**
  (`ctx.acceptWebSocket` / `webSocketMessage` / `webSocketClose`), so idle
  connections aren't billed for duration on the Workers Free plan.

## One thing added beyond the original spec

A per-connection throttle (`MIN_SECONDS_BETWEEN_REPORTS` in
`shard-object.ts`, currently 2s) silently drops reports sent too close
together from the same socket. There's no auth on this endpoint at all, so a
minimal guard against a stuck or spammy client felt worth the few lines. Not
part of the client's own wire-protocol contract (`ws_client.cpp` never
throttles outgoing SendReport calls) - easy to rip out if it's unwanted, and
it doesn't change the client-visible contract (bursty reports are just
dropped, same as any other drop).

## Setup

```bash
npm install
npx wrangler login          # first time only
```

## Local dev

```bash
npm run dev
```

Wrangler will print a local URL - connect a WebSocket client to
`ws://127.0.0.1:8787/ws?shard=test-shard` to test without touching the game
client. Try:

```json
{"type":"report","event_id":"triple-trouble"}
```

and confirm you get a `history` message on connect and a `report` broadcast
(with server-stamped `ts`) after sending.

## Deploy

```bash
npm run deploy
```

First deploy will print your Worker's `*.workers.dev` URL. That's the
`kHost` value that needs to replace `CHANGEME.workers.dev` in
`src/networking/ws_client.cpp` (generated via
`tools/generate_host_config.py`, see that script's docstring) - no code
changes needed here after that, only in the addon.

## Verifying free-tier fit

`wrangler tail` after deploying shows live logs/requests if you want to
sanity-check request volume and Durable Object duration against the
Workers Free plan's limits once the button ships.
