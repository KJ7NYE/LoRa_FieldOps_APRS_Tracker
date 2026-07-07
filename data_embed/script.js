/* LoRa APRS Multi-Mode — Web Config Script
 * Single beacon, single LoRa profile.
 */

'use strict';

let currentSettings = {};

// ── Helpers ───────────────────────────────────────────────────────────────────

function setVal(id, value) {
    const el = document.getElementById(id);
    if (!el) return;
    if (el.type === 'checkbox') {
        el.checked = !!value;
    } else {
        el.value = (value !== null && value !== undefined) ? value : '';
    }
}

function getVal(id) {
    const el = document.getElementById(id);
    if (!el) return '';
    return el.type === 'checkbox' ? el.checked : el.value;
}

function showToast(msg) {
    const toastEl = document.getElementById('toast');
    if (!toastEl) return;
    toastEl.querySelector('.toast-body').textContent = msg;
    const t = bootstrap.Toast.getOrCreateInstance(toastEl);
    t.show();
}

// ── PHG live preview ─────────────────────────────────────────────────────────

function updatePhgPreview() {
    const p = document.getElementById('phg.power')?.value       ?? '7';
    const h = document.getElementById('phg.height')?.value      ?? '2';
    const g = document.getElementById('phg.gain')?.value        ?? '3';
    const d = document.getElementById('phg.directivity')?.value ?? '0';
    const preview = document.getElementById('phgPreview');
    if (preview) preview.value = 'PHG' + p + h + g + d;
}

['phg.power', 'phg.height', 'phg.gain', 'phg.directivity'].forEach(id => {
    document.getElementById(id)?.addEventListener('change', updatePhgPreview);
});

// ── Progressive disclosure ────────────────────────────────────────────────────

function updateVisibility() {
    const role      = parseInt(document.getElementById('deviceRole')?.value ?? '0', 10);
    const gpsSrc    = parseInt(document.getElementById('gpsSource')?.value ?? '0', 10);

    const fixedEl   = document.getElementById('fixedPosFields');
    const aprsIsEl  = document.getElementById('aprsIsFields');
    const tcpKissEl = document.getElementById('tcpKissFields');

    if (fixedEl)   fixedEl.style.display   = (gpsSrc === 1) ? '' : 'none';
    if (aprsIsEl)  aprsIsEl.style.display  = (role === 1)   ? '' : 'none';  // iGate only
    if (tcpKissEl) tcpKissEl.style.display = (role !== 0)   ? '' : 'none';  // not for Tracker

    if (role === 1) startAprsIsPolling(); else stopAprsIsPolling();
}

document.getElementById('deviceRole')?.addEventListener('change', updateVisibility);
document.getElementById('gpsSource')?.addEventListener('change', updateVisibility);

// ── Load settings from JSON ───────────────────────────────────────────────────

