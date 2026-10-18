#!/usr/bin/env node
// Optional browser acceptance test. Start tools/demo.py first; pass its URL here.
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const search = [path.join(root, '.tools/browser'), process.env.CODEX_PRIMARY_RUNTIME_NODE_MODULES].filter(Boolean);
let playwright;
try { playwright = require(require.resolve('playwright', {paths: search})); }
catch { playwright = require(require.resolve('playwright-core', {paths: search})); }
(async () => {
  const launch = {headless: true};
  // Optional portable Chromium for hosts without a system browser.
  if (process.env.CAN_PORTABLE_CHROMIUM === '1') {
    const binary = require(require.resolve('@sparticuz/chromium', {paths: search}));
    launch.executablePath = await binary.executablePath();
    launch.args = binary.args;
  }
  const browser = await playwright.chromium.launch(launch);
  try {
    const page = await browser.newPage({viewport: {width: 1440, height: 1100}});
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(process.argv[2] || 'http://127.0.0.1:8765');
    await page.locator('#messages').getByText('No cyclic messages configured.', {exact: false}).waitFor();
    await page.locator('#start').click();
    await page.locator('#last-reply').filter({hasText: 'ERR no cyclic messages'}).waitFor();
    await page.getByRole('button', {name: 'Add raw test message', exact: true}).click();
    await page.locator('#messages').getByText('0x123', {exact: true}).waitFor();
    await page.locator('#start').click();
    await page.waitForFunction(() => Number(document.getElementById('sent').textContent) > 0);
    await page.locator('#command').fill('SET 0x123 0 127');
    await page.locator('#command-form button').click();
    await page.locator('#messages').getByText('7F', {exact: true}).waitFor();
    await page.locator('#ack').uncheck();
    await page.locator('#fault-box').waitFor({state: 'visible'});
    await page.locator('#ack').check();
    await page.locator('#recover').click();
    await page.locator('#fault-box').waitFor({state: 'hidden'});
    await page.locator('#synthetic-profile').click();
    await page.locator('#signals').waitFor({state: 'visible'});
    await page.locator('#start').click();
    await page.waitForFunction(() => Number(document.getElementById('rpm-value').textContent.replaceAll(',', '')) > 0);
    await page.locator('#vehicle-profile').click();
    await page.locator('#messages').getByText('No cyclic messages configured.', {exact: false}).waitFor();
    await page.locator('#loop').uncheck();
    await page.locator('#replay').click();
    await page.locator('#progress-label').filter({hasText: '49 / 49 frames · done'}).waitFor();
    assert.match(await page.locator('#log').innerText(), /0x301/);
    // Raw-message setup must return to MANUAL after replay has completed.
    await page.locator('#add-raw').click();
    await page.waitForFunction(() => document.getElementById('mode').value === 'MANUAL');
    const sentBeforeRaw = await page.locator('#sent').innerText();
    await page.locator('#start').click();
    await page.waitForFunction(before => Number(document.getElementById('sent').textContent) > Number(before), sentBeforeRaw);
    await page.locator('#stop').click();
    await page.screenshot({path: path.join(root, 'build/demo-desktop.png'), fullPage: true});
    await page.setViewportSize({width: 390, height: 844});
    await page.screenshot({path: path.join(root, 'build/demo-mobile.png'), fullPage: true});
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false, 'mobile layout overflows horizontally');
    assert.deepEqual(errors, [], 'browser JavaScript errors');
    console.log('PASS: browser empty state, raw editing/TX, ACK fault/recovery, AUTO, replay and mobile layout');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
