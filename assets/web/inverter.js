'use strict';
const get = id => document.getElementById(id);
function apiFetch(url, options = {}) {
    // A kapcsolat vagy a JSON-törzs elakadása után a felületen újrapróbálható a kérés.
    if (typeof AbortSignal.timeout === 'function')
        return fetch(url, {...options, signal: AbortSignal.timeout(15000)});
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 15000);
    return fetch(url, {...options, signal: controller.signal}).finally(() => clearTimeout(timer));
}
const serial = 'Inverter sorozatszáma';
const pem = 'Megbízható helyi CA tanúsítványa (PEM, opcionális)';
const hostLabel = 'Inverter, adatgyűjtő vagy átjáró IP-címe / gépneve';
const cloudHost = 'A szolgáltató API-gépneve';
const loggerSerial = 'Adatgyűjtő sorozatszáma (decimális)';
const setupHelp = {
    huawei_modbus: 'A FusionSolar helyi üzembe helyezési felületén nyisd meg a Settings → Communication configuration → Dongle parameter settings menüt, és engedélyezd a Modbus TCP-t az SDongleA-05 modulon. Korlátozott hozzáférésnél add az engedélyezett címekhez a master kijelző IP-címét.',
    goodwe_modbus: 'Az ET/EH inverterhez helyi Modbus TCP-t támogató LAN vagy Wi-Fi/LAN modul kell. A SolarGo alkalmazásban engedélyezd a Modbus-TCP opciót. Az ezt használó modulokon a kapcsoló a Settings → Communication Setting → Privacy & Security menüben található.',
    sungrow_modbus: 'Az SH5.0/6.0/8.0/10RT és RT-20 típusokon az inverter LAN feliratú Ethernet-csatlakozóját kösd a helyi hálózathoz. A port és a Modbus eszközcím egyezzen az inverter beállításával.',
    solax_modbus: 'Válaszd ki az inverter pontos kivitelét. A kijelzővel közös hálózatra Modbus TCP-t támogató Pocket modult vagy Modbus TCP–RS485 átjárót csatlakoztass. Az eszközcím az inverteren beállított Modbus-cím.',
    victron_modbus: 'A GX-en válaszd a Settings → Integrations → Modbus TCP Server menüt, és engedélyezd a szervert. Régebbi rendszeren a kapcsoló a Services menüben található. Az Available services listában ellenőrizd a com.victronenergy.system egységcímét. Az olvasási jogosultság elegendő.',
    solaredge_modbus: 'A SetApp üzembe helyezési felületén válaszd a Site Communication → Modbus TCP → Enable beállítást, majd ellenőrizd a portot (gyárilag 1502). Az inverter egyszerre egy Modbus TCP klienst szolgál ki.',
    deye_modbus: 'A Deye SG04LP3 monitoring RS485-csatlakozására kötött átjárón a Modbus TCP–RTU protokollátalakítást válaszd. Az átjáró portját és az inverter RS485-eszközcímét add meg.',
    deye_rtu: 'Az inverter monitoring RS485-portjára kötött átjárót állítsd átlátszó TCP-szerver módba. Ezen a kapcsolaton teljes Modbus RTU keretek mennek át. Az átjáró soros beállításai egyezzenek az inverterével.',
    deye_lse3: 'A SOLARMAN LSE-3 Ethernet-adatgyűjtőt csatlakoztasd a helyi hálózatra. Ez a profil a RAW keretezésű modult kezeli. Ellenőrizd az adatgyűjtő portját (jellemzően 8899) és a Deye eszközcímét.',
    deye_v5: 'A Deye inverter SOLARMAN V5-kompatibilis adatgyűjtőjét csatlakoztasd a helyi hálózatra. A port jellemzően 8899. Az adatgyűjtő címkéjén lévő decimális sorozatszámot és az inverter Modbus-címét használd.',
    solplanet_modbus: 'Az ASW8000-T RS485-ös kivitelét Modbus TCP–RTU átjárón keresztül csatlakoztasd. Az MB001 szerinti gyári eszközcím 3. A portot az átjárón ellenőrizd. Ennél a hálózati inverternél hagyd üresen a „Van akkumulátor” mezőt.',
    sofar_modbus: 'A HYD 5/6/8/10/15/20KTL-3PH monitoring RS485-csatlakozásához Modbus TCP–RTU átjáró szükséges. Ellenőrizd az átjáró TCP-portját és az inverter kommunikációs eszközcímét. A gyári eszközcím 1.',
    sofar_v5: 'A HYD 5/6/8/10/15/20KTL-3PH inverterhez csatlakozó adatgyűjtőnek támogatnia kell a helyi SOLARMAN V5 protokollt. A logger címkéjéről olvasd le a decimális sorozatszámot. Az eszközcím az inverter monitoring címe, a port jellemzően 8899.',
    fronius_http: 'A Primo/Symo GEN24 webfelületén nyisd meg a Communication → Solar API menüt, és kapcsold be az „Activate communication via Solar API” opciót. Több inverter esetén add meg annak a Solar API-azonosítóját, amelyikhez az akkumulátor csatlakozik.',
    solax_http: 'A két MPPT-s X3-Hybrid G4 Pocket modulját csatlakoztasd a ház hálózatára. Ehhez a profilhoz a 14-es helyi adatprotokoll és a Pocket helyi jelszava szükséges. Egyes moduloknál a beállított jelszó a regisztrációs szám. A jelszó HTTP-n, titkosítás nélkül jut el a modulhoz.',
    solplanet_http: 'Csatlakoztasd a helyi HTTP API-t kínáló adatgyűjtőt a ház hálózatához. A V2 stick portja jellemzően 8484. A sorozatszámot az inverter címkéjéről vagy a felismert inverter adataiból vedd át. Az ASW8000-T esetén hagyd üresen a „Van akkumulátor” mezőt.',
    solplanet_https: 'Használd a helyi HTTPS API-t támogató V2 adatgyűjtő tanúsítványában szereplő nevet vagy IP-címet és az inverter sorozatszámát. Saját hitelesítésszolgáltató esetén a telepítőtől kapott, ellenőrzött PEM-tanúsítvány kell. Az ASW8000-T típushoz hagyd üresen az akkumulátor jelölőjét.',
    victron_mqtt: 'A GX-en legalább Venus OS 3.50 szükséges. A Settings → Integrations → MQTT Access (régebben Services → MQTT on LAN) menüben engedélyezd a helyi MQTT-t. A 12 karakteres VRM Portal ID-t a GX VRM-beállításaiban találod. A 8883-as port vagy helyi CA megadása TLS-t kapcsol. TLS-hez a tanúsítványban szereplő nevet add meg.',
    fronius_cloud: 'A Solar.web rendszeredhez aktív Query API-hozzáférés és adatcsomag szükséges. A PV-rendszer UUID-jét és a Query API-hoz tartozó AccessKeyId és AccessKeyValue kulcspárt add meg.',
    huawei_cloud: 'A FusionSolar telepítőjétől kérj Northbound API-fiókot a telephelyhez, történeti eszközadatok olvasására jogosultsággal. A telepítő a System → Company Management → Northbound Management menüben rendelheti a fiókhoz a telephelyet. A régiós kiszolgáló, az API-felhasználónév, a systemCode és az inverter sorozatszáma szükséges.',
    solax_cloud: 'A SolaXCloud Service → API oldalán ellenőrizd a régiós kiszolgálót és a v2 tokenId hozzáférést. A Wi-Fi modul regisztrációs számát add meg. A fejlesztői portál más API-verzióhoz kiadott kulcsa ezzel a profillal nem használható.',
    solplanet_cloud: 'Az AISWEI/Solplanet ügyfélszolgálatától igényeld a lakossági API-hozzáférést. A fiók AppKey és AppSecret kulcsait az Account and security → Account settings alatt, a Plant API Key kulcsot a választott erőmű adatainál találod. A kijelző időzónája egyezzen az erőműével.'
};
const profiles = {
    huawei_modbus: {name: 'Huawei SUN2000 L1/M1 · Modbus TCP', unit: 1},
    goodwe_modbus: {name: 'GoodWe ET/EH · Modbus TCP', unit: 247},
    sungrow_modbus: {name: 'Sungrow SH · Modbus TCP', unit: 1},
    solax_modbus: {name: 'SolaX X1/X3 G4 · Modbus TCP', unit: 1},
    victron_modbus: {name: 'Victron GX · Modbus TCP', unit: 100},
    solaredge_modbus: {name: 'SolarEdge SunSpec · Modbus TCP', port: 1502, unit: 1},
    deye_modbus: {name: 'Deye SG04LP3 · Modbus TCP átjáró', unit: 1},
    deye_rtu: {name: 'Deye SG04LP3 · általános RTU-over-TCP átjáró', port: 8899, unit: 1},
    deye_lse3: {name: 'Deye SG04LP3 · SOLARMAN LSE-3 RAW adatgyűjtő', port: 8899, unit: 1},
    deye_v5: {name: 'Deye SG04LP3 · SOLARMAN V5', port: 8899, unit: 1, device: loggerSerial},
    solplanet_modbus: {name: 'Solplanet ASW8000-T (ASW-T) · Modbus TCP átjáró', unit: 3},
    sofar_modbus: {name: 'SOFAR HYD 5–20KTL-3PH · Modbus TCP átjáró', unit: 1},
    sofar_v5: {name: 'SOFAR HYD 5–20KTL-3PH · SOLARMAN V5', port: 8899, unit: 1, device: loggerSerial},
    fronius_http: {name: 'Fronius · helyi Solar API', port: 80, device: 'Inverterazonosító az akkutöltöttséghez (több inverter esetén)'},
    solax_http: {name: 'SolaX Pocket · helyi HTTP', port: 80, secret: 'Pocket helyi jelszó'},
    solplanet_http: {name: 'Solplanet ASW8000-T · helyi HTTP', port: 8484, device: serial},
    solplanet_https: {name: 'Solplanet ASW8000-T · helyi HTTPS', port: 443, device: serial, trustedPem: pem},
    victron_mqtt: {name: 'Victron GX · helyi MQTT', port: 1883, device: 'VRM Portal ID (12 hexadecimális karakter)',
        user: 'MQTT-felhasználónév (opcionális)', secret: 'MQTT-jelszó (opcionális)', trustedPem: pem},
    fronius_cloud: {name: 'Fronius Solar.web Query API', port: 443, host: 'api.solarweb.com', interval: 5,
        device: 'PV-rendszer azonosítója (UUID)', key: 'AccessKeyId', secret: 'AccessKeyValue'},
    huawei_cloud: {name: 'Huawei FusionSolar Northbound API', port: 443, host: 'eu5.fusionsolar.huawei.com', interval: 60,
        device: serial, user: 'Northbound API-felhasználó', secret: 'Rendszerkód (systemCode)'},
    solax_cloud: {name: 'SolaXCloud v2', port: 443, host: 'global.solaxcloud.com', interval: 5,
        device: 'Wi-Fi modul sorozatszáma (Wi-Fi SN)', secret: 'API-token (tokenId)'},
    solplanet_cloud: {name: 'Solplanet ASW8000-T · AISWEI Cloud', port: 443, host: 'eu-api-genergal.aisweicloud.com', interval: 20,
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
        if (!input) return true;
        const before = String(saved[key] ?? ''), after = input.value.trim();
        if (key === 'host') return before.toLowerCase().replace(/\.$/, '') === after.toLowerCase().replace(/\.$/, '');
        return before === after;
    });
}
function updateEndpointFields() {
    const same = endpointUnchanged();
    const octets = (get('host')?.value.trim() || '').split('.');
    const numericLan = octets.length === 4 && octets.every(octet =>
        /^(0|[1-9][0-9]{0,2})$/.test(octet) && Number(octet) <= 255);
    const rebind = same && numericLan && !profiles[saved.profile]?.interval;
    get('rebind-control').hidden = !rebind;
    if (!rebind) get('rebind-device').checked = false;
    for (const [key, flag] of [['secret', 'hasSecret'], ['key', 'hasKey']]) {
        const input = get(key);
        if (!input) continue;
        const keep = same && saved[flag];
        input.required = !keep && get('profile').value !== 'victron_mqtt';
        input.placeholder = keep ? 'Mentett érték megtartása' : '';
    }
}
function updateSaveButton() {
    get('inverter-fields').disabled = saving;
    get('inverter-form').setAttribute('aria-busy', String(saving));
    get('save-inverter').disabled = saving || !saved || !inverterRegisters.isReady();
}
function showProfile(initial) {
    const id = get('profile').value, profile = profiles[id];
    inverterDiscovery.select(id);
    inverterRegisters.select(id);
    const grid = get('connection');
    grid.replaceChildren();
    get('transport-help').textContent = '';
    get('setup-help').textContent = setupHelp[id] || '';
    get('rebind-control').hidden = true;
    get('rebind-device').checked = false;
    if (!profile) return;
    const cfg = initial ? saved : {};
    const hint = profile.interval ? 'Felhős lekérdezés ' + profile.interval + ' percenként. A szolgáltatói mérés kora az oldal tetején látható.' :
        'Helyi adatfrissítés 5 másodpercenként. Változó IP-címnél a helyi hálózaton feloldható gépnevet is megadhatod.';
    get('transport-help').textContent = hint + (!profile.interval && get('discovery').hidden ?
        ' Ennél a kapcsolattípusnál az eszköz címét kézzel add meg.' : '');
    field(grid, profile.host ? cloudHost : hostLabel, 'text', cfg.host ?? profile.host,
        {id: 'host', required: true, maxLength: 127, pattern: '[a-zA-Z0-9.\\-]+', oninput: updateEndpointFields});
    field(grid, 'Port', 'number', cfg.port ?? profile.port ?? 502, {id: 'port', min: 1, max: 65535,
        step: 1, required: true, oninput: updateEndpointFields});
    if (profile.unit != null) field(grid, 'Modbus eszközcím (Unit ID)', 'number', cfg.unit ?? profile.unit,
        {id: 'unit', min: 0, max: 255, step: 1, required: true, oninput: updateEndpointFields});
    if (id === 'solax_modbus') {
        const label = el('label', 'Inverter kivitele'), model = el('select');
        model.id = 'device';
        model.onchange = () => {
            updateEndpointFields();
            get('setup-help').textContent = setupHelp[id] + (model.value === 'X3-G4-PRO' ?
                ' A gyártó az X3-HYB-G4 PRO Modbus TCP-kapcsolatához vezetékes LAN-t ír elő.' : '');
            inverterRegisters.variantChanged();
        };
        for (const [value, title] of [['', 'X3-G4 (háromfázisú)'], ['X1-G4', 'X1-G4 (egyfázisú)'],
                ['X3-G4-PRO', 'X3-HYB-G4 PRO (háromfázisú)']]) {
            const option = el('option', title); option.value = value; model.append(option);
        }
        model.value = /^x1/i.test(cfg.device || '') ? 'X1-G4' : cfg.device === 'X3-G4-PRO' ? 'X3-G4-PRO' : '';
        label.append(model); grid.append(label); model.onchange();
    }
    for (const key of ['device', 'user', 'key', 'secret', 'trustedPem']) {
        if (!profile[key]) continue;
        const hidden = key === 'secret' || key === 'key', cert = key === 'trustedPem';
        const input = field(grid, profile[key], hidden ? 'password' : cert ? 'textarea' : 'text', hidden ? '' : cfg[key],
            {id: key, maxLength: cert ? 3071 : hidden ? 159 : 95, autocomplete: hidden ? 'new-password' : 'off',
                required: !cert && !(key === 'user' && id === 'victron_mqtt') && !(key === 'device' && id === 'fronius_http'), oninput: updateEndpointFields});
        if (cert) input.rows = 5;
        if (key === 'device' && id === 'fronius_http') input.pattern = '[0-9]+';
        if (key === 'device' && profile.device === loggerSerial) input.pattern = '[0-9]{1,10}';
        if (key === 'device' && id === 'victron_mqtt') input.pattern = '[a-fA-F0-9]{12}';
    }
    updateEndpointFields();
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
async function readSettings(statusOnly = false) {
    const response = await apiFetch(statusOnly ? '/api/inverter?status=1' : '/api/inverter', {cache: 'no-store'});
    if (!response.ok) throw Error(await response.text());
    return response.json();
}
function syncStatus(data) {
    const age = data.measurementAgeSeconds;
    const ageText = Number.isFinite(age) ? (age < 60 ? age + ' mp' : Math.floor(age / 60) + ' perc') : 'ismeretlen';
    const status = [data.syncStatus];
    if (data.profile) status.push('Mérés kora: ' + ageText);
    if (data.activeIpv4) status.push('Legutóbb igazolt helyi IP-cím: ' + data.activeIpv4);
    get('sync-status').textContent = status.filter(Boolean).join(' · ');
}
function fill(data) {
    saved = data;
    inverterRegisters.reset();
    get('profile').value = data.profile || '';
    showProfile(true);
    get('battery').checked = !!data.battery;
    get('latitude').value = data.latitude ?? '';
    get('longitude').value = data.longitude ?? '';
    get('master-program').replaceChildren();
    master = programEditor(get('master-program'), data.schedule);
    overrides.clear(); get('slave-programs').replaceChildren(); peerPrograms([]);
    updateSaveButton();
    syncStatus(data);
}
function values() {
    const id = get('profile').value;
    const data = {profile: id, host: get('host').value.trim(), port: Number(get('port').value),
        unit: Number(get('unit')?.value ?? (saved.profile === id ? saved.unit : 0) ?? 0), battery: get('battery').checked,
        latitude: Number(get('latitude').value), longitude: Number(get('longitude').value),
        schedule: master.value(), slaveSchedules: {}};
    if (profiles[id]?.unit != null) data.registerOverrides = inverterRegisters.values();
    if (saved.revision != null) data.revision = saved.revision;
    if (!get('rebind-control').hidden && get('rebind-device').checked) data.rebindDevice = true;
    for (const key of ['device', 'user', 'secret', 'key', 'trustedPem']) {
        const input = get(key);
        if (input && (input.value || key === 'trustedPem')) data[key] = (key === 'key' || key === 'secret') ? input.value : input.value.trim();
    }
    for (const [id, entry] of overrides) if (entry.own.checked) data.slaveSchedules[id] = entry.editor.value();
    return data;
}
async function refresh() {
    if (loading || saving || document.hidden) return;
    loading = true;
    try {
        if (saved) syncStatus(await readSettings(true));
        else fill(await readSettings());
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
    if (saving || !saved || !inverterRegisters.isReady() || !get('inverter-form').reportValidity()) return;
    const data = values();
    if (profiles[data.profile]?.device === loggerSerial && (!Number(data.device) || Number(data.device) > 4294967295)) {
        get('result').textContent = 'Az adatgyűjtő sorozatszáma 1 és 4294967295 közötti egész szám lehet.'; return;
    }
    get('rebind-device').checked = false;
    inverterDiscovery.reset();
    saving = true; updateSaveButton();
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
    } finally { saving = false; updateSaveButton(); }
};
inverterDiscovery.bind();
refresh();
setInterval(refresh, 15000);
