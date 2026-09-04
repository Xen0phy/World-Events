//################################################################################
// index.ts
//--------------------------------------------------------------------------------
// default     Worker entrypoint; routes /notify (public, WS) and /relay
//             (secret-gated, POST) to the right RegionHub
// RegionHub   re-exported so wrangler can bind REGION_HUBS to this class
//--------------------------------------------------------------------------------
// Thin routing layer, same split as the gw2-world-events worker's index.ts:
// all protocol logic (broadcast, no-history relay) lives in region-hub.ts.
// One DO instance per region, pinned to a locationHint so its physical
// location matches the region name it was given rather than wherever the
// first request happened to land.
//
// /notify needs no auth - clients connect to it directly, same trust model
// as the existing /ws endpoint (see live-toast-handoff.md section 7).
// /relay is the one entry point that can trigger a region-wide broadcast, so
// it's gated by RELAY_SECRET instead: a Service Binding call from the shard
// worker skips Cloudflare's public routing for that leg, but this Worker
// still has its own public *.workers.dev route by default, so the header
// check is what actually keeps /relay from being a second, unthrottled
// front door (see live-toast-handoff.md section 3).
//--------------------------------------------------------------------------------

import { RegionHub, type RelayedReport } from "./region-hub";

export { RegionHub };

type Region = "EU" | "NA";

//_ Matches ShardIdentity-derived NA/EU worlds - see events_live.h's mumble notes
const REGIONS: readonly Region[] = ["EU", "NA"];

//_ Pins each region's DO to its matching Cloudflare colo group (see file header)
const LOCATION_HINTS: Record<Region, DurableObjectLocationHint> = {
  EU: "weur",
  NA: "enam",
};

//_ Sanity bound, mirrors MAX_EVENT_ID_LENGTH in the shard worker's shard-object.ts
const MAX_EVENT_ID_LENGTH = 128;

//_ Mirrors MAX_REPORTER_NAME_LENGTH in the shard worker - see shard-object.ts
const MAX_REPORTER_NAME_LENGTH = 64;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// isRegion
//--------------------------------------------------------------------------------
// Type guard, not trusted just because it came from the client - see below.
//--------------------------------------------------------------------------------
function isRegion(value: unknown): value is Region {
  return typeof value === "string" && (REGIONS as string[]).includes(value);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// regionHubStub
//--------------------------------------------------------------------------------
// Shared by both routes below so /notify and /relay for the same region
// always resolve to the same DO instance.
//--------------------------------------------------------------------------------
function regionHubStub(env: Env, region: Region): DurableObjectStub<RegionHub> {
  const id = env.REGION_HUBS.idFromName(region);
  return env.REGION_HUBS.get(id, { locationHint: LOCATION_HINTS[region] });
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// handleNotify
//--------------------------------------------------------------------------------
// GET /notify?region=EU|NA -> WS upgrade, forwarded as-is to the region's hub.
//--------------------------------------------------------------------------------
function handleNotify(request: Request, env: Env, url: URL): Promise<Response> | Response {
  if (request.headers.get("Upgrade") !== "websocket") {
    return new Response("Expected WebSocket upgrade", { status: 426 });
  }

  const region = url.searchParams.get("region");
  if (!isRegion(region)) {
    return new Response("Missing or invalid 'region' query parameter", { status: 400 });
  }

  return regionHubStub(env, region).fetch(request);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// handleRelay
//--------------------------------------------------------------------------------
// POST /relay, RELAY_SECRET-gated. Body shape is the shard worker's own
// report broadcast plus region (see shard-object.ts) - re-validated here
// rather than trusted, even though the caller is secret-authenticated.
//--------------------------------------------------------------------------------
async function handleRelay(request: Request, env: Env): Promise<Response> {
  if (request.method !== "POST") {
    return new Response("Method not allowed", { status: 405 });
  }
  if (request.headers.get("X-Relay-Secret") !== env.RELAY_SECRET) {
    return new Response("Forbidden", { status: 403 });
  }

  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return new Response("Invalid JSON body", { status: 400 });
  }

  const { event_id, ts, reporter_name, region, map_id } = body as Record<string, unknown>;

  if (typeof event_id !== "string" || event_id.length === 0 || event_id.length > MAX_EVENT_ID_LENGTH) {
    return new Response("Invalid 'event_id'", { status: 400 });
  }
  if (typeof ts !== "number") {
    return new Response("Invalid 'ts'", { status: 400 });
  }
  if (typeof reporter_name !== "string" || reporter_name.length > MAX_REPORTER_NAME_LENGTH) {
    return new Response("Invalid 'reporter_name'", { status: 400 });
  }
  if (!isRegion(region)) {
    return new Response("Invalid 'region'", { status: 400 });
  }
  if (typeof map_id !== "number") {
    return new Response("Invalid 'map_id'", { status: 400 });
  }

  const report: RelayedReport = { event_id, ts, reporter_name, map_id };
  await regionHubStub(env, region).relayReport(report);

  return new Response(null, { status: 204 });
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/notify") {
      return handleNotify(request, env, url);
    }
    if (url.pathname === "/relay") {
      return handleRelay(request, env);
    }
    return new Response("Not found", { status: 404 });
  },
} satisfies ExportedHandler<Env>;
