'use strict';
const get = id => document.getElementById(id);
const apiFetch = (url, options) => fetch(url, options);
const serial = 'Inverter sorozatszáma';
const pem = 'Megbízható helyi CA tanúsítványa (PEM, opcionális)';
const hostLabel = 'Inverter, adatgyűjtő vagy átjáró IP-címe / gépneve';
const cloudHost = 'A szolgáltató API-gépneve';
const profiles = {
    huawei_modbus: {name: 'Huawei SUN2000 M1 · Modbus TCP', unit: 1},
    goodwe_modbus: {name: 'GoodWe ET · Modbus TCP', unit: 247},
    sungrow_modbus: {name: 'Sungrow SH · Modbus TCP', unit: 1},
    solax_modbus: {name: 'SolaX X1/X3 G4 · Modbus TCP', unit: 1},
    victron_modbus: {name: 'Victron GX · Modbus TCP', unit: 100},
    solaredge_modbus: {name: 'SolarEdge SunSpec · Modbus TCP', port: 1502, unit: 1},
    deye_modbus: {name: 'Deye SG04LP3 · Modbus TCP átjáró', unit: 1},
    deye_rtu: {name: 'Deye SG04LP3 · nyers Modbus RTU TCP-n', port: 8899, unit: 1},
    deye_v5: {name: 'Deye SG04LP3 · SOLARMAN V5', port: 8899, unit: 1, device: 'Adatgyűjtő sorozatszáma (decimális)'},
    solplanet_modbus: {name: 'Solplanet / Tracon ASW-T · Modbus TCP átjáró', unit: 3},
    fronius_http: {name: 'Fronius · helyi Solar API', port: 80},
    solax_http: {name: 'SolaX Pocket · helyi HTTP', port: 80, secret: 'Pocket helyi jelszó'},
    solplanet_http: {name: 'Solplanet / Tracon · helyi HTTP', port: 8484, device: serial},
    solplanet_https: {name: 'Solplanet / Tracon · helyi HTTPS', port: 443, device: serial, trustedPem: pem},
    victron_mqtt: {name: 'Victron GX · helyi MQTT', port: 1883, device: 'VRM Portal ID (12 hexadecimális karakter)',
        user: 'MQTT-felhasználónév (opcionális)', secret: 'MQTT-jelszó (opcionális)', trustedPem: pem},
    fronius_cloud: {name: 'Fronius Solar.web Query API', port: 443, host: 'api.solarweb.com', interval: 5,
        device: 'PV-rendszer azonosítója (UUID)', key: 'AccessKeyId', secret: 'AccessKeyValue'},
    huawei_cloud: {name: 'Huawei FusionSolar Northbound API', port: 443, host: 'eu5.fusionsolar.huawei.com', interval: 60,
        device: serial, user: 'Northbound API-felhasználó', secret: 'Rendszerkód (systemCode)'},
    solax_cloud: {name: 'SolaXCloud v2', port: 443, host: 'global.solaxcloud.com', interval: 5,
        device: 'Wi-Fi modul sorozatszáma (Wi-Fi SN)', secret: 'API-token (tokenId)'},
    solplanet_cloud: {name: 'Solplanet / Tracon · AISWEI Cloud', port: 443, host: 'eu-api-genergal.aisweicloud.com', interval: 20,
        user: 'Fiók AppKey', secret: 'Fiók AppSecret', key: 'Erőmű API-kulcsa (Plant API Key)'}
};
const weekdays = ['Hétfő', 'Kedd', 'Szerda', 'Csütörtök', 'Péntek', 'Szombat', 'Vasárnap'];
let saved, master, loading = false, saving = false;
const overrides = new Map();

