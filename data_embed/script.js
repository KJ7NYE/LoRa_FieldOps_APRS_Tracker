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
    setVal('beacons.0.smartBeaconActive', b.smartBeaconActive ?? true);
    setVal('beacons.0.smartBeaconSetting',b.smartBeaconSetting ?? 2);
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

    const ais = s.aprsIS ?? {};
    setVal('aprsIS.server',   ais.server   ?? 'rotate.aprs.net');
    setVal('aprsIS.port',     ais.port     ?? 14580);
    setVal('aprsIS.passcode', ais.passcode ?? '');
    setVal('aprsIS.filter',   ais.filter   ?? 'r/0/0/0');

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
    setVal('customSmartBeacon.slowRate',       csb.slowRate       ?? 120);
    setVal('customSmartBeacon.slowSpeed',      csb.slowSpeed      ?? 5);
    setVal('customSmartBeacon.fastRate',       csb.fastRate       ?? 60);
    setVal('customSmartBeacon.fastSpeed',      csb.fastSpeed      ?? 40);
    setVal('customSmartBeacon.minTxDist',      csb.minTxDist      ?? 100);
    setVal('customSmartBeacon.minDeltaBeacon', csb.minDeltaBeacon ?? 12);
    setVal('customSmartBeacon.turnMinDeg',     csb.turnMinDeg     ?? 12);
    setVal('customSmartBeacon.turnSlope',      csb.turnSlope      ?? 60);

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

    const oth = s.other ?? {};
    setVal('beaconPath',              s.beaconPath              ?? s.path ?? oth.beaconPath  ?? 'WIDE1-1');
    setVal('nonSmartBeaconRate',      s.nonSmartBeaconRate      ?? oth.nonSmartBeaconRate  ?? 15);
    setVal('sendCommentAfterXBeacons',s.sendCommentAfterXBeacons?? oth.sendCommentAfterXBeacons ?? 10);
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

// ── Init ──────────────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', fetchSettings);
