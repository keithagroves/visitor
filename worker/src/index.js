// Visitor multiplayer server, hosted: a Cloudflare Worker + one Durable Object
// per star system. It speaks the same messages as server/server.mjs (see the
// list there); what differs is how it is built:
//
//   - The Worker only routes: a WebSocket to /?seed=N goes to the room (the
//     Durable Object) named N. Every player of a system lands in the same one.
//   - The room uses the WebSocket HIBERNATION API: when nobody has sent
//     anything for a while it is put to sleep and not billed, and the sockets
//     stay open. So nothing can live only in memory: each socket's own facts
//     (id, name, the world it is at) are its "attachment", and the room's
//     clock start and world log are in its SQLite storage.
//   - The WORLD LOG is kept, not just held: the world stays dug after
//     everybody leaves, and through restarts. Changes are written in BATCHES
//     (one row every few seconds, not one per bite of the cutter), since the
//     free plan allows 100,000 rows written a day.
//   - There is no pub/sub here: "everyone at world p" is a walk over the
//     room's sockets.
//
// Free-plan budget (Durable Objects on the SQLite backend): 100,000 requests a
// day, where a WebSocket connect is one and incoming messages count 20 to one
// (outgoing are free); 13,000 GB-s of time awake a day (a busy room is awake,
// at 128 MB: ~29 room-hours); 100,000 rows written. Over a limit, it stops
// until 00:00 UTC; nothing is charged.
import { DurableObject } from 'cloudflare:workers';

const MAX_OPS = 500000;      // per room
const MAX_NAME = 20;
const MAX_IN = 256 * 1024;   // largest message accepted
const FLUSH_MS = 3000;       // how long a change may wait before it is written
const BATCH = 2000;          // most changes in one row (~80 KB of text)

// A colour for a player joining (a hue for their ship and suit): the one
// they asked for if nobody on now is within MIN_HUE of it, else the hue
// furthest from everyone's (the nearest to the one asked for, among equals).
const MIN_HUE = 30;
function pickHue(want, taken) {
    want = Number.isFinite(+want) ? ((Math.round(+want) % 360) + 360) % 360 : Math.floor(Math.random() * 360);
    const gap = (a, b) => { const d = Math.abs(a - b) % 360; return Math.min(d, 360 - d); };
    const room = (h) => taken.reduce((m, t) => Math.min(m, gap(h, t)), 360);
    if (room(want) >= MIN_HUE) return want;
    let best = want, bestRoom = -1, bestGap = 0;
    for (let h = 0; h < 360; h++) {
        const r = room(h), g = gap(h, want);
        if (r > bestRoom || (r === bestRoom && g < bestGap)) { best = h; bestRoom = r; bestGap = g; }
    }
    return best;
}

export default {
    async fetch(request, env) {
        if ((request.headers.get('Upgrade') || '').toLowerCase() !== 'websocket')
            return new Response('Visitor multiplayer server: connect with a WebSocket.\n');
        const seed = Number(new URL(request.url).searchParams.get('seed')) >>> 0;
        if (!seed) return new Response('Which system? Connect to /?seed=N.\n', { status: 400 });
        return env.ROOMS.get(env.ROOMS.idFromName(String(seed))).fetch(request);
    },
};

export class Room extends DurableObject {
    constructor(ctx, env) {
        super(ctx, env);
        this.sql = ctx.storage.sql;
        this.sql.exec('CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v INTEGER)');
        // One row per batch of changes: n of them, as the JSON text of
        // [[time, kind, ...], ...]. The text goes out to joiners as it is.
        this.sql.exec('CREATE TABLE IF NOT EXISTS ops (n INTEGER, ops TEXT)');
        this.start = this.meta('start');
        if (this.start === null) { this.start = Date.now(); this.setMeta('start', this.start); }
        this.batches = null;     // the stored log, loaded when first needed
        this.count = 0;
        this.pending = [];       // changes not written yet
        this.flushTimer = null;
    }

    meta(k) {
        const rows = this.sql.exec('SELECT v FROM meta WHERE k = ?', k).toArray();
        return rows.length ? rows[0].v : null;
    }
    setMeta(k, v) { this.sql.exec('INSERT OR REPLACE INTO meta (k, v) VALUES (?, ?)', k, v); }
    clock() { return Date.now() - this.start; }

    load() {
        if (this.batches) return;
        this.batches = [];
        this.count = 0;
        for (const row of this.sql.exec('SELECT n, ops FROM ops ORDER BY rowid')) {
            this.batches.push(row.ops);
            this.count += row.n;
        }
    }

    // Write what is waiting, a batch per row. The pending timer also keeps the
    // room awake until then, so nothing is lost to hibernation.
    flush() {
        if (this.flushTimer) { clearTimeout(this.flushTimer); this.flushTimer = null; }
        while (this.pending.length) {
            const part = this.pending.splice(0, BATCH);
            const text = JSON.stringify(part);
            this.sql.exec('INSERT INTO ops (n, ops) VALUES (?, ?)', part.length, text);
            this.batches.push(text);
        }
    }

