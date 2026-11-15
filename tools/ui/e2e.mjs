#!/usr/bin/env node
// Browser journeys through the demo: node tools/ui/e2e.mjs
//
// The demo runs the real UI against a mock bridge (ui/mock.js), so these
// tests check the interface, not the sound. The sound is covered by the C++
// tests in tests/.

import { openDemo } from './harness.mjs';

const results = [];
const page = await openDemo();

async function check (name, fn) {
  try { await fn(); results.push([true, name]); }
  catch (e) { results.push([false, `${name}: ${e.message}`]); }
}
const assert = (ok, message) => { if (!ok) throw new Error(message); };
const visible = (selector) => page.evaluate(`(() => {
  const n = document.querySelector(${JSON.stringify(selector)});
  return !!n && !n.hidden && n.getClientRects().length > 0;
})()`);

try {
  await check('the board renders', async () => {
    for (const id of ['#chain-panel', '#presets-panel', '#drawer', '#foot-panel'])
      assert(await visible(id), `${id} is not visible`);
  });

  await check('the mini tuner sits on the board', async () => {
    assert(await visible('#mini-tuner-card'), 'mini tuner missing');
  });

  await check('the browser has two index cards, TONE3000 first', async () => {
    const tabs = await page.evaluate(`[...document.querySelectorAll('.browser-tab')].map(t => t.textContent.trim())`);
    assert(tabs.length === 2, `expected 2 tabs, got ${tabs.length}`);
    assert(/LIBRARY/.test(tabs[1]), 'second tab should be LIBRARY');
  });

  await check('switching to LIBRARY changes the drawer mode', async () => {
    await page.click('.browser-tab:nth-of-type(2)');
    const mode = await page.evaluate(`document.querySelector('[data-mode]')?.dataset.mode`);
    assert(mode === 'library', `mode is ${mode}`);
    await page.click('.browser-tab:nth-of-type(1)');
  });

  await check('settings open and close', async () => {
    await page.click('#btn-settings');
    assert(await visible('#overlay-settings'), 'settings did not open');
    await page.key('Escape');
    await page.sleep(300);
    assert(!(await visible('#overlay-settings')), 'settings did not close');
  });

  await check('the big tuner opens', async () => {
    await page.click('#btn-tuner');
    await page.sleep(400);
    assert(await visible('#overlay-tuner'), 'tuner did not open');
    await page.click('#btn-tuner');
  });

  await check('TOOLS is marked coming soon and stays shut', async () => {
    const tab = await page.evaluate(`(() => {
      const b = document.querySelector('#rooms button[data-room="tools"]');
      return b && { disabled: b.disabled, soon: /SOON/.test(b.textContent) };
    })()`);
    assert(tab?.disabled && tab.soon, 'TOOLS tab should be disabled and say SOON');
    await page.evaluate(`document.querySelector('#rooms button[data-room="tools"]').click()`);
    assert(await visible('#chain-panel'), 'the board should stay in front');
    assert(!(await visible('#tools-room')), 'the tools room opened');
  });

  await check('a preview carries PREVIEW on its tile and its model row', async () => {
    await page.evaluate(`[...document.querySelectorAll('#drawer button')].find(b => b.textContent.trim() === 'CONNECT')?.click()`);
    await page.sleep(2500);
    await page.evaluate(`document.querySelector('#drawer .t3k-row')?.click()`);
    await page.sleep(3500);
    assert(await visible('.tile-preview'), 'the tile is not marked PREVIEW');
    const tag = await page.evaluate(`document.querySelector('.nam-row[data-kind="bank"] .nam-row-tag')?.textContent`);
    assert(tag === 'PREVIEW', `model row tag is ${tag}`);
  });

  await check('STORE keeps the preview and clears the marks', async () => {
    await page.evaluate(`document.querySelector('.nam-row-action')?.click()`);
    await page.sleep(1200);
    assert(!(await visible('.nam-row-tag')), 'the model row still says PREVIEW');
    assert(!(await visible('.tile-preview')), 'the tile still says PREVIEW');
  });

  await check('SAVE suggests a name from what is on the board', async () => {
    await page.evaluate(`[...document.querySelectorAll('.header-rig-buttons .hs-btn')].find(b => /SAVE/.test(b.textContent)).click()`);
    await page.sleep(300);
    const name = await page.evaluate(`document.querySelector('.dialog-input')?.value`);
    assert(/Blackface/.test(name || ''), `suggested "${name}"`);
    assert(await visible('.dialog-suggest'), 'no wand in the name field');
    await page.key('Escape');
    await page.sleep(200);
  });

  await check('saving a preset stores what is still a preview', async () => {
    await page.evaluate(`document.querySelectorAll('#drawer .t3k-row')[1]?.click()`);
    await page.sleep(3500);
    assert(await visible('.tile-preview'), 'nothing to save as a preview');
    await page.evaluate(`[...document.querySelectorAll('.header-rig-buttons .hs-btn')].find(b => /SAVE/.test(b.textContent)).click()`);
    await page.sleep(300);
    await page.key('Enter');
    await page.sleep(800);
    assert(!(await visible('.tile-preview')), 'a tile still says PREVIEW after saving');
  });

  await check('no script errors', async () => {
    assert(page.errors.length === 0, page.errors.join(' | '));
  });
} finally {
  await page.close();
}

for (const [ok, line] of results) console.log(`${ok ? '✓' : '✗'} ${line}`);
const failed = results.filter(([ok]) => !ok).length;
console.log(`\n${results.length - failed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);
