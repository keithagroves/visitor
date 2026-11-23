// The shots of the 2026-11-17 game update montage. Each one sets a scene up
// through the game's dev hooks (fast-forwarding on the virtual clock, no
// capture), then records a few seconds of real play into build/shots/<name>.mp4.
//
//   node shots.mjs                 # every shot
//   node shots.mjs abduct land     # just these
//
// Needs the game served at http://localhost:8080 (python3 -m http.server 8080
// in build/).

import { launch } from '../rig.mjs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const out = (name) => path.join(HERE, 'build', 'shots', name + '.mp4');
const W = 1920, H = 1080;
const DEG = Math.PI / 180;

// The game's own UI is sized for a desktop window; at video size the pickup
// feed, banners and inventory read better a little larger.
const UI_CSS = `
  #pickups, #event { zoom: 1.4; }
  #inventory { zoom: 1.35; }
  #nav-labels .lbl { font-size: 15px !important; }
  #nav-labels .lbl .sub { font-size: 12px !important; }`;

// ---- reading the game --------------------------------------------------------

// Ship/astronaut state relative to the world it is on: the HUD altitude, speed
// up/down and sideways, and the angle around the world of ship / astronaut.
async function state(rig) {
    return rig.eval(() => {
        Module._debugDump();
        const d = window.GAME_DUMP;
        const b = d.bodies[d.dominant];
        const ux = d.x - b.x, uy = d.y - b.y, L = Math.hypot(ux, uy);
        const rvx = d.vx - b.vx, rvy = d.vy - b.vy;
        return {
            ...d,
            alt: parseFloat(document.getElementById('hud-alt').textContent),
            vUp: (rvx * ux + rvy * uy) / L,
            vSide: (rvx * -uy + rvy * ux) / L,
            shipAngle: Math.atan2(uy, ux) * 180 / Math.PI,
            astroAngle: Math.atan2(d.ay - b.y, d.ax - b.x) * 180 / Math.PI,
        };
    });
}

// A world point on screen, through the camera in a dump.
function toScreen(d, x, y) {
    const c = Math.cos(d.rotv * DEG), s = Math.sin(d.rotv * DEG);
    const dx = (x - d.fx) * d.scale, dy = (y - d.fy) * d.scale;
    return [W / 2 + c * dx - s * dy, H / 2 + s * dx + c * dy];
}

// Angle (degrees, around body `idx`) of the direction to the star.
async function sunAngle(rig, idx) {
    const d = await rig.dump();
    const p = d.bodies[idx], sun = d.bodies[0];
    return Math.atan2(sun.y - p.y, sun.x - p.x) / DEG;
}

// Pin the time of day: the sun `offset` degrees from overhead at the spot
// `angle` degrees around the world you are on (~85-90 = sunset).
const norm360 = (a) => ((a % 360) + 360) % 360;
const light = (rig, angle, offset) => rig.dbg({ sunOverride: norm360(angle + offset) });

// ---- driving it ----------------------------------------------------------------

// Bang-bang thrust toward an altitude (call once a frame).
function hover(rig, target, { maxRate = 2.5, gain = 0.03 } = {}) {
    const ctl = async () => {
        const s = await state(rig);
        const want = Math.max(-maxRate, Math.min(maxRate, (ctl.target - s.alt) * gain));
        if (s.vUp < want) await rig.down('w'); else await rig.up('w');
        return s;
    };
    ctl.target = target;
    return ctl;
}

// Come down to land: fast high up, ~1 unit/frame at touchdown.
function descend(rig, { maxRate = 9, minRate = 0.9, gain = 0.025 } = {}) {
    return async () => {
        const s = await state(rig);
        if (s.state !== 0) { await rig.up('w'); return s; }
        const want = -Math.min(maxRate, Math.max(minRate, s.alt * gain));
        if (s.vUp < want) await rig.down('w'); else await rig.up('w');
        return s;
    };
}

// Step the game (no capture) until done(state) or maxFrames.
async function fastForward(rig, maxFrames, perFrame, done) {
    let s;
    for (let i = 0; i < maxFrames; i++) {
        s = await perFrame(i);
        if (done && done(s, i)) break;
        await rig.step(1);
    }
    await rig.releaseAll();
    return s;
}

