/* A C-be ágyazott, tényleges oldal mentése HTTP-s (nem biztonságos) eredetről. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {chromium} = require('playwright');
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'intercom-web-'));
let html;
try {
    const source = path.join(directory, 'page.c'), binary = path.join(directory, 'page');
    fs.writeFileSync(source, '#include <stdio.h>\n#include "app_loxone_page.h"\nint main(void) { return fputs(app_loxone_page, stdout) < 0; }\n');
    const compiled = spawnSync(process.env.CC || 'cc', ['-Imain', source, '-o', binary], {encoding:'utf8'});
    assert.equal(compiled.status, 0, compiled.stderr);
    const output = spawnSync(binary, [], {encoding:'utf8'});
    assert.equal(output.status, 0, output.stderr);
    html = output.stdout;
} finally {
    fs.rmSync(directory, {recursive:true, force:true});
}
const bell = '11223344-5566-7788-99aabbccddeeff00';
const gate = 'aabbccdd-5566-7788-99aabbccddeeff00';
const catalog = [
    {uuid:bell, control_uuid:gate, name:'Teszt kapcsoló', room:'Interface', category:'Kert', state:'value', type:'Switch', secured:false},
    {uuid:gate, control_uuid:gate, name:'Teszt gomb', room:'Interface', category:'Kert', state:'value', type:'Pushbutton', secured:false},
];
(async () => {
    const browser = await chromium.launch({headless:true});
    try {
        for (const width of [390, 1280]) {
            const context = await browser.newContext({viewport:{width, height:900}});
            const posts = [], errors = [];
            let home = {schema:2, revision:1, busy:false, consumers:[], pages:[], resources_installed:true,
                intercom:{enabled:false, stream_url:'', bell_uuid:'', gate:null}};
            await context.route('http://loxone-device.test/**', async route => {
                const url = new URL(route.request().url());
                let body;
                if (url.pathname === '/') {
                    return route.fulfill({contentType:'text/html; charset=utf-8', body:html});
                }
                if (url.pathname === '/api/loxone/home') {
                    if (route.request().method() === 'POST') {
                        const value = route.request().postDataJSON();
                        assert.equal(route.request().headers()['x-zolee-loxone'], '1');
                        posts.push(value);
                        home = {...home, ...structuredClone(value), revision:home.revision + 1, status:'Elmentve.'};
                        home.intercom.has_video_password = true;
                        delete home.intercom.video_password;
                        body = {accepted:true};
                    } else body = home;
                } else if (url.pathname === '/api/loxone/intercom/speaker-test') {
                    const value = route.request().postDataJSON();
                    assert.equal(value.volume, value.enabled ? 72 : 0);
                    home.speaker_test_active = value.enabled;
                    home.audio_busy = value.enabled;
                    body = {accepted:true};
                } else if (url.pathname === '/api/loxone/slaves') {
                    body = {ready:true, profiles:[], revision:1, busy:false};
                } else if (url.pathname === '/api/loxone') {
                    body = {host:'192.168.2.10', port:80, username:'kijelzo', busy:false, catalog, mappings:{}};
                } else {
                    return route.fulfill({status:404, body:'{}'});
                }
                return route.fulfill({contentType:'application/json', body:JSON.stringify(body)});
            });
            const page = await context.newPage();
            page.on('pageerror', error => errors.push(error.message));
            await page.goto('http://loxone-device.test/');
            assert.equal(await page.evaluate(() => isSecureContext), false);
            await page.locator('#intercom-enabled').waitFor();
            await page.locator('#intercom-speaker-volume').fill('72');
            await page.locator('#intercom-microphone-gain').fill('90');
            await page.locator('#intercom-speaker-test').click();
            await page.waitForFunction(() => document.querySelector('#intercom-speaker-test').textContent.includes('leállítása'));
            await page.locator('#intercom-speaker-test').click();
            await page.waitForFunction(() => document.querySelector('#intercom-speaker-test').textContent.includes('indítása'));
            await page.locator('#intercom-enabled').check();
            await page.locator('#intercom-stream-url').fill(' \u00a0http://192.168.2.21/mjpg/video.mjpg\u00a0 ');
            await page.locator('#intercom-video-username').fill('admin');
            await page.locator('#intercom-video-password').fill('Separate-camera-secret');
            await page.locator('#intercom-bell').selectOption(bell);
            await page.locator('#intercom-gate').selectOption(gate);
            await page.locator('#intercom-save').click();
            await page.waitForFunction(() => document.querySelector('#intercom-status').textContent.includes('Elmentve'));
            assert.equal(posts.length, 1);
            assert.equal(posts[0].intercom.video_username, 'admin');
            assert.equal(posts[0].intercom.video_password, 'Separate-camera-secret');
            assert.equal(await page.locator('#intercom-video-password').inputValue(), '');
            assert.equal(posts[0].intercom.stream_url, 'http://192.168.2.21/mjpg/video.mjpg');
            assert.equal(posts[0].intercom.bell_uuid, bell);
            assert.equal(posts[0].intercom.speaker_volume, 72);
            assert.equal(posts[0].intercom.microphone_gain, 90);
            assert.equal(posts[0].intercom.gate.control_uuid, gate);
            assert.equal(await page.locator('#intercom-stream-url').inputValue(), posts[0].intercom.stream_url);
            await page.locator('#intercom-stream-url').fill('http://192.168.2.21/mjpg/video.mjpg');
            await page.locator('#intercom-save').click();
            await page.waitForFunction(() => !document.querySelector('#intercom-save').disabled);
            assert.equal(posts.length, 2);
            assert.equal(posts[1].intercom.video_password, '');
            for (const url of ['https://192.168.2.21/mjpg/video.mjpg', 'http://user:password@192.168.2.21/mjpg/video.mjpg',
                               'http://192.168.2.21/a b', 'http://192.168.2.21/']) {
                await page.locator('#intercom-stream-url').fill(url);
                await page.locator('#intercom-save').click();
                assert.equal(posts.length, 2, url);
                assert.equal(await page.locator('#intercom-status').getAttribute('class'), 'feedback-error');
            }
            assert.deepEqual(errors, []);
            await context.close();
        }
    } finally {
        await browser.close();
    }
    console.log('PASS: actual C-embedded page, insecure HTTP, desktop/mobile Intercom save, exact user URL with clipboard whitespace, gate and rejected unsafe URLs');
})().catch(error => {console.error(error); process.exitCode = 1;});
