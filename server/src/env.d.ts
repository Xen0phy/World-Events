import type { ShardObject } from "./shard-object";

export {};

declare global {
  interface Env {
    SHARDS: DurableObjectNamespace<ShardObject>;
  }
}
