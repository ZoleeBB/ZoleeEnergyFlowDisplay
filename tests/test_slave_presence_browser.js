/* A teljes kiadott oldal Chromiumban: valódi DOM/fókusz, mobil elrendezés és mentés. */
const fs = require('fs'), http = require('http'), assert = require('node:assert/strict');
const {chromium} = require('playwright');
const path = require('path');
const root = path.resolve(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const icons = JSON.parse(read('assets/loxone-home/catalog.json')).icons.map(({id, hu, en}) => ({id, hu, en}));
const html = read('assets/web/loxone.html')
    .replace('/*SLAVES_JS*/', read('assets/web/loxone-slaves.js'))
    .replace('/*HOME_JS*/', read('assets/web/loxone-home.js').replace('/*HOME_ICONS*/', JSON.stringify(icons)))
    .replace('/*LOXONE_JS*/', read('assets/web/loxone.js'));
const id = 'ble-0-060504030201', uuid = '11223344-5566-7788-99aabbccddeeff00';
const catalog = [{uuid, name:'Nappali jelenlét', room:'Nappali', category:'Jelenlét', state:'value'}];
const state = {schema:1, enabled:true, ready:true, revision:123, saved_revision:123, save_error:0,
    profiles:[id,'ble-0-070504030201','ble-0-080504030201'].map((id, index) => ({
        id, name:['Nappali kijelző','Emeleti kijelző','Konyhai kijelző'][index],
        presence_uuid:index ? '' : uuid, timeout_seconds:30, blank_at_night:false}))};
let posts = 0, gets = 0;
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
    } else if (req.url === '/api/loxone/home') res.end('{"schema":2,"consumers":[],"pages":[],"revision":1,"busy":false}');
    else if (req.url === '/api/loxone') res.end(JSON.stringify({host:'192.168.2.10', port:80, username:'kijelzo', tls:false,
        catalog, mappings:{}, busy:false, status:'Kapcsolódva'}));
    else { res.statusCode = 404; res.end('{}'); }
});
(async () => {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const browser = await chromium.launch({headless:true});
    try {
        const page = await browser.newPage({viewport:{width:1280,height:900}});
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.goto('http://127.0.0.1:' + server.address().port);
        await page.locator('#slave-' + id + '-name').waitFor();
        assert.equal(await page.locator('#slave-profiles fieldset').count(),3);
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
        await page.locator('#slave-room').selectOption(JSON.stringify('Nappali'));
        await page.locator('#slave-' + id + '-blank_at_night').check();
        await page.locator('#slave-' + id + '-save').click();
        await page.locator('#slave-' + id + '-status').filter({hasText:'tartós mentés folyamatban'}).waitFor();
        assert.equal(posts,1);
        assert(await page.locator('#slave-' + id + '-save').isDisabled());
        state.saved_revision = state.revision;
        await page.locator('#slave-' + id + '-status').filter({hasText:'tartósan elmentve'}).waitFor();
        assert.equal(await page.locator('#slave-profiles legend').first().textContent(),'Szerkesztett <b>név</b>');
        assert.equal(await page.locator('#slave-profiles b').count(),0);
        const output = path.resolve(process.env.WEB_SCREENSHOT_DIR || 'out/web-editor');
        fs.mkdirSync(output,{recursive:true});
        await page.locator('#slave-editor').screenshot({path:path.join(output,'slave-editor-desktop.png')});
        await page.setViewportSize({width:390,height:844});
        await page.locator('#slave-editor').scrollIntoViewIfNeeded();
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth));
        await name.fill('Mobil kijelző');
        await page.locator('#slave-' + id + '-save').click();
        await page.locator('#slave-' + id + '-status').filter({hasText:'tartós mentés folyamatban'}).waitFor();
        state.saved_revision = state.revision;
        await page.locator('#slave-' + id + '-status').filter({hasText:'tartósan elmentve'}).waitFor();
        assert.equal(posts,2);
        await page.locator('#slave-editor').screenshot({path:path.join(output,'slave-editor-mobile.png')});
        assert.deepEqual(errors,[]);
        console.log('PASS: full embedded-source page in Chromium, 3 peers, real focus/node retention over two polls, accepted vs durable, safe text, desktop/mobile and no JS exceptions');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode=1; }).finally(() => server.close());