async function landShip(rig) {
    const ctl = descend(rig);
    const s = await fastForward(rig, 60 * 40, () => ctl(), (s) => s.state !== 0);
    if (s.state !== 1) throw new Error('landing failed, state ' + s.state);
    await rig.step(20);
}

async function tapKey(rig, k, adv, frames = 3) {
    await rig.down(k);
    for (let i = 0; i < frames; i++) await adv();
    await rig.up(k);
}

// Zoom the camera (negative = in), spread over a few frames.
async function zoom(rig, dy, frames = 10) {
    for (let i = 0; i < frames; i++) { await rig.wheel(dy / frames); await rig.step(1); }
}

// Hold the mining laser on something until it is gone. find(dump) returns
// its screen point, or null once it has come apart. adv() advances a frame
// (a recorder's frame() or a plain step).
async function laserOn(rig, find, adv, maxFrames = 400) {
    let pressed = false, n = 0;
    for (; n < maxFrames; n++) {
        const d = await rig.dump();
        const at = find(d);
        if (!at) break;
        await rig.mouse(at[0], at[1]);
        if (!pressed) { await rig.mouseDown(); pressed = true; }
        await adv();
    }
    await rig.mouseUp();
    return n;
}
const propAt = (i) => (d) => { const q = d.props.find((p) => p.i === i); return q && q.alive ? [q.x, q.y] : null; };
const itemAt = (i) => (d) => { const q = d.items.find((p) => p.i === i); return q && q.alive ? [q.x, q.y] : null; };
const oreAt = (i) => (d) => { const q = d.ores.find((p) => p.i === i); return q && !q.mined ? [q.x, q.y] : null; };
const onScreen = (q, m = 120) => q.x > m && q.x < W - m && q.y > m && q.y < H - m;

async function setTool(rig, mode, adv) {
    for (let k = 0; k < 2; k++) {
        const d = await rig.dump();
        if (d.tool === mode) return;
        await tapKey(rig, 'q', adv, 2);
        await adv();
    }
}

// Put the ship low over world `idx` (sunlit side, `dAng` degrees on from the
// sun direction) so it becomes the world you are on.
async function goToWorld(rig, idx, dAng, alt) {
    const sa = await sunAngle(rig, idx);
    await rig.call('debugWarpAngle', idx, sa + dAng, alt);
    await rig.step(30);
    return sa + dAng;
}

// ---- shots -------------------------------------------------------------------

const shots = {};

// Opening: the purple exotic world Bredath from high above, the ship's beacon
// falling toward it (time-lapse).
shots.title = async (rig) => {
    await rig.goto(5, { hide: ['#event'] });
    await rig.manual(true);
    await goToWorld(rig, 5, 25, 22000);
    await rig.step(90);
    const rec = await rig.record(out('title'), { speed: 3 });
    await rec.play(7.6);
    await rec.close();
};

// From the landed ship out to the whole solar system (map view, M).
shots.system = async (rig) => {
    await rig.goto(6, { hide: ['#event'], css: UI_CSS });
    await rig.manual(true);
    await landShip(rig);
    await rig.step(40);
    const rec = await rig.record(out('system'), { speed: 2 });
    await rec.play(6.5, {
        cues: [[0.5, () => rig.down('m')], [0.7, () => rig.up('m')]],
        each: async (t) => { if (t > 2.4) await rig.wheel(-1.6); },
    });
    await rec.close();
};

// Flying in space above the volcanic world Thaum: turn and burn, the
// predicted path bending around the planet.
shots.cruise = async (rig) => {
    await rig.goto(6, { hide: ['#event'] });
    await rig.manual(true);
    await goToWorld(rig, 3, 15, 5000);
    await rig.step(60);
    await rig.down('d'); await rig.step(22); await rig.up('d');
    await rig.step(40);
    const rec = await rig.record(out('cruise'), { speed: 2 });
    await rec.play(6, { cues: [[0.4, () => rig.down('w')], [4.6, () => rig.up('w')]] });
    await rec.close();
};

