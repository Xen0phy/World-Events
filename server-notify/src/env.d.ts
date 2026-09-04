//################################################################################
// env.d.ts
//--------------------------------------------------------------------------------

import type { RegionHub } from "./region-hub";

export {};

//********************************************************************************
// Env
//--------------------------------------------------------------------------------
// REGION_HUBS    DO namespace binding, one RegionHub instance per region
// RELAY_SECRET   shared secret gating /relay - see index.ts
//--------------------------------------------------------------------------------
declare global {
  interface Env {
    REGION_HUBS: DurableObjectNamespace<RegionHub>;
    RELAY_SECRET: string;
  }
}
