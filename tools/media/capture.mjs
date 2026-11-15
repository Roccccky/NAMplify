#!/usr/bin/env node
// Screenshots and animations for the README, captured from the browser demo.
//
//   node tools/media/capture.mjs          → docs/media/
//
// Everything here comes from the demo with its mock bridge, so the pictures
// show the interface, never someone else's captures. Pictures that need the
// real app (a TONE3000 search with real tones) are taken by hand and named in
// docs/media/README.md.

import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { openDemo } from '../ui/harness.mjs';
import { encodeApng } from './apng.mjs';

const ROOT = process.env.NAMPLIFY_ROOT || resolve(fileURLToPath(new URL('../..', import.meta.url)));
const OUT = join(ROOT, 'docs/media');
mkdirSync(OUT, { recursive: true });

const only = process.argv.slice(2);
const wanted = (name) => only.length === 0 || only.includes(name);

async function still (page, name, selector) {
  await page.sleep(400);
  const clip = selector ? await page.box(selector) : null;
  const png = await page.screenshot(clip && { x: clip.left, y: clip.top, width: clip.width, height: clip.height });
  writeFileSync(join(OUT, `${name}.png`), png);
  console.log(`  ${name}.png`);
}

/** Records frames of `selector` while `steps` run; `frame(delay)` adds one. */
async function animation (page, name, selector, steps) {
  const b = await page.box(selector);
  const clip = { x: Math.round(b.left), y: Math.round(b.top), width: Math.round(b.width), height: Math.round(b.height) };
  const frames = [];
  const frame = async (delayMs = 120) => frames.push({ png: await page.screenshot(clip), delayMs });
  await steps(frame);
  writeFileSync(join(OUT, `${name}.png`), encodeApng(frames));
  console.log(`  ${name}.png (${frames.length} frames)`);
}

const shots = {
  async board (page) {
    await page.demoBoard();
    const tile = await page.evaluate(`(() => {
      const t = [...document.querySelectorAll('button.tile')].filter(b => !b.classList.contains('tile-add'))[2];
      if (t) t.click();
      return !!t;
    })()`);
    if (!tile) throw new Error('the demo board stayed empty');
    await still(page, 'board');
  },

  async library (page) {
    await page.click('.browser-tab:nth-of-type(2)');
    await still(page, 'library', '#drawer');
  },

  async settings (page) {
    await page.click('#btn-settings');
    await still(page, 'settings');
    await page.key('Escape');
  },

  async browser (page) {
    // Signed in, as a player sees it after the first CONNECT (the demo
    // signs in at once and shows made-up tones).
    await page.click('.browser-tab:nth-of-type(1)');
    await page.evaluate(`[...document.querySelectorAll('#drawer button')].find(b => b.textContent.trim() === 'CONNECT')?.click()`);
    await page.sleep(2500);
    await still(page, 'browser', '#drawer');
  },

  async 'anim-slots' (page) {
    // Click through the board: each slot opens its own card below the chain.
    await page.demoBoard();
    const count = await page.evaluate(`document.querySelectorAll('button.tile:not(.tile-add)').length`);
    await animation(page, 'anim-slots', '#root', async (frame) => {
      await frame(900);
      for (let i = 0; i < count; i++) {
        await page.evaluate(`document.querySelectorAll('button.tile:not(.tile-add)')[${i}].click()`);
        await page.sleep(350);
        await frame(1200);
      }
    });
  },

  async 'anim-strobe' (page) {
    // Same tuner, strobe display: switched in the corner, then opened full size.
    await page.click('.mini-tuner-style');
    await page.click('#btn-tuner');
    await page.sleep(600);
    await animation(page, 'anim-strobe', '#overlay-tuner', async (frame) => {
      for (let i = 0; i < 60; i++) await frame(70);
    });
  },

  async 'anim-tuner' (page) {
    await page.click('#btn-tuner');
    await page.sleep(600);
    await animation(page, 'anim-tuner', '#overlay-tuner', async (frame) => {
      for (let i = 0; i < 60; i++) await frame(70);
    });
  },
};

console.log(`Capturing README media into ${OUT}`);
let failed = 0;
for (const [name, run] of Object.entries(shots)) {
  if (!wanted(name)) continue;
  // A fresh demo per picture, so one shot never depends on what the last one left open.
  const page = await openDemo({ width: 1440, height: 900 });
  try { await run(page); }
  catch (e) { failed++; console.error(`  ✗ ${name}: ${e.message}`); }
  finally { await page.close(); }
}
process.exit(failed ? 1 : 0);