function loadSettings(s) {
    currentSettings = s;
    const b = s.beacons?.[0] ?? {};
    setVal('beacons.0.callsign',          b.callsign          ?? 'NOCALL-7');
    setVal('beacons.0.symbol',            b.symbol            ?? '>');
    setVal('beacons.0.overlay',           b.overlay           ?? '/');
    setVal('beacons.0.micE',              b.micE              ?? '');
    setVal('beacons.0.comment',           b.comment           ?? '');
    setVal('beacons.0.status',            b.status            ?? '');
    setVal('beacons.0.tacticalCallsign',  b.tacticalCallsign  ?? '');
    // SmartBeacon combined dropdown: "" = Disabled, "0"-"4" = profile
    const smartActive  = b.smartBeaconActive  ?? true;
    const smartProfile = b.smartBeaconSetting ?? 2;
    setVal('smartBeaconCombined', smartActive ? String(smartProfile) : '');
    setVal('beacons.0.smartBeaconActive',  smartActive ? 'true' : 'false');
    setVal('beacons.0.smartBeaconSetting', smartProfile);
    setVal('beacons.0.gpsEcoMode',        b.gpsEcoMode        ?? false);

    setVal('deviceRole', s.deviceRole ?? 0);
    setVal('gpsSource',  s.gpsSource  ?? 0);

    const fp = s.fixedPosition ?? {};
    setVal('fixedPosition.latitude',  fp.latitude  ?? 0);
    setVal('fixedPosition.longitude', fp.longitude ?? 0);
    setVal('fixedPosition.elevation', fp.elevation ?? 0);

    const wap = s.wifiAP ?? {};
    setVal('wifiAP.password', wap.password ?? '1234567890');

    const wsta = s.wifiSTA ?? {};
    setVal('wifiSTA.enabled',  wsta.enabled  ?? false);
    setVal('wifiSTA.ssid',     wsta.ssid     ?? '');
    setVal('wifiSTA.password', wsta.password ?? '');
    if (wsta.enabled) startWifiStaPolling(); else stopWifiStaPolling();

    const ais = s.aprsIS ?? {};
    setVal('aprsIS.server',   ais.server   ?? 'rotate.aprs.net');
    setVal('aprsIS.port',     ais.port     ?? 14580);
    setVal('aprsIS.passcode', ais.passcode ?? '');
    setVal('aprsIS.filter',   ais.filter   ?? 'r/0/0/0');
    setVal('aprsIS.downlinkEnabled', ais.downlinkEnabled ?? true);

    const tk = s.tcpKISS ?? {};
    setVal('tcpKISS.enabled',       tk.enabled       ?? false);
    setVal('tcpKISS.port',          tk.port          ?? 8001);

    const lora = s.lora?.[0] ?? {};
    setVal('lora.0.frequency',      lora.frequency       ?? 433775000);
    setVal('lora.0.spreadingFactor',lora.spreadingFactor ?? 12);
    setVal('lora.0.signalBandwidth',lora.signalBandwidth ?? 125000);
    setVal('lora.0.codingRate4',    lora.codingRate4     ?? 5);
    setVal('lora.0.power',          lora.power           ?? 20);

    const csb = s.customSmartBeacon ?? {};
    setVal('customSmartBeacon.slowRate',   csb.slowRate   ?? 120);
    setVal('customSmartBeacon.slowSpeed',  csb.slowSpeed  ?? 5);
    setVal('customSmartBeacon.fastRate',   csb.fastRate   ?? 60);
    setVal('customSmartBeacon.fastSpeed',  csb.fastSpeed  ?? 40);
    setVal('customSmartBeacon.turnMinDeg', csb.turnMinDeg ?? 12);
    setVal('customSmartBeacon.turnSlope',  csb.turnSlope  ?? 60);

    const disp = s.display ?? {};
    setVal('display.ecoMode',      disp.ecoMode      ?? false);
    setVal('display.timeout',      disp.timeout      ?? 4);
    setVal('display.turn180',      disp.turn180      ?? false);
    setVal('display.invertDisplay',disp.invertDisplay?? false);
    setVal('display.ledEnabled',   disp.ledEnabled   ?? true);

    const bt = s.bluetooth ?? {};
    setVal('bluetooth.active',     bt.active     ?? false);
    setVal('bluetooth.deviceName', bt.deviceName ?? 'LoRaAPRS');

    const bat = s.battery ?? {};
    setVal('battery.sendVoltage',       bat.sendVoltage       ?? false);
    setVal('battery.sendVoltageAlways', bat.sendVoltageAlways ?? false);
    setVal('battery.sleepVoltage',      bat.sleepVoltage      ?? 2.9);

    const ptt = s.ptt ?? {};
    setVal('ptt.active',      ptt.active      ?? false);
    setVal('ptt.io_pin',      ptt.io_pin      ?? 0);
    setVal('ptt.reverse',     ptt.reverse     ?? false);
    setVal('ptt.preDelay',    ptt.preDelay    ?? 100);
    setVal('ptt.postDelay',   ptt.postDelay   ?? 100);

    const phg = s.phg ?? {};
    setVal('phg.enabled',     phg.enabled     ?? false);
    setVal('phg.power',       phg.power       ?? 7);
    setVal('phg.height',      phg.height      ?? 2);
    setVal('phg.gain',        phg.gain        ?? 3);
    setVal('phg.directivity', phg.directivity ?? 0);
    setVal('phg.beaconRate',  phg.beaconRate  ?? 10);
    updatePhgPreview();

    const oth = s.other ?? {};
    // Normalize legacy empty-string path ("" = no repeat) to the explicit "DIRECT" option.
    const _bp = s.beaconPath ?? s.path ?? oth.beaconPath ?? 'WIDE1-1';
    setVal('beaconPath', _bp === '' ? 'DIRECT' : _bp);
    setVal('nonSmartBeaconRate',      s.nonSmartBeaconRate      ?? oth.nonSmartBeaconRate  ?? 15);
    setVal('sendCommentAfterXBeacons',s.sendCommentAfterXBeacons?? oth.sendCommentAfterXBeacons ?? 10);
    setVal('sendSpeedCourse',         s.sendSpeedCourse         ?? oth.sendSpeedCourse ?? true);
    setVal('sendAltitude',            s.sendAltitude            ?? oth.sendAltitude ?? true);
    setVal('digiMode',                s.other?.digiMode         ?? oth.digiMode ?? 0);

    updateVisibility();
}

