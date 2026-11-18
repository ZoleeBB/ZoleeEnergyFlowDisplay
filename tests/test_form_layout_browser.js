/* A firmware által kiszolgált szerkesztők valódi DOM-ját és a Chromium által
 * számolt koordinátákat mérjük. Csak az eszköz HTTP-API-ját helyettesítjük. */
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const {chromium} = require('playwright');

function embeddedPage(name) {
    return fs.readFileSync('main/app_' + name + '_page.h', 'utf8').split('\n')
        .filter(line => line.startsWith('"'))
        .map(line => JSON.parse(line.replace(/;$/, '').replace(/\\\?/g, '?'))).join('');
}

const pages = Object.fromEntries(['inverter', 'settings', 'loxone'].map(key => ['/' + key, embeddedPage(key)]));
const peer = {id: 'ble-0-010203040506', name: 'Emeleti kijelző', timeout_seconds: 30, rotation_degrees: 0};
const peers = {schema: 1, ready: true, enabled: true, revision: 1, saved_revision: 1, profiles: [peer]};
const api = {
    '/api/settings': JSON.parse(fs.readFileSync('examples/config.loxone.sample.json', 'utf8')),
    '/api/inverter': {profile: 'sofar_v5', host: '192.168.1.73', port: 8899, unit: 1,
        device: '1234567890', revision: 1, discoverable: ['sofar_v5'], battery: true},
    '/api/inverter/slaves': peers,
    '/api/inverter/registers': {registers: [{id: 'pv', label: 'Napelem teljesítmény', address: 32064,
        words: 2, function: 3, format: 'S32 BE · W'}]},
    '/api/loxone': {host: '192.0.2.10', port: 80, username: 'kijelzo', catalog: [], mappings: {},
        presence: {blank_at_day: false, blank_at_night: true, timeout_seconds: 60}},
    '/api/loxone/slaves': peers,
    '/api/loxone/home': {schema: 2, revision: 1, resources_installed: true, consumers: [], pages: [], intercom: {}},
};

const controls = 'input:not([type="hidden"]), select, textarea';
const phoneWidths = [320, 390];
const widths = [...phoneWidths, 768, 960, 1280];

async function audit(page, description, gridSelector) {
    const failures = [];
    for (const width of widths) {
        await page.setViewportSize({width, height: 900});
        const report = await page.evaluate(({gridSelector, controls, phone}) => {
            const problems = [];
            let compared = 0;
            const tolerance = 1;
            const visible = element => element.getClientRects().length !== 0;
            const tooWide = document.documentElement.scrollWidth - document.documentElement.clientWidth;
            if (tooWide > tolerance) problems.push('page overflow: ' + tooWide + ' px');
            for (const grid of document.querySelectorAll(gridSelector)) {
                if (!visible(grid)) continue;
                const cells = [];
                for (const item of grid.children) {
                    if (!visible(item)) continue;
                    const input = item.querySelector(controls);
                    const box = item.getBoundingClientRect();
                    cells.push({box, control: input && visible(input) ? input : null});
                    if (!input || !visible(input)) continue;
                    const rect = input.getBoundingClientRect();
                    if (rect.left < box.left - tolerance || rect.right > box.right + tolerance)
                        problems.push((input.id || input.name) + ' outside its field');
                    const label = input.labels?.[0];
                    if (!label) {
                        problems.push((input.id || input.name) + ' has no accessible label');
                        continue;
                    }
                    const walker = document.createTreeWalker(label, NodeFilter.SHOW_TEXT);
                    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
                        // A lenyíló opciói és az önálló jelszótörlés felirata külön vezérlőkhöz tartoznak.
                        if (!node.textContent.trim() || node.parentElement.closest('input,select,textarea') ||
                            node.parentElement.closest('label') !== label) continue;
                        const range = document.createRange();
                        range.selectNodeContents(node);
                        for (const line of range.getClientRects()) {
                            const inline = input.type === 'checkbox' || input.type === 'radio';
                            const horizontalOverlap = line.right > rect.left + tolerance && line.left < rect.right - tolerance;
                            const verticalOverlap = line.bottom > rect.top + tolerance && line.top < rect.bottom - tolerance;
                            if (line.left < box.left - tolerance || line.right > box.right + tolerance ||
                                (inline ? horizontalOverlap && verticalOverlap : line.bottom > rect.top + tolerance))
                                problems.push((input.id || input.name) + ' caption overflows or overlaps its control');
                        }
                    }
                }
                for (let i = 0; i < cells.length; ++i) {
                    for (let j = i + 1; j < cells.length; ++j) {
                        const a = cells[i], b = cells[j];
                        // Azonos vizuális sor: a cellák magassága átfed, vízszintesen külön állnak.
                        if (Math.min(a.box.bottom, b.box.bottom) <= Math.max(a.box.top, b.box.top) + tolerance) continue;
                        if (a.box.right > b.box.left + tolerance && b.box.right > a.box.left + tolerance) continue;
                        if (phone) problems.push('multiple fields on mobile row: ' + [a.control?.id, b.control?.id].join(' / '));
                        if (!a.control || !b.control) continue;
                        ++compared;
                        const offset = Math.abs(a.control.getBoundingClientRect().top - b.control.getBoundingClientRect().top);
                        if (offset > tolerance)
                            problems.push([a.control.id || a.control.name, b.control.id || b.control.name].join(' / ') + ': ' + offset + ' px');
                    }
                }
            }
            return {problems, compared};
        }, {controls, gridSelector, phone: phoneWidths.includes(width)});
        if (!phoneWidths.includes(width) && !report.compared) report.problems.push('no multi-column fields were tested');
        failures.push(...report.problems.map(result => description + ' (' + width + ' px): ' + result));
    }
    return failures;
}

