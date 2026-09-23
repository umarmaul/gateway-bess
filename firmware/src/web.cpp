#include "web.h"
#include <WiFi.h>
#include "prov.h"
#include "prov_logic.h"
#include "config.h"

static WebServer s_server(80);

WebServer& webServer() { return s_server; }

// ---------------------------------------------------------------------------
// helper JSON kecil -- semua field bernilai tetap (bukan input pengguna),
// jadi snprintf polos aman dipakai tanpa escaping tambahan.
// ---------------------------------------------------------------------------
static void sendJson(int code, const char* json) {
    s_server.send(code, "application/json", json);
}

static void sendErr(int code, const char* err) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", err);
    sendJson(code, buf);
}

static bool checkCode() {
    String code = s_server.arg("code");
    if (provCheckCode(code.c_str())) return true;
    sendErr(403, "forbidden");
    return false;
}

// Escape minimal untuk nilai yang dirender ke dalam HTML (SSID bisa berisi
// karakter apa saja) -- halaman ini tak berbahaya secara jaringan (AP sudah
// WPA2 + gerbang `code`), tapi tetap tak boleh membiarkan SSID aneh merusak
// struktur halaman.
static String htmlEscape(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// GET /wifi
// ---------------------------------------------------------------------------
static void handleWifiPage() {
    bool connected = WiFi.status() == WL_CONNECTED;
    String sta_ssid = connected ? WiFi.SSID() : String(provStaSsid());
    String sta_ip = connected ? WiFi.localIP().toString() : String("-");

    String page;
    page.reserve(2600);
    page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<title>BEP Gateway - WiFi</title><style>"
               "body{font-family:sans-serif;max-width:480px;margin:16px auto;padding:0 12px}"
               "fieldset{margin-bottom:20px}label{display:block;margin-top:8px}"
               "input{width:100%;box-sizing:border-box;padding:6px;margin-top:2px}"
               "button{margin-top:12px;padding:8px 16px}"
               "table{width:100%}td{padding:2px 4px}"
               "</style></head><body>");
    page += F("<h2>BEP Gateway BESS - Provisioning</h2>");
    page += F("<table><tr><td>SSID router</td><td>");
    page += htmlEscape(sta_ssid);
    page += F("</td></tr><tr><td>STA IP</td><td>");
    page += sta_ip;
    page += F("</td></tr><tr><td>AP fallback</td><td>");
    page += htmlEscape(String(provApSsid()));
    page += provApActive() ? F(" (menyala)") : F(" (mati)");
    page += F("</td></tr><tr><td>mDNS</td><td>");
    page += htmlEscape(String(provMdnsHostname()));
    page += F(".local</td></tr><tr><td>gateway_code</td><td><b>");
    // Ditampilkan APA ADANYA (bukan disamarkan): halaman ini hanya bisa
    // diakses lewat AP yang sudah WPA2, dan code inilah gerbang endpoint
    // provisioning -- operator butuh melihatnya untuk mengisi form.
    page += provGatewayCode();
    page += F("</b></td></tr></table>");

    page += F("<fieldset><legend>Simpan WiFi router</legend>"
               "<form id='f1'>"
               "<label>SSID<input name='ssid' maxlength='32' required></label>"
               "<label>Password (kosong = jaringan terbuka)<input name='pass' maxlength='63'></label>"
               "<label>mDNS hostname<input name='mdns' maxlength='63' value='");
    page += htmlEscape(String(provMdnsHostname()));
    page += F("'></label>"
               "<label><input type='checkbox' name='sta_static' value='1' style='width:auto'> IP statis</label>"
               "<label>IP<input name='sta_ip' placeholder='192.168.1.50'></label>"
               "<label>Gateway<input name='sta_gw' placeholder='192.168.1.1'></label>"
               "<label>Subnet mask<input name='sta_mask' placeholder='255.255.255.0'></label>"
               "<label>DNS 1<input name='sta_dns1' placeholder='8.8.8.8'></label>"
               "<label>DNS 2<input name='sta_dns2' placeholder='8.8.4.4'></label>"
               "<label>gateway_code<input name='code' maxlength='6' required></label>"
               "<button type='button' onclick='save(\"f1\",\"/api/wifi/save\")'>Simpan &amp; reboot</button>"
               "</form></fieldset>");

    page += F("<fieldset><legend>Ganti AP fallback</legend>"
               "<form id='f2'>"
               "<label>SSID AP (kosong = pertahankan default)<input name='ap_ssid' maxlength='32'></label>"
               "<label>Password AP (8-63 char)<input name='ap_pass' maxlength='63' minlength='8' required></label>"
               "<label>gateway_code<input name='code' maxlength='6' required></label>"
               "<button type='button' onclick='save(\"f2\",\"/api/wifi/ap\")'>Simpan &amp; reboot</button>"
               "</form></fieldset>");

    page += F("<fieldset><legend>Lupakan WiFi router</legend>"
               "<form id='f3'>"
               "<label>gateway_code<input name='code' maxlength='6' required></label>"
               "<button type='button' onclick='save(\"f3\",\"/api/wifi/forget\")'>Lupakan &amp; reboot</button>"
               "</form></fieldset>");

    page += F("<pre id='out'></pre><script>"
               "function save(formId,url){"
               "var f=document.getElementById(formId);var fd=new FormData(f);"
               "var params=new URLSearchParams();"
               "fd.forEach(function(v,k){params.append(k,v);});"
               "if(!fd.has('sta_static'))params.append('sta_static','0');"
               "fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()})"
               ".then(function(r){return r.json();})"
               ".then(function(j){document.getElementById('out').textContent=JSON.stringify(j);})"
               ".catch(function(e){document.getElementById('out').textContent=String(e);});"
               "}</script></body></html>");

    s_server.send(200, "text/html", page);
}