// ── Fetch settings on load ────────────────────────────────────────────────────

async function fetchSettings() {
    try {
        const res = await fetch('/configuration.json');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        loadSettings(data);
    } catch (e) {
        showToast('Could not load settings: ' + e.message);
    }
}

// ── Form submit ───────────────────────────────────────────────────────────────

document.getElementById('configForm')?.addEventListener('submit', async function(e) {
    e.preventDefault();
    const saveModal = bootstrap.Modal.getOrCreateInstance(document.getElementById('saveModal'));
    saveModal.show();
    try {
        const fd = new FormData(this);
        const res = await fetch('/configuration.json', { method: 'POST', body: fd });
        if (!res.ok) throw new Error('Save failed: HTTP ' + res.status);
        // Poll /status until device reboots and comes back.
        await pollUntilAlive();
        saveModal.hide();
        bootstrap.Modal.getOrCreateInstance(document.getElementById('savedModal')).show();
        fetchSettings();
    } catch (e) {
        saveModal.hide();
        showToast('Error: ' + e.message);
    }
});

async function pollUntilAlive() {
    // Wait a moment for device to start rebooting, then poll.
    await sleep(3000);
    for (let i = 0; i < 30; i++) {
        await sleep(2000);
        try {
            const r = await fetch('/status', { signal: AbortSignal.timeout(1500) });
            if (r.ok) return;
        } catch (_) { /* still rebooting */ }
    }
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

// ── Backup ────────────────────────────────────────────────────────────────────

document.getElementById('backup')?.addEventListener('click', function(e) {
    e.preventDefault();
    const blob = new Blob([JSON.stringify(currentSettings, null, 2)], { type: 'application/json' });
    const url  = URL.createObjectURL(blob);
    const a    = document.createElement('a');
    a.href     = url;
    a.download = 'lora_aprs_config.json';
    a.click();
    URL.revokeObjectURL(url);
});

// ── Restore ───────────────────────────────────────────────────────────────────

document.getElementById('restore')?.addEventListener('click', function(e) {
    e.preventDefault();
    document.getElementById('restoreFile')?.click();
});

document.getElementById('restoreFile')?.addEventListener('change', function() {
    const file = this.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = function(ev) {
        try {
            const data = JSON.parse(ev.target.result);
            loadSettings(data);
            showToast('Settings restored from file. Press Save to apply.');
        } catch (err) {
            showToast('Invalid JSON file: ' + err.message);
        }
    };
    reader.readAsText(file);
    this.value = '';  // allow re-selecting same file
});

// ── Reboot ────────────────────────────────────────────────────────────────────

document.getElementById('reboot')?.addEventListener('click', async function(e) {
    e.preventDefault();
    if (!confirm('Reboot device?')) return;
    try {
        await fetch('/action', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'type=reboot'
        });
        showToast('Rebooting...');
    } catch (_) {
        showToast('Reboot command sent.');
    }
});

// ── SmartBeacon combined dropdown ─────────────────────────────────────────────
// Syncs the combined profile/disabled dropdown to the two hidden config fields.

document.getElementById('smartBeaconCombined')?.addEventListener('change', function() {
    const disabled = this.value === '';
    setVal('beacons.0.smartBeaconActive',  disabled ? 'false' : 'true');
    setVal('beacons.0.smartBeaconSetting', disabled ? document.getElementById('beacons.0.smartBeaconSetting').value : this.value);
});

// ── Immediate Tx ──────────────────────────────────────────────────────────────
// Send comment or status packet immediately without resetting the beacon timer.

