// Renders a montage's text overlays to transparent 1920x1080 PNGs: the title
// and end cards, and one caption per segment (a short line, with where it was
// shot above it, taken from the shot's .json written by the recorder).
//
//   node cards.mjs 2026-11-17        -> 2026-11-17/build/cards/*.png
//
// Styled after the game's own UI: system sans for text, mono for labels, the
// mint accent (#8fffe0) of the HUD and inventory.

import { chromium } from 'playwright';
import fs from 'node:fs';
import path from 'node:path';

const dir = path.resolve(process.argv[2] || '.');
const spec = JSON.parse(fs.readFileSync(path.join(dir, 'montage.json'), 'utf8'));
const outDir = path.join(dir, 'build', 'cards');
fs.mkdirSync(outDir, { recursive: true });

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const MONO = `ui-monospace, 'SF Mono', 'SFMono-Regular', Menlo, monospace`;
const SANS = `-apple-system, 'SF Pro Display', system-ui, 'Helvetica Neue', sans-serif`;

const BASE = `
  html, body { margin: 0; width: 1920px; height: 1080px; background: transparent; overflow: hidden; }
  * { box-sizing: border-box; }
  .scrim { position: absolute; left: 0; bottom: 0; width: 1250px; height: 460px;
           background: radial-gradient(ellipse 100% 100% at 0% 100%, rgba(4,8,14,.62) 0%, rgba(4,8,14,.30) 42%, rgba(4,8,14,0) 72%); }
  .cap { position: absolute; left: 108px; bottom: 104px; color: #fff; }
  .bar { width: 46px; height: 3px; border-radius: 2px; background: #8fffe0; margin-bottom: 20px;
         box-shadow: 0 0 14px rgba(143,255,224,.55); }
  .kick { font: 600 19px/1 ${MONO}; letter-spacing: .32em; text-transform: uppercase; color: #8fffe0;
          margin-bottom: 16px; text-shadow: 0 1px 10px rgba(0,0,0,.7); }
  .line { font: 650 60px/1.05 ${SANS}; letter-spacing: -.012em;
          text-shadow: 0 2px 22px rgba(0,0,0,.5), 0 1px 3px rgba(0,0,0,.55); }
  .title { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center;
           justify-content: center; color: #fff; }
  .big { font: 800 172px/1 ${SANS}; letter-spacing: .34em; padding-left: .34em;
         text-shadow: 0 0 48px rgba(200,150,255,.38), 0 6px 34px rgba(0,0,0,.55); }
  .rule { width: 160px; height: 2px; margin: 38px 0 30px;
          background: linear-gradient(90deg, rgba(143,255,224,0), #8fffe0, rgba(143,255,224,0)); }
  .sub { font: 500 27px/1 ${MONO}; letter-spacing: .42em; padding-left: .42em; text-transform: uppercase;
         color: #8fffe0; text-shadow: 0 1px 12px rgba(0,0,0,.7); }
  .glow { position: absolute; inset: 0;
          background: radial-gradient(ellipse 52% 40% at 50% 46%, rgba(3,6,12,.55) 0%, rgba(3,6,12,0) 100%); }
`;

function titleCard(lift) {
    return `<div class="glow"></div><div class="title" style="transform: translateY(${lift}px)">
      <div class="big">${esc(spec.title)}</div><div class="rule"></div><div class="sub">${esc(spec.subtitle)}</div></div>`;
}

function kickerFor(seg) {
    if (seg.kicker && seg.kicker !== 'system') return seg.kicker;
    const metaFile = path.join(dir, 'build', 'shots', seg.shot + '.json');
    if (!fs.existsSync(metaFile)) return '';
    const m = JSON.parse(fs.readFileSync(metaFile, 'utf8'));
    if (seg.kicker === 'system') return m.system && m.system !== '—' ? `The ${m.system} system` : '';
    if (seg.kicker) return seg.kicker;   // given outright (the HUD can lag a warp)
    if (!m.world || m.world === '—') return '';
    return `${m.world} · ${m.kind}`;
}

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
const render = async (body, file) => {
    await page.setContent(`<!doctype html><html><head><meta charset="utf-8"><style>${BASE}</style></head><body>${body}</body></html>`);
    await page.evaluate(() => document.fonts.ready);
    await page.screenshot({ path: path.join(outDir, file), omitBackground: true });
};

const made = [];
for (const [i, seg] of spec.segments.entries()) {
    const n = String(i).padStart(2, '0');
    if (seg.caption) {
        const k = kickerFor(seg);
        await render(`<div class="scrim"></div><div class="cap"><div class="bar"></div>` +
            (k ? `<div class="kick">${esc(k)}</div>` : '') + `<div class="line">${esc(seg.caption)}</div></div>`,
            `${n}-caption.png`);
        made.push(`${n}-caption.png  ${k ? k + ' / ' : ''}${seg.caption}`);
    }
    if (seg.card) {
        await render(titleCard(seg.card === 'title' ? -60 : -150), `${n}-${seg.card}.png`);
        made.push(`${n}-${seg.card}.png`);
    }
}
await browser.close();
console.log(made.join('\n'));