// ---------------------------------------------------------------------------
// POST /api/wifi/save
// ---------------------------------------------------------------------------
static void handleWifiSave() {
    if (!checkCode()) return;
    String ssid = s_server.arg("ssid");
    String pass = s_server.arg("pass");
    String mdns = s_server.arg("mdns");
    bool sta_static = s_server.arg("sta_static") == "1" || s_server.arg("sta_static") == "true";
    String ip = s_server.arg("sta_ip"), gw = s_server.arg("sta_gw"), mask = s_server.arg("sta_mask");
    String dns1 = s_server.arg("sta_dns1"), dns2 = s_server.arg("sta_dns2");

    ProvResult r = provSaveWifi(ssid.c_str(), pass.c_str(), mdns.c_str(), sta_static,
                                 ip.c_str(), gw.c_str(), mask.c_str(), dns1.c_str(), dns2.c_str());
    if (r != ProvResult::OK) { sendErr(400, provResultError(r)); return; }
    sendJson(200, "{\"ok\":true,\"restarting\":true}");
    provScheduleReboot(PROV_REBOOT_DELAY_MS);
}

// ---------------------------------------------------------------------------
// POST /api/wifi/ap
// ---------------------------------------------------------------------------
static void handleWifiAp() {
    if (!checkCode()) return;
    String ap_ssid = s_server.arg("ap_ssid");
    String ap_pass = s_server.arg("ap_pass");
    ProvResult r = provSaveAp(ap_ssid.c_str(), ap_pass.c_str());
    if (r != ProvResult::OK) { sendErr(400, provResultError(r)); return; }
    sendJson(200, "{\"ok\":true,\"restarting\":true}");
    provScheduleReboot(PROV_REBOOT_DELAY_MS);
}

// ---------------------------------------------------------------------------
// POST /api/wifi/forget
// ---------------------------------------------------------------------------
static void handleWifiForget() {
    if (!checkCode()) return;
    provForgetWifi();
    sendJson(200, "{\"ok\":true,\"restarting\":true}");
    provScheduleReboot(PROV_REBOOT_DELAY_MS);
}

// ---------------------------------------------------------------------------
// 404 -- captive portal: klien lewat AP diarahkan ke /wifi
// ---------------------------------------------------------------------------
static void handleNotFound() {
    if (provApActive()) {
        s_server.sendHeader("Location", "http://192.168.4.1/wifi", true);
        s_server.send(302, "text/plain", "");
        return;
    }
    s_server.send(404, "text/plain", "not found");
}

void webInit() {
    s_server.on("/wifi", HTTP_GET, handleWifiPage);
    s_server.on("/api/wifi/save", HTTP_POST, handleWifiSave);
    s_server.on("/api/wifi/ap", HTTP_POST, handleWifiAp);
    s_server.on("/api/wifi/forget", HTTP_POST, handleWifiForget);
    s_server.onNotFound(handleNotFound);
    s_server.begin();
    Serial.println("[web] server HTTP mulai (port 80)");
}

void webTick() { s_server.handleClient(); }
