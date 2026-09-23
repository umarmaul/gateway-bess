#include "web_dashboard.h"
#include "web.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sysinfo.h"
#include "state.h"
#include "task_cmd.h"
#include "task_ota.h"
#include "web_cmd.h"
#include "prov.h"
#include "ack_ring.h"

// ---------------------------------------------------------------------------
// GET / -- dashboard satu halaman, HTML+CSS+JS inline di PROGMEM (sub-proyek
// H). TANPA CDN/font eksternal -- gateway sering tanpa akses internet (lihat
// spec `docs/superpowers/specs/2026-09-23-subproyek-EFGH-design.md` §H).
// Poll /api/data setiap 2 dtk BERANTAI (fetch -> tunggu selesai/timeout 4 dtk
// -> setTimeout 2 dtk), BUKAN setInterval -- pelajaran gateway-v2:
// setInterval yang saling tumpang tindih membanjiri server 1-koneksi
// (CLAUDE.md §26 Juli "dashboard menghantam endpoint terberatnya sendiri").
// Semua teks dinamis masuk DOM lewat textContent (tidak pernah innerHTML)
// supaya nilai dari JSON telemetri/ack (mis. SSID, id command) tidak bisa
// menyuntik markup.
// ---------------------------------------------------------------------------
static const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BEP Gateway BESS</title>
<style>
:root{
  --bg:#f2f4f7; --card:#ffffff; --text:#1b2430; --muted:#647085; --border:#dde3ec;
  --ok:#1a8754; --warn:#b8860b; --bad:#c0392b; --accent:#2563eb;
}
@media (prefers-color-scheme: dark){
  :root{ --bg:#12161c; --card:#1b212b; --text:#e7ebf1; --muted:#93a0b4; --border:#2b333f; }
}
*{box-sizing:border-box}
body{margin:0;padding:12px;background:var(--bg);color:var(--text);
     font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif;font-size:15px}
h1{font-size:18px;margin:4px 0 2px}
h2{font-size:14px;margin:0 0 8px;color:var(--muted);text-transform:uppercase;letter-spacing:.04em}
.sub{color:var(--muted);font-size:12px;margin-bottom:10px}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;
      padding:12px;margin-bottom:10px}
.banner{display:none;background:var(--bad);color:#fff;padding:8px 12px;border-radius:8px;
        margin-bottom:10px;font-weight:600;text-align:center}
.badges{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:8px}
.badge{display:inline-block;padding:3px 8px;border-radius:999px;font-size:12px;
       background:var(--border);color:var(--text)}