// Descend to Bredath and touch down: "First landing" banner.
shots.land = async (rig) => {
    await rig.goto(5, { css: UI_CSS });
    await rig.manual(true);
    await goToWorld(rig, 5, 40, 1500);
    await rig.step(30);
    const ctl = descend(rig);
    let landedAt = -1;
    let rec = await rig.record(out('land'));
    await rec.play(12, {
        each: async (t) => {
            const s = await ctl();
            if (s.state === 1 && landedAt < 0) landedAt = t;
            if (s.state === 2) throw new Error('crashed');
            if (landedAt >= 0 && t > landedAt + 2.5) return 'stop';
        },
    });
    await rec.close();

    // -- out of the ship and over to the crystals
    await rig.style({ hide: ['#event'], css: UI_CSS });
    await zoom(rig, -380, 20);
    await rig.step(20);
    let d = await rig.dump();
    let [ax, ay] = toScreen(d, d.ax || d.x, d.ay || d.y);
    const crystalSide = (() => {
        const c = d.items.filter((q) => q.alive && q.kind === 2);
        return c.length ? Math.sign(c[0].x - W / 2) || 1 : 1;
    })();
    rec = await rig.record(out('stepout'));
    let adv = () => rec.frame();
    await rec.play(0.4);
    await tapKey(rig, 'e', adv, 3);
    await rec.play(0.6);
    const walkKey = crystalSide > 0 ? 'd' : 'a';
    await rig.down(walkKey);
    await rec.play(3.0, {
        each: async () => {
            const dd = await rig.dump();
            const [sx] = toScreen(dd, dd.ax, dd.ay);
            const near = dd.items.filter((q) => q.alive && q.kind === 2)
                .some((q) => Math.abs(q.x - sx) < 230);
            if (near) return 'stop';
        },
    });
    await rig.up(walkKey);
    await rec.play(0.9);
    await rec.close();

    // -- the mining laser: a crystal cluster, then a plant
    rec = await rig.record(out('laser'));
    adv = () => rec.frame();
    await rec.play(0.3);
    for (const kinds of [[2], [2], [0, 1]]) {
        const dd = await rig.dump();
        const [sx, sy] = toScreen(dd, dd.ax, dd.ay);
        const pick = dd.items.filter((q) => q.alive && kinds.includes(q.kind) && onScreen(q, 60))
            .sort((p, q) => Math.hypot(p.x - sx, p.y - sy) - Math.hypot(q.x - sx, q.y - sy))[0];
        if (!pick) continue;
        await laserOn(rig, itemAt(pick.i), adv, 240);
        await rec.play(0.3);
    }
    await rig.park();
    await rec.play(1.4);
    await rec.close();
};

// The herd under the drop-in point: hover and beam one up.
shots.abduct = async (rig) => {
    await rig.goto(6, { hide: ['#event'] });
    await rig.manual(true);
    // (no pinned sun here: a pinned world stops falling round the star, and a
    // flying ship then drifts off sideways under the star's pull)
    const ctl = hover(rig, 150);
    await fastForward(rig, 60 * 20, () => ctl(), (s, i) => i > 300 && Math.abs(s.alt - 150) < 12 && Math.abs(s.vUp) < 0.4);
    await zoom(rig, -300, 20);
    const rec = await rig.record(out('abduct'));
    await rec.play(5, { cues: [[0.6, () => rig.down('space')]], each: () => ctl() });
    await rec.close();
};

// Out of the ship and off across the grass, past the cows.
shots.walk = async (rig) => {
    await rig.goto(6, { hide: ['#event'] });
    await rig.manual(true);
    await light(rig, -90, 62);
    await landShip(rig);
    await zoom(rig, -450, 20);
    await rig.step(30);
    const rec = await rig.record(out('walk'));
    const adv = () => rec.frame();
    await rec.play(0.5);
    await tapKey(rig, 'e', adv, 3);
    await rec.play(0.5);
    await rec.play(3.4, {
        cues: [[0, () => rig.down('d')], [1.6, () => rig.down('w')], [1.75, () => rig.up('w')]],
    });
    await rec.close();
};

