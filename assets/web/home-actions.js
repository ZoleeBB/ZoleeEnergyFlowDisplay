(() => {
  const hu = document.documentElement.lang === 'hu';
  const words = hu ? {
    cancel: 'Mégsem', restart: 'Újraindítás',
    restartTitle: 'Újraindítod a kijelzőt?',
    restartDetail: 'A beállítások megmaradnak. A kijelző és a webes felület rövid ideig elérhetetlen lesz.',
    factoryTitle: 'Alaphelyzetbe állítod ezt a kijelzőt?',
    factoryDetail: 'Erről a kijelzőről végleg törlődnek a beállítások, a Wi-Fi-adatok, a párosítások, a helyi statisztikák, az eseménynapló és a tárolt beállításmentések. Az SD-kártyán lévő témák és csengőhangok megmaradnak. Újraindítás után a kijelzőn újra be kell állítani a Wi-Fi-t.',
    submitReset: 'Adatok törlése és újraindítás', sending: 'A kérés küldése…',
    rejected: 'A kijelző elutasította a kérést.', unknown: 'Nem érkezett válasz. Újrapróbálás előtt ellenőrizd a kijelzőn, hogy elindult-e a művelet.',
    restarting: 'A kijelző újraindul', accepted: 'A kijelző elfogadta a gyári visszaállítást.',
    countdown: 'Újracsatlakozás', reconnecting: 'Kapcsolódás az újraindult kijelzőhöz…',
    next: 'Újraindítás után válassz Wi-Fi-hálózatot a kijelzőn, majd nyisd meg az ott megjelenő webcímet.'
  } : {
    cancel: 'Cancel', restart: 'Restart',
    restartTitle: 'Restart this display?',
    restartDetail: 'Your settings will be kept. The display and its web interface will be unavailable briefly.',
    factoryTitle: 'Restore this display to factory settings?',
    factoryDetail: 'This permanently deletes the settings, Wi-Fi credentials, pairings, local statistics, event log and settings backups stored on this display. Themes and ringtones on the SD card will be kept. After it restarts, set up Wi-Fi on the display again.',
    submitReset: 'Erase data and restart', sending: 'Sending request…',
    rejected: 'The display rejected the request.', unknown: 'No response received. Check the display before retrying to see whether the operation has started.',
    restarting: 'The display is restarting', accepted: 'The display accepted the factory reset.',
    countdown: 'Reconnecting in', reconnecting: 'Reconnecting to the display…',
    next: 'After the restart, select a Wi-Fi network on the display, then open the web address shown there.'
  };
  const get = id => document.getElementById(id);
  const dialog = get('device-dialog'), title = get('device-title'), detail = get('device-detail');
  const status = get('device-status'), counter = get('device-countdown');
  const cancel = get('device-cancel'), confirm = get('device-confirm'), buttons = get('device-buttons');
  let factory = false, busy = false;
  cancel.textContent = words.cancel;

  document.querySelectorAll('[data-device-action]').forEach(button => button.addEventListener('click', () => {
    if (busy || dialog.open) return;
    factory = button.dataset.deviceAction === 'factory';
    title.textContent = factory ? words.factoryTitle : words.restartTitle;
    detail.textContent = factory ? words.factoryDetail : words.restartDetail;
    confirm.textContent = factory ? words.submitReset : words.restart;
    confirm.className = factory ? 'danger' : 'restart';
    status.textContent = '';
    dialog.showModal();
    cancel.focus();
  }));
  cancel.addEventListener('click', () => { if (!busy) dialog.close(); });
  dialog.addEventListener('cancel', event => { if (busy) event.preventDefault(); });

  // A sima újraindítás után a korábbi címet próbáljuk újra. Gyári törlés után a Wi-Fi-t a kijelzőn kell megadni.
  function reconnect() {
    const deadline = Date.now() + 20000;
    async function tick() {
      const seconds = Math.ceil((deadline - Date.now()) / 1000);
      if (seconds > 0) {
        counter.hidden = false;
        counter.textContent = words.countdown + ': ' + seconds + ' s';
        setTimeout(tick, 1000);
        return;
      }
      counter.hidden = true;
      status.textContent = words.reconnecting;
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), 1800);
      try {
        const response = await fetch('/api/settings?ping=' + Date.now(), {cache:'no-store', signal:controller.signal});
        if (response.ok) { location.replace('/'); return; }
      } catch (_) { /* Az eszköz még újraindul. */ }
      finally { clearTimeout(timer); }
      setTimeout(tick, 2000);
    }
    tick();
  }

  confirm.addEventListener('click', async () => {
    if (busy || !dialog.open) return;
    busy = cancel.disabled = confirm.disabled = true;
    status.className = '';
    status.textContent = words.sending;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 15000);
    const options = {method:'POST', cache:'no-store', signal:controller.signal};
    if (factory) {
      options.headers = {'Content-Type':'application/json', 'X-Zolee-Settings':'1'};
      options.body = JSON.stringify({confirm:true});
    }
    try {
      const response = await fetch(factory ? '/api/factory-reset' : '/reboot', options);
      if (!response.ok) {
        const reason = (await response.text()).trim();
        status.textContent = words.rejected + ' (HTTP ' + response.status + ')' + (reason ? ' ' + reason : '');
      } else {
        buttons.hidden = true;
        status.textContent = '';
        detail.textContent = factory ? words.next : '';
        title.textContent = factory ? words.accepted : words.restarting;
        title.focus();
        if (!factory) reconnect();
        return;
      }
    } catch (_) {
      status.textContent = words.unknown;
    } finally {
      clearTimeout(timer);
    }
    status.className = 'error';
    busy = cancel.disabled = confirm.disabled = false;
    cancel.focus();
  });
})();
