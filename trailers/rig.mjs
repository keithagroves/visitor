// Capture rig for gameplay footage of the web build.
//
// The game runs in headless Chrome on the real GPU (ANGLE/Metal; the default
// headless shell falls back to SwiftShader at ~2 fps). While a shot records,
// the page runs on a virtual clock instead of wall time: performance.now,
// Date.now, requestAnimationFrame, timers and CSS animations/transitions only
// advance when the rig steps them, one 1/60 s game frame at a time, and every
// captured frame is a screenshot of the whole page (canvas + the HTML UI on
// top of it: pickup feed, inventory, banners). So the footage is perfectly
// smooth however long each screenshot takes, and the game's physics sees the
// same 60 Hz frames as in normal play.
//
//   const rig = await launch();
//   await rig.goto(7);                       // game.html?seed=7, waits for the wasm
//   const rec = await rig.record('build/shots/takeoff.mp4');
//   await rec.play(4, { cues: [[0.5, () => rig.down('w')]] });
//   await rec.close();

import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

// Installed before any page script runs. window.__vt.setManual(true) freezes
// the page clock; __vt.step(ms) then advances it: due timers, then the queued
// animation-frame callbacks (the game's main loop), then CSS animations are
// seeked to the new time.
export const VT_INIT = String.raw`(() => {
  if (window.__vt) return;
  const P = performance, realNow = P.now.bind(P);
  const realRAF = window.requestAnimationFrame.bind(window), realCAF = window.cancelAnimationFrame.bind(window);
  const realST = window.setTimeout.bind(window), realCT = window.clearTimeout.bind(window);
  const realSI = window.setInterval.bind(window), realCI = window.clearInterval.bind(window);
  const V = { manual: false, offset: 0, vnow: 0, raf: new Map(), timers: new Map(), id: 1e9, anims: new Map(), frame: 0 };
  window.__vt = V;
  const now = () => (V.manual ? V.vnow : realNow() + V.offset);
  V.now = now;
  P.now = now;
  const dateBase = Date.now() - realNow();
  Date.now = () => Math.floor(dateBase + now());
  window.requestAnimationFrame = (cb) => {
    if (!V.manual) return realRAF(() => cb(now()));
    const id = ++V.id; V.raf.set(id, cb); return id;
  };
  window.cancelAnimationFrame = (id) => { if (!V.raf.delete(id)) realCAF(id); };
  window.setTimeout = (fn, ms, ...a) => {
    if (!V.manual) return realST(fn, ms, ...a);
    const id = ++V.id; V.timers.set(id, { due: V.vnow + (+ms || 0), fn, a, every: 0 }); return id;
  };
  window.clearTimeout = (id) => { if (!V.timers.delete(id)) realCT(id); };
  window.setInterval = (fn, ms, ...a) => {
    if (!V.manual) return realSI(fn, ms, ...a);
    const every = Math.max(1, +ms || 0);
    const id = ++V.id; V.timers.set(id, { due: V.vnow + every, fn, a, every }); return id;
  };
  window.clearInterval = (id) => { if (!V.timers.delete(id)) realCI(id); };

  function runTimers() {
    for (let guard = 0; guard < 2000; guard++) {
      let best = null, bestId = 0;
      for (const [id, t] of V.timers)
        if (t.due <= V.vnow && (!best || t.due < best.due || (t.due === best.due && id < bestId))) { best = t; bestId = id; }
      if (!best) return;
      if (best.every) best.due += best.every; else V.timers.delete(bestId);
      try { typeof best.fn === 'function' ? best.fn(...best.a) : (0, eval)(best.fn); } catch (e) { console.error(e); }
    }
  }
  function seekAnims() {
    for (const an of document.getAnimations()) {
      let s = V.anims.get(an);
      if (!s) { s = { start: V.vnow - (an.currentTime || 0) }; V.anims.set(an, s); try { an.pause(); } catch (e) {} }
      const t = V.vnow - s.start;
      const end = an.effect ? an.effect.getComputedTiming().endTime : Infinity;
      if (isFinite(end) && t >= end) { try { an.finish(); } catch (e) {} V.anims.delete(an); }
      else { try { an.currentTime = t; } catch (e) {} }
    }
  }
  V.setManual = (on) => {
    if (on === V.manual) return;
    if (on) { V.vnow = realNow() + V.offset; V.manual = true; return; }
    V.offset = V.vnow - realNow();
    V.manual = false;
    const raf = V.raf; V.raf = new Map();
    for (const cb of raf.values()) realRAF(() => cb(now()));
    const timers = V.timers; V.timers = new Map();
    for (const t of timers.values()) {
      if (t.every) realSI(t.fn, t.every, ...t.a); else realST(t.fn, Math.max(0, t.due - V.vnow), ...t.a);
    }
    for (const an of V.anims.keys()) { try { an.play(); } catch (e) {} }
    V.anims.clear();
  };
  V.step = (ms) => {
    V.vnow += ms; V.frame++;
    runTimers();
    const raf = V.raf; V.raf = new Map();
    for (const cb of raf.values()) { try { cb(V.vnow); } catch (e) { console.error(e); } }
    seekAnims();
  };
})();`;