async function txNow(type) {
    try {
        const r = await fetch('/action', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: new URLSearchParams({ type })
        });
        showToast(r.ok ? await r.text() : 'TX failed: HTTP ' + r.status);
    } catch (e) {
        showToast('TX error: ' + e.message);
    }
}

// ── APRS-IS connection status ─────────────────────────────────────────────────

let _aprsIsPoller = null;

async function checkAprsIsStatus() {
    const led = document.getElementById('aprsIsLed');
    const txt = document.getElementById('aprsIsStatusText');
    if (!led) return;
    try {
        const res = await fetch('/aprs-is-status.json');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const d = await res.json();
        if (d.connected) {
            led.style.background = '#28a745';
            txt.textContent = 'connected';
            txt.className = 'text-success small';
        } else {
            led.style.background = '#dc3545';
            txt.textContent = 'disconnected';
            txt.className = 'text-danger small';
        }
    } catch (_) {
        led.style.background = '#6c757d';
        txt.textContent = '—';
        txt.className = 'text-muted small';
    }
}

function startAprsIsPolling() {
    if (_aprsIsPoller) return;
    checkAprsIsStatus();
    _aprsIsPoller = setInterval(checkAprsIsStatus, 5000);
}

function stopAprsIsPolling() {
    if (_aprsIsPoller) { clearInterval(_aprsIsPoller); _aprsIsPoller = null; }
    const led = document.getElementById('aprsIsLed');
    const txt = document.getElementById('aprsIsStatusText');
    if (led) led.style.background = '#6c757d';
    if (txt) { txt.textContent = '—'; txt.className = 'text-muted small'; }
}

document.getElementById('aprsIsCheckBtn')?.addEventListener('click', checkAprsIsStatus);

// ── WiFi STA connection status ────────────────────────────────────────────────

let _wifiStaPoller = null;

async function checkWifiStaStatus() {
    const led = document.getElementById('wifiStaLed');
    const txt = document.getElementById('wifiStaStatusText');
    if (!led) return;
    try {
        const res = await fetch('/wifi-sta-status.json');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const d = await res.json();
        if (d.connected) {
            led.style.background = '#28a745';
            txt.textContent = 'connected';
            txt.className = 'text-success small';
        } else {
            led.style.background = '#dc3545';
            txt.textContent = 'disconnected';
            txt.className = 'text-danger small';
        }
    } catch (_) {
        led.style.background = '#6c757d';
        txt.textContent = '—';
        txt.className = 'text-muted small';
    }
}

function startWifiStaPolling() {
    if (_wifiStaPoller) return;
    checkWifiStaStatus();
    _wifiStaPoller = setInterval(checkWifiStaStatus, 5000);
}

function stopWifiStaPolling() {
    if (_wifiStaPoller) { clearInterval(_wifiStaPoller); _wifiStaPoller = null; }
    const led = document.getElementById('wifiStaLed');
    const txt = document.getElementById('wifiStaStatusText');
    if (led) led.style.background = '#6c757d';
    if (txt) { txt.textContent = '—'; txt.className = 'text-muted small'; }
}

document.getElementById('wifiStaCheckBtn')?.addEventListener('click', checkWifiStaStatus);

document.getElementById('wifiSTA.enabled')?.addEventListener('change', function() {
    if (this.checked) startWifiStaPolling(); else stopWifiStaPolling();
});

document.getElementById('wifiStaPassToggle')?.addEventListener('click', function() {
    const inp = document.getElementById('wifiSTA.password');
    if (!inp) return;
    inp.type = inp.type === 'password' ? 'text' : 'password';
});

document.getElementById('wifiStaScanBtn')?.addEventListener('click', async function() {
    const btn = this;
    const dl  = document.getElementById('wifiStaSsidList');
    if (!dl) return;
    btn.disabled = true;
    const prevText = btn.textContent;
    btn.textContent = 'Scanning…';
    try {
        const res = await fetch('/wifi-scan.json', { signal: AbortSignal.timeout(8000) });
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const nets = await res.json();
        dl.innerHTML = '';
        nets.forEach(nw => {
            const o = document.createElement('option');
            o.value = nw.ssid;
            o.label = nw.ssid + ' (' + nw.rssi + ' dBm' + (nw.secure ? '' : ', open') + ')';
            dl.appendChild(o);
        });
        if (nets.length === 0) showToast('No networks found');
    } catch (e) {
        showToast('Scan failed: ' + e.message);
    } finally {
        btn.disabled = false;
        btn.textContent = prevText;
    }
});

