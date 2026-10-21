/* A párosított kijelzők szerkesztése. A 202 csak átvétel; a tartós mentést
 * a külön visszaolvasott saved_revision igazolja. A draft nem veszhet el. */
const LoxoneSlaves = (() => {
    'use strict';
    const get = id => document.getElementById(id);
    const rows = new Map();
    const keys = ['name', 'presence_uuid', 'timeout_seconds', 'blank_at_night'];
    const profile = value => Object.fromEntries(keys.map(key => [key, value[key]]));
    const same = (a, b) => keys.every(key => a[key] === b[key]);
    let catalog = [], catalogKey = '', state = null, fetching = null;
    let online = false, writing = null, otherBusy = false;

    function create(tag, text) {
        const node = document.createElement(tag);
        if (text !== undefined) node.textContent = text;
        return node;
    }
    function tell(node, text, error = false) {
        node.textContent = text;
        node.className = error ? 'feedback-error' : '';
    }
    function controls() {
        for (const row of rows.values()) {
            const locked = writing === row || (row.pending && !state?.save_error);
            for (const input of Object.values(row.inputs)) input.disabled = !!locked;
            row.save.disabled = !online || !state?.enabled || !state?.ready ||
                otherBusy || !!writing || !!locked || !row.dirty || !row.present;
            row.discard.disabled = !!locked || !online || !row.present;
        }
    }
    function choices(row) {
        const select = row.inputs.presence_uuid, selected = row.draft.presence_uuid;
        const room = get('slave-room').value, category = get('slave-category').value;
        select.replaceChildren();
        const add = (value, text) => {
            const option = create('option', text);
            option.value = value;
            select.append(option);
        };
        add('', 'Csak érintéses ébresztés');
        const seen = new Set();
        for (const item of catalog) {
            if (!item.uuid || seen.has(item.uuid)) continue;
            if (item.uuid !== selected && ((room && JSON.stringify(item.room || '') !== room) ||
                (category && JSON.stringify(item.category || '') !== category))) continue;
            seen.add(item.uuid);
            add(item.uuid, (item.name || 'Névtelen') + ' (' + (item.room || '–') + ' / ' +
                (item.category || '–') + ') · ' + (item.state || 'állapot'));
        }
        if (selected && !seen.has(selected)) add(selected, 'Korábban párosított / nem található: ' + selected);
        select.value = selected;
    }
    function filters() {
        for (const [id, key, title] of [['slave-room', 'room', 'Összes helyiség'],
            ['slave-category', 'category', 'Összes funkció']]) {
            const select = get(id), selected = select.value;
            select.replaceChildren();
            const values = [...new Set(catalog.map(item => item[key] || ''))].sort((a, b) => a.localeCompare(b, 'hu'));
            for (const [value, text] of [['', title], ...values.map(value => [JSON.stringify(value), value || 'Nincs megadva'])]) {
                const option = create('option', text);
                option.value = value;
                select.append(option);
            }
            select.value = values.some(value => JSON.stringify(value) === selected) ? selected : '';
        }
        for (const row of rows.values()) choices(row);
    }
    function load(row, value) {
        row.base = profile(value);
        row.draft = profile(value);
        row.dirty = false;
        row.pending = null;
        for (const key of keys) {
            const input = row.inputs[key];
            if (key === 'blank_at_night') input.checked = value[key];
            else input.value = value[key];
        }
        row.legend.textContent = value.name;
        choices(row);
    }
    function make(value) {
        const row = {id: value.id, inputs: {}, dirty: false, pending: null, present: true};
        row.box = create('fieldset');
        row.legend = create('legend', value.name);
        row.box.append(row.legend);
        row.box.append(create('small', 'Eszköz: ' + value.id));
        const grid = create('div');
        grid.className = 'grid';
        for (const [key, title, type] of [['name', 'Eszköz neve', 'text'],
            ['presence_uuid', 'Loxone jelenlétállapot', 'select'],
            ['timeout_seconds', 'Ébrenlét az utolsó jel / érintés után (másodperc)', 'number'],
            ['blank_at_night', 'Sötétítés éjszaka, ha nincs jelenlét', 'checkbox']]) {
            const label = create('label', title), input = create(type === 'select' ? 'select' : 'input');
            input.id = 'slave-' + value.id + '-' + key;
            if (type !== 'select') input.type = type;
            if (type === 'text') input.maxLength = 80;
            if (type === 'number') { input.min = 1; input.max = 86400; input.step = 1; }
            input.oninput = input.onchange = () => {
                const current = state?.profiles.find(value => value.id === row.id);
                if (row.pending && current && same(current, row.pending)) row.base = profile(current);
                row.draft[key] = type === 'checkbox' ? input.checked : type === 'number' ?
                    (input.value.trim() ? Number(input.value) : NaN) : input.value;
                row.pending = null;
                row.dirty = !same(row.base, row.draft);
                tell(row.status, row.dirty ? 'Még nincs elmentve.' : 'Nincs módosítás.');
                controls();
            };
            row.inputs[key] = input;
            label.append(input);
            grid.append(label);
        }
        row.box.append(grid);
        row.save = create('button', 'Eszköz beállításainak mentése');
        row.discard = create('button', 'Szerkesztés elvetése');
        row.save.id = 'slave-' + value.id + '-save';
        row.discard.id = 'slave-' + value.id + '-discard';
        row.save.type = row.discard.type = 'button';
        row.save.onclick = () => save(row);
        row.discard.onclick = () => {
            if (row.discard.disabled) return;
            const current = state.profiles.find(item => item.id === row.id);
            if (current) { load(row, current); tell(row.status, 'Az eszközről beolvasott beállítások.'); controls(); }
        };
        row.status = create('p');
        row.status.id = 'slave-' + value.id + '-status';
        row.status.setAttribute('role', 'status');
        row.status.setAttribute('aria-live', 'polite');
        row.box.append(row.save);
        row.box.append(row.discard);
        row.box.append(row.status);
        rows.set(row.id, row);
        get('slave-profiles').append(row.box);
        load(row, value);
        return row;
    }
    function apply(next) {
        if (next.schema !== 1 || !Array.isArray(next.profiles)) throw Error('Ismeretlen eszközlista.');
        state = next;
        online = true;
        for (const row of rows.values()) row.present = false;
        for (const value of next.profiles) {
            const row = rows.get(value.id) || make(value);
            row.present = true;
            if (row.pending) {
                if (!same(value, row.pending)) {
                    row.pending = null;
                    tell(row.status, same(value, row.base) ?
                        'A mentés nem igazolt; az eszköz még a korábbi beállítást mutatja. Újrapróbálhatod.' :
                        'A profil közben másik helyen változott. Ellenőrizd, vagy vesd el a szerkesztést.', true);
                } else if (next.save_error) {
                    tell(row.status, 'A tartós mentés sikertelen. A beállítás még nincs biztonságban; újrapróbálhatod a mentést.', true);
                } else if (next.saved_revision === next.revision) {
                    load(row, value);
                    tell(row.status, 'Beállítások tartósan elmentve.');
                } else tell(row.status, 'Beállítások átvéve, tartós mentés folyamatban…');
            } else if (!row.dirty) {
                load(row, value);
                tell(row.status, next.save_error ? 'A Master tartós mentése hibát jelzett.' :
                    next.saved_revision !== next.revision ? 'Tartós mentés folyamatban…' :
                    'Beállítások tartósan elmentve.', !!next.save_error);
            }
            else if (!same(value, row.base)) tell(row.status,
                'A profil közben másik helyen változott. A saját szerkesztésed megmaradt; ellenőrizd, vagy vesd el.', true);
        }
        for (const row of rows.values()) if (!row.present) tell(row.status,
            'Az eszköz most nem érhető el a listában. A szerkesztésed megmaradt.', true);
        let text = !next.enabled ? 'Kapcsold be a Master adat seed funkcióját a Beállítások oldalon.' :
            !next.ready ? 'A Slave-szolgáltatás nem érhető el. Ellenőrizd az eszközt.' :
            !next.profiles.length ? 'Még nincs párosított Slave-kijelző. Indítsd a párosítást a Master diagnosztikai oldalán, a PIN-kóddal.' :
            next.save_error ? 'A tartós mentés hibát jelzett. A Master újrapróbálja; az eszközbeállításoknál is újraindíthatod a mentést.' :
            next.saved_revision !== next.revision ? 'Tartós mentés folyamatban…' : 'A kijelzők beállításai tartósan mentve.';
        tell(get('slave-status'), text, next.enabled && (!next.ready || !!next.save_error));
        controls();
    }
    async function read() {
        if (fetching) return fetching;
        fetching = (async () => {
            try {
                const response = await apiFetch('/api/loxone/slaves', {cache: 'no-store'});
                if (!response.ok) throw Error(await response.text());
                apply(await response.json());
                return true;
            } catch (error) {
                online = false;
                tell(get('slave-status'), 'Az eszközlista nem frissíthető. A szerkesztésed megmaradt. ' + error.message, true);
                controls();
                return false;
            }
        })();
        try { return await fetching; } finally { fetching = null; }
    }
    function validate(value) {
        if (!value.name || new TextEncoder().encode(value.name).length > 80 ||
            ![...value.name].some(char => ![' ', '\u00a0'].includes(char)) ||
            [...value.name].some(char => { const code = char.codePointAt(0); return code < 32 ||
                (code >= 127 && code <= 159) || (code >= 0xd800 && code <= 0xdfff); }))
            return 'Adj meg olvasható, legfeljebb 80 bájtos nevet (az ékezetes betűk több helyet foglalnak).';
        if (!Number.isInteger(value.timeout_seconds) || value.timeout_seconds < 1 || value.timeout_seconds > 86400)
            return 'Az ébrenlét 1 és 86400 közötti egész másodperc legyen.';
        if (value.presence_uuid && !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{16}$/.test(value.presence_uuid))
            return 'Válassz érvényes Loxone jelenlétállapotot.';
        return '';
    }
    async function save(row) {
        if (row.save.disabled) return;
        const desired = profile(row.draft), error = validate(desired);
        if (error) { tell(row.status, error, true); return; }
        writing = row;
        controls();
        try {
            if (!await read() || !state.enabled || !state.ready) return;
            const current = state.profiles.find(value => value.id === row.id);
            if (!current || (!same(current, row.base) && !(row.pending && same(current, row.pending)))) {
                tell(row.status, 'A profil közben változott. Vesd el a szerkesztést, majd ellenőrizd az új beállításokat.', true);
                return;
            }
            let response;
            try {
                response = await apiFetch('/api/loxone/slaves', {method: 'POST',
                    headers: {'Content-Type': 'application/json', 'X-Zolee-Loxone': '1'},
                    body: JSON.stringify({schema: 1, revision: state.revision, id: row.id, profile: desired})});
            } catch (error) {
                /* A válasz elveszhetett a kérés átvétele után. Csak visszaolvasunk. */
                row.pending = desired;
                if (await read()) return;
                throw error;
            }
            if (!response.ok) throw Error(await response.text());
            row.pending = desired;
            tell(row.status, 'Beállítások átvéve, tartós mentés folyamatban…');
            await read();
        } catch (error) {
            /* Elveszett válasznál nincs automatikus újraküldés vagy sikerállítás. */
            tell(row.status, 'A mentés nem igazolt. Ellenőrizd a friss állapotot, majd próbáld újra. ' + error.message, true);
        } finally {
            writing = null;
            controls();
        }
    }
    async function refresh(nextCatalog = [], busy = false) {
        otherBusy = busy;
        const key = JSON.stringify(nextCatalog);
        if (key !== catalogKey) { catalogKey = key; catalog = nextCatalog; filters(); }
        controls();
        if (!writing) await read();
    }
    for (const id of ['slave-room', 'slave-category']) get(id).onchange = () => {
        for (const row of rows.values()) choices(row);
    };
    filters();
    return {refresh};
})();
