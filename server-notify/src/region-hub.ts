//################################################################################
// region-hub.ts
//--------------------------------------------------------------------------------
// RegionHub   one DO instance per region ("EU"/"NA"); live WS viewers only, no
//             history, no subscription state
//--------------------------------------------------------------------------------
// Hibernation API (ctx.acceptWebSocket) keeps idle connections unbilled on the
// Workers Free plan, same as ShardObject in the gw2-world-events worker.
// Unlike ShardObject, this DO holds no storage at all: late joiners just wait
// for the next live report (see live-toast-handoff.md section 3).
//
// Wire protocol (must match notification_client.cpp exactly - see
// live-toast-handoff.md section 2):
//   Connect:  GET /notify?region=EU -> WS upgrade (routing lives in index.ts)
//   Server->client, broadcast, no filtering, no subscription state server-side:
//     {"type":"report","event_id":"...","ts":...,"reporter_name":"...","map_id":...}
//     {"type":"presence","region_viewers":...} - see broadcastPresence below
// The client decides locally whether event_id is subscribed before spawning a
// toast. This DO never receives a message from a /notify client - the only
// input is relayReport(), called from index.ts's /relay handler.
//--------------------------------------------------------------------------------

import { DurableObject } from "cloudflare:workers";

//********************************************************************************
// RelayedReport
//--------------------------------------------------------------------------------
// event_id/ts/reporter_name/map_id   already validated by the shard worker
//                                     before it called /relay - see index.ts
//--------------------------------------------------------------------------------
export interface RelayedReport {
  event_id: string;
  ts: number;
  reporter_name: string;
  map_id: number;
}

//********************************************************************************
// RegionHub
//--------------------------------------------------------------------------------
// One DO instance per region. No storage, so no alarm/cleanup story like
// ShardObject's INACTIVITY_TIMEOUT_MS - there's nothing here to wipe.
//--------------------------------------------------------------------------------
export class RegionHub extends DurableObject<Env> {
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // fetch
  //--------------------------------------------------------------------------------
  // Upgrades to a WebSocket via the Hibernation API. Only /notify is handled
  // here - /relay is a plain POST, checked and dispatched entirely in
  // index.ts, which calls relayReport() directly instead of routing through
  // fetch().
  //--------------------------------------------------------------------------------
  async fetch(request: Request): Promise<Response> {
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("Expected WebSocket upgrade", { status: 426 });
    }

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);

    this.ctx.acceptWebSocket(server);

    //. Diagnostic only, same rationale as ShardObject's connect log.
    const region = new URL(request.url).searchParams.get("region");
    console.log(`[fetch] connect (region=${region}, liveSockets=${this.ctx.getWebSockets().length})`);

    this.broadcastPresence();

    return new Response(null, { status: 101, webSocket: client });
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // broadcastPresence   (group: fetch, webSocketClose, webSocketError)
  //--------------------------------------------------------------------------------
  // Tells every socket currently connected to this region how many peers it has,
  // itself included - "so users can see they're not alone" is the whole point,
  // not precise concurrency. Called once after a connect completes and once after
  // a close/error completes, so every remaining socket's count stays current;
  // no periodic timer, since the count only ever changes on those two events.
  //--------------------------------------------------------------------------------
  private broadcastPresence(): void {
    const msg = JSON.stringify({ type: "presence", region_viewers: this.ctx.getWebSockets().length });
    for (const socket of this.ctx.getWebSockets()) {
      try {
        socket.send(msg);
      } catch {
        //_ dead socket - cleanup happens in webSocketClose/webSocketError
      }
    }
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // relayReport
  //--------------------------------------------------------------------------------
  // Called directly (Workers RPC, not fetch) from index.ts's /relay handler,
  // after the RELAY_SECRET check. Broadcasts to every socket connected to
  // this region right now; the hub never learns who's subscribed to what.
  //--------------------------------------------------------------------------------
  async relayReport(report: RelayedReport): Promise<void> {
    const broadcast = JSON.stringify({ type: "report", ...report });
    for (const socket of this.ctx.getWebSockets()) {
      try {
        socket.send(broadcast);
      } catch (err) {
        //. dead socket - cleanup happens in webSocketClose/webSocketError
        console.log(`[relayReport] send failed, skipping (event_id=${report.event_id}, err=${err})`);
      }
    }
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // webSocketMessage
  //--------------------------------------------------------------------------------
  // No client-to-hub protocol exists (see file header) - anything received is
  // ignored. Still required by the Hibernation API contract.
  //--------------------------------------------------------------------------------
  async webSocketMessage(_ws: WebSocket, _message: string | ArrayBuffer): Promise<void> {
    //_ intentionally empty - see block header
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // webSocketClose / webSocketError
  //--------------------------------------------------------------------------------
  // Same completion pattern as ShardObject: complete the closing handshake,
  // swallow an already-closing/closed socket. broadcastPresence runs after the
  // close completes, once the closing socket is out of getWebSockets(), so
  // survivors see the count drop.
  //--------------------------------------------------------------------------------
  async webSocketClose(ws: WebSocket, code: number, reason: string, wasClean: boolean): Promise<void> {
    try {
      ws.close(code, reason);
    } catch {
      //_ already closing/closed, nothing to do
    }
    this.broadcastPresence();
  }

  async webSocketError(ws: WebSocket): Promise<void> {
    try {
      ws.close(1011, "WebSocket error");
    } catch {
      //_ already closing/closed, nothing to do
    }
    this.broadcastPresence();
  }
}