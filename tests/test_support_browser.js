/* A tényleges oldalak támogatási kártyája mobil és asztali Chromiumban. */
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const {chromium} = require('playwright');

const fixtures = spawnSync(process.env.PYTHON || 'python3', ['-c', String.raw`
import json, re, sys
from pathlib import Path
sys.path.insert(0, 'tools')
from prepare_support import render_card
config = {'revolut_me_url': 'https://revolut.me/supporttest',
          'ko_fi_url': 'https://ko-fi.com/supporttest'}
source = Path('main/app_web.c').read_text()
def literals(text):
    return ''.join(json.loads(value) for value in re.findall(r'"(?:[^"\\]|\\.)*"', text))
pages = []
for name in ('settings', 'template-manager', 'loxone'):
    page = Path('assets/web/' + name + '.html').read_text()
    pages.append([name, page.replace('<!--APP_SUPPORT-->', render_card(config, 'hu'))])
for language in ('hu', 'en'):
    match = re.search(r'static const char page_' + language + r'\[\] =\s*(.*?);\n', source, re.S)
    page = literals(match[1])
    pages.append(['update-' + language, page.replace('<!--APP_SUPPORT-->', render_card(config, language))])
diagnostics = source.split('static esp_err_t diagnostics_page(', 1)[1].split('\n}', 1)[0]
page = literals(diagnostics.split('return app_web_send_page(req,', 1)[1].rsplit(', s_hu);', 1)[0])
pages.append(['diagnostics', page.replace('<!--APP_SUPPORT-->', render_card(config, 'hu'))])
root = source.split('"<!doctype html><html lang=', 1)[1].split('HTTPD_RESP_USE_STRLEN);', 1)[0]
page = literals('"<!doctype html><html lang=' + root)
pages.append(['root', page + render_card(config, 'hu') + '<h1>Zolee Energy Flow Display</h1><div class="card">Eszközinformáció</div>' + '</body></html>'])
print(json.dumps(pages))
`], {encoding: 'utf8'});
assert.equal(fixtures.status, 0, fixtures.stderr);

(async () => {
    const browser = await chromium.launch({headless: true});
    try {
        for (const width of [390, 1280]) {
            // A támogatási részhez nincs szükség az adminisztráció JS-ének futására.
            const context = await browser.newContext({viewport: {width, height: 900}, javaScriptEnabled: false});
            const requests = [];
            await context.route('**/*', route => {
                requests.push(route.request().url());
                return route.abort();
            });
            const page = await context.newPage();
            for (const [name, html] of JSON.parse(fixtures.stdout)) {
                await page.setContent(html);
                const card = page.locator('#development-support');
                assert.equal(await card.count(), 1, name);
                assert.equal(await card.isVisible(), true, name);
                const initialBounds = await card.boundingBox();
                const heading = await page.locator('h1').first().boundingBox();
                assert(initialBounds.y >= 0 && initialBounds.y + initialBounds.height <= 900, name + ': support is not in the initial viewport');
                assert(heading.y >= initialBounds.y + initialBounds.height, name + ': support must precede page content');
                assert.equal(await card.locator('a').count(), 2, name);
                const bounds = await card.boundingBox();
                assert(bounds.x >= 0 && bounds.x + bounds.width <= width + 1, name + ': card overflows viewport');
                for (const [index, host] of ['revolut.me', 'ko-fi.com'].entries()) {
                    const link = card.locator('a').nth(index);
                    assert.equal(await link.getAttribute('href'), `https://${host}/supporttest`);
                    assert.equal(await link.getAttribute('target'), '_blank');
                    assert.equal(await link.getAttribute('referrerpolicy'), 'no-referrer');
                    await link.click({trial: true});
                    const box = await link.boundingBox();
                    assert(box.height >= 44 && box.x >= bounds.x && box.x + box.width <= bounds.x + bounds.width + 1);
                }
                assert.equal(requests.length, 0, name + ': unsolicited external request');
            }
            await context.close();
        }
    } finally {
        await browser.close();
    }
    console.log('PASS: support on all six web routes, HU/EN OTA, 390/1280px, no background network or JS dependency');
})().catch(error => {console.error(error); process.exit(1);});
