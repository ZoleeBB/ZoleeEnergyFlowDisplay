/* A teljes kiadott oldal Chromiumban: valódi DOM/fókusz, mobil elrendezés és mentés. */
const fs = require('fs'), http = require('http'), assert = require('node:assert/strict');
const {chromium} = require('playwright');
const path = require('path');
const root = path.resolve(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const icons = JSON.parse(read('assets/loxone-home/catalog.json')).icons.map(({id, hu, en}) => ({id, hu, en}));
const html = read('assets/web/loxone.html')
    .replace('/*COMMON_CSS*/', read('assets/web/common.css'))
    .replace('/*SLAVES_JS*/', read('assets/web/loxone-slaves.js'))
    .replace('/*HOME_JS*/', read('assets/web/loxone-home.js').replace('/*HOME_ICONS*/', JSON.stringify(icons)))
    .replace('/*LOXONE_JS*/', read('assets/web/loxone.js'));
const id = 'ble-0-060504030201', uuid = '11223344-5566-7788-99aabbccddeeff00';
const catalog = [{uuid, name:'Nappali jelenlét', room:'Nappali', category:'Jelenlét', state:'value'}];
const state = {schema:1, enabled:true, ready:true, revision:123, saved_revision:123, save_error:0,
    profiles:[id,'ble-0-070504030201','ble-0-080504030201'].map((id, index) => ({
        id, name:['Nappali kijelző','Emeleti kijelző','Konyhai kijelző'][index],
        presence_uuid:index ? '' : uuid, timeout_seconds:30, blank_at_night:false,
        rotation_degrees:0, day_brightness:null, night_brightness:null}))};
const master = {host:'192.168.2.10', port:80, username:'kijelzo', tls:false,
    catalog, mappings:{}, busy:false, status:'Kapcsolódva',
    presence:{blank_at_day:false,blank_at_night:true,timeout_seconds:60}};
let posts = 0, gets = 0, masterPosts = 0, masterGets = 0, masterDesired;
const output = path.resolve(process.env.WEB_SCREENSHOT_DIR || 'out/web-editor');
fs.mkdirSync(output,{recursive:true});
const server = http.createServer(async (req, res) => {
    res.setHeader('Cache-Control','no-store');
    if (req.url === '/') { res.setHeader('Content-Type','text/html; charset=utf-8'); res.end(html); return; }
    res.setHeader('Content-Type','application/json');
    if (req.url === '/api/loxone/slaves') {
        if (req.method === 'POST') {
            let body = '';
            for await (const chunk of req) body += chunk;
            const value = JSON.parse(body);
            assert.equal(req.headers['x-zolee-loxone'],'1');
            assert.equal(value.revision,state.revision);
            Object.assign(state.profiles.find(item => item.id === value.id), value.profile);
            state.revision++;
            posts++;
            res.statusCode = 202;
            res.end('{"accepted":true}');
        } else { gets++; res.end(JSON.stringify(state)); }
    } else if (req.url === '/api/loxone/save') {
        let body = '';
        for await (const chunk of req) body += chunk;
        const value = JSON.parse(body);
        assert.equal(req.headers['x-zolee-loxone'],'1');
        assert.deepEqual(Object.keys(value).sort(),['presence','section']);
        assert.equal(value.section,'presence');
        masterDesired = value.presence;
        master.busy = true;
        master.status = 'Mentés folyamatban…';
        masterPosts++;
        res.statusCode = 202;
        res.end('{"accepted":true}');
    } else if (req.url === '/api/loxone/home') res.end('{"schema":2,"consumers":[],"pages":[],"revision":1,"busy":false}');
    else if (req.url === '/api/loxone') {masterGets++;res.end(JSON.stringify(master));}
    else { res.statusCode = 404; res.end('{}'); }
});

async function checkEditorLayout(page) {
    assert(!await page.locator('#home-page-panel').isVisible());
    await page.locator('#home-add-page').click();
    await page.locator('#home-add-card').click();
    await page.locator('#home-cards [data-field=type] select').selectOption('value');
    const presence = '#slave-' + id + '-presence_uuid';
    const pairs = [
        ['#slave-' + id + '-name', presence],
        ['#home-cards [data-field=warning_low] input', '#home-cards [data-field=warning_high] input'],
    ];
    // A böngésző egymás mellett és egymás alatt is kirakja ugyanazokat az élő mezőket.
    await page.locator(presence).evaluate(node => {
        node.labels[0].firstChild.textContent = 'Az emeleti kijelző elsötétítését vezérlő Loxone jelenlétállapot';
        node.closest('fieldset').querySelector('legend').textContent = 'EmeletiKijelző'.repeat(5);
    });
    for (const width of [320, 530, 680, 915, 1280]) {
        await page.setViewportSize({width, height:900});
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), `${width}px`);
        for (const [first, second] of pairs) {
            const a = await page.locator(first).boundingBox(), b = await page.locator(second).boundingBox();
            assert(a && b, `${first}, ${second}`);
            if (b.x >= a.x + a.width) assert(Math.abs(a.y - b.y) <= 1, `${width}px: ${first}, ${second}`);
            else assert(b.y >= a.y + a.height, `${width}px: ${second}`);
        }
    }
    assert.equal(await page.getByRole('combobox', {name:'Hálózati teljesítmény (kW) – Loxone állapot', exact:true}).count(), 1);
    assert.equal(await page.getByRole('spinbutton', {name:'Hálózati teljesítmény (kW) – Szorzó', exact:true}).count(), 1);
    await page.reload();
    await page.locator(presence).waitFor();
}