// Everything on the page that is debug/dev chrome rather than game. The
// pickup feed, inventory, discovery banner and world labels stay.
const CLEAN_CSS = `#hud, #debug-toggle, #debug-panel, #toast, #message, #status, #notice, #title { display: none !important; }`;

const KEYS = { w: 'KeyW', a: 'KeyA', s: 'KeyS', d: 'KeyD', e: 'KeyE', q: 'KeyQ', m: 'KeyM', r: 'KeyR', i: 'KeyI',
               space: 'Space', tab: 'Tab', esc: 'Escape', left: 'ArrowLeft', right: 'ArrowRight', up: 'ArrowUp' };
const keyName = (k) => KEYS[k] || k;

export async function launch({ width = 1920, height = 1080, base = 'http://localhost:8080' } = {}) {
    const browser = await chromium.launch({
        headless: true,
        channel: 'chromium',        // new headless mode: real GPU via ANGLE
        args: ['--use-angle=metal', '--enable-gpu', '--ignore-gpu-blocklist', '--hide-scrollbars', '--mute-audio'],
    });
    const context = await browser.newContext({ viewport: { width, height }, deviceScaleFactor: 1 });
    await context.addInitScript(VT_INIT);
    const page = await context.newPage();
    const cdp = await context.newCDPSession(page);
    // python's http.server sends no cache headers: never let a stale wasm in.
    await cdp.send('Network.enable');
    await cdp.send('Network.clearBrowserCache');
    await cdp.send('Network.setCacheDisabled', { cacheDisabled: true });
    page.on('pageerror', (e) => console.error('[page error]', e.message));
    return new Rig(browser, page, cdp, { width, height, base });
}

export class Rig {
    constructor(browser, page, cdp, opts) {
        Object.assign(this, { browser, page, cdp, ...opts });
        this.held = new Set();
    }

    // Load game.html?seed=N and wait for the wasm. `hide`: extra selectors
    // to hide on top of the dev chrome (e.g. '#event' for no banners);
    // `css`: extra styles for the video.
    async goto(seed, { clean = true, hide = [], css = '' } = {}) {
        // notitle: straight into a new game (no title screen, no saved game).
        await this.page.goto(`${this.base}/game.html?seed=${seed}&notitle`);
        await this.page.waitForFunction(() => typeof Module !== 'undefined' && Module._debugDump &&
            document.getElementById('status').style.display === 'none' && window.DBG, null, { timeout: 30000 });
        if (clean) await this.page.addStyleTag({ content: CLEAN_CSS });
        this.extraStyle = await this.page.addStyleTag({ content: '/* per-shot */' });
        await this.style({ hide, css });
        await this.park();
        this.held.clear();
        await this.wait(400);
    }
    // Replace the per-shot styles (see goto).
    async style({ hide = [], css = '' } = {}) {
        const text = (hide.length ? `${hide.join(', ')} { display: none !important; }\n` : '') + css;
        await this.extraStyle.evaluate((el, t) => { el.textContent = t; }, text);
    }
    // Mouse out of the way (a cursor over something shows the laser's target brackets).
    park() { return this.page.mouse.move(this.width - 4, 4); }

    // Real-time wait (only meaningful outside a recording).
    wait(ms) { return new Promise((r) => setTimeout(r, ms)); }

    // Game frames without capturing (virtual clock must be on).
    async step(frames = 1) {
        await this.page.evaluate((n) => { for (let i = 0; i < n; i++) window.__vt.step(1000 / 60); }, frames);
    }
    async manual(on) { await this.page.evaluate((v) => window.__vt.setManual(v), on); }

    dbg(values) { return this.page.evaluate((v) => Object.assign(window.DBG, v), values); }
    call(name, ...args) { return this.page.evaluate(([n, a]) => Module['_' + n](...a), [name, args]); }
    eval(fn, arg) { return this.page.evaluate(fn, arg); }
    async dump() {
        return this.page.evaluate(() => { Module._debugDump(); return window.GAME_DUMP; });
    }
    hud() {
        return this.page.evaluate(() => ({
            world: document.getElementById('hud-world').textContent,
            kind: document.getElementById('hud-kind').textContent,
            status: document.getElementById('hud-status').textContent,
            tool: document.getElementById('hud-tool').textContent,
        }));
    }

