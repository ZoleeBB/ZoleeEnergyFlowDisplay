/* Külön videós hitelesítés az Otthon-szerkesztőben. A háttérfrissítés nem írja felül a szerkesztést. */
const LoxoneHome = (() => {
    'use strict';
    const icons = /*HOME_ICONS*/;
    const get = id => document.getElementById(id);
    let model = {schema: 2, consumers: [], pages: [], intercom: {enabled: false, stream_url: '', bell_uuid: '', gate: null}};
    let selectedPage = 0;
    const activeCards = () => model.pages[selectedPage]?.cards || [];
    let catalog = [], catalogKey = '', loaded = false, fetching = false;
    let pending = false, waiting = false, remoteBusy = false, connectionBusy = false;
    let revision = 0, saveRevision = 0, currentSection = '';
    let resourcesInstalled = false, resourcesVersion = 0;
    const domId = (entry, key) => 'home-' + currentSection + '-' + entry.id + '-' + key;
    const controls = [];

    function notify(text, error = false) {
        const node = get('home-status');
        node.textContent = text;
        node.className = error ? 'feedback-error' : '';
        const doorStatus = get('intercom-status');
        if (doorStatus) { doorStatus.textContent = text; doorStatus.className = node.className; }
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
        if (get('intercom-save')) get('intercom-save').disabled = busy();
        get('home-add-consumer').disabled = busy() || model.consumers.length >= 12;
        get('home-add-card').disabled = busy() || !model.pages.length || activeCards().length >= 12;
        get('home-add-page').disabled = busy() || model.pages.length >= 8;
    }
    function field(parent, title, entry, key, type = 'text', optional = false) {
        const label = create('label', title), input = create('input');
        input.type = type;
        if (type === 'checkbox') input.checked = !!entry[key];
        else input.value = entry[key] ?? '';
        if (type === 'number') input.step = 'any';
        if (type === 'text') input.maxLength = ['name', 'title'].includes(key) ? 80 : key === 'unit' ? 16 : key === 'stream_url' ? 256 : 36;
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
            if (control && item.action_uuid === '') continue;
            const uuid = control ? item.control_uuid : item.uuid;
            if (!uuid || seen.has(uuid)) continue;
            seen.add(uuid);
            choices.push([uuid, (item.name || 'Névtelen') + ' (' + (item.room || '–') + ' / ' +
                (item.category || '–') + ')' + (control ? ' · vezérlés' : ' · ' + (item.state || 'állapot'))]);
        }
        return choices;
    }
    function iconField(parent, entry) {
        const input = select(parent, 'Ikon', entry, 'icon', icons.map(icon => [icon.id, icon.hu]));
        if (!resourcesInstalled) return;
        const preview = create('img');
        preview.id = domId(entry, 'icon-preview');
        preview.className = 'home-icon-preview';
        preview.width = preview.height = 64;
        preview.loading = 'lazy'; preview.decoding = 'async';
        const update = () => {
            preview.hidden = false;
            preview.alt = (icons.find(icon => icon.id === entry.icon)?.hu || 'Ikon') + ' előnézete';
            preview.src = '/api/templates/home-icon?id=' + encodeURIComponent(entry.icon) + '&v=' + resourcesVersion;
        };
        preview.onerror = () => { preview.hidden = true; };
        const change = input.onchange;
        input.onchange = () => { change(); update(); };
        input.parentNode.append(preview);
        update();
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
    function renderPages() {
        const target = get('home-pages'); target.replaceChildren();
        if (!model.pages.length) {
            target.append(create('p', 'Először adj hozzá egy Otthon-oldalt, majd azon kártyákat.'));
            return;
        }
        selectedPage = Math.min(selectedPage, model.pages.length - 1);
        const page = model.pages[selectedPage];
        currentSection = 'pages';
        const selector = select(target, 'Szerkesztett oldal', {id: 'selector', selected: page.id}, 'selected',
            model.pages.map((item, index) => [item.id, (index + 1) + '. ' + item.title + ' · ' + item.cards.length + '/12 kártya']));
        selector.id = 'home-page-select';
        selector.onchange = () => { selectedPage = model.pages.findIndex(item => item.id === selector.value); render(); };
        const title = field(target, 'Oldal címe (a kijelző fejlécében)', page, 'title');
        title.id = 'home-page-title';
        const updateTitle = title.oninput;
        title.oninput = title.onchange = () => {
            updateTitle();
            selector.children[selectedPage].textContent = (selectedPage + 1) + '. ' + page.title + ' · ' + page.cards.length + '/12 kártya';
        };
        for (const [id, label, step] of [['home-page-up', 'Oldal előrébb', -1], ['home-page-down', 'Oldal hátrébb', 1]]) {
            const button = create('button', label); button.type = 'button'; button.id = id;
            button.homeFixed = selectedPage + step < 0 || selectedPage + step >= model.pages.length;
            button.onclick = () => {
                if (busy() || button.homeFixed) return;
                const next = selectedPage + step;
                [model.pages[selectedPage], model.pages[next]] = [model.pages[next], model.pages[selectedPage]];
                selectedPage = next; render();
            };
            controls.push(button); target.append(button);
        }
        const remove = create('button', 'Oldal és kártyáinak törlése'); remove.type = 'button'; remove.id = 'home-remove-page';
        remove.onclick = () => {
            if (busy()) return;
            model.pages.splice(selectedPage, 1); selectedPage = Math.max(0, selectedPage - 1); render();
        };
        controls.push(remove); target.append(remove);
    }
    function addPage() {
        if (busy() || model.pages.length >= 8) return;
        let serial = 1;
        while (model.pages.some(page => page.id === 'page-' + serial)) serial++;
        model.pages.push({id: 'page-' + serial, title: 'Új oldal', cards: []});
        selectedPage = model.pages.length - 1; render();
    }
    function renderIntercom() {
        const target = get('intercom-config');
        if (!target) return;
        target.replaceChildren(); currentSection = 'intercom';
        const door = model.intercom;
        const enabled = field(target, 'Kaputelefon engedélyezése', door, 'enabled', 'checkbox');
        enabled.id = 'intercom-enabled';
        const url = field(target, 'Kaputelefon MJPEG-videócíme (HTTP)', door, 'stream_url');
        url.id = 'intercom-stream-url'; url.placeholder = 'http://192.168.1.50/mjpg/video.mjpg';
        const username = field(target, 'Videó felhasználóneve (admin)', door, 'video_username');
        username.id = 'intercom-video-username'; username.maxLength = 95; username.autocomplete = 'off';
        const password = field(target, 'Videó jelszava', door, 'video_password', 'password');
        password.id = 'intercom-video-password'; password.maxLength = 127; password.autocomplete = 'new-password';
        password.placeholder = door.has_video_password ? 'Mentett jelszó / üresen változatlan' : 'Admin felhasználó jelszava';
        const bell = select(target, 'Csengetés folyamatban – Loxone állapot', door, 'bell_uuid', stateChoices());
        bell.id = 'intercom-bell';
        const gate = select(target, 'Kapunyitás – vezérelhető Loxone objektum (opcionális)',
            {id: 'gate', control_uuid: door.gate?.control_uuid || ''}, 'control_uuid', stateChoices(true, true));
        gate.id = 'intercom-gate';
        gate.onchange = () => {
            door.gate = gate.value ? {id: 'intercom-gate', name: door.gate?.name || 'Kapunyitás',
                icon: 'door', type: 'pulse', control_uuid: gate.value} : null;
            render();
        };
        if (door.gate) {
            const title = field(target, 'Kapunyitó gomb felirata', door.gate, 'name');
            title.id = 'intercom-gate-name';
        }
    }
    function render() {
        controls.length = 0;
        renderIntercom();
        renderPages();
        for (const section of ['consumers', 'cards']) {
            currentSection = section;
            const target = get('home-' + section); target.replaceChildren();
            for (const entry of section === 'consumers' ? model.consumers : activeCards()) {
                const box = create('fieldset'), legend = create('legend', entry.name || 'Új elem');
                box.append(legend);
                const grid = create('div'); grid.className = 'grid'; box.append(grid);
                field(grid, 'Név', entry, 'name');
                iconField(grid, entry);
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
                remove.onclick = () => {
                    if (busy()) return;
                    if (section === 'consumers') model.consumers = model.consumers.filter(item => item.id !== entry.id);
                    else model.pages[selectedPage].cards = activeCards().filter(item => item.id !== entry.id);
                    render();
                };
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
        const entries = section === 'consumers' ? model.consumers : activeCards();
        if (busy() || entries.length >= 12 || (section === 'cards' && !model.pages.length)) return;
        let serial = 1;
        const all = section === 'consumers' ? model.consumers : model.pages.flatMap(page => page.cards);
        while (all.some(item => item.id === 'item-' + serial)) serial++;
        const entry = {id: 'item-' + serial, name: '', icon: icons[0].id, source: {uuid: '', scale: 1, offset: 0}};
        if (section === 'consumers') Object.assign(entry, {average_kw: null, suggest_start: false});
        else Object.assign(entry, {type: 'value', control_uuid: '', unit: '', decimals: 1,
            warning_low: null, warning_high: null, warning_color: '#E66B6B'});
        entries.push(entry); render();
    }
    const uuidValid = value => /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{16}$/i.test(value || '');
    function validate() {
        const door = model.intercom;
        door.stream_url = (door.stream_url || '').trim();
        if (door.enabled && (!door.stream_url || !uuidValid(door.bell_uuid)))
            return 'Kaputelefon: add meg a videó címét és válaszd ki a csengetésállapotot.';
        if (new TextEncoder().encode(door.video_username || '').length > 95 ||
            new TextEncoder().encode(door.video_password || '').length > 127)
            return 'Kaputelefon: a felhasználónév legfeljebb 95, a jelszó 127 bájt lehet.';
        if (door.enabled && (!door.video_username || (!door.video_password && !door.has_video_password)))
            return 'Kaputelefon: add meg az admin videós felhasználónevét és jelszavát.';
        if (door.stream_url) {
            let url;
            try { url = new URL(door.stream_url); }
            catch (_) { return 'Kaputelefon: érvénytelen videócím. Példa: http://192.168.2.21/mjpg/video.mjpg'; }
            if (url.protocol !== 'http:') return 'Kaputelefon: a videócím http:// kezdetű legyen.';
            if (url.username || url.password) return 'Kaputelefon: a felhasználónevet és jelszót ne írd a videócímbe.';
            if (!url.hostname || url.pathname === '/' || url.hash || /[\s\\]/.test(door.stream_url))
                return 'Kaputelefon: teljes HTTP videócím kell, belső szóköz, fordított perjel és # nélkül.';
            if (new TextEncoder().encode(url.href).length > 256)
                return 'Kaputelefon: a videócím legfeljebb 256 bájt lehet.';
            door.stream_url = url.href;
            if (get('intercom-stream-url')) get('intercom-stream-url').value = door.stream_url;
        }
        if (door.gate && (!uuidValid(door.gate.control_uuid) || !door.gate.name.trim() ||
            /[\x00-\x1f\x7f]/.test(door.gate.name) || new TextEncoder().encode(door.gate.name).length > 80))
            return 'Kaputelefon: válassz kapunyitó objektumot és adj legfeljebb 80 bájtos gombfeliratot.';
        for (const section of ['consumers', 'cards']) for (const entry of section === 'consumers' ? model.consumers : activeCards()) {
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
            const previousResources = resourcesInstalled;
            const previousVersion = resourcesVersion;
            if (typeof state.resources_installed === 'boolean') {
                resourcesInstalled = state.resources_installed;
                resourcesVersion = state.resources_version || 0;
                get('home-resource-warning').hidden = resourcesInstalled;
            }
            if (first) {
                model = {schema: 2, intercom: state.intercom || {enabled: false, stream_url: '', bell_uuid: '', gate: null}, consumers: state.consumers || [], pages: state.pages ||
                    (state.cards?.length ? [{id: 'home', title: 'Otthon', cards: state.cards}] : [])};
                for (const entry of model.pages.flatMap(page => page.cards)) {
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
                if (revision > saveRevision) {
                    model.intercom.has_video_password = !!state.intercom?.has_video_password;
                    model.intercom.video_password = '';
                    if (get('intercom-video-password')) get('intercom-video-password').value = '';
                }
                notify(state.status || (revision > saveRevision ? 'Otthon-beállítások elmentve.' : 'A mentés nem fejeződött be.'), revision <= saveRevision);
            } else if (first) notify(state.status || 'Legfeljebb 12 fogyasztó és 8 elnevezhető oldal, oldalanként 12 kártyával.');
            const nextKey = JSON.stringify(nextCatalog);
            if (first || nextKey !== catalogKey || previousResources !== resourcesInstalled || previousVersion !== resourcesVersion) {
                catalog = nextCatalog; catalogKey = nextKey; filters(); render();
            }
        } catch (error) {
            notify('Otthon-beállítások: ' + (error.name === 'AbortError' ? 'az eszköz nem válaszol.' : error.message), true);
        } finally { fetching = false; buttons(); }
    }
    async function save() {
        if (busy()) return;
        let error;
        try { error = validate(); }
        catch (failure) { notify('A böngésző nem tudta ellenőrizni a beállításokat: ' + failure.message, true); return; }
        if (error) { notify(error, true); return; }
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
        get('home-add-page').onclick = addPage;
        get('home-add-card').onclick = () => add('cards');
        get('home-save').onclick = save;
        if (get('intercom-save')) get('intercom-save').onclick = save;
        get('home-room').onchange = get('home-category').onchange = render;
        buttons();
    }
    return {refresh};
})();
