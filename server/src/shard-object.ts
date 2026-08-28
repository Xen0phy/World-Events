// ################################################################################
// shard-object.ts
// --------------------------------------------------------------------------------
// One Durable Object instance per shard key (see shard_id.h client-side -
// "map{mapId}-{addressHash}"). Holds:
//   - live WebSocket connections for everyone currently on that shard, via the
//     Hibernation API (state.acceptWebSocket) so idle connections aren't
//     billed for duration on the Workers Free plan.
//   - an embedded SQLite table capped at the last 10 reports per event_id,
//     so late joiners get history without a separate database.
//
// Wire protocol (must match ws_client.cpp exactly - see networking-handoff.md
// section 5):
//
//   Connect:  GET /ws?shard=<key>  ->  WebSocket upgrade. One connection = one
//             shard (routing to the right ShardObject happens in index.ts).
//
//   Server -> client, on connect:
//     {"type":"history","reports":[{"event_id":"...","ts":1234567890}, ...]}
//     (any order - client sorts/trims itself)
//
//   Client -> server, on report button press:
//     {"type":"report","event_id":"..."}
//
//   Server -> client, broadcast to everyone on that shard (including sender),
//   ts is server-stamped at receipt:
//     {"type":"report","event_id":"...","ts":1234567890}
// --------------------------------------------------------------------------------

import { DurableObject } from "cloudflare:workers";

/** Reports kept per event_id - mirrors the client's local trim (see ws_client.cpp). */
const MAX_REPORTS_PER_EVENT = 10;

/** Sanity bound so a malformed/malicious client can't stuff huge strings into storage. */
const MAX_EVENT_ID_LENGTH = 128;

/** Minimum spacing between accepted reports from a single connection - not in the
 *  original spec, but a bare-minimum guard against a stuck/spammy client hammering
 *  the button; cheap to justify given there's no server-side auth at all. */
const MIN_SECONDS_BETWEEN_REPORTS = 2;

interface ReportRow extends Record<string, SqlStorageValue> {
  event_id: string;
  ts: number;
}

interface ClientMessage {
  type?: unknown;
  event_id?: unknown;
}

/** Per-connection state stashed via WebSocket attachments so it survives hibernation. */
interface SocketAttachment {
  lastReportTs?: number;
}

export class ShardObject extends DurableObject<Env> {
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
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

  async fetch(request: Request): Promise<Response> {
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("Expected WebSocket upgrade", { status: 426 });
    }

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);

    // Hibernation API: the runtime can drop this JS object between messages and
    // wake it back up on the next event without re-running the constructor's
    // in-memory state - only ctx.storage persists. We don't keep any in-memory
    // per-connection maps for that reason; everything either lives in SQLite or
    // in the socket's serialized attachment.
    this.ctx.acceptWebSocket(server);

    // Send history immediately on connect. Client re-sorts/trims itself, so
    // order here doesn't matter.
    server.send(JSON.stringify({ type: "history", reports: this.readHistory() }));

    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): Promise<void> {
    if (typeof message !== "string") return; // binary frames aren't part of this protocol

    let parsed: ClientMessage;
    try {
      parsed = JSON.parse(message);
    } catch {
      return; // malformed JSON - silently ignore, matches the "fire and forget" spirit
    }

    if (parsed.type !== "report") return;
    if (typeof parsed.event_id !== "string") return;

    const eventId = parsed.event_id.trim();
    if (eventId.length === 0 || eventId.length > MAX_EVENT_ID_LENGTH) return;

    const nowSeconds = Math.floor(Date.now() / 1000);

    const attachment = (ws.deserializeAttachment() as SocketAttachment) ?? {};
    if (
      attachment.lastReportTs !== undefined &&
      nowSeconds - attachment.lastReportTs < MIN_SECONDS_BETWEEN_REPORTS
    ) {
      return; // throttled - drop silently, same "no ack" contract as a normal report
    }
    attachment.lastReportTs = nowSeconds;
    ws.serializeAttachment(attachment);

    this.insertAndTrim(eventId, nowSeconds);

    const broadcast = JSON.stringify({ type: "report", event_id: eventId, ts: nowSeconds });
    for (const socket of this.ctx.getWebSockets()) {
      // Broadcasts to everyone on the shard, including the sender - server is
      // the timestamp authority, so the sender learns ts the same way as
      // everyone else rather than optimistically stamping client-side.
      try {
        socket.send(broadcast);
      } catch {
        // A dead socket here will get cleaned up via webSocketClose/webSocketError;
        // nothing to do at broadcast time.
      }
    }
  }

  async webSocketClose(ws: WebSocket, code: number, reason: string, wasClean: boolean): Promise<void> {
    try {
      ws.close(code, reason);
    } catch {
      // already closing/closed - nothing to do
    }
  }

  async webSocketError(ws: WebSocket): Promise<void> {
    try {
      ws.close(1011, "WebSocket error");
    } catch {
      // already closing/closed - nothing to do
    }
  }

  /** All buffered reports across every event_id this shard has seen, each event
   *  already capped at MAX_REPORTS_PER_EVENT by insertAndTrim on write. */
  private readHistory(): ReportRow[] {
    const cursor = this.ctx.storage.sql.exec<ReportRow>(
      `SELECT event_id, ts FROM reports ORDER BY ts DESC;`
    );
    return [...cursor];
  }

  private insertAndTrim(eventId: string, ts: number): void {
    this.ctx.storage.sql.exec(
      `INSERT INTO reports (event_id, ts) VALUES (?, ?);`,
      eventId,
      ts
    );
    // Keep only the newest MAX_REPORTS_PER_EVENT rows for this event_id.
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
