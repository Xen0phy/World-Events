//################################################################################
// index.ts
//--------------------------------------------------------------------------------
// default        Worker entrypoint; routes /ws upgrades to the right ShardObject
// ShardObject    re-exported so wrangler can bind SHARDS to this class
//--------------------------------------------------------------------------------
// Thin routing layer - all protocol logic (history/report/broadcast, SQLite
// last-10 buffer) lives in shard-object.ts. One DO instance per shard key,
// named directly off the client's ShardIdentity::ToKey().
// Client connects to:
//   wss://<this-worker>.workers.dev/ws?shard=map1062-a1b2c3d4e5f6a7b8
//--------------------------------------------------------------------------------

import { ShardObject } from "./shard-object";

export { ShardObject };

//_ Loose bound on shard key length; real keys are shorter (see shard_id.cpp)
const MAX_SHARD_KEY_LENGTH = 256;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// fetch
//--------------------------------------------------------------------------------
// Validates the /ws upgrade and shard key, then routes to the ShardObject for
// that key via idFromName - the same key always resolves to the same instance.
//--------------------------------------------------------------------------------
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

    const id = env.SHARDS.idFromName(shardKey);
    const stub = env.SHARDS.get(id);

    return stub.fetch(request);
  },
} satisfies ExportedHandler<Env>;