    async down(k) { k = keyName(k); if (!this.held.has(k)) { this.held.add(k); await this.page.keyboard.down(k); } }
    async up(k) { k = keyName(k); if (this.held.has(k)) { this.held.delete(k); await this.page.keyboard.up(k); } }
    async releaseAll() { for (const k of [...this.held]) await this.up(k); await this.page.mouse.up(); }
    // The game polls key state once a frame: a tap must stay down across a
    // frame boundary, so down() and up() go in different frames (see Recorder.tap).
    mouse(x, y) { return this.page.mouse.move(x, y); }
    mouseDown() { return this.page.mouse.down(); }
    mouseUp() { return this.page.mouse.up(); }
    wheel(dy) { return this.page.mouse.wheel(0, dy); }

    async shot(file) {
        const { data } = await this.cdp.send('Page.captureScreenshot', { format: 'jpeg', quality: 92 });
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, Buffer.from(data, 'base64'));
    }

    // Start a recording into `file` (an H.264 intermediate). The virtual
    // clock is switched on until close().
    // `speed` > 1 is a time-lapse: that many game frames per video frame.
    async record(file, { fps = 60, speed = 1 } = {}) {
        const steps = (60 / fps) * speed;
        if (!Number.isInteger(steps) || steps < 1) throw new Error('60/fps*speed must be a whole number');
        fs.mkdirSync(path.dirname(file), { recursive: true });
        await this.manual(true);
        const where = await this.page.evaluate(() => ({
            world: document.getElementById('hud-world').textContent,
            kind: document.getElementById('hud-kind').textContent,
            system: document.getElementById('hud-system').textContent,
        }));
        return new Recorder(this, file, fps, steps, where);
    }

    async close() { await this.browser.close(); }
}

export class Recorder {
    constructor(rig, file, fps, steps, where = {}) {
        this.rig = rig; this.file = file; this.fps = fps; this.where = where;
        this.stepsPerFrame = steps;
        this.n = 0;            // frames captured
        this.ff = spawn('ffmpeg', ['-y', '-loglevel', 'error', '-f', 'image2pipe', '-framerate', String(fps), '-c:v', 'mjpeg', '-i', '-',
            '-c:v', 'libx264', '-preset', 'fast', '-crf', '10', '-pix_fmt', 'yuv420p', '-r', String(fps), file],
            { stdio: ['pipe', 'inherit', 'inherit'] });
        this.done = new Promise((res, rej) => this.ff.on('exit', (c) => (c ? rej(new Error('ffmpeg exit ' + c)) : res())));
    }
    get t() { return this.n / this.fps; }

    // Advance one video frame (1 or more game frames) and capture it.
    async frame() {
        await this.rig.page.evaluate((k) => { for (let i = 0; i < k; i++) window.__vt.step(1000 / 60); }, this.stepsPerFrame);
        const { data } = await this.rig.cdp.send('Page.captureScreenshot', { format: 'jpeg', quality: 95, optimizeForSpeed: true });
        const buf = Buffer.from(data, 'base64');
        if (!this.ff.stdin.write(buf)) await new Promise((r) => this.ff.stdin.once('drain', r));
        this.n++;
    }

    // Record `seconds` of footage. cues: [[time, async fn], ...] fire once when
    // their time comes (before that frame is stepped); each(t, i) runs before
    // every frame (aim tracking, camera moves). Returns early if each() returns 'stop'.
    async play(seconds, { cues = [], each = null } = {}) {
        const pending = [...cues].sort((a, b) => a[0] - b[0]);
        const t0 = this.t;
        const frames = Math.round(seconds * this.fps);
        for (let i = 0; i < frames; i++) {
            const t = i / this.fps;
            while (pending.length && pending[0][0] <= t + 1e-6) await pending.shift()[1]();
            if (each && (await each(t, i)) === 'stop') break;
            await this.frame();
        }
        return this.t - t0;
    }

    // Press a key for `frames` game frames (while recording).
    async tap(k, frames = 3) {
        await this.rig.down(k);
        for (let i = 0; i < Math.ceil(frames / this.stepsPerFrame); i++) await this.frame();
        await this.rig.up(k);
    }

    async close() {
        await this.rig.releaseAll();
        this.ff.stdin.end();
        await this.done;
        await this.rig.manual(false);
        // Where it was shot (world / kind / system), for the captions.
        fs.writeFileSync(this.file.replace(/\.mp4$/, '.json'), JSON.stringify(
            { ...this.where, frames: this.n, fps: this.fps, speed: this.stepsPerFrame * this.fps / 60 }, null, 2));
        return this.n;
    }
}
