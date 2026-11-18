/* A tényleges firmware-be ágyazott felület: hitelesítő mezők, időprogramok és
 * öröklés, választott profil, szerkesztés megőrzése és mobil elrendezés. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const {chromium} = require('playwright');

const lines = fs.readFileSync('main/app_inverter_page.h', 'utf8').split('\n').filter(line => line.startsWith('"'));
const html = lines.map(line => JSON.parse(line.replace(/;$/, '').replace(/\\\?/g, '?'))).join('');
const id = 'ble-0-010203040506';
const initial = {
    discoverable: ['huawei_modbus', 'solplanet_http', 'sofar_v5', 'victron_modbus', 'solax_modbus'],
    profile: 'solax_cloud', host: 'global.solaxcloud.com', device: 'A123', hasSecret: true, hasKey: false,
    user: '', unit: 1, port: 443, revision: 5, battery: false, latitude: 0, longitude: 0,
    schedule: {days: Array.from({length: 7}, () => ({enabled: false, start: 1320, end: 360})), touchWakeSeconds: 30},
    slaveSchedules: {}, syncStatus: 'Szinkronizálva', measurementAgeSeconds: 0
};
const peers = {schema: 1, ready: true, enabled: true, revision: 1, saved_revision: 1, save_error: 0, profiles: [
    {id, name: 'Konyha', rotation_degrees: 0, day_brightness: null, night_brightness: null}
]};

(async () => {
    const browser = await chromium.launch({headless: true, executablePath: process.env.CHROMIUM_EXECUTABLE || undefined});
    try {
        const page = await browser.newPage({viewport: {width: 390, height: 900}});
        const errors = [], unexpected = [];
        let stored = structuredClone(initial), posted = null, fail = false;
        const registerRequests = [], discoveryRequests = [], cancellations = [];
        let job = 20, discoveryStatus = {}, holdStart, startReply, nextStartError = false;

        const schema = [
            {id: 'pv', label: 'PV teljesítmény', address: 32064, words: 2, function: 3, base: null, format: 'S32 BE · W'},
            {id: 'soc', label: 'Töltöttség', address: 0, words: 1, function: 4, base: null, format: 'U16 · %'}
        ];
        const solaxRegisters = [
            {...schema[0], id: 'x1_ac', label: 'X1 AC', address: 2, words: 1},
            {...schema[0], id: 'x3_ac_l1', label: 'X3 AC', address: 108, words: 1},
            {...schema[0], id: 'pro_protocol', label: 'PRO protokoll', address: 130, words: 1},
            schema[0]
        ];
        let failSchema = '', stallSchema = '', resumeSchema, schemaStarted;

        page.on('pageerror', e => errors.push(e.message));
        await page.route('**/*', async route => {
            const request = route.request(), path = new URL(request.url()).pathname;
            if (path === '/inverter') return route.fulfill({contentType: 'text/html; charset=utf-8', body: html});
            if (path === '/api/inverter/slaves') return route.fulfill({json: peers});
            if (path === '/api/inverter/discovery/cancel') {
                assert.equal(request.method(), 'POST');
                assert.equal(request.headers()['x-zolee-inverter'], '1');
                cancellations.push(request.postDataJSON().job);
                return route.fulfill({json: {cancelled: true}});
            }
            if (path === '/api/inverter/discovery') {
                if (request.method() === 'POST') {
                    assert.equal(request.headers()['x-zolee-inverter'], '1');
                    const params = request.postDataJSON();
                    assert(Object.keys(params).every(k => ['profile', 'unit', 'port', 'device'].includes(k)));
                    discoveryRequests.push(params);
                    if (nextStartError) return route.fulfill({status: 409, body: 'Már fut egy keresés.'});
                    const thisJob = ++job;
                    if (holdStart) await new Promise(resolve => { startReply = resolve; holdStart(); });
                    return route.fulfill({json: {job: thisJob}});
                }
                const profile = discoveryRequests[discoveryRequests.length - 1].profile;
                assert.equal(Number(new URL(request.url()).searchParams.get('job')), job);
                return route.fulfill({json: {profile, job, state: 'done', checked: 16, total: 16, limited: false,
                    results: [], ...discoveryStatus}});
            }
            if (path === '/api/inverter/registers') {
                const profile = new URL(request.url()).searchParams.get('profile');
                registerRequests.push(profile);
                if (profile === failSchema) return route.fulfill({status: 503, body: 'busy'});
                if (profile === stallSchema) await new Promise(resolve => { resumeSchema = resolve; schemaStarted(); });
                return route.fulfill({json: {registers: profile === 'solaredge_modbus' ?
                    [{...schema[0], address: 6, base: 'SunSpec invertermodell kezdőcíme'}] : profile === 'solax_modbus' ? solaxRegisters : schema}});
            }
            if (path !== '/api/inverter') { unexpected.push(path); return route.abort(); }
            if (request.method() === 'POST') {
                assert.equal(request.headers()['x-zolee-inverter'], '1');
                posted = request.postDataJSON();
                if (fail) return route.fulfill({status: 409, body: 'Másik mentés'});
                stored = {...stored, ...posted, revision: stored.revision + 1};
                delete stored.secret; delete stored.key;
                return route.fulfill({json: {saved: true}});
            }
            return route.fulfill({json: stored});
        });
        await page.goto('http://display.test/inverter');
        await page.locator('#slave-programs legend').waitFor();
        assert.equal(await page.locator('#profile option').count(), 23);
        assert(!await page.locator('#search-inverter').isVisible());
        assert((await page.locator('#setup-help').textContent()).includes('tokenId'));
        assert.equal(await page.locator('#registers').isVisible(), false);
        assert.deepEqual(registerRequests, []);
        assert(!await page.locator('#rebind-control').isVisible());
        assert.equal(await page.locator('#latitude').inputValue(), '0');
        assert.equal(await page.locator('#secret').inputValue(), '');
        assert.equal(await page.locator('#secret').evaluate(e => e.required), false);
        await page.locator('#host').fill('GLOBAL.SolaXCloud.COM.');
        assert.equal(await page.locator('#secret').evaluate(e => e.required), false);
        await page.locator('#host').fill('other.solaxcloud.com');
        assert.equal(await page.locator('#secret').evaluate(e => e.required), true);
        await page.locator('#host').fill(initial.host);
        await page.locator('#device').fill('NEW_SN');
        assert.equal(await page.locator('#secret').evaluate(e => e.required), true);
        await page.locator('#device').fill(initial.device);
        await page.locator('#master-program .week-row').first().locator('input[type=checkbox]').check();
        const own = page.locator('#slave-programs fieldset').first();
        await own.getByLabel('Saját időprogram', {exact: false}).check();
        const monday = own.locator('.week-row').first();
        assert(await monday.locator('input[type=checkbox]').isChecked());
        await monday.getByLabel('Vége').fill('07:15');
        await own.getByLabel('Érintés utáni', {exact: false}).fill('42');
        assert.equal(await page.locator('#slave-profiles select[id$="presence_uuid"]').count(), 0);
        assert.equal(await page.locator('#slave-profiles input[id$="day_brightness"]').count(), 1);
        await page.locator('#save-inverter').click();
        await page.waitForFunction(() => document.getElementById('result').textContent.includes('Mentve.'));
        assert.equal(posted.rebindDevice, undefined);
        assert.equal(posted.registerOverrides, undefined);
        assert.equal(posted.secret, undefined); assert.equal(posted.key, undefined);
        assert.equal(posted.unit, 1);
        assert.equal(posted.revision, 5); assert.equal(posted.latitude, 0);
        assert.deepEqual(posted.schedule.days[0], {enabled: true, start: 1320, end: 360});
        assert.equal(posted.slaveSchedules[id].days[0].end, 435);
        assert.equal(posted.slaveSchedules[id].touchWakeSeconds, 42);
        await own.getByLabel('Saját időprogram', {exact: false}).uncheck();
        await page.locator('#save-inverter').click();
        await page.waitForFunction(() => document.getElementById('result').textContent.includes('Mentve.'));
        assert.deepEqual(posted.slaveSchedules, {});
        for (const [profile, port, unit] of [['goodwe_modbus', 502, 247], ['solaredge_modbus', 1502, 1],
            ['solplanet_modbus', 502, 3], ['deye_rtu', 8899, 1], ['deye_lse3', 8899, 1], ['victron_mqtt', 1883, null]]) {
            await page.locator('#profile').selectOption(profile);
            assert.equal(await page.locator('#port').inputValue(), String(port));
            if (profile === 'deye_lse3') assert.equal(await page.locator('#device').count(), 0);
            if (unit == null) assert.equal(await page.locator('#unit').count(), 0);
            else assert.equal(await page.locator('#unit').inputValue(), String(unit));
        }
        assert.equal(await page.locator('#trustedPem').count(), 1);
        await page.locator('#profile').selectOption('solax_modbus');
        const x1 = page.getByRole('spinbutton', {name: 'X1 AC', exact: true});
        const x3 = page.getByRole('spinbutton', {name: 'X3 AC', exact: true});
        const pro = page.getByRole('spinbutton', {name: 'PRO protokoll', exact: true});
        await x3.waitFor(); assert.equal(await x1.count(), 0); assert.equal(await pro.count(), 0);
        await page.locator('#device').selectOption('X3-G4-PRO');
        await pro.fill('0'); assert.equal(await x3.inputValue(), '108');
        assert.equal(await page.locator('#device').inputValue(), 'X3-G4-PRO');
        await page.locator('#device').selectOption('X1-G4');
        assert.equal(await pro.count(), 0); assert.equal(await x3.count(), 0);
        await x1.fill('10');
        await page.locator('#host').fill('192.0.2.1');
        fail = true;
        await page.locator('#save-inverter').click();
        await page.waitForFunction(() => document.getElementById('result').textContent.includes('Másik mentés'));
        assert.equal(await page.locator('#host').inputValue(), '192.0.2.1');
        assert.equal(posted.device, 'X1-G4');
        assert.deepEqual(posted.registerOverrides, {x1_ac: 10});
        await page.locator('#device').selectOption('X3-G4-PRO');
        assert.equal(await pro.inputValue(), '0');
        await page.evaluate(() => refresh());
        assert.equal(await page.locator('#profile').inputValue(), 'solax_modbus');
        for (const width of [320, 390, 1280]) {
            await page.setViewportSize({width, height: 900});
            assert(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1));
        }
        // Ismételt lekérdezés, profilváltás és másik profilból későn érkező válasz.
        fail = false;
        failSchema = 'huawei_modbus';
        await page.locator('#profile').selectOption(failSchema);
        await page.locator('#register-retry').waitFor();
        assert(await page.locator('#save-inverter').isDisabled());
        failSchema = '';
        await page.locator('#register-retry').click();
        const pv = page.getByRole('spinbutton', {name: 'PV teljesítmény', exact: true});
        const soc = page.getByRole('spinbutton', {name: 'Töltöttség', exact: true});
        const resetPv = page.getByRole('button', {name: 'Gyári cím visszaállítása – PV teljesítmény'});
        const resetSoc = page.getByRole('button', {name: 'Gyári cím visszaállítása – Töltöttség'});
        await pv.waitFor();
        assert.equal(await pv.inputValue(), '32064');
        assert.equal(await pv.getAttribute('max'), '65534');
        assert.equal(await soc.inputValue(), '0');
        assert(await resetPv.isDisabled() && await resetSoc.isDisabled());
        assert((await page.locator('#register-rows').textContent()).includes('S32 BE · W'));
        await pv.fill('0');
        await soc.fill('17');
        await resetSoc.click();
        assert.equal(await soc.inputValue(), '0');
        assert.equal(await pv.inputValue(), '0');
        assert(!await resetPv.isDisabled() && await resetSoc.isDisabled());
        assert((await page.locator('#register-status').textContent()).includes('1'));
        await page.evaluate(() => refresh());
        assert.equal(await pv.inputValue(), '0');
        const stalled = new Promise(resolve => { schemaStarted = resolve; });
        stallSchema = 'goodwe_modbus';
        await page.locator('#profile').selectOption(stallSchema);
        await stalled;
        assert((await page.locator('#register-status').textContent()).includes('betöltése'));
        await page.locator('#profile').selectOption('solaredge_modbus');
        await pv.waitFor();
        assert.equal(await pv.inputValue(), '6');
        assert((await page.locator('#register-rows').textContent()).includes('SunSpec invertermodell'));
        assert.equal(await soc.count(), 0);
        assert.equal(typeof resumeSchema, 'function');
        const late = page.waitForResponse(r => r.url().endsWith('profile=goodwe_modbus'));
        resumeSchema(); await late; stallSchema = '';
        assert.equal(await pv.inputValue(), '6');
        assert.equal(await soc.count(), 0);
        await page.locator('#profile').selectOption('huawei_modbus');
        await pv.waitFor(); assert.equal(await pv.inputValue(), '0');
        await page.locator('#host').fill('192.0.2.10');
        await soc.fill('82'); await pv.fill('65535');
        assert.equal(await pv.evaluate(n => n.validity.rangeOverflow), true);
        await pv.fill('1.5'); assert.equal(await pv.evaluate(n => n.validity.stepMismatch), true);
        await pv.fill('0');
        for (const width of [320, 390, 1280]) {
            await page.setViewportSize({width, height: 900});
            assert(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1));
            assert((await resetSoc.boundingBox()).width >= 44);
        }
        async function persist() {
            const write = page.waitForResponse(r => r.url().endsWith('/api/inverter') && r.request().method() === 'POST');
            await page.locator('#save-inverter').click();
            assert.equal((await write).status(), 200);
            await page.waitForFunction(() => !document.getElementById('save-inverter').disabled &&
                document.getElementById('result').textContent.includes('Mentve.'));
        }
        await persist();
        assert.deepEqual(posted.registerOverrides, {pv: 0, soc: 82});
        assert.equal(posted.rebindDevice, undefined);
        stored.activeIpv4 = '192.0.2.12';
        await page.reload();
        await pv.waitFor(); assert.equal(await pv.inputValue(), '0');
        assert((await page.locator('#sync-status').textContent()).includes('Legutóbb igazolt helyi IP-cím: 192.0.2.12'));
        assert.equal(await page.locator('#host').inputValue(), '192.0.2.10');
        assert.equal(await soc.inputValue(), '82');
        assert.equal(await page.locator('#register-status').textContent(), '');
        const rebind = page.locator('#rebind-device');
        assert(await rebind.isVisible() && !await rebind.isChecked());
        await rebind.check(); await page.locator('#port').fill('1502');
        assert(!await rebind.isVisible() && !await rebind.isChecked());
        await page.locator('#port').fill('502'); assert(await rebind.isVisible());
        await rebind.check();
        await resetSoc.click(); await persist();
        assert.equal(posted.rebindDevice, true);
        assert(!await rebind.isChecked());
        assert.deepEqual(posted.registerOverrides, {pv: 0});
        await resetPv.click(); await persist();
        assert.deepEqual(posted.registerOverrides, {});
        assert.equal(posted.rebindDevice, undefined);
        stored = {...stored, profile: 'solax_modbus', device: 'X1-G4', unit: 1,
            registerOverrides: {x1_ac: 21, x3_ac_l1: 43, pro_protocol: 0}};
        await page.reload();
        await x1.waitFor(); assert.equal(await x1.inputValue(), '21');
        assert.equal(await x3.count(), 0); assert.equal(await pro.count(), 0);
        await x1.fill('2'); await persist();
        assert.deepEqual(posted.registerOverrides, {x3_ac_l1: 43, pro_protocol: 0});
        const profile = page.locator('#profile'), search = page.locator('#search-inverter');
        const setup = page.locator('#setup-help'), searchStatus = page.locator('#discovery-status');
        for (const profileId of await profile.locator('option').evaluateAll(items => items.map(i => i.value).filter(Boolean))) {
            await profile.selectOption(profileId);
            assert((await setup.textContent()).length > 30, 'Missing setup help: ' + profileId);
            assert.equal(await search.isVisible(), initial.discoverable.includes(profileId), profileId);
        }
        assert.deepEqual(discoveryRequests, []);
        const oldLogger = '3910000000';
        stored = {...stored, profile: 'sofar_v5', host: '192.0.2.19', port: 8899, device: oldLogger, unit: 1, registerOverrides: {}};
        await page.reload(); await search.waitFor();
        assert.equal(await profile.inputValue(), 'sofar_v5');
        assert((await setup.textContent()).includes('SOLARMAN V5'));
        assert.equal(await page.locator('#port').inputValue(), '8899');
        assert.equal(await page.locator('#unit').inputValue(), '1');
        await page.locator('#port').fill('8898');
        const active = {host: '192.0.2.21', port: 8898, unit: 1, model: 'HYD 10KTL-3PH',
            serial: 'INVERTER-SN', device: '3912345678', verified: true};
        const probable = {...active, host: '192.0.2.22', serial: '', device: '', model: 'SOLARMAN <img>', verified: false};
        discoveryStatus = {state: 'running', results: [active, probable]};
        await search.click();
        const pick = page.locator('#discovery-results button');
        await pick.first().waitFor();
        assert.equal(await pick.count(), 2);
        assert.equal(await page.locator('#discovery-results img').count(), 0);
        assert((await page.locator('#discovery-results').textContent()).includes('Eszköz észlelve'));
        assert((await page.locator('#discovery-results').textContent()).includes('Adatgyűjtő száma: ' + active.device));
        assert.equal(await page.locator('#host').inputValue(), '192.0.2.19');
        assert.equal(await page.locator('#device').inputValue(), oldLogger);
        assert.deepEqual(discoveryRequests.at(-1), {profile: 'sofar_v5', port: 8898, unit: 1});
        const selectedJob = job;
        const selectCancel = page.waitForResponse(r => r.url().endsWith('/discovery/cancel'));
        await pick.first().click(); await selectCancel;
        assert.equal(cancellations.at(-1), selectedJob);
        assert.equal(await page.locator('#host').inputValue(), active.host);
        assert.equal(await page.locator('#device').inputValue(), active.device);
        assert.equal(posted.profile, 'solax_modbus');
        await profile.selectOption('huawei_modbus');
        discoveryStatus = {state: 'done', results: [], limited: true};
        await search.click();
        await page.waitForFunction(() => document.querySelector('#discovery-status').textContent.includes('Nincs találat'));
        assert((await searchStatus.textContent()).includes('korlátját'));
        nextStartError = true;
        await search.click();
        await page.waitForFunction(() => document.querySelector('#discovery-status').textContent.includes('Már fut'));
        assert(await search.isEnabled());
        nextStartError = false;
        const pending = new Promise(resolve => { holdStart = resolve; });
        await search.click(); await pending;
        await profile.selectOption('solplanet_https');
        assert(!await search.isVisible());
        assert((await setup.textContent()).includes('tanúsítvány'));
        const lateCancel = page.waitForResponse(r => r.url().endsWith('/discovery/cancel'));
        holdStart = null; startReply(); await lateCancel;
        assert.equal(cancellations.at(-1), job);
        assert.equal(await page.locator('#host').inputValue(), '');
        assert.equal(await pick.count(), 0);
        await profile.selectOption('solplanet_http');
        discoveryStatus = {state: 'running'};
        await search.click();
        await page.waitForFunction(() => document.querySelector('#discovery-status').textContent.includes('Ellenőrzött címek'));
        const explicitCancel = page.waitForResponse(r => r.url().endsWith('/discovery/cancel'));
        await page.locator('#cancel-search').click(); await explicitCancel;
        assert.equal(cancellations.at(-1), job);
        assert(await search.isEnabled());
        await profile.selectOption('solax_modbus');
        await page.locator('#device').selectOption('X3-G4-PRO');
        assert((await setup.textContent()).includes('vezetékes LAN'));
        discoveryStatus = {state: 'done'};
        await search.click();
        await page.waitForFunction(() => document.querySelector('#discovery-status').textContent.includes('Nincs találat'));
        assert.deepEqual(discoveryRequests.at(-1), {profile: 'solax_modbus', port: 502, unit: 1, device: 'X3-G4-PRO'});
        assert(registerRequests.every(id => id.includes('modbus') || id.startsWith('deye_') || id === 'sofar_v5'));
        assert.deepEqual(errors, []);
        assert.deepEqual(unexpected, []);
        console.log('PASS: all inverter options, credentials, GPS origin, Slave inheritance/schedules, Modbus defaults, per-row reset, late schema replies, edit retention and responsive layout');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
