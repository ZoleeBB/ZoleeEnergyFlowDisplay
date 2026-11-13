/* A tényleges beágyazott oldal redőnyszerkesztése asztali és mobil Chromiumban. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {chromium} = require('playwright');
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'shading-web-'));
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
const blind = '40000000-0000-0000-0000000000000001';
const central = '40000000-0000-0000-0000000000000002';
const sensor = '40000000-0000-0000-0000000000000003';
const blocked = '40000000-0000-0000-0000000000000004';
const irc = '20000000-0000-0000-0000000000000001';
const switchId = '30000000-0000-0000-0000000000000001';
const entry = {uuid:sensor, control_uuid:blind, name:'Nappali redőny', room:'Nappali', category:'Árnyékolás',
    state:'position', type:'Jalousie', secured:false, shading:'{}', shading_blocked:false, shading_central:false};
const catalog = [
    entry,
    {...entry, state:'up'},
    {...entry, control_uuid:central, name:'Emeleti központi árnyékolás', room:'Emelet', type:'CentralJalousie', shading_central:true},
    {...entry, control_uuid:blocked, name:'Védett redőny', secured:true, shading_blocked:true},
    {...entry, control_uuid:'50000000-0000-0000-0000000000000001', name:'Kézi redőny', shading:undefined},
    {...entry, control_uuid:'50000000-0000-0000-0000000000000002', shading:''},
    {...entry, control_uuid:'50000000-0000-0000-0000000000000003', shading:{}},
    {...entry, control_uuid:'50000000-0000-0000-0000000000000004', action_uuid:''},
    {...entry, control_uuid:'50000000-0000-0000-0000000000000005', type:'Switch'},
    {uuid:sensor, control_uuid:irc, name:'Nappali IRC', room:'Nappali', category:'Klíma', state:'tempTarget',
        type:'IRoomControllerV2', secured:false, irc:'{}', irc_blocked:false},
    {uuid:sensor, control_uuid:switchId, name:'Kapcsoló', room:'Nappali', category:'Világítás', state:'active',
        type:'Switch', secured:false},
];
const imagesById = new Map([
    ['blinds-shading', fs.readFileSync('main/assets/png/shading_blinds.png')],
    ['thermostat-irc', fs.readFileSync('main/assets/png/irc_thermostat.png')],
]);
(async () => {
    const browser = await chromium.launch({headless:true,
        ...(process.env.CHROMIUM_EXECUTABLE ? {executablePath:process.env.CHROMIUM_EXECUTABLE} : {})});
    try {
        for (const width of [390, 1280]) {
            const context = await browser.newContext({viewport:{width, height:900}});
            const posts = [], errors = [], images = [];
            let home = {schema:2, revision:1, busy:false, consumers:[], resources_installed:false, intercom:{}, pages:[
                {id:'floor', title:'Földszint', cards:[{id:'shutter', name:'Nappali redőny', type:'shading', icon:'lamp',
                    control_uuid:blind, source:{uuid:'invalid', scale:0, offset:null}, unit:'bad', decimals:99,
                    warning_low:'bad', warning_high:{}, warning_color:'bad'}]},
                {id:'upper', title:'Emelet', cards:[
                    {id:'central', name:'Központi árnyékolás', type:'shading', control_uuid:central},
                    {id:'imported', name:'Importált redőny', type:'shading', icon:'<img onerror=alert(1)>',
                        control_uuid:blind, source:{uuid:'invalid', scale:0}}]}]};
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
                    const id = url.searchParams.get('id');
                    images.push(id);
                    assert(imagesById.has(id), id);
                    return route.fulfill({contentType:'image/png', body:imagesById.get(id)});
                } else return route.fulfill({status:404, body:'{}'});
                return route.fulfill({contentType:'application/json', body:JSON.stringify(body)});
            });
            const page = await context.newPage();
            page.on('pageerror', error => errors.push(error.message));
            await page.goto('http://loxone-device.test/');
            const prefix = '#home-cards-shutter-';
            await page.locator(prefix + 'control_uuid').waitFor();
            await page.locator(prefix + 'icon-preview').scrollIntoViewIfNeeded();
            await page.waitForFunction(() => {
                const preview = document.querySelector('#home-cards-shutter-icon-preview');
                return preview?.complete && preview.naturalWidth === 96 && preview.naturalHeight === 96;
            });
            assert.equal(await page.locator('#home-resource-warning').isVisible(), true);
            assert.equal(await page.locator(prefix + 'icon-preview').isVisible(), true);
            assert.equal(await page.locator(prefix + 'icon-preview').getAttribute('alt'), 'Redőnyvezérlő előnézete');
            assert.equal(await page.locator(prefix + 'type').inputValue(), 'shading');
            assert.deepEqual(await page.locator(prefix + 'control_uuid option').evaluateAll(options => options.map(item => item.value)),
                ['', central, blind, blocked]);
            for (const field of ['icon', 'uuid', 'scale', 'offset', 'unit', 'decimals', 'warning_low', 'warning_high', 'warning_color'])
                assert.equal(await page.locator(prefix + field).count(), 0, field);
            const bounds = await page.locator('#home-cards fieldset').boundingBox();
            assert(bounds.width > 0 && bounds.x >= 0 && bounds.x + bounds.width <= width);
            const text = await page.locator('#home-cards').textContent();
            assert.match(text, /Fel \/ Stop \/ Le/);
            assert.match(text, /0% teljesen felhúzva, 100% teljesen leengedve/);
            assert.match(text, /központi blokkhoz nincs százalékos állítás/);
            assert.equal(await page.locator('#home-cards button').filter({hasText:/^Auto$/}).count(), 0);
            if (process.env.ZOLEE_SHADING_WEB_PREVIEWS) {
                fs.mkdirSync(process.env.ZOLEE_SHADING_WEB_PREVIEWS, {recursive:true});
                await page.locator('#home-cards fieldset').screenshot({path:path.join(process.env.ZOLEE_SHADING_WEB_PREVIEWS, `shading-editor-${width}.png`)});
            }
            await page.locator('#home-save').click();
            await page.waitForFunction(() => document.querySelector('#home-status').textContent.includes('Elmentve'));
            assert.equal(posts.length, 1);
            for (const card of posts[0].pages.flatMap(item => item.cards)) {
                assert.equal(card.type, 'shading');
                assert.equal(card.icon, 'blinds-shading');
                assert.deepEqual(Object.keys(card).sort(), ['control_uuid', 'icon', 'id', 'name', 'type']);
            }
            await page.locator('#home-room').selectOption(JSON.stringify('Emelet'));
            assert.deepEqual(await page.locator(prefix + 'control_uuid option').evaluateAll(options => options.map(item => item.value)),
                ['', central, blind]);
            assert.equal(await page.locator(prefix + 'control_uuid').inputValue(), blind);
            await page.locator(prefix + 'control_uuid').selectOption('');
            await page.locator('#home-category').selectOption(JSON.stringify('Világítás'));
            assert.deepEqual(await page.locator(prefix + 'control_uuid option').evaluateAll(options => options.map(item => item.value)), ['']);
            await page.locator('#home-room').selectOption('');
            await page.locator('#home-category').selectOption('');
            await page.locator('#home-save').click();
            assert.equal(posts.length, 1);
            assert.equal(await page.locator('#home-status').getAttribute('class'), 'feedback-error');
            await page.locator(prefix + 'control_uuid').selectOption(blind);
            await page.locator(prefix + 'type').selectOption('irc');
            assert.equal(await page.locator(prefix + 'control_uuid').inputValue(), '');
            assert.deepEqual(await page.locator(prefix + 'control_uuid option').evaluateAll(options => options.map(item => item.value)), ['', irc]);
            await page.locator(prefix + 'control_uuid').selectOption(irc);
            await page.locator(prefix + 'type').selectOption('shading');
            assert.equal(await page.locator(prefix + 'control_uuid').inputValue(), '');
            await page.locator(prefix + 'type').selectOption('value');
            assert.equal(await page.locator(prefix + 'icon').inputValue(), 'lamp');
            assert.equal(await page.locator(prefix + 'uuid').count(), 1);
            await page.locator(prefix + 'icon').selectOption('washing-machine');
            await page.locator(prefix + 'type').selectOption('switch');
            await page.locator(prefix + 'control_uuid').selectOption(switchId);
            await page.locator(prefix + 'type').selectOption('shading');
            assert.equal(await page.locator(prefix + 'control_uuid').inputValue(), '');
            assert.equal(await page.locator(prefix + 'icon').count(), 0);
            await page.locator(prefix + 'control_uuid').selectOption(central);
            await page.locator('#home-save').click();
            await page.waitForFunction(() => document.querySelector('#home-status').textContent.includes('Elmentve'));
            assert.equal(posts.length, 2);
            assert.equal(posts[1].pages[0].cards[0].control_uuid, central);
            assert.equal(posts[1].pages[0].cards[0].icon, 'blinds-shading');
            await page.locator('#home-page-select').selectOption('upper');
            assert.equal(await page.locator('#home-cards-central-icon').count(), 0);
            assert.equal(await page.locator('#home-cards-central-icon-preview').getAttribute('src'), '/api/templates/home-icon?id=blinds-shading');
            await page.locator('#home-cards-central-type').selectOption('value');
            assert.equal(await page.locator('#home-cards-central-icon').inputValue(), 'blinds');
            assert(images.includes('blinds-shading'));
            assert.deepEqual(errors, []);
            await context.close();
        }
    } finally {
        await browser.close();
    }
    console.log('PASS: actual C-embedded shading editor, desktop/mobile, supported-block filter, fixed PNG without SD, imported/hidden-page save and IRC/generic type changes');
})().catch(error => {console.error(error); process.exitCode = 1;});
