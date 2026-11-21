/* Az adatokat a firmware gyári regisztertérképe adja. Csak a kiválasztott
 * profil eltérései kerülnek a mentésbe. A többi vázlat a böngészőben marad. */
const inverterRegisters = (() => {
    const drafts = new Map();
    let generation = 0, selected = '', rows = [], ready = false;

    function savedOverrides() {
        return saved?.profile === selected ? (saved.registerOverrides || {}) : {};
    }
    function applicable(spec) {
        if (selected !== 'solax_modbus') return true;
        const model = get('device')?.value || '';
        if (spec.id.startsWith('x1_')) return model === 'X1-G4';
        if (spec.id.startsWith('x3_')) return model !== 'X1-G4';
        return !spec.id.startsWith('pro_') || model === 'X3-G4-PRO';
    }
    function status() {
        const defaults = savedOverrides();
        let edited = 0;
        for (const {spec, input, restore, box} of rows) {
            box.hidden = input.disabled = !applicable(spec);
            if (box.hidden) continue;
            const value = input.valueAsNumber;
            restore.disabled = value === spec.address;
            if (value !== (defaults[spec.id] ?? spec.address)) ++edited;
        }
        get('register-status').textContent = edited ? 'Nem mentett regisztercímek: ' + edited + '.' : '';
    }
    function row(spec, draft) {
        const box = el('div'), description = el('div'), label = el('label', spec.label);
        const input = el('input'), restore = el('button', 'Gyári cím visszaállítása');
        box.className = 'register-row';
        input.id = 'register-' + spec.id;
        label.htmlFor = input.id;
        input.type = 'number'; input.min = '0'; input.max = String(65536 - spec.words);
        input.step = '1'; input.required = true; input.inputMode = 'numeric';
        input.value = draft.has(spec.id) ? draft.get(spec.id) : spec.address;
        const info = el('small', (spec.base ? 'Gyári eltolás: ' : 'Gyári cím: ') + spec.address +
            ' · FC' + String(spec.function).padStart(2, '0') + ' · ' + spec.words + ' szó' +
            (spec.format ? ' · ' + spec.format : '') + (spec.base ? ' · ' + spec.base : ''));
        info.id = input.id + '-info';
        input.setAttribute('aria-describedby', info.id);
        restore.type = 'button'; restore.className = 'secondary';
        restore.setAttribute('aria-label', 'Gyári cím visszaállítása – ' + spec.label);
        input.oninput = () => {
            if (input.valueAsNumber === spec.address) draft.delete(spec.id);
            else draft.set(spec.id, input.value);
            status();
        };
        restore.onclick = () => { input.value = spec.address; input.oninput(); input.focus(); };
        description.append(label, info);
        box.append(description, input, restore);
        get('register-rows').append(box);
        return {spec, input, restore, box};
    }
    function validSchema(list) {
        if (!Array.isArray(list) || !list.length) return false;
        const ids = new Set();
        return list.every(spec => {
            if (!spec || typeof spec.id !== 'string' || !spec.id || ids.has(spec.id) ||
                typeof spec.label !== 'string' || !spec.label || ![3, 4].includes(spec.function) ||
                !Number.isInteger(spec.words) || spec.words < 1 || spec.words > 125 ||
                !Number.isInteger(spec.address) || spec.address < 0 || spec.address > 65536 - spec.words)
                return false;
            ids.add(spec.id);
            return true;
        });
    }
    async function select(id) {
        const token = ++generation;
        selected = id;
        rows = [];
        const modbus = profiles[id]?.unit != null;
        ready = !modbus;
        get('registers').hidden = !modbus;
        get('register-rows').replaceChildren();
        get('register-retry').hidden = true;
        get('register-status').textContent = modbus ? 'Gyári regisztercímek betöltése…' : '';
        updateSaveButton();
        if (!modbus) return;
        try {
            const reply = await apiFetch('/api/inverter/registers?profile=' + encodeURIComponent(id), {cache: 'no-store'});
            if (!reply.ok) throw Error('HTTP ' + reply.status);
            const {registers} = await reply.json();
            if (token !== generation) return;
            if (!validSchema(registers)) throw Error('Érvénytelen regiszterlista.');
            if (!drafts.has(id)) drafts.set(id, new Map(Object.entries(savedOverrides())));
            const draft = drafts.get(id);
            rows = registers.map(spec => row(spec, draft));
            ready = true;
            status();
        } catch (error) {
            if (token !== generation) return;
            get('register-status').textContent = 'Nem sikerült betölteni a gyári regisztercímeket. ' + error.message;
            const retry = get('register-retry');
            retry.hidden = false;
            retry.onclick = () => select(id);
        } finally { if (token === generation) updateSaveButton(); }
    }
    function values() {
        const result = {}, previous = savedOverrides();
        for (const {spec, input} of rows) {
            // A rejtett változat mentett címét egy másik modell mentése ne módosítsa.
            const value = applicable(spec) ? input.valueAsNumber : (previous[spec.id] ?? spec.address);
            if (value !== spec.address) result[spec.id] = value;
        }
        return result;
    }
    return {select, reset: () => drafts.clear(), isReady: () => ready, variantChanged: () => { if (ready) status(); }, values};
})();
