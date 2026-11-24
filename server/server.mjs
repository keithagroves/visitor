// Visitor multiplayer server (uWebSockets.js).
//
// One ROOM per star system (the seed builds the whole system the same way on
// every client, so only what changes needs to travel). A room holds:
//   - a CLOCK: milliseconds since the room opened. Every client puts the
//     planets on their orbits from it, so everyone sees the same sky.
//   - the WORLD LOG: every change anyone has made to the world, in the order
//     the server received it — bites dug out of the rock, plants cut, cave
//     formations and ore mined, trading-post stock. A player who joins late
//     replays it and sees everyone's tunnels.
//   - the PLAYERS, each subscribed to the topic of the world they are at.
//     Position updates go only to that topic: you hear about the players in
//     your vicinity, not the whole system.
// Each player's own progress (credits, equipment, what they carry) stays in
// their own browser; the server does not judge or validate anything yet.
//
// Messages are JSON text frames:
//   client -> server
//     {t:'join', seed, name, hue}            enter a system's room (hue: the
//                                            colour I would like)
//     {t:'s', p, d:[...], o?:[[kind, ...]]}  my state (p = the world I am at),
//                                            with any changes I made since
//     {t:'ops', o:[[kind, ...], ...]}        changes to the world
//     {t:'op', o:[kind, ...]}                one change (older clients)
//     {t:'ping', c}                          clock sync
//   server -> client
//     {t:'welcome', id, hue, clock, ops:[[time, kind, ...], ...], logs}
//                                            (hue: my colour, this session)
//     {t:'log', ops:[...]}                   more of the world log: \`logs\` of
//                                            these follow the welcome
//     {t:'s', id, p, d}                      a nearby player's state
//     {t:'op', o:[time, kind, ...], id}      someone changed the world
//     {t:'bye', id}                          a player left your vicinity
//     {t:'roster', players:[{id, name, p, hue}]}  who is in the system
//     {t:'pong', c, s}                       clock sync
//
// The client picks the room by ?seed=N in the address too, which is how the
// hosted server (worker/, a Cloudflare Durable Object per system) routes; this
// one goes by the join message.
//
// Run: cd server && npm install && npm start   (PORT=9001 by default)
import uWS from 'uWebSockets.js';

const PORT = +(process.env.PORT || 9001);
const MAX_OPS = 500000;          // per room: a few hours of hard digging
const MAX_NAME = 20;

const rooms = new Map();         // seed -> room
let nextId = 1;

function room(seed) {
    let r = rooms.get(seed);
    if (!r) {
        r = { seed, start: Date.now(), ops: [], players: new Map() };
        rooms.set(seed, r);
        console.log(`room ${seed} opened`);
    }
    return r;
}
const clock = (r) => Date.now() - r.start;
const roomTopic = (r) => `r/${r.seed}`;
const planetTopic = (r, p) => `r/${r.seed}/p/${p}`;

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

function roster(r) {
    return JSON.stringify({
        t: 'roster',
        players: [...r.players.values()].map((u) => ({ id: u.id, name: u.name, p: u.planet, hue: u.hue })),
    });
}

// Changes to the world: stamped with the room clock, logged, and sent to
// everyone in the system (not only the players nearby: it is all theirs to
// walk into later).
function changes(ws, u, r, list) {
    if (list.length > 64) return;
    for (const o of list) {
        if (!Array.isArray(o) || !o.length || o.length > 16) continue;
        const op = [clock(r), ...o.map((x) => (Number.isFinite(+x) ? +x : 0))];
        if (r.ops.length < MAX_OPS) r.ops.push(op);
        ws.publish(roomTopic(r), JSON.stringify({ t: 'op', o: op, id: u.id }));
    }
}

const app = uWS.App();
app.ws('/*', {
    compression: uWS.SHARED_COMPRESSOR,
    maxPayloadLength: 256 * 1024,
    idleTimeout: 60,
    sendPingsAutomatically: true,

    upgrade: (res, req, context) => {
        res.upgrade({ id: nextId++, room: null, name: '', planet: -1 },
            req.getHeader('sec-websocket-key'), req.getHeader('sec-websocket-protocol'),
            req.getHeader('sec-websocket-extensions'), context);
    },

    open: (ws) => {},

    message: (ws, data) => {
        let m;
        try { m = JSON.parse(Buffer.from(data).toString()); } catch (e) { return; }
        const u = ws.getUserData();
        if (m.t === 'join') {
            if (u.room) return;
            const seed = (m.seed >>> 0) || 1;
            const r = room(seed);
            u.room = r;
            u.name = String(m.name || 'Visitor').replace(/[^\w .'-]/g, '').slice(0, MAX_NAME) || 'Visitor';
            u.ws = ws;
            u.hue = pickHue(m.hue, [...r.players.values()].map((q) => q.hue));
            r.players.set(u.id, u);
            ws.subscribe(roomTopic(r));
            // The world log can be big: send it with the welcome in one frame.
            ws.send(JSON.stringify({ t: 'welcome', id: u.id, hue: u.hue, clock: clock(r), ops: r.ops, logs: 0 }));
            app.publish(roomTopic(r), roster(r));
            console.log(`${u.name} (#${u.id}) joined room ${seed}: ${r.players.size} playing`);
            return;
        }
        const r = u.room;
        if (!r) return;
        switch (m.t) {
            case 's': {
                // State goes to the world you are at. Moving to another world
                // switches topics, and the old one is told you have gone.
                const p = m.p | 0;
                if (p !== u.planet) {
                    if (u.planet >= 0) {
                        ws.unsubscribe(planetTopic(r, u.planet));
                        app.publish(planetTopic(r, u.planet), JSON.stringify({ t: 'bye', id: u.id }));
                    }
                    u.planet = p;
                    ws.subscribe(planetTopic(r, p));
                    app.publish(roomTopic(r), roster(r));
                }
                if (Array.isArray(m.d) && m.d.length <= 64)
                    ws.publish(planetTopic(r, p), JSON.stringify({ t: 's', id: u.id, p, d: m.d }));
                if (Array.isArray(m.o)) changes(ws, u, r, m.o);
                break;
            }
            case 'ops':
                if (Array.isArray(m.o)) changes(ws, u, r, m.o);
                break;
            case 'op':
                changes(ws, u, r, [m.o]);
                break;
            case 'ping':
                ws.send(JSON.stringify({ t: 'pong', c: m.c, s: clock(r) }));
                break;
        }
    },

    close: (ws) => {
        const u = ws.getUserData();
        const r = u.room;
        if (!r) return;
        r.players.delete(u.id);
        if (u.planet >= 0) app.publish(planetTopic(r, u.planet), JSON.stringify({ t: 'bye', id: u.id }));
        app.publish(roomTopic(r), roster(r));
        console.log(`${u.name} (#${u.id}) left room ${r.seed}: ${r.players.size} playing`);
    },
});
app.get('/*', (res) => { res.end('Visitor multiplayer server: connect with a WebSocket.'); });
app.listen(PORT, (token) => {
    if (token) console.log(`Visitor server listening on ws://localhost:${PORT}`);
    else { console.error(`could not listen on port ${PORT}`); process.exit(1); }
});
