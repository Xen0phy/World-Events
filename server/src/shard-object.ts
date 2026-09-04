//################################################################################
// shard-object.ts
//--------------------------------------------------------------------------------
// ShardObject   one DO instance per shard key; live WS clients + last-10-per-
//               event report history
//--------------------------------------------------------------------------------
// Hibernation API (state.acceptWebSocket) keeps idle connections unbilled on
// the Workers Free plan. History lives in an embedded SQLite table capped at
// MAX_REPORTS_PER_EVENT rows per event_id, so late joiners get history
// without a separate database.
// Wire protocol (must match ws_client.cpp exactly - see networking-handoff.md
// section 5):
//   Connect:  GET /ws?shard=<key> -> WS upgrade (routing lives in index.ts)
//   Server->client on connect:
//     {"type":"history","reports":[{"event_id":"...","ts":1234567890}, ...]}
//   Client->server, report button press:
//     {"type":"report","event_id":"...","reporter_name":"...","region":"NA"|"EU"|""}
//   Server->client broadcast (incl. sender), ts server-stamped at receipt:
//     {"type":"report","event_id":"...","ts":1234567890}
// reporter_name/region are relayed onward to the region hub (see
// relayToNotify) - the shard's own broadcast above never repeats them, unlike
// networking-handoff.md's not-yet-upgraded shard worker (see index.ts).
//--------------------------------------------------------------------------------

import { DurableObject } from "cloudflare:workers";

//_ Reports kept per event_id, mirrors the client's local trim (ws_client.cpp)
const MAX_REPORTS_PER_EVENT = 10;

//_ Sanity bound so a malformed/malicious client can't stuff huge event ids
const MAX_EVENT_ID_LENGTH = 128;

//_ Mirrors MAX_REPORTER_NAME_LENGTH in the notify worker's index.ts
const MAX_REPORTER_NAME_LENGTH = 64;

//_ Min spacing between accepted reports from one connection, anti-spam guard
const MIN_SECONDS_BETWEEN_REPORTS = 2;

//_ Idle shard (no viewers, no reports) this long wipes its own storage
const INACTIVITY_TIMEOUT_MS = 12 * 60 * 60 * 1000;

//_ Matches this DO's own shard key format - see SHARD_KEY_PATTERN in index.ts
const SHARD_KEY_MAP_ID_PATTERN = /^map(\d+)-/;

//********************************************************************************
// ReportRow
//--------------------------------------------------------------------------------
// event_id   report's event identifier
// ts         unix seconds when the report was accepted
//--------------------------------------------------------------------------------
interface ReportRow extends Record<string, SqlStorageValue> {
  event_id: string;
  ts: number;
}

//********************************************************************************
// ClientMessage
//--------------------------------------------------------------------------------
// type            unvalidated, checked against the literal "report"
// event_id        unvalidated, checked to be a string in webSocketMessage
// reporter_name   unvalidated, empty-string default - see relayToNotify
// region          unvalidated, empty-string default - see relayToNotify
//--------------------------------------------------------------------------------
interface ClientMessage {
  type?: unknown;
  event_id?: unknown;
  reporter_name?: unknown;
  region?: unknown;
}

//********************************************************************************
// SocketAttachment
//--------------------------------------------------------------------------------
// lastReportTs   unix seconds of this connection's last accepted report
// shardMapId     this shard's numeric map id, captured once in fetch() - see
//                relayToNotify for why this lives here and not a class field
//--------------------------------------------------------------------------------
// Stashed via WebSocket attachments so per-connection state survives
// hibernation.
//--------------------------------------------------------------------------------
interface SocketAttachment {
  lastReportTs?: number;
  shardMapId?: number;
}

