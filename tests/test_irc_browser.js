/* A tényleges beágyazott oldal IRC-szerkesztése asztali és mobil Chromiumban. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {chromium} = require('playwright');
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'irc-web-'));
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
const irc = '20000000-0000-0000-0000000000000001';
const sensor = '20000000-0000-0000-0000000000000002';
const switchId = '30000000-0000-0000-0000000000000001';
const catalog = [
    {uuid:sensor, control_uuid:irc, name:'Nappali IRC', room:'Nappali', category:'Klíma', state:'tempTarget',
        type:'IRoomControllerV2', secured:false, irc:'{}', irc_blocked:false},
    {uuid:sensor, control_uuid:irc, name:'Nappali IRC', room:'Nappali', category:'Klíma', state:'tempActual',
        type:'IRoomControllerV2', secured:false},
    {uuid:sensor, control_uuid:switchId, name:'Kapcsoló', room:'Nappali', category:'Világítás', state:'active',
        type:'Switch', secured:false},
];
const iconPaths = new Map(JSON.parse(fs.readFileSync('assets/loxone-home/catalog.json')).icons.map(icon => [icon.id, 'assets/loxone-home/' + icon.file]));
(async () => {
    const browser = await chromium.launch({headless:true,
        ...(process.env.CHROMIUM_EXECUTABLE ? {executablePath:process.env.CHROMIUM_EXECUTABLE} : {})});
    try {
        for (const width of [390, 1280]) {
            const context = await browser.newContext({viewport:{width, height:900}});
            const posts = [], errors = [], images = [];
            let home = {schema:2, revision:1, busy:false, consumers:[], resources_installed:false, intercom:{}, pages:[
                {id:'floor', title:'Földszint', cards:[{id:'thermostat', name:'Nappali termosztát', type:'irc', icon:'lamp',
                    control_uuid:irc, source:{uuid:'invalid', scale:0, offset:null}, unit:'bad', decimals:99}]},
                {id:'upper', title:'Emelet', cards:[{id:'hidden', name:'Háló termosztát', type:'irc', control_uuid:irc}]}]};
            await context.route('http://loxone-device.test/**', async route => {
                const url = new URL(route.request().url());
                let body;
                if (url.pathname === '/') return route.fulfill({contentType:'text/html; charset=utf-8', body:html});
                if (url.pathname === '/api/loxone/home') {
                    if (route.request().method() === 'POST') {
                        const value = route.request().postDataJSON();
                        assert.equal(route.request().headers()['x-zolee-loxone'], '1');
                        posts.push(value);
                        home = {...home, ...structuredClone(value), revision:home.revision + 1, status:'Elmentve.'};
                        body = {accepted:true};
                    } else body = home;
                } else if (url.pathname === '/api/loxone/slaves') {
                    body = {ready:true, profiles:[], revision:1, busy:false};
                } else if (url.pathname === '/api/loxone') {
                    body = {host:'192.168.2.10', port:80, username:'kijelzo', busy:false, catalog, mappings:{}};
                } else if (url.pathname === '/api/templates/home-icon') {
                    images.push(url.searchParams.get('id'));
                    const id = url.searchParams.get('id');
                    assert(iconPaths.has(id), id);
                    return route.fulfill({contentType:'image/png', body:fs.readFileSync(iconPaths.get(id))});
                } else return route.fulfill({status:404, body:'{}'});
                return route.fulfill({contentType:'application/json', body:JSON.stringify(body)});
            });
            const page = await context.newPage();
            page.on('pageerror', error => errors.push(error.message));
            await page.goto('http://loxone-device.test/');
            const prefix = '#home-cards-thermostat-';
            await page.locator(prefix + 'control_uuid').waitFor();
            assert.equal(await page.locator(prefix + 'icon-preview').count(), 0);
            assert.equal(await page.locator('#home-resource-warning').isVisible(), true);
            assert.equal(await page.locator('#home-save').isDisabled(), false);
            assert.equal(images.length, 0);
            home.resources_installed = true;home.resources_version = 3;
            await page.reload();
            await page.locator(prefix + 'control_uuid').waitFor();
            assert.equal(await page.locator(prefix + 'icon-preview').isVisible(), false);
            assert.match(await page.locator('#home-cards').textContent(), /friss loxone-home.zip/);
            assert.equal(images.length, 0);
            home.resources_version = 4;
            await page.reload();
            await page.locator(prefix + 'control_uuid').waitFor();
            await page.locator(prefix + 'icon-preview').scrollIntoViewIfNeeded();
            await page.waitForFunction(() => {
                const preview = document.querySelector('#home-cards-thermostat-icon-preview');
                return preview?.complete && preview.naturalWidth === 96 && preview.naturalHeight === 96;
            });
            assert.equal(await page.locator('#home-resource-warning').isVisible(), false);
            assert.equal(await page.locator(prefix + 'icon-preview').isVisible(), true);
            assert.equal(await page.locator(prefix + 'type').inputValue(), 'irc');
            assert.deepEqual(await page.locator(prefix + 'control_uuid option').evaluateAll(options => options.map(item => item.value)), ['', irc]);
            for (const field of ['icon', 'uuid', 'scale', 'offset', 'unit', 'decimals', 'warning_low', 'warning_high', 'warning_color'])
                assert.equal(await page.locator(prefix + field).count(), 0, field);
            const bounds = await page.locator('#home-cards fieldset').boundingBox();
            assert(bounds.width > 0 && bounds.x >= 0 && bounds.x + bounds.width <= width);
            assert.match(await page.locator('#home-cards').textContent(), /0,5 °C/);
            if (process.env.ZOLEE_IRC_WEB_PREVIEWS) {
                fs.mkdirSync(process.env.ZOLEE_IRC_WEB_PREVIEWS, {recursive:true});
                await page.locator('#home-cards fieldset').screenshot({path:path.join(process.env.ZOLEE_IRC_WEB_PREVIEWS, `irc-editor-${width}.png`)});
            }
            await page.locator('#home-save').click();
            await page.waitForFunction(() => document.querySelector('#home-status').textContent.includes('Elmentve'));
            assert.equal(posts.length, 1);
            for (const card of posts[0].pages.flatMap(item => item.cards)) {
                assert.equal(card.icon, 'thermostat-irc');
                assert.deepEqual(Object.keys(card).sort(), ['control_uuid', 'icon', 'id', 'name', 'type']);
            }
            await page.locator(prefix + 'type').selectOption('value');
            assert.equal(await page.locator(prefix + 'icon').inputValue(), 'lamp');
            assert.equal(await page.locator(prefix + 'uuid').count(), 1);
            await page.locator(prefix + 'icon').selectOption('washing-machine');
            await page.locator(prefix + 'type').selectOption('switch');
            await page.locator(prefix + 'control_uuid').selectOption(switchId);
            await page.locator(prefix + 'type').selectOption('irc');
            assert.equal(await page.locator(prefix + 'control_uuid').inputValue(), '');
            assert.equal(await page.locator(prefix + 'icon').count(), 0);
            await page.locator('#home-save').click();
            assert.equal(posts.length, 1);
            assert.equal(await page.locator('#home-status').getAttribute('class'), 'feedback-error');
            await page.locator(prefix + 'control_uuid').selectOption(irc);
            await page.locator('#home-save').click();
            await page.waitForFunction(() => document.querySelector('#home-status').textContent.includes('Elmentve'));
            assert.equal(posts.length, 2);
            assert.equal(posts[1].pages[0].cards[0].icon, 'thermostat-irc');
            assert(images.length > 0);
            assert.deepEqual(errors, []);
            await context.close();
        }
    } finally {
        await browser.close();
    }
    console.log('PASS: actual C-embedded IRC editor, desktop/mobile, real fixed PNG from v4 SD, missing/old pack fallback, imported/hidden-page save, type changes and incompatible-target rejection');
})().catch(error => {console.error(error); process.exitCode = 1;});
