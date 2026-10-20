/* Jelszómentes Otthon-szerkesztő. A háttérfrissítés nem írja felül a szerkesztést. */
const LoxoneHome = (() => {
    'use strict';
    const icons = /*HOME_ICONS*/;
    const get = id => document.getElementById(id);
    let model = {schema: 1, consumers: [], cards: []};
    let catalog = [], catalogKey = '', loaded = false, fetching = false;
    let pending = false, waiting = false, remoteBusy = false, connectionBusy = false;
    let revision = 0, saveRevision = 0, currentSection = '';
    const domId = (entry, key) => 'home-' + currentSection + '-' + entry.id + '-' + key;
    const controls = [];

    function notify(text, error = false) {
        const node = get('home-status');
        node.textContent = text;
        node.className = error ? 'feedback-error' : '';
    }
    function create(tag, text) {
        const node = document.createElement(tag);
        if (text !== undefined) node.textContent = text;
        return node;
    }
    function busy() { return !loaded || pending || waiting || remoteBusy || connectionBusy; }
    function buttons() {
        for (const control of controls) control.disabled = busy() || !!control.homeFixed;
        get('home-save').disabled = busy();
        get('home-add-consumer').disabled = busy() || model.consumers.length >= 12;
        get('home-add-card').disabled = busy() || model.cards.length >= 12;
    }
    function field(parent, title, entry, key, type = 'text', optional = false) {
        const label = create('label', title), input = create('input');
        input.type = type;
        if (type === 'checkbox') input.checked = !!entry[key];
        else input.value = entry[key] ?? '';
        if (type === 'number') input.step = 'any';
        if (type === 'text') input.maxLength = key === 'name' ? 80 : key === 'unit' ? 16 : 36;
        input.id = domId(entry, key);
        input.oninput = input.onchange = () => {
            entry[key] = type === 'checkbox' ? input.checked : type === 'number' ?
                (optional && !input.value.trim() ? null : input.value.trim() ? Number(input.value) : NaN) : input.value;
        };
        controls.push(input);
        label.append(input); parent.append(label);
        return input;
    }
    function select(parent, title, entry, key, choices) {
        const label = create('label', title), input = create('select');
        input.id = domId(entry, key);
        for (const [value, text] of choices) {
            const option = create('option', text); option.value = value; input.append(option);
        }
        if (entry[key] && !choices.some(([value]) => value === entry[key])) {
            const retained = create('option', 'Korábban párosított / nem található: ' + entry[key]);
            retained.value = entry[key]; input.append(retained);
        }
        input.value = entry[key] ?? '';
        input.onchange = () => { entry[key] = input.value; };
        controls.push(input); label.append(input); parent.append(label);
        return input;
    }
    function stateChoices(control = false, pulse = false) {
        const choices = [['', 'Nincs hozzárendelve']], seen = new Set();
        const room = get('home-room').value, category = get('home-category').value;
        for (const item of catalog) {
            if ((room && JSON.stringify(item.room || '') !== room) ||
                (category && JSON.stringify(item.category || '') !== category)) continue;
            if (control && !(item.secured === false && (item.type === 'Switch' ||
                (pulse && ['Pushbutton', 'TimedSwitch'].includes(item.type))))) continue;
            const uuid = control ? item.control_uuid : item.uuid;
            if (!uuid || seen.has(uuid)) continue;
            seen.add(uuid);
            choices.push([uuid, (item.name || 'Névtelen') + ' (' + (item.room || '–') + ' / ' +
                (item.category || '–') + ')' + (control ? ' · vezérlés' : ' · ' + (item.state || 'állapot'))]);
        }
        return choices;
    }
    function source(parent, entry) {
        const wrapper = {id: entry.id, uuid: entry.source?.uuid || ''};
        const input = select(parent, 'Loxone visszajelző állapot', wrapper, 'uuid', stateChoices());
        input.onchange = () => { entry.source.uuid = input.value; };
        const scale = field(parent, 'Szorzó (W → kW: 0.001)', entry.source, 'scale', 'number');
        scale.id = domId(entry, 'scale');
        if (entry.type === 'value') {
            const offset = field(parent, 'Eltolás', entry.source, 'offset', 'number');
            offset.id = domId(entry, 'offset');
        }
        if (entry.type === 'switch') scale.homeFixed = true;
    }
    function render() {
        controls.length = 0;
        for (const section of ['consumers', 'cards']) {
            currentSection = section;
            const target = get('home-' + section); target.replaceChildren();
            for (const entry of model[section]) {
                const box = create('fieldset'), legend = create('legend', entry.name || 'Új elem');
                box.append(legend);
                const grid = create('div'); grid.className = 'grid'; box.append(grid);
                field(grid, 'Név', entry, 'name');
                select(grid, 'Ikon', entry, 'icon', icons.map(icon => [icon.id, icon.hu]));
                if (section === 'consumers') {
                    source(grid, entry);
                    field(grid, 'Átlagos üzemi teljesítmény (kW, opcionális)', entry, 'average_kw', 'number', true);
                    field(grid, 'Indítás javasolható (alapból kikapcsolva)', entry, 'suggest_start', 'checkbox');
                } else {
                    const type = select(grid, 'Kártyatípus', entry, 'type', [
                        ['value', 'Érték'], ['pulse', 'Pillanatnyi gomb'], ['switch', 'Be/ki kapcsoló']]);
                    type.onchange = () => {
                        entry.type = type.value;
                        if (entry.type === 'switch') { entry.source.scale = 1; entry.source.offset = 0; }
                        render();
                    };
                    if (entry.type !== 'pulse') source(grid, entry);
                    if (entry.type !== 'value')
                        select(grid, 'Vezérelhető Loxone objektum', entry, 'control_uuid', stateChoices(true, entry.type === 'pulse'));
                    if (entry.type === 'value') {
                        field(grid, 'Mértékegység', entry, 'unit');
                        const decimals = field(grid, 'Tizedesek (0–3)', entry, 'decimals', 'number');
                        decimals.min = 0; decimals.max = 3; decimals.step = 1;
                        field(grid, 'Alsó figyelmeztetési határ (opcionális)', entry, 'warning_low', 'number', true);
                        field(grid, 'Felső figyelmeztetési határ (opcionális)', entry, 'warning_high', 'number', true);
                        field(grid, 'Figyelmeztetés színe', entry, 'warning_color', 'color');
                    }
                }
                const remove = create('button', 'Elem törlése'); remove.type = 'button';
                remove.onclick = () => { model[section] = model[section].filter(item => item.id !== entry.id); render(); };
                controls.push(remove); box.append(remove); target.append(box);
            }
        }
        buttons();
    }
    function filters() {
        for (const [id, key, title] of [['home-room', 'room', 'Összes helyiség'], ['home-category', 'category', 'Összes funkció']]) {
            const node = get(id), previous = node.value;
            node.replaceChildren();
            const blank = create('option', title); blank.value = ''; node.append(blank);
            const values = [...new Set(catalog.map(item => item[key] || ''))].sort((a,b) => a.localeCompare(b, 'hu'));
            for (const value of values) {
                const option = create('option', value || 'Nincs megadva'); option.value = JSON.stringify(value); node.append(option);
            }
            node.value = values.some(value => JSON.stringify(value) === previous) ? previous : '';
        }
    }
    function add(section) {
        if (busy() || model[section].length >= 12) return;
        let serial = 1;
        while (model[section].some(item => item.id === 'item-' + serial)) serial++;
        const entry = {id: 'item-' + serial, name: '', icon: icons[0].id, source: {uuid: '', scale: 1, offset: 0}};
        if (section === 'consumers') Object.assign(entry, {average_kw: null, suggest_start: false});
        else Object.assign(entry, {type: 'value', control_uuid: '', unit: '', decimals: 1,
            warning_low: null, warning_high: null, warning_color: '#E66B6B'});
        model[section].push(entry); render();
    }
    const uuidValid = value => /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{16}$/i.test(value || '');
    function validate() {
        for (const section of ['consumers', 'cards']) for (const entry of model[section]) {
            const title = entry.name || 'Névtelen elem';
            if (!entry.name.trim() || new TextEncoder().encode(entry.name).length > 80 || /[\x00-\x1f\x7f]/.test(entry.name)) return title + ': adj meg legfeljebb 80 bájtos nevet.';
            if (!icons.some(icon => icon.id === entry.icon)) return title + ': válassz ikont.';
            if (entry.type !== 'pulse' && (!uuidValid(entry.source.uuid) || !Number.isFinite(entry.source.scale) ||
                !entry.source.scale || Math.abs(entry.source.scale) > 1e6 || !Number.isFinite(entry.source.offset) || Math.abs(entry.source.offset) > 1e6))
                return title + ': hiányzó állapotpárosítás vagy érvénytelen átalakítás.';
            if (section === 'consumers') {
                if (entry.source.offset !== 0) return title + ': a fogyasztói teljesítmény eltolása nulla legyen.';
                if (entry.average_kw !== null && (!Number.isFinite(entry.average_kw) || entry.average_kw <= 0 || entry.average_kw > 1000)) return title + ': az üzemi teljesítmény 0 és 1000 kW közötti pozitív szám legyen.';
            } else {
                if (entry.type !== 'value' && !uuidValid(entry.control_uuid)) return title + ': válassz vezérelhető objektumot; az állapot UUID nem parancscél.';
                if (entry.type === 'switch' && (entry.source.scale !== 1 || entry.source.offset !== 0)) return title + ': a kapcsoló állapota nem skálázható.';
                if (entry.type === 'value' && (!Number.isInteger(entry.decimals) || entry.decimals < 0 || entry.decimals > 3 ||
                    new TextEncoder().encode(entry.unit || '').length > 16 ||
                    /[\x00-\x1f\x7f]/.test(entry.unit || '') ||
                    !/^#[0-9a-f]{6}$/i.test(entry.warning_color) ||
                    [entry.warning_low, entry.warning_high].some(value => value !== null && !Number.isFinite(value)) ||
                    (entry.warning_low !== null && entry.warning_high !== null && entry.warning_low > entry.warning_high)))
                    return title + ': érvénytelen mértékegység, tizedes vagy figyelmeztetési határ.';
            }
        }
        return '';
    }
    async function refresh(nextCatalog = [], otherBusy = false) {
        if (!get('home-editor') || fetching) return;
        fetching = true; connectionBusy = otherBusy;
        try {
            const response = await apiFetch('/api/loxone/home', {cache: 'no-store'});
            if (!response.ok) throw Error(await response.text());
            const state = await response.json();
            const first = !loaded;
            if (first) {
                model = {schema: 1, consumers: state.consumers || [], cards: state.cards || []};
                for (const entry of model.cards) {
                    entry.source ||= {uuid: '', scale: 1, offset: 0};
                    entry.control_uuid ||= '';
                    entry.unit ??= '';
                    entry.decimals ??= 1;
                    entry.warning_low ??= null;
                    entry.warning_high ??= null;
                    entry.warning_color ||= '#E66B6B';
                }
                loaded = true;
            }
            revision = state.revision; remoteBusy = !!state.busy;
            if (waiting && !remoteBusy) {
                waiting = false;
                notify(state.status || (revision > saveRevision ? 'Otthon-beállítások elmentve.' : 'A mentés nem fejeződött be.'), revision <= saveRevision);
            } else if (first) notify(state.status || 'Legfeljebb 12 fogyasztó és 12 Otthon-kártya állítható be.');
            const nextKey = JSON.stringify(nextCatalog);
            if (first || nextKey !== catalogKey) {
                catalog = nextCatalog; catalogKey = nextKey; filters(); render();
            }
        } catch (error) {
            notify('Otthon-beállítások: ' + (error.name === 'AbortError' ? 'az eszköz nem válaszol.' : error.message), true);
        } finally { fetching = false; buttons(); }
    }
    async function save() {
        if (busy()) return;
        const error = validate(); if (error) { notify(error, true); return; }
        pending = true; buttons(); notify('Otthon-beállítások mentése…');
        saveRevision = revision;
        try {
            const response = await apiFetch('/api/loxone/home', {method: 'POST',
                headers: {'Content-Type': 'application/json', 'X-Zolee-Loxone': '1'}, body: JSON.stringify(model)});
            if (!response.ok) throw Error(await response.text());
            waiting = true;
            await refresh(catalog, connectionBusy);
        } catch (error) { notify('A mentés eredményét ellenőrizd: ' + error.message, true); }
        finally { pending = false; buttons(); }
    }
    if (get('home-editor')) {
        get('home-add-consumer').onclick = () => add('consumers');
        get('home-add-card').onclick = () => add('cards');
        get('home-save').onclick = save;
        get('home-room').onchange = get('home-category').onchange = render;
        buttons();
    }
    return {refresh};
})();