// The terrain cutter on the volcanic world: a tunnel down into the rock.
shots.dig = async (rig) => {
    await rig.goto(6, { hide: ['#event'], css: UI_CSS });
    await rig.manual(true);
    const base = (await sunAngle(rig, 3)) + 30;
    await goToWorld(rig, 3, 27, 500);                   // park the ship 3 degrees off
    await landShip(rig);
    await rig.call('debugTeleportAngle', base);         // a slope to cut into
    await light(rig, base, 60);
    await rig.step(90);
    await zoom(rig, -450, 20);
    await rig.step(30);
    await setTool(rig, 1, () => rig.step(1));
    const rec = await rig.record(out('dig'));
    await rec.play(0.3);
    await rig.mouseDown();
    await rec.play(6, {
        cues: [[0.6, () => rig.down('d')]],
        each: async () => {
            const d = await rig.dump();
            const [ax, ay] = toScreen(d, d.ax, d.ay);
            await rig.mouse(ax + 130, ay + 135);   // ahead and down
        },
    });
    await rig.mouseUp();
    await rec.close();
};

// The ice-blue crystal cave on the home world: a walk, and a crystal lasered
// out; then the inventory, after taking a few more things from the cave.
shots.cave = async (rig) => {
    await rig.goto(6, { hide: ['#event'], css: UI_CSS });
    await rig.manual(true);
    await landShip(rig);
    await rig.call('debugTeleportProp', 537);
    await rig.step(90);
    await zoom(rig, -500, 20);
    await rig.step(40);
    const rec = await rig.record(out('cave'));
    const adv = () => rec.frame();
    await rec.play(2.4, { cues: [[0.2, () => rig.down('a')], [2.0, () => rig.up('a')]] });
    {
        // a crystal hanging overhead, if there is one in reach; else the nearest
        const d = await rig.dump();
        const [ax, ay] = toScreen(d, d.ax, d.ay);
        const cands = d.props.filter((q) => q.alive && q.kind === 3 && onScreen(q, 60));
        const dist = (q) => Math.hypot(q.x - ax, q.y - ay);
        const pick = cands.filter((q) => q.y < ay - 60).sort((p, q) => dist(p) - dist(q))[0] ||
                     cands.sort((p, q) => dist(p) - dist(q))[0];
        if (pick) await laserOn(rig, propAt(pick.i), adv, 240);
    }
    await rig.park();
    await rec.play(1.6);
    await rec.close();

    // -- off camera: a little haul from the same cave for the inventory
    // (glowcaps, calcite, a gold vein, a boulder's ferrite)
    const step = () => rig.step(1);
    for (const kind of [4, 0, 5, 1, 6, 3]) {
        const dd = await rig.dump();
        const pick = dd.props.find((q) => q.alive && q.kind === kind && onScreen(q, 20));
        if (pick) await laserOn(rig, propAt(pick.i), step, 500);
    }
    await rig.park();
    await rig.step(300);   // let the pickup feed clear

    // -- the inventory (without the pickup feed peeking out from under it)
    await rig.style({ hide: ['#event', '#pickups'], css: UI_CSS });
    const rec2 = await rig.record(out('inventory'));
    const adv2 = () => rec2.frame();
    await rec2.play(0.4);
    await tapKey(rig, 'i', adv2, 3);
    await rec2.play(0.6);
    const slots = await rig.eval(() => [...document.querySelectorAll('.inv-slot.filled')].map((el) => {
        const r = el.getBoundingClientRect();
        return [r.x + r.width / 2, r.y + r.height / 2, el.dataset.item];
    }));
    console.log('inventory slots', slots.map((s) => s[2]).join(','));
    for (const [x, y] of slots.slice(0, 6)) {
        await rig.mouse(x, y);
        await rec2.play(0.45);
    }
    await rec2.play(0.8);
    await rec2.close();
};