(async () => {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const browser = await chromium.launch({headless:true,executablePath:process.env.CHROMIUM_EXECUTABLE||undefined});
    try {
        const page = await browser.newPage({viewport:{width:1280,height:900}});
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.goto('http://127.0.0.1:' + server.address().port);
        await page.locator('#slave-' + id + '-name').waitFor();
        assert.equal(await page.locator('#slave-profiles fieldset').count(),3);
        await checkEditorLayout(page);
        const name = page.locator('#slave-' + id + '-name');
        await name.fill('Szerkesztett <b>név</b>');
        const before = gets;
        /* Két teljes háttérfrissítés alatt a valódi fókusz és a DOM-csomópont is maradjon. */
        const handle = await name.elementHandle();
        const deadline = Date.now() + 15000;
        while (gets < before + 2 && Date.now() < deadline) await page.waitForTimeout(100);
        assert(gets >= before + 2);
        assert.equal(await name.inputValue(),'Szerkesztett <b>név</b>');
        assert(await name.evaluate((node, original) => node === original && document.activeElement === node,handle));
        const masterDelay = page.locator('#presence-timeout_seconds');
        assert.equal(await masterDelay.inputValue(),'60');
        assert(!await page.locator('#presence-blank_at_day').isChecked());
        assert(await page.locator('#presence-blank_at_night').isChecked());
        await page.locator('#presence-blank_at_day').check();
        await page.locator('#presence-blank_at_night').uncheck();
        await masterDelay.fill('7');
        const masterHandle = await masterDelay.elementHandle();
        const masterBefore = masterGets;
        const masterDeadline = Date.now() + 15000;
        while (masterGets < masterBefore + 2 && Date.now() < masterDeadline) await page.waitForTimeout(100);
        assert(masterGets >= masterBefore + 2);
        assert.equal(await masterDelay.inputValue(),'7');
        assert(await masterDelay.evaluate((node, original) => node === original && document.activeElement === node,masterHandle));
        await page.locator('#presence-save').click();
        await page.locator('#presence-status').filter({hasText:'tartós mentés folyamatban'}).waitFor();
        assert.equal(masterPosts,1);assert.equal(posts,0);
        assert.deepEqual(masterDesired,{blank_at_day:true,blank_at_night:false,timeout_seconds:7});
        assert.equal(master.presence.timeout_seconds,60);
        assert(await masterDelay.isDisabled());
        master.presence = masterDesired;master.busy = false;master.status = 'Beállítások elmentve.';
        await page.locator('#presence-status').filter({hasText:'tartósan elmentve'}).waitFor();
        assert(!await masterDelay.isDisabled());
        assert.equal(await name.inputValue(),'Szerkesztett <b>név</b>');
        await page.locator('#master-presence').screenshot({path:path.join(output,'master-presence-desktop.png')});
        await page.locator('#slave-room').selectOption(JSON.stringify('Nappali'));
        assert(!await page.locator('#slave-' + id + '-blank_at_day').isChecked());
        await page.locator('#slave-' + id + '-blank_at_day').check();
        await page.locator('#slave-' + id + '-blank_at_night').check();
        const day = page.locator('#slave-' + id + '-day_brightness');
        const night = page.locator('#slave-' + id + '-night_brightness');
        assert.equal(await day.inputValue(),'');
        assert.equal(await night.getAttribute('placeholder'),'Master értéke');
        await day.fill('72');
        await night.fill('14');
        await page.locator('#slave-' + id + '-rotation_degrees').selectOption('180');
        await page.locator('#slave-' + id + '-save').click();
        await page.locator('#slave-' + id + '-status').filter({hasText:'tartós mentés folyamatban'}).waitFor();
        assert.equal(posts,1);
        assert.equal(state.profiles[0].rotation_degrees,180);
        assert.equal(state.profiles[0].blank_at_day,true);assert.equal(state.profiles[0].blank_at_night,true);
        assert.equal(state.profiles[0].day_brightness,72);
        assert.equal(state.profiles[0].night_brightness,14);
        assert(await page.locator('#slave-' + id + '-save').isDisabled());
        state.saved_revision = state.revision;
        await page.locator('#slave-' + id + '-status').filter({hasText:'tartósan elmentve'}).waitFor();
        assert.equal(await page.locator('#slave-profiles legend').first().textContent(),'Szerkesztett <b>név</b>');
        assert.equal(await page.locator('#slave-profiles b').count(),0);
        await page.locator('#slave-editor').screenshot({path:path.join(output,'slave-editor-desktop.png')});
        await page.setViewportSize({width:390,height:844});
        await page.locator('#slave-editor').scrollIntoViewIfNeeded();
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth));
        await page.locator('#presence-blank_at_day').uncheck();
        await page.locator('#presence-blank_at_night').check();
        await masterDelay.fill('25');
        await page.locator('#presence-save').click();
        await page.locator('#presence-status').filter({hasText:'tartós mentés folyamatban'}).waitFor();
        master.presence = masterDesired;master.busy = false;
        await page.locator('#presence-status').filter({hasText:'tartósan elmentve'}).waitFor();
        assert.equal(masterPosts,2);assert.equal(master.presence.timeout_seconds,25);
        assert.equal(master.presence.blank_at_day,false);assert.equal(master.presence.blank_at_night,true);
        await page.locator('#master-presence').screenshot({path:path.join(output,'master-presence-mobile.png')});
        await name.fill('Mobil kijelző');
        await page.locator('#slave-' + id + '-blank_at_day').uncheck();
        await day.fill(''); /* A nappali mező visszaáll a Master öröklésére. */
        await page.locator('#slave-' + id + '-save').click();
        await page.locator('#slave-' + id + '-status').filter({hasText:'tartós mentés folyamatban'}).waitFor();
        state.saved_revision = state.revision;
        await page.locator('#slave-' + id + '-status').filter({hasText:'tartósan elmentve'}).waitFor();
        assert.equal(posts,2);
        assert.equal(state.profiles[0].day_brightness,null);
        assert.equal(state.profiles[0].blank_at_day,false);assert.equal(state.profiles[0].blank_at_night,true);
        assert.equal(state.profiles[0].night_brightness,14);
        await page.locator('#slave-editor').screenshot({path:path.join(output,'slave-editor-mobile.png')});
        await page.reload();
        await page.locator('#slave-' + id + '-name').waitFor();
        assert.equal(await masterDelay.inputValue(),'25');
        assert(!await page.locator('#presence-blank_at_day').isChecked());assert(await page.locator('#presence-blank_at_night').isChecked());
        assert(!await page.locator('#slave-' + id + '-blank_at_day').isChecked());assert(await page.locator('#slave-' + id + '-blank_at_night').isChecked());
        assert.deepEqual(errors,[]);
        console.log('PASS: complete styled page in Chromium, independent master/slave day/night saves, real focus/draft retention over polls, accepted vs durable, reload persistence, safe text and desktop/mobile layouts');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode=1; }).finally(() => server.close());