.badge.ok{background:var(--ok);color:#fff}
.badge.warn{background:var(--warn);color:#fff}
.badge.bad{background:var(--bad);color:#fff}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.stat{background:var(--bg);border:1px solid var(--border);border-radius:8px;padding:8px}
.stat .v{font-size:20px;font-weight:700}
.stat .l{font-size:11px;color:var(--muted);text-transform:uppercase}
ul.alarms{margin:4px 0 0;padding-left:18px}
ul.alarms li{color:var(--bad);font-weight:600}
label{display:block;margin-top:8px;font-size:12px;color:var(--muted)}
input[type=text],input[type=password],input[type=number]{
  width:100%;box-sizing:border-box;padding:7px;margin-top:2px;border:1px solid var(--border);
  border-radius:6px;background:var(--card);color:var(--text)}
button{margin-top:10px;padding:9px 14px;border-radius:6px;border:1px solid var(--border);
       background:var(--accent);color:#fff;font-weight:600;cursor:pointer}
button.secondary{background:var(--card);color:var(--text)}
button.danger{background:var(--bad)}
.row{display:flex;gap:8px;flex-wrap:wrap}
.row > *{flex:1}
.result{font-size:12px;color:var(--muted);margin-top:6px;word-break:break-word}
table{width:100%;border-collapse:collapse;font-size:12px}
th,td{text-align:left;padding:4px 6px;border-bottom:1px solid var(--border)}
th{color:var(--muted);font-weight:600}
a{color:var(--accent)}
.checkrow{display:flex;align-items:center;gap:6px;margin-top:8px}
.checkrow input{width:auto;margin:0}
</style>
</head>
<body>

<h1>BEP Gateway BESS</h1>
<div class="sub">gw <span id="gw">-</span> &middot; firmware <span id="fw-top">-</span> &middot; update terakhir <span id="last-update">-</span></div>

<div id="stale-banner" class="banner">DATA BASI -- gateway tidak merespons atau BESS comm_lost</div>

<div class="card">
  <h2>Koneksi</h2>
  <div class="badges">
    <span class="badge" id="badge-wifi">WiFi: -</span>
    <span class="badge" id="badge-ap">AP fallback: -</span>
  </div>
  <div class="sub">IP <span id="ip">-</span> &middot; RSSI <span id="rssi">-</span> dBm &middot; mDNS <span id="mdns">-</span></div>
</div>

<div class="card">
  <h2>BESS</h2>
  <div class="badges">
    <span class="badge" id="badge-status">-</span>
  </div>
  <div class="grid">
    <div class="stat"><div class="v" id="daya">-</div><div class="l">Daya aktif</div></div>
    <div class="stat"><div class="v" id="soc">-</div><div class="l">SOC</div></div>
    <div class="stat"><div class="v" id="vdc">-</div><div class="l">Tegangan DC</div></div>
    <div class="stat"><div class="v" id="setpoint">-</div><div class="l">Setpoint daya</div></div>
  </div>
</div>

<div class="card">
  <h2>Alarm aktif</h2>
  <div id="alarms-empty" class="sub">Tidak ada alarm aktif</div>
  <ul class="alarms" id="alarms"></ul>
</div>

<div class="card">
  <h2>Kontrol</h2>
  <label for="code">gateway_code</label>
  <input type="password" id="code" autocomplete="off" maxlength="6" placeholder="6 karakter">
  <div class="row">
    <button id="btn-enable">Enable</button>
    <button id="btn-disable" class="danger">Disable</button>
  </div>
  <label for="power-w">Set daya keluaran (W, + = ekspor)</label>
  <div class="row">
    <input type="number" id="power-w" step="1" placeholder="mis. 1500">
    <button id="btn-setoutput" class="secondary">Set Output</button>
  </div>
  <div class="result" id="cmd-result"></div>
</div>

<div class="card">
  <h2>Jadwal + auto-SOC</h2>
  <div class="checkrow">
    <input type="checkbox" id="sched-enabled">
    <label for="sched-enabled" style="margin:0">Aktifkan jadwal</label>
  </div>
  <div class="row">
    <div>
      <label for="sched-start">Mulai (HH:MM)</label>
      <input type="text" id="sched-start" placeholder="17:00">
    </div>
    <div>
      <label for="sched-end">Selesai (HH:MM)</label>
      <input type="text" id="sched-end" placeholder="21:00">
    </div>
  </div>
  <div class="row">
    <div>
      <label for="sched-threshold">SOC stop (%)</label>
      <input type="number" id="sched-threshold" step="0.1">
    </div>
    <div>
      <label for="sched-recovery">SOC recovery (%)</label>
      <input type="number" id="sched-recovery" step="0.1">
    </div>
  </div>
  <div class="row">
    <div>
      <label for="sched-power">Daya jadwal (W)</label>
      <input type="number" id="sched-power" step="1">
    </div>
    <div>
      <label for="sched-tz">tz_offset (menit)</label>
      <input type="number" id="sched-tz" step="1">
    </div>
  </div>
  <div class="row">
    <button id="sched-save">Simpan jadwal</button>
    <button id="sched-reload" class="secondary">Muat ulang</button>
  </div>
  <div class="sub" id="sched-status">-</div>
  <div class="result" id="sched-result"></div>
</div>

<div class="card">
  <h2>Ack terakhir</h2>
  <table>
    <thead><tr><th>id</th><th>cmd</th><th>result</th><th>detail</th><th>ts</th></tr></thead>
    <tbody id="acks-body"></tbody>
  </table>
</div>

<div class="card">
  <h2>Firmware &amp; OTA</h2>
  <table>
    <tr><td>Firmware</td><td id="fw-version">-</td></tr>
    <tr><td>Partisi berjalan</td><td id="fw-partition">-</td></tr>
    <tr><td>OTA state</td><td id="fw-ota-state">-</td></tr>
    <tr><td>OTA id</td><td id="fw-ota-id">-</td></tr>
    <tr><td>Pending verify</td><td id="fw-ota-pending">-</td></tr>
    <tr><td>Boot count</td><td id="boot-count">-</td></tr>
    <tr><td>Reset terakhir</td><td id="reset-reason">-</td></tr>
    <tr><td>Heap bebas / minimum</td><td id="heap">-</td></tr>
    <tr><td>Crash terakhir</td><td id="crash">-</td></tr>
  </table>
</div>

<div class="card">
  <a href="/wifi">Pengaturan WiFi &amp; provisioning &rarr;</a>
</div>

<script>
(function () {
  'use strict';
  var POLL_MS = 2000;
  var FETCH_TIMEOUT_MS = 4000;
  var STALE_AFTER_FAILS = 2;

  var failStreak = 0;
  var cycle = 0;

  function qs(id) { return document.getElementById(id); }
  function setText(id, text) {
    var el = qs(id);
    if (el) el.textContent = text;
  }
  function fmtNum(v, digits, suffix) {
    if (v === undefined || v === null || typeof v !== 'number' || isNaN(v)) return '-';
    return v.toFixed(digits) + (suffix || '');
  }

  var codeInput = qs('code');
  try {
    var savedCode = sessionStorage.getItem('bep_code');
    if (savedCode) codeInput.value = savedCode;
  } catch (e) { /* sessionStorage tidak tersedia -- abaikan */ }
  codeInput.addEventListener('change', function () {
    try { sessionStorage.setItem('bep_code', codeInput.value); } catch (e) {}
  });

  function fetchWithTimeout(url, opts, ms) {
    var ctrl = (typeof AbortController !== 'undefined') ? new AbortController() : null;
    var timer = ctrl ? setTimeout(function () { ctrl.abort(); }, ms) : null;
    opts = opts || {};
    if (ctrl) opts.signal = ctrl.signal;
    return fetch(url, opts).then(function (r) {
      if (timer) clearTimeout(timer);
      return r;
    }, function (err) {
      if (timer) clearTimeout(timer);
      throw err;
    });
  }

  function setStale(isStale) {
    var b = qs('stale-banner');
    b.style.display = isStale ? 'block' : 'none';
  }

  function setBadge(id, label, cls) {
    var el = qs(id);
    if (!el) return;
    el.textContent = label;
    el.className = 'badge' + (cls ? ' ' + cls : '');
  }

  function renderData(d) {
    var data = d.data || {};
    var bess = data.bess || {};
    var net = data.network || {};
    var ota = data.ota || {};

    setText('gw', d.gw || '-');
    setText('fw-top', data.firmware_version || '-');

    setText('daya', fmtNum(bess.active_power_kw, 2, ' kW'));
    setText('soc', fmtNum(bess.soc_percent, 1, ' %'));
    setText('vdc', fmtNum(bess.dc_voltage_v, 1, ' V'));
    setText('setpoint', fmtNum(bess.power_setpoint_percent, 1, ' %'));

    var statusLabel = 'standby', statusCls = '';
    if (bess.comm_lost) { statusLabel = 'COMM LOST'; statusCls = 'bad'; }
    else if (bess.fault) { statusLabel = 'FAULT'; statusCls = 'bad'; }
    else if (bess.running) { statusLabel = 'RUNNING'; statusCls = 'ok'; }
    else if (bess.standby) { statusLabel = 'STANDBY'; statusCls = 'warn'; }
    setBadge('badge-status', statusLabel, statusCls);

    var ssidTxt = net.ssid ? net.ssid : '(tidak tersambung)';
    setBadge('badge-wifi', 'WiFi: ' + ssidTxt, net.ssid ? 'ok' : 'bad');
    setBadge('badge-ap', 'AP fallback: ' + (net.ap_active ? 'AKTIF' : 'mati'), net.ap_active ? 'warn' : '');
    setText('ip', net.ip || '-');
    setText('rssi', (net.rssi_dbm !== undefined && net.rssi_dbm !== null) ? net.rssi_dbm : '-');
    setText('mdns', net.mdns || '-');

    var al = bess.alarms_decoded || {};
    var alarmList = qs('alarms');
    alarmList.textContent = '';
    var anyAlarm = false;
    Object.keys(al).forEach(function (k) {
      if (al[k]) {
        anyAlarm = true;
        var li = document.createElement('li');
        li.textContent = k;
        alarmList.appendChild(li);
      }
    });
    qs('alarms-empty').style.display = anyAlarm ? 'none' : 'block';

    setText('fw-version', data.firmware_version || '-');
    setText('boot-count', (data.boot_count !== undefined) ? data.boot_count : '-');
    setText('reset-reason', data.last_reset_reason || '-');
    var heapTxt = '-';
    if (data.free_heap_bytes !== undefined && data.min_free_heap_bytes !== undefined) {
      heapTxt = data.free_heap_bytes + ' / ' + data.min_free_heap_bytes + ' byte';
    }
    setText('heap', heapTxt);
    var crash = data.last_crash;
    setText('crash', crash ? (crash.task + ' pc=' + crash.pc + ' mcause=' + crash.mcause +
      ' boot=' + crash.boot_count) : 'tidak ada');
    setText('fw-partition', ota.running_partition || '-');
    setText('fw-ota-state', ota.state || '-');
    setText('fw-ota-id', ota.id || '(tidak ada)');
    setText('fw-ota-pending', ota.pending_verify ? 'ya (PENDING_VERIFY)' : 'tidak');

    setStale(!!bess.comm_lost);
  }

  function fetchData() {
    return fetchWithTimeout('/api/data', { cache: 'no-store' }, FETCH_TIMEOUT_MS)
      .then(function (r) {
        if (!r.ok) throw new Error('http ' + r.status);
        return r.json();
      })
      .then(function (d) {
        failStreak = 0;
        renderData(d);
        setText('last-update', new Date().toLocaleTimeString());
      })
      .catch(function () {
        failStreak++;
        if (failStreak >= STALE_AFTER_FAILS) setStale(true);
      });
  }

  function fetchAcks() {
    return fetchWithTimeout('/api/acks', { cache: 'no-store' }, FETCH_TIMEOUT_MS)
      .then(function (r) { return r.json(); })
      .then(function (arr) {
        var tbody = qs('acks-body');
        tbody.textContent = '';
        (arr || []).forEach(function (a) {
          var tr = document.createElement('tr');
          [a.id, a.cmd, a.result, a.detail, a.ts].forEach(function (v) {
            var td = document.createElement('td');
            td.textContent = (v === undefined || v === null || v === '') ? '-' : String(v);
            tr.appendChild(td);
          });
          tbody.appendChild(tr);
        });
      })
      .catch(function () {});
  }

  function fetchFirmware() {
    return fetchWithTimeout('/api/firmware_versions', { cache: 'no-store' }, FETCH_TIMEOUT_MS)
      .then(function (r) { return r.json(); })
      .then(function (j) {
        setText('fw-version', j.firmware_version || '-');
        setText('fw-partition', j.running_partition || '-');
        var o = j.ota || {};
        setText('fw-ota-state', o.state || '-');
        setText('fw-ota-id', o.id || '(tidak ada)');
        setText('fw-ota-pending', o.pending_verify ? 'ya (PENDING_VERIFY)' : 'tidak');
      })
      .catch(function () {});
  }

  // Satu rantai poll -- BUKAN setInterval (pelajaran gateway-v2: server
  // 1-koneksi tertumpuk kalau beberapa setInterval saling tumpang tindih).
  // /api/data tiap siklus (2 dtk), /api/acks tiap 5 siklus (~10 dtk),
  // /api/firmware_versions tiap 15 siklus (~30 dtk) -- semua SEKUENSIAL,
  // tak pernah dua request terbang bersamaan.
  function pollLoop() {
    cycle++;
    fetchData()
      .then(function () { return (cycle % 5 === 0) ? fetchAcks() : null; })
      .then(function () { return (cycle % 15 === 0) ? fetchFirmware() : null; })
      .catch(function () {})
      .then(function () { setTimeout(pollLoop, POLL_MS); });
  }

  function applyScheduleToForm(j) {
    qs('sched-enabled').checked = !!j.enabled;
    qs('sched-start').value = j.start || '';
    qs('sched-end').value = j.end || '';
    qs('sched-threshold').value = (j.threshold !== undefined) ? j.threshold : '';
    qs('sched-recovery').value = (j.recovery !== undefined) ? j.recovery : '';
    qs('sched-power').value = (j.power_w !== undefined) ? j.power_w : '';
    qs('sched-tz').value = (j.tz_offset !== undefined) ? j.tz_offset : '';
    setText('sched-status', 'in_window=' + (!!j.in_window) + ' battery_ready=' + (!!j.battery_ready));
  }

  function loadSchedule() {
    fetchWithTimeout('/api/auto/config', { cache: 'no-store' }, FETCH_TIMEOUT_MS)
      .then(function (r) { return r.json(); })
      .then(applyScheduleToForm)
      .catch(function () {});
  }

  qs('sched-reload').addEventListener('click', loadSchedule);
  qs('sched-save').addEventListener('click', function () {
    var params = new URLSearchParams();
    params.append('enabled', qs('sched-enabled').checked ? '1' : '0');
    params.append('start', qs('sched-start').value);
    params.append('end', qs('sched-end').value);
    params.append('threshold', qs('sched-threshold').value);
    params.append('recovery', qs('sched-recovery').value);
    params.append('power_w', qs('sched-power').value);
    params.append('tz_offset', qs('sched-tz').value);
    params.append('code', codeInput.value || '');
    setText('sched-result', 'Menyimpan...');
    fetchWithTimeout('/api/auto/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: params.toString()
    }, FETCH_TIMEOUT_MS)
      .then(function (r) { return r.json(); })
      .then(function (j) {
        if (j.ok) { applyScheduleToForm(j); setText('sched-result', 'Tersimpan.'); }
        else setText('sched-result', 'Gagal: ' + (j.error || 'tidak diketahui'));
      })
      .catch(function (e) { setText('sched-result', 'Gagal: ' + e); });
  });

  function sendCommand(cmd, args) {
    var code = codeInput.value || '';
    var body = { cmd: cmd };
    if (args) body.args = args;
    return fetchWithTimeout('/api/command?code=' + encodeURIComponent(code), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    }, FETCH_TIMEOUT_MS).then(function (r) {
      return r.json().catch(function () { return {}; }).then(function (j) {
        return { status: r.status, body: j };
      });
    });
  }

  function showCmdResult(text) { setText('cmd-result', text); }

  qs('btn-enable').addEventListener('click', function () {
    if (!confirm('Yakin ENABLE BESS?')) return;
    showCmdResult('Mengirim enable...');
    sendCommand('enable')
      .then(function (res) { showCmdResult('enable -> HTTP ' + res.status + ' ' + JSON.stringify(res.body)); })
      .catch(function (e) { showCmdResult('enable gagal: ' + e); });
  });

  qs('btn-disable').addEventListener('click', function () {
    if (!confirm('Yakin DISABLE BESS?')) return;
    showCmdResult('Mengirim disable...');
    sendCommand('disable')
      .then(function (res) { showCmdResult('disable -> HTTP ' + res.status + ' ' + JSON.stringify(res.body)); })
      .catch(function (e) { showCmdResult('disable gagal: ' + e); });
  });

  qs('btn-setoutput').addEventListener('click', function () {
    var w = parseFloat(qs('power-w').value);
    if (isNaN(w)) { showCmdResult('Daya tidak valid'); return; }
    showCmdResult('Mengirim set_output...');
    sendCommand('set_output', { power_w: w })
      .then(function (res) { showCmdResult('set_output -> HTTP ' + res.status + ' ' + JSON.stringify(res.body)); })
      .catch(function (e) { showCmdResult('set_output gagal: ' + e); });
  });

  loadSchedule();
  pollLoop();
})();
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// helper JSON kecil (pola sama dengan web.cpp) -- err statis, checkCodeD
// mendelegasikan ke provCheckCode (sub-proyek E) supaya /api/command wajib
// gateway_code juga (paritas keselamatan dengan /api/wifi/* dan
// /api/auto/config: LAN "trusted" saja tak cukup untuk konverter 50 kW).
// ---------------------------------------------------------------------------
static void sendJsonD(int code, const char* json) {
    webServer().send(code, "application/json", json);
}

static void sendErrD(int code, const char* err) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", err);
    sendJsonD(code, buf);
}

// Auth /api/command: `code` sebagai query/form arg (`?code=<gateway_code>`),
// SAMA jalur dengan checkCode() di web.cpp -- BUKAN field "code" di body
// JSON (body JSON dipakai murni sebagai command, identik dengan payload
// MQTT). Didokumentasikan di firmware/README.md §Dashboard & API lokal.
static bool checkCodeD() {
    String code = webServer().arg("code");
    if (provCheckCode(code.c_str())) return true;
    sendErrD(403, "forbidden");
    return false;
}

static void handleRoot() {
    webServer().send_P(200, "text/html", DASHBOARD_HTML);
}

// ---------------------------------------------------------------------------
// GET /api/data -- JSON PERSIS sama builder dengan telemetri MQTT
// (buildTelemetryJson, lihat lib/bess_core/payload.h) supaya dashboard dan
// cloud membaca satu kontrak yang sama. `seq` SENGAJA memakai nilai
// `g_state.seq` SAAT INI TANPA increment -- seq itu milik telemetri MQTT
// (di-increment HANYA oleh main.cpp::loop() tiap kirim telemetri), lihat
// src/sysinfo.h.
// ---------------------------------------------------------------------------
static void handleApiData() {
    SysInfo si{};
    fillSysInfo(si);
    stateLock();
    si.seq = g_state.seq;             // TANPA increment -- lihat sysinfo.h
    BessData snapshot = g_state.bess;
    stateUnlock();
    static char json[TELEMETRY_JSON_MAX];
    size_t n = buildTelemetryJson(si, snapshot, json, sizeof(json));
    webServer().sendHeader("Cache-Control", "no-store");
    if (n == 0) { sendErrD(500, "build_failed"); return; }
    webServer().send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// GET /api/acks -- 8 ack terakhir (ring buffer task_cmd.cpp), terbaru dulu,
// sebagai array JSON objek ack ASLI (bentuk sama dengan device/<gw>/command/ack
// MQTT) -- lihat lib/bess_core/ack_ring.h.
// ---------------------------------------------------------------------------
static void handleApiAcks() {
    static char buf[ACK_RING_JSON_CAP];
    size_t n = taskCmdGetAcksJson(buf, sizeof(buf));
    webServer().sendHeader("Cache-Control", "no-store");
    webServer().send(200, "application/json", n ? buf : "[]");
}

// ---------------------------------------------------------------------------
// POST /api/command -- body JSON command PERSIS seperti MQTT
// {"id","cmd","args"} (id opsional -- dibangkitkan "web-<millis>" kalau
// tidak ada, lihat lib/bess_core/web_cmd.h). Command dari sini dianggap
// MANUAL (seperti cloud) -- menonaktifkan jadwal (schedule.h) sama seperti
// command MQTT. Auth lewat checkCodeD() (query/form arg `code`).
// ---------------------------------------------------------------------------
static void handleApiCommand() {
    if (!checkCodeD()) return;
    String body = webServer().arg("plain");
    if (body.length() == 0) { sendErrD(400, "empty_body"); return; }
    if (body.length() > CMD_JSON_MAX) { sendErrD(413, "payload_too_large"); return; }

    static char buf[CMD_JSON_MAX + 1];
    char id[40];
    size_t n = 0;
    if (!webCmdEnsureId(body.c_str(), body.length(), millis(), buf, sizeof(buf), n, id, sizeof(id))) {
        // Body bukan objek JSON valid -- diteruskan APA ADANYA ke task_cmd,
        // yang menjawabnya "bad_json"/"unsupported_cmd" lewat parseCommand --
        // satu kontrak kegagalan dengan command MQTT yang korup, bukan dua
        // (lihat lib/bess_core/web_cmd.h).
        n = body.length();
        memcpy(buf, body.c_str(), n);
        buf[n] = 0;
        id[0] = 0;
    }
    if (!taskCmdSubmitWeb(buf, n)) { sendErrD(503, "queue_full"); return; }

    // JsonDocument (bukan snprintf %s) -- `id` bisa berasal dari body yang
    // dikirim operator/browser dan boleh berisi kutip/backslash; builder
    // ArduinoJson meng-escape-nya dengan benar, snprintf polos tidak.
    JsonDocument resp;
    resp["queued"] = true;
    resp["id"] = id;
    char out[96];
    size_t rn = serializeJson(resp, out, sizeof(out));
    if (rn == 0) { sendJsonD(202, "{\"queued\":true}"); return; }
    webServer().send(202, "application/json", out);
}

// ---------------------------------------------------------------------------
// GET /api/firmware_versions
// ---------------------------------------------------------------------------
static void handleApiFirmwareVersions() {
    OtaInfo info{};
    otaGetInfo(info);
    JsonDocument doc;
    doc["firmware_version"] = FW_VERSION;
    doc["running_partition"] = info.running_partition;
    JsonObject ota = doc["ota"].to<JsonObject>();
    ota["state"] = info.state[0] ? info.state : "idle";
    ota["id"] = info.id;
    ota["pending_verify"] = info.pending_verify;
    char buf[256];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    webServer().sendHeader("Cache-Control", "no-store");
    if (n == 0) { sendErrD(500, "build_failed"); return; }
    webServer().send(200, "application/json", buf);
}

void webDashboardInit() {
    webServer().on("/", HTTP_GET, handleRoot);
    webServer().on("/api/data", HTTP_GET, handleApiData);
    webServer().on("/api/acks", HTTP_GET, handleApiAcks);
    webServer().on("/api/command", HTTP_POST, handleApiCommand);
    webServer().on("/api/firmware_versions", HTTP_GET, handleApiFirmwareVersions);
    Serial.println("[web] dashboard + API lokal terdaftar (sub-proyek H)");
}
