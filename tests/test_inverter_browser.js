/* A tényleges firmware-be ágyazott felület: hitelesítő mezők, időprogramok és
 * öröklés, választott profil, szerkesztés megőrzése és mobil elrendezés. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const {chromium} = require('playwright');

const lines = fs.readFileSync('main/app_inverter_page.h', 'utf8').split('\n').filter(line => line.startsWith('"'));
const html = lines.map(line => JSON.parse(line.replace(/;$/, '').replace(/\\\?/g, '?'))).join('');
const id = 'ble-0-010203040506';
const initial = {
    profile: 'solax_cloud', host: 'global.solaxcloud.com', device: 'A123', hasSecret: true, hasKey: false,
    user: '', unit: 0, port: 443, revision: 5, battery: false, latitude: 0, longitude: 0,
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
        page.on('pageerror', e => errors.push(e.message));
        await page.route('**/*', async route => {
            const request = route.request(), path = new URL(request.url()).pathname;
            if (path === '/inverter') return route.fulfill({contentType: 'text/html; charset=utf-8', body: html});
            if (path === '/api/inverter/slaves') return route.fulfill({json: peers});
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
        assert.equal(await page.locator('#profile option').count(), 20);
        assert.equal(await page.locator('#latitude').inputValue(), '0');
        assert.equal(await page.locator('#secret').inputValue(), '');
        assert.equal(await page.locator('#secret').evaluate(e => e.required), false);
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
        assert.equal(posted.secret, undefined); assert.equal(posted.key, undefined);
        assert.equal(posted.revision, 5); assert.equal(posted.latitude, 0);
        assert.deepEqual(posted.schedule.days[0], {enabled: true, start: 1320, end: 360});
        assert.equal(posted.slaveSchedules[id].days[0].end, 435);
        assert.equal(posted.slaveSchedules[id].touchWakeSeconds, 42);
        await own.getByLabel('Saját időprogram', {exact: false}).uncheck();
        await page.locator('#save-inverter').click();
        await page.waitForFunction(() => document.getElementById('result').textContent.includes('Mentve.'));
        assert.deepEqual(posted.slaveSchedules, {});
        for (const [profile, port, unit] of [['goodwe_modbus', 502, 247], ['solaredge_modbus', 1502, 1],
            ['solplanet_modbus', 502, 3], ['deye_rtu', 8899, 1], ['victron_mqtt', 1883, null]]) {
            await page.locator('#profile').selectOption(profile);
            assert.equal(await page.locator('#port').inputValue(), String(port));
            if (unit == null) assert.equal(await page.locator('#unit').count(), 0);
            else assert.equal(await page.locator('#unit').inputValue(), String(unit));
        }
        assert.equal(await page.locator('#trustedPem').count(), 1);
        await page.locator('#profile').selectOption('solax_modbus');
        await page.locator('#device').selectOption('X3-G4-PRO');
        assert.equal(await page.locator('#device').inputValue(), 'X3-G4-PRO');
        await page.locator('#device').selectOption('X1-G4');
        await page.locator('#host').fill('192.0.2.1');
        fail = true;
        await page.locator('#save-inverter').click();
        await page.waitForFunction(() => document.getElementById('result').textContent.includes('Másik mentés'));
        assert.equal(await page.locator('#host').inputValue(), '192.0.2.1');
        assert.equal(posted.device, 'X1-G4');
        await page.evaluate(() => refresh());
        assert.equal(await page.locator('#profile').inputValue(), 'solax_modbus');
        for (const width of [320, 390, 1280]) {
            await page.setViewportSize({width, height: 900});
            assert(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1));
        }
        assert.deepEqual(errors, []);
        assert.deepEqual(unexpected, []);
        console.log('PASS: all inverter options, credentials, GPS origin, Slave inheritance/schedules, edit retention and responsive layout');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