(async () => {
    const browser = await chromium.launch({headless: true, executablePath: process.env.CHROMIUM_EXECUTABLE || undefined});
    try {
        const page = await browser.newPage();
        const failures = [], errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route('http://display.test/**', route => {
            const url = new URL(route.request().url()), path = url.pathname;
            if (path === '/api/templates/home-icon') {
                const id = url.searchParams.get('id');
                assert.match(id, /^[a-z0-9-]+$/);
                return route.fulfill({contentType: 'image/png', path: 'assets/loxone-home/icons/' + id + '.png'});
            }
            if (pages[path]) return route.fulfill({contentType: 'text/html; charset=utf-8', body: pages[path]});
            if (api[path]) return route.fulfill({json: api[path]});
            throw new Error('Unexpected device request: ' + path);
        });
        await page.goto('http://display.test/inverter');
        await page.locator('#slave-' + peer.id + '-name').waitFor();
        assert.equal(await page.locator('#host').inputValue(), '192.168.1.73');
        assert.equal(await page.locator('#port').inputValue(), '8899');
        assert.equal(await page.locator('#device').inputValue(), '1234567890');
        // A bejelentett háromhasábos esetben az IP címkéje valóban sort tör,
        // a sorozatszám már az új sorhoz tartozik. Egy mobil-only javítás ne legyen elég.
        await page.setViewportSize({width: 960, height: 900});
        const reproduced = await page.locator('#host').evaluate(input => {
            const caption = input.labels[0], text = [...caption.childNodes].find(node => node.nodeType === Node.TEXT_NODE);
            const range = document.createRange();
            range.selectNodeContents(text);
            return {lines: range.getClientRects().length,
                positions: ['host', 'port', 'unit', 'device'].map(id => {
                    const rect = document.getElementById(id).getBoundingClientRect();
                    return {x: rect.x, y: rect.y};
                })};
        });
        assert(reproduced.lines >= 2, 'SOLARMAN IP caption must wrap in the three-column case');
        const [host, port, unit, serial] = reproduced.positions;
        assert(host.x < port.x && port.x < unit.x && Math.abs(host.x - serial.x) <= 1 && serial.y > host.y);

        for (const profile of ['sofar_v5', 'deye_v5', 'solplanet_https']) {
            await page.locator('#profile').selectOption(profile);
            if (profile === 'solplanet_https') await page.locator('textarea#trustedPem').waitFor();
            failures.push(...await audit(page, profile, '.grid'));
        }

        await page.goto('http://display.test/settings');
        await page.waitForFunction(() => document.getElementById('DataReceptMode').value === 'loxone');
        const clearPassword = page.locator('[id="wifi.password__clear"]');
        await clearPassword.waitFor();
        const password = page.getByLabel('Wi-Fi jelszó', {exact: true});
        const passwordBox = await password.boundingBox(), clearBox = await clearPassword.boundingBox();
        assert(passwordBox && clearBox && clearBox.y >= passwordBox.y + passwordBox.height);
        await clearPassword.check();
        assert(await password.isDisabled());
        await clearPassword.uncheck();
        assert(await password.isEnabled());
        await page.locator('[id="energy.battery_min_soc_pct"]').waitFor();
        for (const mode of ['loxone', 'mqtt', 'http', 'inverter', 'slave']) {
            await page.locator('#DataReceptMode').selectOption(mode);
            if (mode === 'mqtt') await page.locator('[id="mqtt.password__clear"]').waitFor();
            if (mode === 'http') await page.locator('[id="http.data_timeout_seconds"]').waitFor();
            failures.push(...await audit(page, 'settings/' + mode, '.grid'));
        }

        await page.goto('http://display.test/loxone');
        await page.locator('#intercom-speaker-volume').waitFor();
        await page.locator('#slave-' + peer.id + '-name').waitFor();
        await page.locator('#home-add-consumer').click();
        await page.locator('#home-consumers .card-grid').waitFor();
        await page.locator('#home-add-page').click();
        await page.locator('#home-add-card').click();
        await page.locator('#home-cards .card-grid').first().waitFor();
        failures.push(...await audit(page, 'loxone', '.grid, .card-grid, .intercom-grid'));
        assert.deepEqual(errors, []);
        assert.equal(failures.length, 0, failures.join('\n'));
        console.log('PASS: real inverter, Master/Slave, Loxone and settings controls align by row at five viewport widths');
    } finally {
        await browser.close();
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
