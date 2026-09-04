//################################################################################
// env.d.ts
//--------------------------------------------------------------------------------

import type { ShardObject } from "./shard-object";

export {};

//********************************************************************************
// Env
//--------------------------------------------------------------------------------
// SHARDS         DO namespace binding, one ShardObject instance per shard key
// NOTIFY_RELAY   Service Binding to gw2-world-events-notify, optional - relay
//                to the region hub is a no-op while unset (see shard-object.ts)
// RELAY_SECRET   sent as X-Relay-Secret on every NOTIFY_RELAY call; same value
//                as server-notify's own RELAY_SECRET (see its env.d.ts)
//--------------------------------------------------------------------------------
declare global {
  interface Env {
    SHARDS: DurableObjectNamespace<ShardObject>;
    NOTIFY_RELAY?: Fetcher;
    RELAY_SECRET?: string;
  }
}