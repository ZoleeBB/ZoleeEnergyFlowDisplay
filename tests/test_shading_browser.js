/* A tényleges beágyazott oldal redőny- és típusos blokk-szerkesztése Chromiumban. */
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
const advancedTypes = [
    ['dimmer', 'Dimmer', 'dimmer'],
    ['central-lighting', 'CentralLightController', 'central-lighting'],
    ['slider', 'Slider', 'slider'],
    ['timed-switch', 'TimedSwitch', 'timed-switch'],
    ['window-monitor', 'WindowMonitor', 'window-monitor'],
    ['gate', 'Gate', 'gate-double'],
    ['window', 'Window', 'motor-window'],
    ['audio', 'AudioZoneV2', 'audio-player'],
];
const advancedId = index => '60000000-0000-0000-' + String(index + 1).padStart(16, '0');
catalog.push(...advancedTypes.map(([controls_type, type], index) => ({
    uuid:advancedId(index + 100), control_uuid:advancedId(index),
    action_uuid:controls_type === 'window-monitor' ? '' : advancedId(index + 50),
    name:'Blokk ' + controls_type, room:'Nappali', category:'Új blokkok', type,
    controls_type, controls:'{}', controls_blocked:false,
    gate_animation:controls_type === 'gate' ? 3 : undefined,
})));
const iconPaths = new Map(JSON.parse(fs.readFileSync('assets/loxone-home/catalog.json')).icons.map(icon => [icon.id, 'assets/loxone-home/' + icon.file]));
(async () => {
    const browser = await chromium.launch({headless:true,
        ...(process.env.CHROMIUM_EXECUTABLE ? {executablePath:process.env.CHROMIUM_EXECUTABLE} : {})});
    try {
        for (const width of [390, 1280]) {
            const context = await browser.newContext({viewport:{width, height:900}});
            const posts = [], errors = [], images = [];
            let home = {schema:2, revision:1, busy:false, consumers:[], resources_installed:true, resources_version:4, intercom:{}, pages:[
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
                    assert(iconPaths.has(id), id);
                    return route.fulfill({contentType:'image/png', body:fs.readFileSync(iconPaths.get(id))});
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
            assert.equal(await page.locator('#home-resource-warning').isVisible(), false);
            assert.equal(await page.locator(prefix + 'icon-preview').isVisible(), true);
            assert.equal(await page.locator(prefix + 'icon-preview').getAttribute('alt'), 'Redőnyvezérlő előnézete');
            assert.equal(await page.locator(prefix + 'type').inputValue(), 'shading');
            assert.deepEqual(await page.locator(prefix + 'control_uuid option').evaluateAll(options => options.map(item => item.value)),
                ['', central, blind, blocked]);
            for (const field of ['icon', 'uuid', 'scale', 'offset', 'unit', 'decimals', 'warning_low', 'warning_high', 'warning_color'])
                assert.equal(await page.locator(prefix + field).count(), 0, field);
            const typeBounds = await page.locator(prefix + 'type').boundingBox();
            const sourceBounds = await page.locator(prefix + 'control_uuid').boundingBox();
            const nameBounds = await page.locator(prefix + 'name').boundingBox();
            assert(typeBounds.y + typeBounds.height <= sourceBounds.y);
            assert(sourceBounds.y + sourceBounds.height <= nameBounds.y);
            assert.equal(await page.locator(prefix + 'move-up').isDisabled(), true);
            assert.equal(await page.locator(prefix + 'move-down').isDisabled(), true);
            const moveBounds = await page.locator(prefix + 'move-up').boundingBox();
            const removeBounds = await page.locator(prefix + 'remove').boundingBox();
            assert(moveBounds.x < removeBounds.x);
            assert(Math.abs(moveBounds.y - removeBounds.y) < 2);
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
            assert.equal(await page.locator('#home-cards-central-icon-preview').getAttribute('src'), '/api/templates/home-icon?id=blinds-shading&v=4');
            await page.locator('#home-cards-central-type').selectOption('value');
            assert.equal(await page.locator('#home-cards-central-icon').inputValue(), 'blinds');
            assert(images.includes('blinds-shading'));
            /* Mozgatáskor az egész kártya marad meg, újratöltés után is. */
            await page.locator('#home-cards-central-type').selectOption('shading');
            await page.locator('#home-cards-central-control_uuid').selectOption(central);
            await page.locator('#home-cards-central-name').fill('Még nem mentett cím');
            await page.locator('#home-cards-central-move-down').click();
            assert.deepEqual(await page.locator('#home-cards>fieldset').evaluateAll(boxes=>boxes.map(box=>box.id)),
                ['home-cards-imported-editor', 'home-cards-central-editor']);
            assert.equal(await page.locator('#home-cards-central-name').inputValue(), 'Még nem mentett cím');
            assert.equal(await page.locator('#home-cards-central-move-down').isDisabled(), true);
            assert.equal(await page.evaluate(()=>document.activeElement.id), 'home-cards-central-move-up');
            await page.locator('#home-save').click();
            await page.waitForFunction(()=>document.querySelector('#home-status').textContent.includes('Elmentve'));
            assert.deepEqual(posts.at(-1).pages[1].cards.map(card=>card.id), ['imported', 'central']);
            assert.equal(posts.at(-1).pages[1].cards[1].name, 'Még nem mentett cím');
            await page.reload();
            await page.locator('#home-page-select').selectOption('upper');
            assert.deepEqual(await page.locator('#home-cards>fieldset').evaluateAll(boxes=>boxes.map(box=>box.id)),
                ['home-cards-imported-editor', 'home-cards-central-editor']);
            assert.equal(await page.locator('#home-cards-central-name').inputValue(), 'Még nem mentett cím');
            /* Új kártya csak típusválasztás után nyitja meg a függő mezőket. */
            await page.locator('#home-add-card').click();
            const fresh = '#home-cards-item-1-';
            assert.equal(await page.locator(fresh + 'type').inputValue(), '');
            assert.equal(await page.locator(fresh + 'name').count(), 0);
            assert.equal(await page.locator(fresh + 'control_uuid').count(), 0);
            assert.equal(await page.locator(fresh + 'icon-preview').count(), 0);
            const beforeEmpty = posts.length;
            await page.locator('#home-save').click();
            assert.equal(posts.length, beforeEmpty);
            assert.match(await page.locator('#home-status').textContent(), /kártyatípust/);
            await page.locator(fresh + 'type').selectOption('shading');
            assert.equal(await page.locator(fresh + 'control_uuid').count(), 1);
            await page.locator(fresh + 'remove').click();
            /* Mind a nyolc típus ugyanazon kiadott HTML/JS mentési útját járja be.
             * A korábbi két oldal közben rejtett, de adataiknak meg kell maradniuk. */
            const previousPages = structuredClone(home.pages);
            home = {...home, revision:home.revision + 1, pages:[{
                id:'advanced', title:'Új funkcióblokkok', cards:advancedTypes.map(([type, , icon], index) => ({
                    id:'block-' + type, name:'Kártya ' + type, type, icon, control_uuid:advancedId(index),
                })),
            }, ...previousPages]};
            await page.reload();
            await page.locator('#home-cards-block-dimmer-type').waitFor();
            for (const [index, [type, , icon]] of advancedTypes.entries()) {
                const card = '#home-cards-block-' + type + '-';
                assert.equal(await page.locator(card + 'type').inputValue(), type);
                assert.deepEqual(await page.locator(card + 'control_uuid option').evaluateAll(options => options.map(item => item.value)),
                    ['', advancedId(index)]);
                assert.equal(await page.locator(card + 'control_uuid').inputValue(), advancedId(index));
                for (const field of ['uuid', 'scale', 'offset', 'unit', 'decimals'])
                    assert.equal(await page.locator(card + field).count(), 0, type + ': ' + field);
                if (['dimmer', 'slider', 'timed-switch'].includes(type))
                    assert.equal(await page.locator(card + 'icon').inputValue(), icon);
                else assert.equal(await page.locator(card + 'icon').count(), 0);
                assert.equal(await page.locator(card + 'icon-preview').getAttribute('src'),
                    '/api/templates/home-icon?id=' + icon + '&v=4');
                const first = await page.locator(card + 'type').boundingBox();
                const second = await page.locator(card + 'control_uuid').boundingBox();
                const third = await page.locator(card + 'name').boundingBox();
                assert(first.y + first.height <= second.y, type);
                assert(second.y + second.height <= third.y, type);
            }
            const dimmer = '#home-cards-block-dimmer-';
            await page.locator(dimmer + 'name').fill('Saját olvasólámpa');
            await page.locator(dimmer + 'icon').selectOption('washing-machine');
            await page.locator(dimmer + 'type').selectOption('audio');
            assert.equal(await page.locator(dimmer + 'control_uuid').inputValue(), '');
            assert.equal(await page.locator(dimmer + 'icon').count(), 0);
            await page.locator(dimmer + 'control_uuid').selectOption(advancedId(7));
            assert.equal(await page.locator(dimmer + 'name').inputValue(), 'Saját olvasólámpa');
            await page.locator(dimmer + 'type').selectOption('dimmer');
            assert.equal(await page.locator(dimmer + 'control_uuid').inputValue(), '');
            assert.equal(await page.locator(dimmer + 'icon').inputValue(), 'washing-machine');
            await page.locator(dimmer + 'control_uuid').selectOption(advancedId(0));
            const permanent = '#home-cards-block-timed-switch-allow_permanent';
            assert.equal(await page.locator(permanent).isChecked(), true);
            await page.locator(permanent).uncheck();
            await page.locator('#home-cards-block-gate-move-up').click();
            const expectedOrder = ['dimmer', 'central-lighting', 'slider', 'timed-switch',
                'gate', 'window-monitor', 'window', 'audio'];
            assert.deepEqual(await page.locator('#home-cards>fieldset').evaluateAll(boxes => boxes.map(box => box.id)),
                expectedOrder.map(type => 'home-cards-block-' + type + '-editor'));
            assert.equal(await page.locator(dimmer + 'name').inputValue(), 'Saját olvasólámpa');
            assert.equal(await page.locator(permanent).isChecked(), false);
            await page.locator('#home-save').click();
            await page.waitForFunction(() => document.querySelector('#home-status').textContent.includes('Elmentve'));
            const saved = posts.at(-1);
            assert.deepEqual(saved.pages.slice(1), previousPages);
            assert.deepEqual(saved.pages[0].cards.map(card => card.type), expectedOrder);
            for (const card of saved.pages[0].cards) {
                const index = advancedTypes.findIndex(([type]) => type === card.type);
                assert.equal(card.control_uuid, advancedId(index));
                assert.deepEqual(Object.keys(card).sort(),
                    ['control_uuid', 'icon', 'id', 'name', 'type', ...(card.type === 'timed-switch' ? ['allow_permanent'] : [])].sort());
            }
            assert.equal(saved.pages[0].cards[0].icon, 'washing-machine');
            assert.equal(saved.pages[0].cards.find(card => card.type === 'timed-switch').allow_permanent, false);
            await page.reload();
            await page.locator(dimmer + 'type').waitFor();
            assert.deepEqual(await page.locator('#home-cards>fieldset').evaluateAll(boxes => boxes.map(box => box.id)),
                expectedOrder.map(type => 'home-cards-block-' + type + '-editor'));
            assert.equal(await page.locator(dimmer + 'name').inputValue(), 'Saját olvasólámpa');
            assert.equal(await page.locator(dimmer + 'icon').inputValue(), 'washing-machine');
            assert.equal(await page.locator(permanent).isChecked(), false);
            assert.equal(await page.locator('#home-cards-block-gate-icon-preview').getAttribute('src'),
                '/api/templates/home-icon?id=gate-double&v=4');
            for (const [index, [type]] of advancedTypes.entries())
                assert.equal(await page.locator('#home-cards-block-' + type + '-control_uuid').inputValue(), advancedId(index));
            await page.locator('#home-page-select').selectOption('upper');
            assert.deepEqual(await page.locator('#home-cards>fieldset').evaluateAll(boxes => boxes.map(box => box.id)),
                ['home-cards-imported-editor', 'home-cards-central-editor']);
            assert.equal(await page.locator('#home-cards-central-name').inputValue(), 'Még nem mentett cím');
            assert.deepEqual(errors, []);
            await context.close();
        }
    } finally {
        await browser.close();
    }
    console.log('PASS: actual C-embedded editor, desktop/mobile, shading and eight typed blocks, SD icons, type-first DOM, arrow geometry, custom name/icon and permanent-mode retention, per-page ordering, minimal save and reload');
})().catch(error => {console.error(error); process.exitCode = 1;});