// ── Live battery read ─────────────────────────────────────────────────────────

document.getElementById('batteryReadBtn')?.addEventListener('click', async function() {
    const display = document.getElementById('batteryDisplay');
    this.disabled = true;
    display.textContent = 'reading…';
    try {
        const res = await fetch('/battery.json');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const d = await res.json();
        display.textContent = `B: ${d.voltage}V  ${d.percent}%`;
    } catch (_) {
        display.textContent = 'B: error';
    }
    this.disabled = false;
});

// ── Live Events (SSE) ─────────────────────────────────────────────────────────

const LOG_TYPE = {
    'R': { label: 'RX',   color: '#58d6f5' },   // cyan
    'T': { label: 'TX',   color: '#f5c542' },   // yellow
    'D': { label: 'DIG',  color: '#5cf582' },   // green
    'I': { label: 'IGT',  color: '#7fa7f5' },   // blue
    'N': { label: 'NET',  color: '#c0c0c0' },   // silver
    'i': { label: 'INFO', color: '#a0a0a0' },   // grey
};

let _evtSrc = null;

function liveEscapeHtml(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function appendLogEntry(jsonData) {
    const log = document.getElementById('liveEventsLog');
    if (!log) return;
    let entry;
    try { entry = JSON.parse(jsonData); } catch (_) { return; }

    const t   = LOG_TYPE[entry.t] || { label: '?', color: '#a0a0a0' };
    const ms  = entry.ms || 0;
    const sec = Math.floor(ms / 1000);
    const hh  = String(Math.floor(sec / 3600) % 24).padStart(2, '0');
    const mm  = String(Math.floor(sec / 60) % 60).padStart(2, '0');
    const ss  = String(sec % 60).padStart(2, '0');

    const line = document.createElement('div');
    line.innerHTML =
        `<span style="color:#666">${hh}:${mm}:${ss}</span> ` +
        `<span style="color:${t.color};font-weight:bold">[${t.label}]</span> ` +
        `<span>${liveEscapeHtml(entry.msg || '')}</span>`;
    log.appendChild(line);

    // Trim to last 200 lines to prevent unbounded DOM growth
    while (log.children.length > 200) log.removeChild(log.firstChild);

    const autoScroll = document.getElementById('liveEventsAutoScroll');
    if (autoScroll?.checked) log.scrollTop = log.scrollHeight;
}

function liveEventsConnect() {
    if (_evtSrc) return;
    const btn    = document.getElementById('liveEventsConnectBtn');
    const status = document.getElementById('liveEventsStatus');
    if (!('EventSource' in window)) {
        if (status) { status.textContent = 'EventSource not supported'; status.className = 'text-warning small ms-auto'; }
        return;
    }
    _evtSrc = new EventSource('/events');
    _evtSrc.addEventListener('log', e => appendLogEntry(e.data));
    _evtSrc.onopen = () => {
        if (status) { status.textContent = 'Connected'; status.className = 'text-success small ms-auto'; }
        if (btn)    { btn.textContent = 'Disconnect'; btn.className = 'btn btn-sm btn-outline-danger'; }
    };
    _evtSrc.onerror = () => {
        if (status) { status.textContent = 'Connection error'; status.className = 'text-danger small ms-auto'; }
    };
}

function liveEventsDisconnect() {
    if (!_evtSrc) return;
    _evtSrc.close();
    _evtSrc = null;
    const btn    = document.getElementById('liveEventsConnectBtn');
    const status = document.getElementById('liveEventsStatus');
    if (btn)    { btn.textContent = 'Connect'; btn.className = 'btn btn-sm btn-outline-primary'; }
    if (status) { status.textContent = 'Disconnected'; status.className = 'text-muted small ms-auto'; }
}

document.getElementById('liveEventsConnectBtn')?.addEventListener('click', function () {
    if (_evtSrc) liveEventsDisconnect(); else liveEventsConnect();
});

document.getElementById('liveEventsClearBtn')?.addEventListener('click', function () {
    const log = document.getElementById('liveEventsLog');
    if (log) log.innerHTML = '';
});

// Cleanly close the SSE connection when the browser tab is closed/navigated away,
// so the device's AsyncEventSource slot is freed promptly.
window.addEventListener('beforeunload', liveEventsDisconnect);

// ── OTA Firmware Update ───────────────────────────────────────────────────────

(function () {
    const showBtn   = document.getElementById('showOtaModal');
    const uploadBtn = document.getElementById('otaUploadBtn');
    const fileInput = document.getElementById('otaFile');
    const progress  = document.getElementById('otaProgress');
    const bar       = document.getElementById('otaBar');
    const status    = document.getElementById('otaStatus');
    const verDiv    = document.getElementById('otaCurrentVersion');
    const reloadBtn = document.getElementById('otaReloadBtn');

    if (!showBtn) return;

    // Open modal and populate current version
    showBtn.addEventListener('click', function (e) {
        e.preventDefault();
        fetch('/version.json')
            .then(r => r.json())
            .then(d => {
                if (verDiv) verDiv.textContent = 'Running: ' + d.version + ' (' + d.env + ')';
            })
            .catch(() => { if (verDiv) verDiv.textContent = ''; });
        if (progress) progress.style.display = 'none';
        if (bar)      { bar.style.width = '0%'; bar.textContent = '0%'; }
        if (status)   status.textContent = '';
        if (fileInput) fileInput.value = '';
        bootstrap.Modal.getOrCreateInstance(document.getElementById('otaModal')).show();
    });

    if (!uploadBtn) return;
    uploadBtn.addEventListener('click', function () {
        const file = fileInput && fileInput.files[0];
        if (!file) { showToast('Select a firmware.bin file first.'); return; }

        uploadBtn.disabled = true;
        if (progress) progress.style.display = 'block';

        const xhr = new XMLHttpRequest();
        xhr.upload.addEventListener('progress', function (e) {
            if (!e.lengthComputable) return;
            const pct = Math.round(e.loaded / e.total * 100);
            if (bar)    { bar.style.width = pct + '%'; bar.textContent = pct + '%'; }
            if (status) status.textContent = 'Uploading… ' + pct + '%';
        });
        xhr.addEventListener('load', function () {
            if (xhr.status === 200 && xhr.responseText === 'OK') {
                if (bar)    { bar.style.width = '100%'; bar.textContent = '100%'; bar.classList.remove('progress-bar-animated'); }
                if (status) status.textContent = 'Upload complete — waiting for device to reboot…';
                uploadBtn.disabled = false;
                pollForReboot();
            } else {
                if (status) status.textContent = 'Error: ' + xhr.responseText;
                uploadBtn.disabled = false;
            }
        });
        xhr.addEventListener('error', function () {
            if (status) status.textContent = 'Upload failed — check connection and retry.';
            uploadBtn.disabled = false;
        });

        xhr.open('POST', '/update');
        const fd = new FormData();
        fd.append('file', file, file.name);
        xhr.send(fd);
    });

    function pollForReboot() {
        // Device schedules restart 1500 ms after responding — allow time to shut down and boot.
        const INITIAL_DELAY_MS = 6000;   // 1.5 s restart delay + ~4 s ESP32 boot
        const POLL_INTERVAL_MS = 2000;
        const TIMEOUT_MS       = 60000;

        let elapsed = 0;

        function attempt() {
            fetch('/version.json', { cache: 'no-store' })
                .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
                .then(function (d) {
                    // Device is back — show new version and auto-reload after 2 s.
                    if (status) status.textContent =
                        'Device back online — firmware ' + d.version + ' (' + d.env + '). Reloading…';
                    setTimeout(function () { location.reload(); }, 2000);
                })
                .catch(function () {
                    elapsed += POLL_INTERVAL_MS;
                    if (elapsed >= TIMEOUT_MS) {
                        if (status) status.textContent =
                            'Device did not respond after 60 s — check power and reload manually.';
                        if (reloadBtn) reloadBtn.style.display = '';
                        return;
                    }
                    setTimeout(attempt, POLL_INTERVAL_MS);
                });
        }

        setTimeout(attempt, INITIAL_DELAY_MS);
    }
}());

// ── Init ──────────────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', function () {
    fetchSettings();
    fetch('/version.json')
        .then(r => r.json())
        .then(d => {
            const el = document.getElementById('navVersion');
            if (el && d.version) el.textContent = 'v' + d.version;
        })
        .catch(() => {});
});
