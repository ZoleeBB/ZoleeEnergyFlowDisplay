/* A keresést csak a gomb indítja. A böngésző a Master feladatát követi;
 * a mezőket kizárólag a felhasználó által kiválasztott találat töltheti ki. */
const inverterDiscovery = (() => {
    const path = '/api/inverter/discovery';
    let current = null, cleanup = Promise.resolve();

    async function request(url, body) {
        const options = body ? {method: 'POST', headers: {'Content-Type': 'application/json', 'X-Zolee-Inverter': '1'},
            body: JSON.stringify(body), keepalive: true} : {cache: 'no-store'};
        const response = await apiFetch(url, options);
        if (!response.ok) throw Error((await response.text()) || ('HTTP ' + response.status));
        return response.json();
    }
    function stop() {
        const search = current;
        current = null;
        if (!search) return;
        clearTimeout(search.timer);
        // Profilváltáskor az indítás válasza is úton lehet. Csak a saját feladatot állítjuk le.
        cleanup = search.started.then(({job}) => request(path + '/cancel', {job})).catch(() => {});
    }
    function controls(busy) {
        get('search-inverter').disabled = busy;
        get('cancel-search').hidden = !busy;
    }
    function reset(message = '') {
        stop();
        controls(false);
        get('discovery-status').textContent = message;
        get('discovery-results').replaceChildren();
    }
    function select(profile) {
        reset();
        get('discovery').hidden = !saved?.discoverable?.includes(profile) || !profiles[profile] || !!profiles[profile].interval;
    }
    function apply(item) {
        // Az adatgyűjtő és az inverter sorozatszáma eltérhet. A device mező csak
        // a profillal ténylegesen használható beállítási azonosítót tartalmazza.
        get('host').value = item.host;
        get('port').value = item.port;
        if (get('unit') && Number.isInteger(item.unit)) get('unit').value = item.unit;
        const device = get('device');
        if (device && device.tagName === 'INPUT' && item.device) device.value = item.device;
        updateEndpointFields();
        let message = 'A kiválasztott eszköz adatai bekerültek a mezőkbe.';
        if (!get('rebind-control').hidden)
            message += ' Ha ezen az IP-címen az invertert vagy az adatgyűjtőt kicserélted, jelöld be az „Új inverter párosítása” mezőt.';
        if (get('profile').value === 'victron_mqtt' && (item.port === 8883 || get('trustedPem')?.value.trim()))
            message += ' Ha a TLS-tanúsítványban az IP-cím helyett a GX neve szerepel, a Cím mezőbe azt a nevet írd.';
        reset(message + ' Ellenőrizd a beállításokat, majd mentsd az űrlapot.');
        get('host').focus();
    }
    function showResults(search, results) {
        const previous = search.rows;
        search.rows = new Map();
        for (const item of results) {
            const key = JSON.stringify([item.host, item.port, item.unit, item.device, item.serial]);
            let entry = previous.get(key);
            if (!entry) {
                const row = el('div'), details = el('span'), choose = el('button', 'Kiválasztás');
                row.className = 'discovery-row'; choose.type = 'button'; choose.className = 'secondary';
                row.append(details, choose); get('discovery-results').append(row);
                entry = {row, details, choose};
            }
            search.rows.set(key, entry);
            previous.delete(key);
            const signature = JSON.stringify(item);
            if (entry.signature === signature) continue;
            entry.signature = signature;
            const address = [item.host + ':' + item.port];
            if (item.serial) address.push('Sorozatszám: ' + item.serial);
            if (item.device && profiles[search.profile]?.device === loggerSerial)
                address.push('Adatgyűjtő száma: ' + item.device);
            if (get('unit') && Number.isInteger(item.unit)) address.push('Modbus-cím: ' + item.unit);
            entry.details.textContent = item.model || 'Helyi eszköz';
            entry.details.append(el('small', address.join(' · ')), el('small', item.verified ?
                'Inverter azonosítva' : 'Eszköz észlelve · az adatok olvasása mentés után ellenőrizhető'));
            entry.choose.setAttribute('aria-label', 'Kiválasztás – ' + [item.model, ...address].filter(Boolean).join(' · '));
            entry.choose.onclick = () => apply(item);
        }
        for (const entry of previous.values()) entry.row.remove();
    }
    async function poll(search, job) {
        try {
            const status = await request(path + '?job=' + job);
            if (current !== search) return;
            if (status.job !== job || status.profile !== search.profile || !Array.isArray(status.results))
                throw Error('A kijelző másik keresés állapotát küldte.');
            search.retries = 0;
            showResults(search, status.results);
            let message;
            if (status.state === 'running') {
                const progress = status.total > 0 ? ' · Ellenőrzött címek: ' + status.checked + '/' + status.total : '';
                get('discovery-status').textContent = 'Keresés a helyi hálózaton…' + progress;
                search.timer = setTimeout(() => poll(search, job), 1000);
                return;
            }
            if (status.state === 'cancelled') message = 'A keresés leállt.';
            else if (status.state === 'failed') message = 'A keresés megszakadt. Ellenőrizd a kijelző hálózati kapcsolatát, majd próbáld újra.';
            else if (status.state === 'done') message = status.results.length ? 'Találatok száma: ' + status.results.length + '.' :
                'Nincs találat. Ellenőrizd a fenti beállításokat, a portot és az eszközök közötti helyi hálózati kapcsolatot. Az elérést kézzel is megadhatod.';
            else throw Error('Ismeretlen keresési állapot.');
            if (status.limited) message += ' A keresés elérte a bejárható címek, az idő vagy a találatok korlátját. Másik eszköz címét kézzel is megadhatod.';
            current = null; controls(false);
            get('discovery-status').textContent = message;
        } catch (error) {
            if (current !== search) return;
            if (++search.retries <= 2) {
                get('discovery-status').textContent = 'Az állapot lekérése sikertelen. Újrapróbálás…';
                search.timer = setTimeout(() => poll(search, job), 1500);
                return;
            }
            reset('A keresés eredménye nem érhető el. Új keresést indíthatsz, vagy megadhatod a címet kézzel. ' + error.message);
        }
    }
    async function start() {
        if (current || saving || get('discovery').hidden) return;
        const profile = get('profile').value, port = get('port'), unit = get('unit');
        if (!port.reportValidity() || (unit && !unit.reportValidity())) return;
        const params = {profile, port: port.valueAsNumber, unit: unit ? unit.valueAsNumber : 0};
        // A mentett sorozatszám a korábbi eszközé is lehet. Kereséshez csak
        // a SolaX protokollváltozatát adjuk át, így a csereeszközök is látszanak.
        if (profile === 'solax_modbus' && get('device').value) params.device = get('device').value;
        reset('A keresés indul…'); controls(true);
        const search = {profile, rows: new Map(), retries: 0, timer: null};
        current = search;
        search.started = cleanup.then(() => request(path, params));
        try {
            const {job} = await search.started;
            if (current !== search) return;
            if (!Number.isSafeInteger(job) || job < 1) throw Error('Érvénytelen keresésazonosító.');
            await poll(search, job);
        } catch (error) {
            if (current !== search) return;
            current = null; controls(false);
            get('discovery-status').textContent = 'A keresés nem indult el. ' + error.message;
        }
    }
    function bind() {
        get('search-inverter').onclick = start;
        get('cancel-search').onclick = () => reset('A keresés leállítását kérted.');
        get('connection').addEventListener('input', event => {
            if (['port', 'unit', 'device', 'host'].includes(event.target.id)) reset();
        });
        window.addEventListener('pagehide', () => reset());
    }
    return {select, reset, bind};
})();
