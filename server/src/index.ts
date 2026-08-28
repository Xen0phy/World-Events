// ################################################################################
// index.ts
// --------------------------------------------------------------------------------
// Thin routing layer. All the actual protocol logic (history/report/broadcast,
// SQLite last-10 buffer) lives in shard-object.ts - one Durable Object instance
// per shard key, named directly off the client's ShardIdentity::ToKey().
//
// Client connects to: wss://<this-worker>.workers.dev/ws?shard=map1062-a1b2c3d4e5f6a7b8
// --------------------------------------------------------------------------------

import { ShardObject } from "./shard-object";

export { ShardObject };

/** Loose bound on shard key length - real keys are "map{id}-{16 hex chars}"
 *  (see shard_id.cpp), well under this; just guarding against abuse. */
const MAX_SHARD_KEY_LENGTH = 256;

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname !== "/ws") {
      return new Response("Not found", { status: 404 });
    }

    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("Expected WebSocket upgrade", { status: 426 });
    }

    const shardKey = url.searchParams.get("shard");
    if (!shardKey || shardKey.length === 0 || shardKey.length > MAX_SHARD_KEY_LENGTH) {
      return new Response("Missing or invalid 'shard' query parameter", { status: 400 });
    }

    // One Durable Object per shard key - idFromName gives every client that
    // passes the same key a deterministic route to the same object instance.
    const id = env.SHARDS.idFromName(shardKey);
    const stub = env.SHARDS.get(id);

    return stub.fetch(request);
  },
} satisfies ExportedHandler<Env>;