function el(tag, text) {
    const node = document.createElement(tag);
    if (text != null) node.textContent = text;
    return node;
}
function field(target, title, type, value, attrs = {}) {
    const label = el('label', title);
    const input = el(type === 'textarea' ? type : 'input');
    if (type !== 'textarea') input.type = type;
    Object.assign(input, attrs);
    if (type === 'checkbox') input.checked = !!value;
    else input.value = value ?? '';
    label.append(input);
    target.append(label);
    return input;
}
function endpointUnchanged() {
    if (!saved || !saved.profile || get('profile').value !== saved.profile) return false;
    return ['host', 'port', 'unit', 'device', 'user'].every(key => {
        const input = get(key);
        return !input || String(saved[key] ?? '') === input.value.trim();
    });
}
function secretRequirements() {
    const same = endpointUnchanged();
    for (const [key, flag] of [['secret', 'hasSecret'], ['key', 'hasKey']]) {
        const input = get(key);
        if (!input) continue;
        const keep = same && saved[flag];
        input.required = !keep && get('profile').value !== 'victron_mqtt';
        input.placeholder = keep ? 'Mentett érték megtartása' : '';
    }
}
function showProfile(initial) {
    const id = get('profile').value, profile = profiles[id];
    const grid = get('connection');
    grid.replaceChildren();
    get('transport-help').textContent = '';
    if (!profile) return;
    const cfg = initial ? saved : {};
    const hint = profile.interval ? 'Felhős lekérdezés ' + profile.interval + ' percenként. A szolgáltatói mérés kora az oldal tetején látható.' :
        'Helyi adatfrissítés 5 másodpercenként.';
    get('transport-help').textContent = hint + (profile.trustedPem ? ' TLS-nél a megadott gépnévnek szerepelnie kell a tanúsítványban.' : '') +
        (id === 'victron_mqtt' ? ' A 8883-as port vagy helyi CA megadása TLS-t kapcsol.' : '') +
        (id === 'solax_http' ? ' A Pocket helyi HTTP-kapcsolata titkosítás nélkül küldi a jelszót.' : '');
    field(grid, profile.host ? cloudHost : hostLabel, 'text', cfg.host ?? profile.host,
        {id: 'host', required: true, maxLength: 127, pattern: '[a-zA-Z0-9.\\-]+', oninput: secretRequirements});
    field(grid, 'Port', 'number', cfg.port ?? profile.port ?? 502, {id: 'port', min: 1, max: 65535,
        step: 1, required: true, oninput: secretRequirements});
    if (profile.unit != null) field(grid, 'Modbus eszközcím (Unit ID)', 'number', cfg.unit ?? profile.unit,
        {id: 'unit', min: 0, max: 255, step: 1, required: true, oninput: secretRequirements});
    if (id === 'solax_modbus') {
        const label = el('label', 'Inverter kivitele'), model = el('select');
        model.id = 'device';
        for (const [value, title] of [['', 'X3-G4 (háromfázisú)'], ['X1-G4', 'X1-G4 (egyfázisú)']]) {
            const option = el('option', title); option.value = value; model.append(option);
        }
        model.value = /^x1/i.test(cfg.device || '') ? 'X1-G4' : '';
        label.append(model); grid.append(label);
    }
    for (const key of ['device', 'user', 'key', 'secret', 'trustedPem']) {
        if (!profile[key]) continue;
        const hidden = key === 'secret' || key === 'key', cert = key === 'trustedPem';
        const input = field(grid, profile[key], hidden ? 'password' : cert ? 'textarea' : 'text', hidden ? '' : cfg[key],
            {id: key, maxLength: cert ? 3071 : hidden ? 159 : 95, autocomplete: hidden ? 'new-password' : 'off',
                required: !cert && !(key === 'user' && id === 'victron_mqtt'), oninput: secretRequirements});
        if (cert) input.rows = 5;
        if (key === 'device' && id === 'deye_v5') input.pattern = '[0-9]{1,10}';
        if (key === 'device' && id === 'victron_mqtt') input.pattern = '[a-fA-F0-9]{12}';
    }
    secretRequirements();
}
function clock(minutes) {
    return String(Math.floor(minutes / 60)).padStart(2, '0') + ':' + String(minutes % 60).padStart(2, '0');
}
function minutes(clockTime) {
    const [hours, mins] = clockTime.split(':').map(Number);
    return hours * 60 + mins;
}
function programEditor(target, source) {
    const program = el('div');
    target.append(program);
    const rows = weekdays.map((day, i) => {
        const time = source?.days?.[i];
        const row = el('div'); row.className = 'week-row';
        const enabled = field(row, day, 'checkbox', time?.enabled);
        const start = field(row, 'Kezdet', 'time', clock(time?.start ?? 1320), {required: true});
        const end = field(row, 'Vége', 'time', clock(time?.end ?? 360), {required: true});
        enabled.onchange = () => { start.disabled = end.disabled = !enabled.checked; };
        enabled.onchange(); program.append(row);
        return {enabled, start, end};
    });
    const wake = field(program, 'Érintés utáni ébrenlét (másodperc)', 'number', source?.touchWakeSeconds || 30,
        {required: true, min: 1, max: 3600, step: 1});
    return {node: program, value: () => ({days: rows.map(({enabled, start, end}) =>
        ({enabled: enabled.checked, start: minutes(start.value), end: minutes(end.value)})), touchWakeSeconds: Number(wake.value)})};
}
function peerPrograms(peers) {
    const ids = new Map(Object.keys(saved.slaveSchedules || {}).map(id => [id, id]));
    for (const peer of peers) ids.set(peer.id, peer.name);
    for (const [id, name] of ids) {
        if (overrides.has(id)) { overrides.get(id).legend.textContent = name + ' · időprogram'; continue; }
        const box = el('fieldset'), legend = el('legend', name + ' · időprogram');
        box.append(legend);
        const previous = saved.slaveSchedules?.[id];
        const own = field(box, 'Saját időprogram és ébresztési idő használata', 'checkbox', !!previous);
        // Az örökölt program szerkesztése a Master aktuális űrlapértékeiből indul.
        const entry = {own, legend, editor: null};
        own.onchange = () => {
            if (own.checked && !entry.editor) entry.editor = programEditor(box, previous || master.value());
            if (entry.editor) {
                entry.editor.node.hidden = !own.checked;
                entry.editor.node.querySelectorAll('input').forEach(input => {
                    if (!own.checked) input.disabled = true;
                    else if (input.type !== 'time') input.disabled = false;
                });
                if (own.checked) entry.editor.node.querySelectorAll('input[type=checkbox]').forEach(input => input.onchange());
            }
        };
        own.onchange();
        overrides.set(id, entry);
        get('slave-programs').append(box);
    }
}
async function readSettings() {
    const response = await apiFetch('/api/inverter', {cache: 'no-store'});
    if (!response.ok) throw Error(await response.text());
    return response.json();
}
function syncStatus(data) {
    const age = data.measurementAgeSeconds;
    const ageText = Number.isFinite(age) ? (age < 60 ? age + ' mp' : Math.floor(age / 60) + ' perc') : 'ismeretlen';
    get('sync-status').textContent = (data.syncStatus || '') + (data.profile ? ' · Mérés kora: ' + ageText : '');
}
function fill(data) {
    saved = data;
    get('profile').value = data.profile || '';
    showProfile(true);
    get('battery').checked = !!data.battery;
    get('latitude').value = data.latitude ?? '';
    get('longitude').value = data.longitude ?? '';
    get('master-program').replaceChildren();
    master = programEditor(get('master-program'), data.schedule);
    overrides.clear(); get('slave-programs').replaceChildren(); peerPrograms([]);
    get('save-inverter').disabled = false;
    syncStatus(data);
}
function values() {
    const id = get('profile').value;
    const data = {profile: id, host: get('host').value.trim(), port: Number(get('port').value),
        unit: Number(get('unit')?.value ?? 0), battery: get('battery').checked,
        latitude: Number(get('latitude').value), longitude: Number(get('longitude').value),
        schedule: master.value(), slaveSchedules: {}};
    if (saved.revision != null) data.revision = saved.revision;
    for (const key of ['device', 'user', 'secret', 'key', 'trustedPem']) {
        const input = get(key);
        if (input && input.value) data[key] = (key === 'key' || key === 'secret') ? input.value : input.value.trim();
    }
    for (const [id, entry] of overrides) if (entry.own.checked) data.slaveSchedules[id] = entry.editor.value();
    return data;
}
async function refresh() {
    if (loading || saving || document.hidden) return;
    loading = true;
    try {
        const data = await readSettings();
        if (!saved) fill(data); else syncStatus(data);
        peerPrograms(await LoxoneSlaves.refresh());
    } catch (error) {
        get('sync-status').textContent = 'A kijelző nem olvasható. ' + error.message;
    } finally { loading = false; }
}
for (const [id, profile] of Object.entries(profiles)) {
    const option = el('option', profile.name); option.value = id; get('profile').append(option);
}
get('profile').onchange = () => showProfile(get('profile').value === saved?.profile);
get('inverter-form').onsubmit = async event => {
    event.preventDefault();
    if (saving || !saved || !get('inverter-form').reportValidity()) return;
    const data = values();
    if (data.profile === 'deye_v5' && (!Number(data.device) || Number(data.device) > 4294967295)) {
        get('result').textContent = 'Az adatgyűjtő sorozatszáma 1 és 4294967295 közötti egész szám lehet.'; return;
    }
    saving = true; get('save-inverter').disabled = true;
    get('result').textContent = 'Mentés folyamatban…';
    try {
        const response = await apiFetch('/api/inverter', {method: 'POST', headers: {'Content-Type': 'application/json',
            'X-Zolee-Inverter': '1'}, body: JSON.stringify(data)});
        if (!response.ok) throw Error(await response.text());
        for (const key of ['secret', 'key']) if (get(key)) get(key).value = '';
        fill(await readSettings());
        peerPrograms(await LoxoneSlaves.refresh());
        get('result').textContent = 'Mentve. A kijelző szinkronizálja az adatokat.';
    } catch (error) {
        get('result').textContent = 'A mentés nem igazolt. ' + error.message;
    } finally { saving = false; get('save-inverter').disabled = false; }
};
refresh();
setInterval(refresh, 15000);
