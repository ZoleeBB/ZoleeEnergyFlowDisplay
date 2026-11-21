/* A kezdőlap valódi C-ből küldött vezérlői és megerősítései Chromiumban. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {chromium} = require('playwright');

function homePages() {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'home-actions-'));
    try {
        const source = fs.readFileSync('main/app_web.c', 'utf8').split('static esp_err_t root_handler(')[1].split('\n}')[0];
        function chunk(pattern) {
            const match = source.match(pattern);
            assert(match, 'Missing home page response chunk: ' + pattern);
            return match[0];
        }
        const head = chunk(/httpd_resp_send_chunk\(\s*req,\s*"<!doctype html>[\s\S]*?HTTPD_RESP_USE_STRLEN\);/);
        const controls = chunk(/httpd_resp_send_chunk\(req, s_hu \?\s*"<p class='lead'>[\s\S]*?HTTPD_RESP_USE_STRLEN\);/);
        const script = chunk(/sendf\(req,\s*"<script>document\.documentElement\.lang=[^\n]*?\);\s*if \(httpd_resp_send_chunk\(req, app_home_actions,[\s\S]*?\n    }/);
        const main = `#include <stdio.h>
#include <string.h>
#include "app_web_style.h"
#include "app_home_actions.h"
#define req stdout
#define sendf fprintf
#define ESP_OK 0
#define ESP_FAIL 1
#define HTTPD_RESP_USE_STRLEN -1
static int httpd_resp_send_chunk(FILE *out, const char *s, int n) {
    return fwrite(s, 1, n < 0 ? strlen(s) : (size_t)n, out) == 0;
}
int main(int argc, char **argv) {
    int s_hu = argc > 1 && strcmp(argv[1], "hu") == 0;
    ${head}
    fputs(s_hu ? APP_WEB_NAV_HU : APP_WEB_NAV_EN, stdout);
    ${controls}
    ${script}
    return fputs("</body></html>", stdout) < 0;
}`;
        const file = path.join(dir, 'home.c'), binary = path.join(dir, 'home');
        fs.writeFileSync(file, main);
        const build = spawnSync(process.env.CC || 'cc', ['-Wall', '-Wextra', '-Werror', '-Imain', file, '-o', binary], {encoding:'utf8'});
        assert.equal(build.status, 0, build.stderr);
        return Object.fromEntries(['hu', 'en'].map(language => {
            const result = spawnSync(binary, [language], {encoding:'utf8'});
            assert.equal(result.status, 0, result.stderr);
            return [language, result.stdout];
        }));
    } finally {
        fs.rmSync(dir, {recursive:true, force:true});
    }
}

function luminance(rgb) {
    const channels = rgb.match(/\d+/g).map(c => Number(c) / 255)
        .map(v => v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4);
    return channels[0] * 0.2126 + channels[1] * 0.7152 + channels[2] * 0.0722;
}

async function deviceCase(context, html, {lang, width, action}) {
    const factory = action === 'factory', hu = lang === 'hu';
    const page = await context.newPage(), errors = [], posts = [];
    const errorText = 'Teszt <em>SD</em>';
    let release, signalPending, failure = 'offline', probes = 0, visits = 0;
    const pending = new Promise(resolve => { signalPending = resolve; });
    page.on('pageerror', error => errors.push(error.message));
    await page.route('http://device.test/**', async route => {
        const request = route.request(), pathname = new URL(request.url()).pathname;
        if (pathname === '/') {
            visits++;
            return route.fulfill({contentType:'text/html; charset=utf-8', body:html});
        }
        if (pathname === '/api/settings') {
            probes++;
            return route.fulfill({contentType:'application/json', body:'{}'});
        }
        if (request.method() !== 'POST') return route.abort();
        posts.push(request);
        if (failure === 'offline') return route.abort('connectionreset');
        if (failure === 'server') return route.fulfill({status:503, contentType:'text/plain', body:errorText});
        await new Promise(resolve => { release = resolve; signalPending(); });
        return route.fulfill({status:factory ? 202 : 200, contentType:'text/plain', body:'OK'});
    });
    await page.goto('http://device.test/');
    await page.clock.install();
    const opener = page.locator('[data-device-action="' + action + '"]');
    const dialog = page.getByRole('dialog'), accept = page.locator('#device-confirm');
    const cancel = page.getByRole('button', {name:hu ? 'Mégsem' : 'Cancel', exact:true});
    const status = page.locator('#device-status');
    const restart = page.locator('button.restart').first(), reset = page.locator('button.danger').first();
    const bgs = [];
    for (const button of [restart, reset]) {
        const style = await button.evaluate(el => [getComputedStyle(el).color, getComputedStyle(el).backgroundColor]);
        const [hi, lo] = style.map(luminance).sort((a, b) => b - a);
        assert((hi + 0.05) / (lo + 0.05) >= 4.5, 'action contrast');
        bgs.push(style[1]);
    }
    assert.notEqual(bgs[0], bgs[1]);
    if (width === 320) {
        const first = await restart.boundingBox(), second = await reset.boundingBox();
        assert(second.y >= first.y + first.height && Math.abs(first.width - second.width) < 1);
    }

    const shots = hu && width === 1280 && factory && process.env.HOME_ACTIONS_SCREENSHOTS_DIR;
    if (shots) {
        fs.mkdirSync(shots, {recursive:true});
        const first = await page.locator('.device-actions > :first-child').boundingBox();
        const last = await reset.boundingBox();
        const clip = {x:first.x, y:first.y, width:last.x + last.width - first.x, height:first.height};
        await page.screenshot({path:path.join(shots, 'home-actions-hu.png'), clip});
    }
    await opener.click();
    assert.equal(await dialog.isVisible(), true);
    if (shots) await dialog.screenshot({path:path.join(shots, 'factory-reset-hu.png')});
    assert.equal(await cancel.evaluate(el => el === document.activeElement), true);
    assert.match(await dialog.innerText(), factory ? /Wi-Fi/ : hu ? /beállítások megmaradnak/ : /settings will be kept/);
    if (factory) {
        assert.match(await dialog.innerText(), /SD/);
        assert.match(await accept.textContent(), hu ? /Adatok törlése/ : /Erase data/);
    }
    for (const element of [dialog, cancel, accept]) {
        const box = await element.boundingBox();
        assert(box.x >= 0 && box.x + box.width <= width + 1);
    }
    await cancel.click();
    assert.equal(await dialog.isVisible(), false);
    assert.equal(await opener.evaluate(el => el === document.activeElement), true);
    await opener.click();
    await page.keyboard.press('Escape');
    assert.equal(await dialog.isVisible(), false);
    assert.equal(posts.length, 0);

    await opener.click();
    await accept.click();
    await page.waitForFunction(() => !document.getElementById('device-confirm').disabled);
    assert.match(await status.textContent(), hu ? /Nem érkezett válasz/ : /No response received/);
    assert.equal(posts.length, 1);
    failure = 'server';
    await accept.click();
    await page.waitForFunction(() => document.getElementById('device-status').textContent.includes('503'));
    assert((await status.textContent()).includes(errorText));
    assert.equal(await status.locator('em').count(), 0);
    assert.equal(probes, 0);
    await cancel.click();
    await opener.click();
    assert.equal(await status.textContent(), '');

    failure = '';
    await accept.evaluate(button => { button.click(); button.click(); });
    await pending;
    assert.equal(await accept.isDisabled(), true);
    await page.keyboard.press('Escape');
    assert.equal(await dialog.isVisible(), true);
    assert.equal(await cancel.isDisabled(), true);
    release();
    await page.locator('#device-buttons').waitFor({state:'hidden'});
    assert.equal(posts.length, 3);
    for (const request of posts) {
        assert.equal(new URL(request.url()).pathname, factory ? '/api/factory-reset' : '/reboot');
        if (factory) {
            assert.deepEqual(request.postDataJSON(), {confirm:true});
            assert.equal(request.headers()['content-type'], 'application/json');
            assert.equal(request.headers()['x-zolee-settings'], '1');
        } else assert.equal(request.postData(), null);
    }
    if (factory) {
        assert.match(await dialog.innerText(), hu ? /kijelzőn.*webcímet/ : /on the display.*web address/);
        await page.clock.fastForward(60000);
        assert.equal(probes, 0);
        assert.equal(visits, 1);
    } else {
        assert.equal(await page.locator('#device-countdown').isVisible(), true);
        await Promise.all([page.waitForEvent('domcontentloaded'), page.clock.fastForward(20000)]);
        assert.equal(probes, 1);
        assert.equal(visits, 2);
    }
    assert.equal(posts.length, 3);
    assert.deepEqual(errors, []);
    await page.close();
}

(async () => {
    const html = homePages();
    const browser = await chromium.launch({headless:true, executablePath:process.env.CHROMIUM_EXECUTABLE || undefined});
    try {
        for (const lang of ['hu', 'en']) for (const [width, colorScheme] of [[320, 'dark'], [1280, 'light']]) {
            const context = await browser.newContext({viewport:{width, height:width === 320 ? 568 : 900}, colorScheme});
            for (const action of ['restart', 'factory']) await deviceCase(context, html[lang], {lang, width, action});
            await context.close();
        }
    } finally {
        await browser.close();
    }
    console.log('PASS: HU/EN home page device actions, contrast and mobile layout, cancel, errors and retry, duplicate suppression and distinct reconnect behavior');
})().catch(error => { console.error(error); process.exitCode = 1; });