    // The players: every open socket that has joined, with its attachment.
    players() {
        const out = [];
        for (const ws of this.ctx.getWebSockets()) {
            if (ws.readyState !== 1) continue;
            const u = ws.deserializeAttachment();
            if (u && u.joined) out.push({ ws, u });
        }
        return out;
    }
    roster(list = this.players()) {
        return JSON.stringify({ t: 'roster', players: list.map(({ u }) => ({ id: u.id, name: u.name, p: u.planet, hue: u.hue })) });
    }
    static send(ws, text) { try { ws.send(text); } catch (e) { /* closing */ } }

    async fetch(request) {
        const [client, server] = Object.values(new WebSocketPair());
        const id = (this.meta('next') || 1);
        this.setMeta('next', id + 1);
        this.ctx.acceptWebSocket(server);
        server.serializeAttachment({ id, name: '', planet: -1, joined: false });
        return new Response(null, { status: 101, webSocket: client });
    }

    async webSocketMessage(ws, data) {
        if (typeof data !== 'string' || data.length > MAX_IN) return;
        let m;
        try { m = JSON.parse(data); } catch (e) { return; }
        if (!m || typeof m !== 'object') return;
        const u = ws.deserializeAttachment();
        if (m.t === 'join') {
            if (u.joined) return;
            u.name = String(m.name || 'Visitor').replace(/[^\w .'-]/g, '').slice(0, MAX_NAME) || 'Visitor';
            u.hue = pickHue(m.hue, this.players().map((q) => q.u.hue));
            u.joined = true;
            ws.serializeAttachment(u);
            // The log goes out as it is stored: the welcome says how many
            // batches follow, then one message per batch.
            this.load();
            const logs = this.batches.length + (this.pending.length ? 1 : 0);
            Room.send(ws, JSON.stringify({ t: 'welcome', id: u.id, hue: u.hue, clock: this.clock(), ops: [], logs }));
            for (const text of this.batches) Room.send(ws, '{"t":"log","ops":' + text + '}');
            if (this.pending.length) Room.send(ws, JSON.stringify({ t: 'log', ops: this.pending }));
            const list = this.players();
            const r = this.roster(list);
            for (const p of list) Room.send(p.ws, r);
            return;
        }
        if (!u.joined) return;
        switch (m.t) {
            case 's': {
                const p = m.p | 0;
                let list = null;
                if (p !== u.planet) {
                    list = this.players();
                    const bye = JSON.stringify({ t: 'bye', id: u.id });
                    for (const q of list) if (q.u.id !== u.id && q.u.planet === u.planet && u.planet >= 0) Room.send(q.ws, bye);
                    u.planet = p;
                    ws.serializeAttachment(u);
                    for (const q of list) if (q.u.id === u.id) q.u.planet = p;
                    const r = this.roster(list);
                    for (const q of list) Room.send(q.ws, r);
                }
                if (Array.isArray(m.d) && m.d.length <= 64) {
                    const text = JSON.stringify({ t: 's', id: u.id, p, d: m.d });
                    for (const q of list || this.players()) if (q.u.id !== u.id && q.u.planet === p) Room.send(q.ws, text);
                }
                // Changes to the world ride along with the state.
                if (Array.isArray(m.o)) this.ops(u, m.o);
                break;
            }
            case 'ops':
                if (Array.isArray(m.o)) this.ops(u, m.o);
                break;
            case 'op':
                this.ops(u, [m.o]);
                break;
            case 'ping':
                Room.send(ws, JSON.stringify({ t: 'pong', c: m.c, s: this.clock() }));
                break;
        }
    }

    // Changes to the world: stamped with the room clock, logged, and sent to
    // everyone in the system.
    ops(u, list) {
        if (list.length > 64) return;
        this.load();
        let everyone = null;
        for (const o of list) {
            if (!Array.isArray(o) || !o.length || o.length > 16) continue;
            const op = [this.clock(), ...o.map((x) => (Number.isFinite(+x) ? +x : 0))];
            if (this.count < MAX_OPS) {
                this.pending.push(op);
                this.count++;
            }
            everyone = everyone || this.players();
            const text = JSON.stringify({ t: 'op', o: op, id: u.id });
            for (const q of everyone) if (q.u.id !== u.id) Room.send(q.ws, text);
        }
        if (this.pending.length >= BATCH) this.flush();
        else if (this.pending.length && !this.flushTimer) this.flushTimer = setTimeout(() => this.flush(), FLUSH_MS);
    }

    left(ws) {
        const u = ws.deserializeAttachment();
        if (!u || !u.joined) return;
        u.joined = false;
        ws.serializeAttachment(u);
        const list = this.players().filter((q) => q.u.id !== u.id);
        const bye = JSON.stringify({ t: 'bye', id: u.id });
        for (const q of list) if (q.u.planet === u.planet && u.planet >= 0) Room.send(q.ws, bye);
        const r = this.roster(list);
        for (const q of list) Room.send(q.ws, r);
        if (!list.length && this.pending.length) this.flush();
    }
    async webSocketClose(ws, code, reason) {
        this.left(ws);
        try { ws.close(code, reason); } catch (e) { /* already closed */ }
    }
    async webSocketError(ws) { this.left(ws); }
}
