# gw2-world-events-notify (Cloudflare Worker + Durable Object)

Region-wide toast layer for Live Event reports, alongside the per-shard
`gw2-world-events` worker in `server/`. Separate deploy from that shipping
worker so a broken deploy here can't touch the existing anonymous per-shard
reporting flow.

## What it is

- **One Durable Object per region** (`RegionHub`, `idFromName("EU")` /
  `("NA")`, pinned to a `locationHint`). Holds only live WebSocket
  connections - no SQLite, no history, no subscription state. Late joiners
  wait for the next live report.
- **Two routes, both in `src/index.ts`:**
  - `GET /notify?region=EU|NA` - public, no auth, WS upgrade. Clients
    connect here directly.
  - `POST /relay` - `RELAY_SECRET`-gated. The shard worker calls this via a
    Service Binding after an accepted report, and this Worker rebroadcasts
    it to every socket connected to that region's hub.

## Setup

```bash
npm install
npx wrangler login          # first time only
npx wrangler secret put RELAY_SECRET
```

Use the same secret value on the `gw2-world-events` worker (`server/`) -
its `relayToNotify` (`server/src/shard-object.ts`) calls `/relay` here via a
Service Binding once `NOTIFY_RELAY`/`RELAY_SECRET` are set on that side -
`wrangler secret put` there too, never in either `wrangler.toml`.

## Local dev

```bash
npm run dev
```

Connect a WebSocket client to `ws://127.0.0.1:8787/notify?region=EU` to
confirm the socket accepts and stays open with no messages (no history
here, unlike the shard worker). Test a relay push with:

```bash
curl -X POST http://127.0.0.1:8787/relay \
  -H "X-Relay-Secret: <your local secret>" \
  -H "Content-Type: application/json" \
  -d '{"event_id":"triple-trouble","ts":1234567890,"reporter_name":"","region":"EU","map_id":1062}'
```

and confirm the connected `/notify?region=EU` socket receives a
`{"type":"report",...}` broadcast. A request with a missing or wrong
`X-Relay-Secret` should get `403`; a `region` outside `EU`/`NA` should get
`400` on either route.

## Deploy

```bash
npm run deploy
```

First deploy prints this Worker's `*.workers.dev` URL - that's the host
`notification_client.h/.cpp` needs on the client side, generated into its
own header the same way
`src/networking/host_config.h` is (see `tools/generate_host_config.py`).