//********************************************************************************
// ShardObject
//--------------------------------------------------------------------------------
// One DO instance per shard key. Reports are ephemeral: any shard with no
// live connections and no activity for INACTIVITY_TIMEOUT_MS wipes its own
// storage via alarm() rather than staying provisioned forever.
//--------------------------------------------------------------------------------
export class ShardObject extends DurableObject<Env> {
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // constructor
  //--------------------------------------------------------------------------------
  // blockConcurrencyWhile so no request is served against a mid-setup
  // instance; covers both first-ever creation and every hibernation wake.
  //--------------------------------------------------------------------------------
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    this.ctx.blockConcurrencyWhile(async () => {
      this.ensureSchema();
      //. Defensive backstop only - do NOT unconditionally reschedule here.
      //. The constructor reruns on *every* hibernation wake (any message,
      //. webSocketClose, webSocketError, even a dropped/throttled message),
      //. not just real activity. Real activity already reschedules
      //. explicitly (see fetch() and webSocketMessage()); resetting the
      //. alarm here too would push the 12h deadline out on every wake,
      //. contradicting alarm()'s "nothing resets the deadline" invariant
      //. and letting idle shards live indefinitely.
      const existingAlarm = await this.ctx.storage.getAlarm();
      if (existingAlarm === null) {
        await this.scheduleIdleAlarm();
      }
    });
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // fetch
  //--------------------------------------------------------------------------------
  // Upgrades to a WebSocket via the Hibernation API, so idle connections
  // aren't billed on the Workers Free plan. No in-memory per-connection state
  // is kept, since the runtime can drop and reload this instance between
  // messages - state lives only in SQLite or the socket's attachment.
  //--------------------------------------------------------------------------------
  async fetch(request: Request): Promise<Response> {
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("Expected WebSocket upgrade", { status: 426 });
    }

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);

    this.ctx.acceptWebSocket(server);
    await this.scheduleIdleAlarm();  //. new connection counts as activity

    //. Diagnostic only: if this count climbs above what's actually live
    //. (e.g. 2, 3... on a shard with one real viewer), it's ghost sockets
    //. from connections that died without a webSocketClose/webSocketError
    //. ever reaching this DO - see shard-object.ts header notes.
    const shardKey = new URL(request.url).searchParams.get("shard");
    console.log(`[fetch] connect (shard=${shardKey}, liveSockets=${this.ctx.getWebSockets().length})`);

    //_ Attachment, not a class field - relayToNotify needs this to survive a hibernation wake between fetch() and webSocketMessage().
    const match = shardKey?.match(SHARD_KEY_MAP_ID_PATTERN);
    if (match) server.serializeAttachment({ shardMapId: Number(match[1]) } satisfies SocketAttachment);

    server.send(JSON.stringify({ type: "history", reports: this.readHistory() }));

    return new Response(null, { status: 101, webSocket: client });
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // webSocketMessage
  //--------------------------------------------------------------------------------
  // Handles one "report" message: throttles per-connection, persists it, then
  // broadcasts to every connected socket including the sender, since the
  // server is the single timestamp authority for a report's ts.
  //--------------------------------------------------------------------------------
  async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): Promise<void> {
    if (typeof message !== "string") return;  //. binary frames unsupported here

    let parsed: ClientMessage;
    try {
      parsed = JSON.parse(message);
    } catch {
      return;  //. malformed JSON, ignored silently
    }

    if (parsed.type !== "report") return;
    if (typeof parsed.event_id !== "string") return;

    const eventId = parsed.event_id.trim();
    if (eventId.length === 0 || eventId.length > MAX_EVENT_ID_LENGTH) return;

    const reporterName = typeof parsed.reporter_name === "string"
      ? parsed.reporter_name.slice(0, MAX_REPORTER_NAME_LENGTH)
      : "";
    const region = typeof parsed.region === "string" ? parsed.region : "";

    const nowSeconds = Math.floor(Date.now() / 1000);

    const attachment = (ws.deserializeAttachment() as SocketAttachment) ?? {};
    if (
      attachment.lastReportTs !== undefined &&
      nowSeconds - attachment.lastReportTs < MIN_SECONDS_BETWEEN_REPORTS
    ) {
      return;  //. throttled, dropped silently
    }
    attachment.lastReportTs = nowSeconds;
    ws.serializeAttachment(attachment);

    this.insertAndTrim(eventId, nowSeconds);
    await this.scheduleIdleAlarm();  //. accepted report counts as activity

    const broadcast = JSON.stringify({ type: "report", event_id: eventId, ts: nowSeconds });
    for (const socket of this.ctx.getWebSockets()) {
      try {
        socket.send(broadcast);  //. send to this shard
      } catch {
        //_ dead socket - cleanup happens in webSocketClose/webSocketError
      }
    }

    await this.relayToNotify(eventId, nowSeconds, reporterName, region, attachment.shardMapId);
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // relayToNotify
  //--------------------------------------------------------------------------------
  // Forwards an already-accepted report to the region hub, on top of this
  // shard's own broadcast above - a no-op, not an error, whenever the pieces
  // needed for that aren't there: NOTIFY_RELAY unbound (relay disabled for
  // this deployment - see env.d.ts), unrecognized region, or no shardMapId on
  // this socket's attachment (only possible for a connection accepted before
  // SocketAttachment gained that field - logged, since it shouldn't recur).
  // Failures (network, non-204 response) are logged and swallowed - the
  // shard's own report/broadcast already succeeded above and must not be
  // undone by a downstream problem on the relay leg.
  //--------------------------------------------------------------------------------
  private async relayToNotify(
    eventId: string,
    ts: number,
    reporterName: string,
    region: string,
    shardMapId: number | undefined
  ): Promise<void> {
    if (!this.env.NOTIFY_RELAY) return;             //. relay disabled for this deployment
    if (region !== "EU" && region !== "NA") return; //. Unknown/missing - nowhere to relay to
    if (shardMapId === undefined) {
      console.log(`[relayToNotify] no shardMapId on this socket's attachment (event_id=${eventId})`);
      return;
    }

    try {
      const response = await this.env.NOTIFY_RELAY.fetch("https://internal/relay", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "X-Relay-Secret": this.env.RELAY_SECRET ?? "",
        },
        body: JSON.stringify({
          event_id: eventId,
          ts,
          reporter_name: reporterName,
          region,
          map_id: shardMapId,
        }),
      });
      if (!response.ok) {
        console.log(`[relayToNotify] non-OK response (status=${response.status}, event_id=${eventId})`);
      }
    } catch (err) {
      console.log(`[relayToNotify] failed (event_id=${eventId}): ${err}`);
    }
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // webSocketClose / webSocketError
  //--------------------------------------------------------------------------------
  // Hibernation API handlers; ws.close() completes the closing handshake and
  // an already-closing/closed socket is swallowed since there's nothing left
  // to do.
  //--------------------------------------------------------------------------------
  async webSocketClose(ws: WebSocket, code: number, reason: string, wasClean: boolean): Promise<void> {
    try {
      ws.close(code, reason);
    } catch {
      //_ already closing/closed, nothing to do
    }
  }

  async webSocketError(ws: WebSocket): Promise<void> {
    try {
      ws.close(1011, "WebSocket error");
    } catch {
      //_ already closing/closed, nothing to do
    }
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // alarm
  //--------------------------------------------------------------------------------
  // Fires after INACTIVITY_TIMEOUT_MS with nothing resetting the deadline. A
  // connected shard reschedules instead of wiping - a live viewer counts as
  // activity even without reports. Otherwise wipes storage; deleteAll() also
  // clears the alarm itself (compat date >= 2026-02-24), so the next fetch()
  // for this shard key just re-runs the constructor on a clean slate.
  //--------------------------------------------------------------------------------
  async alarm(): Promise<void> {
    const socketCount = this.ctx.getWebSockets().length;
    if (socketCount > 0) {
      console.log(`[alarm] rescheduling, ${socketCount} socket(s) still connected`);
      await this.scheduleIdleAlarm();
      return;
    }
    console.log("[alarm] no sockets connected, wiping storage");
    await this.ctx.storage.deleteAll();
    this.ensureSchema();
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // ensureSchema
  //--------------------------------------------------------------------------------
  // Also called right after deleteAll() in alarm(): the runtime doesn't
  // guarantee eviction immediately after an alarm, so a surviving in-memory
  // instance could otherwise hit a missing table on its next request.
  //--------------------------------------------------------------------------------
  private ensureSchema(): void {
    this.ctx.storage.sql.exec(`
      CREATE TABLE IF NOT EXISTS reports (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        event_id TEXT NOT NULL,
        ts INTEGER NOT NULL
      );
    `);
    this.ctx.storage.sql.exec(`
      CREATE INDEX IF NOT EXISTS idx_reports_event_ts
      ON reports (event_id, ts DESC);
    `);
  }

  private async scheduleIdleAlarm(): Promise<void> {
    await this.ctx.storage.setAlarm(Date.now() + INACTIVITY_TIMEOUT_MS);
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // readHistory
  //--------------------------------------------------------------------------------
  // Every event_id this shard has seen, each already capped at
  // MAX_REPORTS_PER_EVENT by insertAndTrim on write.
  //--------------------------------------------------------------------------------
  private readHistory(): ReportRow[] {
    const cursor = this.ctx.storage.sql.exec<ReportRow>(
      `SELECT event_id, ts FROM reports ORDER BY ts DESC;`
    );
    return [...cursor];
  }

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // insertAndTrim
  //--------------------------------------------------------------------------------
  // Inserts one report row, then deletes older rows for that event_id beyond
  // MAX_REPORTS_PER_EVENT.
  //--------------------------------------------------------------------------------
  private insertAndTrim(eventId: string, ts: number): void {
    this.ctx.storage.sql.exec(
      `INSERT INTO reports (event_id, ts) VALUES (?, ?);`,
      eventId,
      ts
    );
    this.ctx.storage.sql.exec(
      `DELETE FROM reports
       WHERE event_id = ?
         AND id NOT IN (
           SELECT id FROM reports
           WHERE event_id = ?
           ORDER BY ts DESC, id DESC
           LIMIT ?
         );`,
      eventId,
      eventId,
      MAX_REPORTS_PER_EVENT
    );
  }
}