// Buried gold on the home world. The shaft has to be cut in a fresh session:
// after a teleport elsewhere (the cave) an astronaut put at its mouth stands
// on the undug surface.
shots.ore = async (rig) => {
    await rig.goto(6, { hide: ['#event'], css: UI_CSS });
    await rig.manual(true);
    const step = () => rig.step(1);
    await landShip(rig);

    // A shaft beside a gold clump 316 down (found with the native ore
    // harness: seed 6, world 1, angle -81.118, size 8.6); drop down it.
    const oreAngle = -81.118, oreDepth = 315.7;
    const shaftAngle = oreAngle - (55 / 32000) / DEG;
    await rig.call('debugDigShaft', shaftAngle, oreDepth + 14, 20);
    await rig.call('debugTeleportAngle', shaftAngle);
    await rig.step(260);
    await zoom(rig, -800, 30);
    await rig.step(50);
    let d = await rig.dump();
    console.log('in shaft: floorGap', d.floorGap, 'belowLine', d.belowLine, 'ores', d.ores.length);
    if (d.belowLine < 100) throw new Error('astronaut did not drop down the shaft');
    await setTool(rig, 1, step);
    let rec = await rig.record(out('ore'));
    let adv = () => rec.frame();
    await rec.play(0.4);
    // cut toward the clump until the cutter is refused and it shows
    const known = d.ores.length;
    {
        const d0 = await rig.dump();
        const [ax, ay] = toScreen(d0, d0.ax, d0.ay);
        const b = d0.bodies[d0.dominant];
        const r = Math.hypot(d0.ax - b.x, d0.ay - b.y);
        const [ox] = toScreen(d0, b.x + Math.cos(oreAngle * DEG) * r, b.y + Math.sin(oreAngle * DEG) * r);
        const dir = Math.sign(ox - ax) || 1;
        console.log('astronaut on screen', ax.toFixed(0), ay.toFixed(0), 'clump side', dir);
        const reach = 75 * d0.scale;   // aim 75 world units out, past the clump
        await rig.mouse(ax + dir * reach, ay + 2 * d0.scale);
        await rig.mouseDown();
        for (let i = 0; i < 240; i++) {
            await adv();
            const dd = await rig.dump();
            if (dd.ores.length > known && dd.ores.some((o) => !o.mined && o.kind === 2)) break;
            const [bx, by] = toScreen(dd, dd.ax, dd.ay);
            await rig.mouse(bx + dir * 75 * dd.scale, by + 2 * dd.scale);
        }
        await rec.play(0.6);   // a moment of sparks on the refused bite
        await rig.mouseUp();
    }
    await rec.play(0.4);
    await setTool(rig, 0, adv);
    await rec.play(0.2);
    {
        const dd = await rig.dump();
        const [sx, sy] = toScreen(dd, dd.ax, dd.ay);
        const target = dd.ores.filter((o) => !o.mined && o.kind === 2)
            .sort((p, q) => Math.hypot(p.x - sx, p.y - sy) - Math.hypot(q.x - sx, q.y - sy))[0];
        console.log('gold target', JSON.stringify(target));
        if (target) await laserOn(rig, oreAt(target.i), adv, 400);
    }
    await rig.park();
    await rec.play(1.8);
    await rec.close();

};

// Afternoon to night in a few seconds, by the landed ship.
shots.dusk = async (rig) => {
    await rig.goto(6, { hide: ['#event'] });
    await rig.manual(true);
    await light(rig, -90, 58);
    await landShip(rig);
    await tapKey(rig, 'e', () => rig.step(1), 3);
    await rig.step(30);
    await rig.down('a'); await rig.step(50); await rig.up('a');
    await zoom(rig, -250, 20);
    await rig.step(60);
    const rec = await rig.record(out('dusk'));
    await rec.play(6, { each: async (t) => { await light(rig, -90, 58 + (t / 6) * 50); } });
    await rec.close();
};

// Lift off and climb out of the atmosphere (time-lapse).
shots.liftoff = async (rig) => {
    await rig.goto(6, { hide: ['#event'] });
    await rig.manual(true);
    await light(rig, -90, 55);
    await landShip(rig);
    await rig.step(60);
    const rec = await rig.record(out('liftoff'), { speed: 3 });
    await rec.play(15, { cues: [[0.3, () => rig.down('w')]] });
    await rec.close();
};

// ---- main --------------------------------------------------------------------

const want = process.argv.slice(2);
const names = want.length ? want : Object.keys(shots);
const rig = await launch();
try {
    for (const n of names) {
        if (!shots[n]) throw new Error('no shot ' + n);
        const t0 = Date.now();
        await shots[n](rig);
        console.log(`${n}: ${((Date.now() - t0) / 1000).toFixed(1)} s`);
    }
} finally {
    await rig.close();
}
