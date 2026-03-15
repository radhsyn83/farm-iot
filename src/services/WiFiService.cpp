#include "WiFiService.h"
#include "PeripheralManager.h"
#include "SensorService.h"
#include "LampService.h"
#include "MqttService.h"
#include "FailsafeService.h"
#include "StateService.h"
#include "../config.h"
#include "../web_api_bridge.h"

// ===== FW version (shown in /api/status and /system) =====
static const char* FW_VERSION = "2.1.0";

void WiFiService::begin() {
  Logger::info("WiFiService begin()");
  WiFi.persistent(false);
  WiFi.onEvent(WiFiService::WiFiEvent);
  registerRoutes();

  if (!trySavedWifi(10000)) {
    Logger::warn("Saved WiFi failed, trying config.h SSID: %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    connect(WIFI_SSID, WIFI_PASSWORD);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Logger::info("Connected. IP: %s", WiFi.localIP().toString().c_str());
    startMdns();
  }

  if (!serverActive) {
    server.begin();
    serverActive = true;
    Logger::info("Web server started");
  }
}

bool WiFiService::isConnected() { return WiFi.status() == WL_CONNECTED; }
IPAddress WiFiService::ip() { return WiFi.localIP(); }

bool WiFiService::hasInternetPing() {
  if (!isConnected()) return false;
  return Ping.ping("103.175.220.242", 3);
}

void WiFiService::loop() {
  if (serverActive || provisioningActive) server.handleClient();
  if (provisioningActive) dns.processNextRequest();
}

void WiFiService::resetCredentials() {
  prefs.begin(NVS_NS, false);
  prefs.clear();
  prefs.end();
  WiFi.disconnect(true, true);
  Logger::warn("WiFi credentials cleared");
}

void WiFiService::startProvisioningAP() {
  if (provisioningActive) return;
  wifiReconnectTimer.detach();
  reconnectAttempts = 0;
  startCaptiveAP();
  provisioningActive = true;
}

void WiFiService::stopProvisioningAP() {
  if (!provisioningActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  provisioningActive = false;
  if (!serverActive) { server.begin(); serverActive = true; }
  startMdns();
  Logger::info("Provisioning AP stopped, web server stays active");
}

void WiFiService::startMdns() {
  String host = String(DEVICE_ID);
  host.toLowerCase();
  for (int i = 0; i < (int)host.length(); i++) {
    char c = host[i];
    if (!isalnum(c) && c != '-') host[i] = '-';
  }
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Logger::info("mDNS started: http://%s.local", host.c_str());
  } else {
    Logger::warn("mDNS start failed");
  }
}

String WiFiService::mdnsName() {
  String host = String(DEVICE_ID);
  host.toLowerCase();
  for (int i = 0; i < (int)host.length(); i++) {
    char c = host[i];
    if (!isalnum(c) && c != '-') host[i] = '-';
  }
  return host + ".local";
}

/* ================== Private ================== */

bool WiFiService::trySavedWifi(uint32_t timeoutMs) {
  String ssid, pass;
  if (!loadCredentials(ssid, pass) || ssid.isEmpty()) return false;
  Logger::info("Connecting to saved SSID: %s ...", ssid.c_str());
  WiFi.mode(WIFI_STA);
  connect(ssid, pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) delay(200);
  return WiFi.status() == WL_CONNECTED;
}

void WiFiService::connect(const String& ssid, const String& pass) {
  WiFi.begin(ssid.c_str(), pass.c_str());
}

void WiFiService::WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_CONNECTED:
      Logger::info("WiFi associated");
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      reconnectAttempts = 0;
      Logger::info("Got IP: %s", WiFi.localIP().toString().c_str());
      startMdns();
      configTime(NTP_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Logger::warn("WiFi disconnected, scheduling reconnect...");
      scheduleReconnect(3);
      break;
    default: break;
  }
}

void WiFiService::scheduleReconnect(uint32_t sec) {
  if (provisioningActive) return;
  uint32_t backoff = sec;
  for (uint8_t i = 0; i < reconnectAttempts && i < 4; i++) backoff *= 2;
  if (backoff > 30) backoff = 30;

  wifiReconnectTimer.once(backoff, [](){
    if (provisioningActive) return;
    reconnectAttempts++;
    if (reconnectAttempts > MAX_ATTEMPTS_BEFORE_RESET) {
      Logger::warn("WiFi stuck after %d attempts — full reset cycle", reconnectAttempts);
      WiFi.disconnect(true); delay(100); reconnectAttempts = 0;
    }
    String ssid, pass;
    if (loadCredentials(ssid, pass) && !ssid.isEmpty()) {
      Logger::info("Reconnecting to saved SSID: %s (attempt %d)", ssid.c_str(), reconnectAttempts);
      connect(ssid, pass);
    } else {
      Logger::info("Reconnecting to config SSID: %s (attempt %d)", WIFI_SSID, reconnectAttempts);
      connect(WIFI_SSID, WIFI_PASSWORD);
    }
  });
}

/* ========================================================================= */
/*                              SHARED CSS                                   */
/* ========================================================================= */
static const char APP_CSS[] PROGMEM = R"CSS(
:root{--bg:#0f172a;--card:#1e293b;--text:#e2e8f0;--muted:#94a3b8;--primary:#3b82f6;
--ok:#22c55e;--warn:#eab308;--danger:#ef4444;--cold:#38bdf8;--border:#334155}
*{box-sizing:border-box;margin:0}
body{background:var(--bg);color:var(--text);font-family:ui-sans-serif,system-ui,Arial;padding-bottom:72px}
.page{max-width:480px;margin:0 auto;padding:16px}
.card{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:16px;margin-bottom:12px}
h1{font-size:20px;margin-bottom:4px}
h2{font-size:16px;margin:12px 0 8px;color:var(--muted)}
.row{display:flex;gap:10px;align-items:center;margin:8px 0}
.btn{padding:12px 16px;border:0;border-radius:10px;background:var(--primary);color:#fff;cursor:pointer;
font-weight:600;font-size:14px;flex:1;text-align:center;text-decoration:none}
.btn.sm{padding:8px 12px;font-size:13px;flex:none}
.btn.secondary{background:var(--border);color:var(--text)}
.btn.danger{background:var(--danger)}
.btn.ok{background:var(--ok);color:#000}
.btn.link{background:transparent;color:var(--primary);padding:0;flex:none}
.input,select{flex:1;padding:10px;border:1px solid var(--border);border-radius:10px;
background:var(--bg);color:var(--text);font-size:14px}
.input.sm{padding:8px;font-size:13px}
.list{display:flex;flex-direction:column;gap:8px;margin:8px 0}
.item{padding:12px;border:1px solid var(--border);border-radius:12px;display:flex;
justify-content:space-between;align-items:center;background:var(--card)}
.item .left{flex:1;display:flex;flex-direction:column}
.badge{font-size:12px;color:var(--muted)}
.pill{display:inline-block;padding:3px 8px;border-radius:20px;font-size:11px;font-weight:700}
.pill.ok{background:#16a34a22;color:var(--ok)}
.pill.warn{background:#eab30822;color:var(--warn)}
.pill.danger{background:#ef444422;color:var(--danger)}
.pill.cold{background:#38bdf822;color:var(--cold)}
.pill.idle{background:var(--border);color:var(--muted)}
.alert{display:none;margin:10px 0;padding:12px;border-radius:10px;border:1px solid}
.alert.info{display:block;background:#1e3a5f;color:#93c5fd;border-color:#3b82f6}
.alert.ok{display:block;background:#14532d;color:#86efac;border-color:#22c55e}
.alert.err{display:block;background:#450a0a;color:#fca5a5;border-color:#ef4444}
.big-temp{font-size:48px;font-weight:800;text-align:center;margin:8px 0}
.big-temp .unit{font-size:20px;color:var(--muted)}
.sensor-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:8px}
.s-card{background:var(--bg);border:1px solid var(--border);border-radius:12px;padding:12px;text-align:center}
.s-card .val{font-size:24px;font-weight:700}
.s-card .hum{font-size:14px;color:var(--muted)}
.s-card .label{font-size:11px;color:var(--muted);margin-top:4px}
.toggle{position:relative;width:52px;height:28px;flex:none}
.toggle input{opacity:0;width:0;height:0}
.toggle .slider{position:absolute;inset:0;background:var(--border);border-radius:28px;cursor:pointer;transition:.3s}
.toggle .slider:before{content:'';position:absolute;width:22px;height:22px;left:3px;bottom:3px;
background:#fff;border-radius:50%;transition:.3s}
.toggle input:checked+.slider{background:var(--ok)}
.toggle input:checked+.slider:before{transform:translateX(24px)}
.lamp-card{background:var(--bg);border:1px solid var(--border);border-radius:12px;padding:14px}
.lamp-card .head{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
.lamp-card .head b{font-size:15px}
.slider-row{display:flex;gap:8px;align-items:center}
.slider-row input[type=range]{flex:1;accent-color:var(--primary);height:24px}
.slider-row .pct{width:40px;text-align:right;font-weight:700}
nav{position:fixed;bottom:0;left:0;right:0;background:var(--card);border-top:1px solid var(--border);
display:flex;z-index:99}
nav a{flex:1;padding:10px 0 8px;text-align:center;color:var(--muted);text-decoration:none;font-size:11px;
display:flex;flex-direction:column;align-items:center;gap:2px}
nav a.active{color:var(--primary)}
nav a svg{width:22px;height:22px;fill:currentColor}
.section{margin:14px 0}
.linkback{margin-top:12px;text-align:center}
.sys-row{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid var(--border)}
.sys-row:last-child{border:none}
.sys-row .k{color:var(--muted);font-size:13px}
.sys-row .v{font-weight:600;font-size:13px}
)CSS";

// SVG icons for nav (compact)
#define IC_HOME "<svg viewBox='0 0 24 24'><path d='M3 12l9-9 9 9M5 10v10a1 1 0 001 1h3v-5h6v5h3a1 1 0 001-1V10'/></svg>"
#define IC_TEMP "<svg viewBox='0 0 24 24'><path d='M12 2a3 3 0 00-3 3v8.26A5 5 0 1017 17a5 5 0 00-2-3.74V5a3 3 0 00-3-3z'/></svg>"
#define IC_LAMP "<svg viewBox='0 0 24 24'><path d='M9 21h6M12 3a6 6 0 014 10.47V17a1 1 0 01-1 1H9a1 1 0 01-1-1v-3.53A6 6 0 0112 3z'/></svg>"
#define IC_SYS  "<svg viewBox='0 0 24 24'><path d='M12 15a3 3 0 100-6 3 3 0 000 6zM19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 11-2.83 2.83l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 11-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 11-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 110-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 112.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 114 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 112.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09a1.65 1.65 0 00-1.51 1z'/></svg>"
#define IC_CFG  "<svg viewBox='0 0 24 24'><path d='M4 6h16M4 12h16M4 18h16'/></svg>"

// Bottom nav HTML snippet
static String navBar(const char* active) {
  String n = "<nav>";
  auto link = [&](const char* href, const char* icon, const char* label) {
    n += "<a href='"; n += href; n += "'";
    if (strcmp(active, href) == 0) n += " class='active'";
    n += ">"; n += icon; n += label; n += "</a>";
  };
  link("/",       IC_HOME, "Home");
  link("/temp",   IC_TEMP, "Temp");
  link("/lamp",   IC_LAMP, "Lamp");
  link("/system", IC_SYS,  "System");
  link("/config", IC_CFG,  "Config");
  n += "</nav>";
  return n;
}

static String pageHead(const char* title) {
  String h = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/><title>";
  h += title;
  h += "</title><link rel='stylesheet' href='/app.css'></head><body>";
  return h;
}

/* ========= Captive Portal ========= */
void WiFiService::handleCaptivePortal() {
  if (!provisioningActive) { server.send(404, "text/plain", "Not found"); return; }
  String url = "http://" + WiFi.softAPIP().toString() + "/wifi";
  server.sendHeader("Location", url, true);
  server.send(302, "text/plain", "");
}

/* ========= Route registration ========= */
void WiFiService::registerRoutes() {
  // UI pages
  server.on("/",        HTTP_GET, handleHome);
  server.on("/wifi",    HTTP_GET, handleWifi);
  server.on("/temp",    HTTP_GET, handleTemp);
  server.on("/lamp",    HTTP_GET, handleLamp);
  server.on("/system",  HTTP_GET, handleSystem);
  server.on("/config",  HTTP_GET, handleConfigPage);
  server.on("/app.css", HTTP_GET, handleCss);

  // Wi-Fi API
  server.on("/wifi/scan",      HTTP_GET,  handleScan);
  server.on("/wifi/provision", HTTP_POST, handleProvision);
  server.on("/wifi/status",    HTTP_GET,  handleWifiStatus);
  server.on("/wifi/reset",     HTTP_POST, handleWifiReset);

  // Peripheral config API
  server.on("/api/peripherals", HTTP_GET,  handleGetPeripherals);
  server.on("/api/peripherals", HTTP_POST, handlePostPeripherals);

  // New APIs
  server.on("/api/status",    HTTP_GET,  handleApiStatus);
  server.on("/api/lamp",      HTTP_POST, handleApiLamp);
  server.on("/api/master",    HTTP_POST, handleApiMaster);
  server.on("/api/setpoint",  HTTP_POST, handleApiSetpoint);
  server.on("/api/failsafe",  HTTP_POST, handleApiFailsafe);
  server.on("/api/restart",   HTTP_POST, handleApiRestart);

  // Legacy
  server.on("/status", HTTP_GET, handleWifiStatus);
  server.on("/reset",  HTTP_POST, handleWifiReset);

  // Captive portal
  server.on("/generate_204",         HTTP_GET, handleCaptivePortal);
  server.on("/gen_204",              HTTP_GET, handleCaptivePortal);
  server.on("/hotspot-detect.html",  HTTP_GET, handleCaptivePortal);
  server.on("/connecttest.txt",      HTTP_GET, handleCaptivePortal);
  server.on("/redirect",            HTTP_GET, handleCaptivePortal);
  server.on("/ncsi.txt",            HTTP_GET, handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);
}

/* ========= CSS ========= */
void WiFiService::handleCss() { server.send(200, "text/css", APP_CSS); }

/* ========================================================================= */
/*                         GET /api/status                                   */
/* ========================================================================= */
void WiFiService::handleApiStatus() {
  const auto& cfg = PeripheralManager::config();
  String j = "{";

  // sensors
  j += "\"sensors\":[";
  for (uint8_t i = 0; i < cfg.sensorCount; i++) {
    if (i) j += ",";
    const char* st = g_outlier[i] ? "outlier" : (g_suspect[i] ? "suspect" : "ok");
    j += "{\"id\":\""; j += cfg.sensors[i].id;
    j += "\",\"pin\":"; j += cfg.sensors[i].pin;
    j += ",\"t\":"; j += isnan(g_readings[i].temp) ? "null" : String(g_readings[i].temp, 1);
    j += ",\"h\":"; j += isnan(g_readings[i].hum) ? "null" : String(g_readings[i].hum, 1);
    j += ",\"status\":\""; j += st; j += "\"}";
  }
  j += "],";

  // lamps
  j += "\"lamps\":[";
  for (uint8_t i = 0; i < cfg.lampCount; i++) {
    if (i) j += ",";
    LampState ls = LampService::getLamp(i);
    const char* mode = "relay";
    if (cfg.lamps[i].mode == LampMode::PWM_DC) mode = "pwm";
    else if (cfg.lamps[i].mode == LampMode::ROBOTDYN_AC) mode = "ac_dimmer";
    j += "{\"id\":\""; j += cfg.lamps[i].id;
    j += "\",\"pin\":"; j += cfg.lamps[i].pin;
    j += ",\"mode\":\""; j += mode;
    j += "\",\"power\":"; j += String(ls.power, 2); j += "}";
  }
  j += "],";

  // master
  j += "\"master\":"; j += LampService::getMaster() ? "true" : "false"; j += ",";

  // failsafe
  const char* modeStr = "IDLE";
  if (Failsafe::currentMode() == Failsafe::Mode::HEATING) modeStr = "HEATING";
  else if (Failsafe::currentMode() == Failsafe::Mode::COOLING) modeStr = "COOLING";
  j += "\"failsafe\":{\"mode\":\""; j += modeStr;
  j += "\",\"auto\":"; j += g_fsAuto ? "true" : "false";
  j += ",\"hard_min\":"; j += String(g_hardMin, 1);
  j += ",\"hard_max\":"; j += String(g_hardMax, 1);
  j += ",\"hyst\":"; j += String(g_hystC, 1);
  j += ",\"setpoint\":"; j += String(g_setpoint, 1);
  j += ",\"time_fallback\":"; j += g_timeFallbackActive ? "true" : "false";
  j += "},";

  // avg_temp
  float avgT = 0; uint8_t vc = 0;
  for (uint8_t i = 0; i < cfg.sensorCount; i++) {
    float t = g_readings[i].temp;
    if (!isnan(t) && t > 0 && t < 80 && !g_outlier[i]) { avgT += t; vc++; }
  }
  if (vc > 0) { j += "\"avg_temp\":"; j += String(avgT / vc, 1); }
  else { j += "\"avg_temp\":null"; }
  j += ",";

  // system
  j += "\"system\":{\"device_id\":\""; j += DEVICE_ID;
  j += "\",\"fw\":\""; j += FW_VERSION;
  j += "\",\"uptime_s\":"; j += millis() / 1000;
  j += ",\"wifi_rssi\":"; j += isConnected() ? String(WiFi.RSSI()) : "0";
  j += ",\"wifi_ssid\":\""; j += isConnected() ? WiFi.SSID() : ""; j += "\"";
  j += ",\"mqtt\":"; j += MqttService::connected() ? "true" : "false";
  j += ",\"heap\":"; j += ESP.getFreeHeap();
  // time
  time_t now; time(&now);
  struct tm ti; localtime_r(&now, &ti);
  if (ti.tm_year > 100) {
    char tbuf[26]; strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%S", &ti);
    j += ",\"time\":\""; j += tbuf; j += "\"";
  } else {
    j += ",\"time\":null";
  }
  j += "}}";

  server.send(200, "application/json", j);
}

/* ========================================================================= */
/*                           POST APIs                                       */
/* ========================================================================= */

void WiFiService::handleApiLamp() {
  String body = server.arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) { server.send(400,"application/json","{\"ok\":false,\"error\":\"bad json\"}"); return; }

  const char* id = doc["id"];
  float power = doc["power"] | -1.0f;
  if (!id || power < 0) { server.send(400,"application/json","{\"ok\":false,\"error\":\"need id and power\"}"); return; }

  if (!LampService::setLampById(id, power)) {
    server.send(404,"application/json","{\"ok\":false,\"error\":\"lamp not found\"}"); return;
  }
  LampService::saveState();
  if (MqttService::connected()) MqttService::publishReportedLamp(id);
  server.send(200,"application/json","{\"ok\":true}");
}

void WiFiService::handleApiMaster() {
  String body = server.arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) { server.send(400,"application/json","{\"ok\":false,\"error\":\"bad json\"}"); return; }

  bool on = doc["on"] | false;
  LampService::setMaster(on);
  StateService::saveMaster(on);
  if (MqttService::connected()) MqttService::publishReportedMaster();
  server.send(200,"application/json","{\"ok\":true}");
}

void WiFiService::handleApiSetpoint() {
  String body = server.arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) { server.send(400,"application/json","{\"ok\":false,\"error\":\"bad json\"}"); return; }

  float val = doc["value"] | -999.0f;
  if (val < 0 || val > 60) { server.send(400,"application/json","{\"ok\":false,\"error\":\"invalid value\"}"); return; }

  webApplySetpoint(val);
  server.send(200,"application/json","{\"ok\":true}");
}

void WiFiService::handleApiFailsafe() {
  String body = server.arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) { server.send(400,"application/json","{\"ok\":false,\"error\":\"bad json\"}"); return; }

  float hMin = doc["hard_min"] | g_hardMin;
  float hMax = doc["hard_max"] | g_hardMax;
  float hyst = doc["hyst"] | g_hystC;
  bool  fsAuto = doc["auto"] | g_fsAuto;

  if (hMin >= hMax) { server.send(400,"application/json","{\"ok\":false,\"error\":\"hard_min must be < hard_max\"}"); return; }

  webApplyFailsafeConfig(hMin, hMax, hyst, fsAuto);
  server.send(200,"application/json","{\"ok\":true}");
}

void WiFiService::handleApiRestart() {
  server.send(200,"application/json","{\"ok\":true}");
  webApplyRestart(500);
}

/* ========================================================================= */
/*                          DASHBOARD (/)                                    */
/* ========================================================================= */
void WiFiService::handleHome() {
  String html = pageHead("Dashboard");
  html += "<div class='page'>";
  html += "<div class='card'><h1>" + String(DEVICE_ID) + "</h1>";
  html += "<div class='badge'>"; html += FW_VERSION;
  if (isConnected()) { html += " &bull; "; html += WiFi.localIP().toString(); }
  html += "</div></div>";

  // content filled by JS
  html += "<div id='dash'><div class='card'><div class='badge'>Loading...</div></div></div>";

  html += navBar("/");
  html += R"HTML(
<script>
const $=id=>document.getElementById(id);
function pill(cls,txt){return "<span class='pill "+cls+"'>"+txt+"</span>";}
function modeClass(m){return m==='HEATING'?'danger':m==='COOLING'?'cold':'idle';}
function tempClass(t,mn,mx){return t<mn?'cold':t>mx?'danger':'ok';}
function fmtTime(t){if(!t)return'Not synced';try{const d=new Date(t);return d.toLocaleDateString('id-ID',{day:'numeric',month:'short',year:'numeric'})+' '+d.toLocaleTimeString('id-ID',{hour:'2-digit',minute:'2-digit'});}catch(e){return t;}}
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),m=Math.floor((s%3600)/60);return(d?d+'d ':'')+h+'h '+m+'m';}
async function refresh(){
  try{
    const r=await fetch('/api/status');const d=await r.json();
    let h='';
    // avg temp card
    const at=d.avg_temp!==null?d.avg_temp.toFixed(1):'--';
    const tc=d.avg_temp!==null?tempClass(d.avg_temp,d.failsafe.hard_min,d.failsafe.hard_max):'muted';
    h+="<div class='card'><div style='display:flex;justify-content:space-between;align-items:center'>";
    h+="<div><div class='badge'>Average Temp</div><div class='big-temp' style='color:var(--"+tc+")'>"+at+"<span class='unit'>&deg;C</span></div></div>";
    h+="<div style='text-align:right'>"+pill(modeClass(d.failsafe.mode),d.failsafe.mode);
    h+="<br><span class='badge'>Setpoint "+d.failsafe.setpoint.toFixed(1)+"&deg;C</span></div></div></div>";

    // sensors
    h+="<div class='card'><h2>Sensors</h2><div class='sensor-grid'>";
    d.sensors.forEach(s=>{
      const sc=s.status==='ok'?'ok':(s.status==='suspect'?'warn':'danger');
      const tv=s.t!==null?s.t.toFixed(1)+'&deg;':'--';
      const hv=s.h!==null?s.h.toFixed(0)+'%':'--';
      h+="<div class='s-card'><div class='val' style='color:var(--"+sc+")'>"+tv+"</div>";
      h+="<div class='hum'>"+hv+"</div>";
      h+="<div class='label'>"+s.id+" "+pill(sc,s.status)+"</div></div>";
    });
    h+="</div></div>";

    // lamps
    h+="<div class='card'><h2>Lamps</h2>";
    h+="<div style='margin-bottom:8px'>Master: "+pill(d.master?'ok':'idle',d.master?'ON':'OFF')+"</div>";
    h+="<div class='list'>";
    d.lamps.forEach(l=>{
      const pct=Math.round(l.power*100);
      const lc=pct>0?'ok':'idle';
      h+="<div class='item'><div class='left'><b>"+l.id+"</b><span class='badge'>"+l.mode+" &bull; pin "+l.pin+"</span></div>";
      h+=pill(lc,pct+'%')+"</div>";
    });
    h+="</div></div>";

    // system bar
    h+="<div class='card'><h2>System</h2><div class='sys-row'><span class='k'>WiFi</span><span class='v'>"
      +(d.system.wifi_ssid||'—')+" ("+d.system.wifi_rssi+"dBm)</span></div>";
    h+="<div class='sys-row'><span class='k'>MQTT</span><span class='v'>"+pill(d.system.mqtt?'ok':'danger',d.system.mqtt?'Connected':'Offline')+"</span></div>";
    h+="<div class='sys-row'><span class='k'>Uptime</span><span class='v'>"+fmtUp(d.system.uptime_s)+"</span></div>";
    h+="<div class='sys-row'><span class='k'>Time</span><span class='v'>"+fmtTime(d.system.time)+"</span></div>";
    h+="<div class='sys-row'><span class='k'>Heap</span><span class='v'>"+(d.system.heap/1024).toFixed(0)+"KB</span></div>";
    h+="</div>";

    $('dash').innerHTML=h;
  }catch(e){$('dash').innerHTML="<div class='card'><div class='alert err'>Failed to load</div></div>";}
}
refresh();setInterval(refresh,5000);
</script>
)HTML";
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

/* ========================================================================= */
/*                      TEMPERATURE PAGE (/temp)                             */
/* ========================================================================= */
void WiFiService::handleTemp() {
  String html = pageHead("Temperature");
  html += "<div class='page'>";
  html += "<div id='content'><div class='card'><div class='badge'>Loading...</div></div></div>";
  html += navBar("/temp");
  html += R"HTML(
<script>
const $=id=>document.getElementById(id);
function pill(c,t){return "<span class='pill "+c+"'>"+t+"</span>";}
async function refresh(){
  try{
    const r=await fetch('/api/status');const d=await r.json();const f=d.failsafe;
    let h='';
    // Setpoint control
    h+="<div class='card'><h2>Setpoint</h2>";
    h+="<div class='row'><button class='btn sm secondary' onclick='adj(-0.5)'>-0.5</button>";
    h+="<div style='flex:1;text-align:center;font-size:28px;font-weight:800'>"+f.setpoint.toFixed(1)+"&deg;C</div>";
    h+="<button class='btn sm secondary' onclick='adj(0.5)'>+0.5</button></div></div>";

    // Sensors
    h+="<div class='card'><h2>Sensors</h2><div class='sensor-grid'>";
    d.sensors.forEach(s=>{
      const sc=s.status==='ok'?'ok':(s.status==='suspect'?'warn':'danger');
      const tv=s.t!==null?s.t.toFixed(1)+'&deg;':'--';
      const hv=s.h!==null?s.h.toFixed(0)+'%':'--';
      h+="<div class='s-card'><div class='val' style='color:var(--"+sc+")'>"+tv+"</div>";
      h+="<div class='hum'>"+hv+" RH</div>";
      h+="<div class='label'>"+s.id+" (pin "+s.pin+") "+pill(sc,s.status)+"</div></div>";
    });
    h+="</div></div>";

    // Failsafe config
    h+="<div class='card'><h2>Failsafe Config</h2>";
    h+="<div style='margin-bottom:8px'>Mode: "+pill(f.mode==='HEATING'?'danger':f.mode==='COOLING'?'cold':'idle',f.mode)+"</div>";
    h+="<div class='row'><span class='badge' style='width:80px'>Auto</span>";
    h+="<label class='toggle'><input type='checkbox' id='fsAuto' "+(f.auto?'checked':'')+"><span class='slider'></span></label></div>";
    h+="<div class='row'><span class='badge' style='width:80px'>Hard Min</span><input id='hMin' class='input sm' type='number' step='0.5' value='"+f.hard_min.toFixed(1)+"'></div>";
    h+="<div class='row'><span class='badge' style='width:80px'>Hard Max</span><input id='hMax' class='input sm' type='number' step='0.5' value='"+f.hard_max.toFixed(1)+"'></div>";
    h+="<div class='row'><span class='badge' style='width:80px'>Hysteresis</span><input id='hyst' class='input sm' type='number' step='0.5' value='"+f.hyst.toFixed(1)+"'></div>";
    h+="<div class='row'><button class='btn' onclick='saveFs()'>Save Failsafe</button></div>";
    h+="<div id='fsMsg' class='alert'></div></div>";

    $('content').innerHTML=h;
  }catch(e){$('content').innerHTML="<div class='card alert err'>Load failed</div>";}
}
async function adj(delta){
  try{
    const r=await fetch('/api/status');const d=await r.json();
    const nv=d.failsafe.setpoint+delta;
    await fetch('/api/setpoint',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({value:nv})});
    refresh();
  }catch(e){}
}
async function saveFs(){
  const b={hard_min:parseFloat($('hMin').value),hard_max:parseFloat($('hMax').value),
           hyst:parseFloat($('hyst').value),auto:$('fsAuto').checked};
  try{
    const r=await fetch('/api/failsafe',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});
    const j=await r.json();
    const el=$('fsMsg');
    if(j.ok){el.className='alert ok';el.textContent='Saved!';}
    else{el.className='alert err';el.textContent=j.error||'Failed';}
    setTimeout(()=>{el.className='alert';},3000);
    refresh();
  }catch(e){const el=$('fsMsg');el.className='alert err';el.textContent='Request failed';}
}
refresh();setInterval(refresh,5000);
</script>
)HTML";
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

/* ========================================================================= */
/*                       LAMP CONTROL PAGE (/lamp)                           */
/* ========================================================================= */
void WiFiService::handleLamp() {
  String html = pageHead("Lamp Control");
  html += "<div class='page'>";
  html += "<div id='content'><div class='card'><div class='badge'>Loading...</div></div></div>";
  html += navBar("/lamp");
  html += R"HTML(
<script>
const $=id=>document.getElementById(id);
function pill(c,t){return "<span class='pill "+c+"'>"+t+"</span>";}
let fsActive=false;
async function refresh(){
  try{
    const r=await fetch('/api/status');const d=await r.json();
    fsActive=d.failsafe.auto&&d.failsafe.mode!=='IDLE';
    let h='';
    // Master
    h+="<div class='card'><div style='display:flex;justify-content:space-between;align-items:center'>";
    h+="<div><h2 style='margin:0'>Master Relay</h2></div>";
    h+="<label class='toggle'><input type='checkbox' onchange='setMaster(this.checked)' "+(d.master?'checked':'')+"><span class='slider'></span></label>";
    h+="</div></div>";

    // Failsafe warning
    if(fsActive){
      h+="<div class='alert err' style='display:block'>Failsafe "+d.failsafe.mode+" — lamps controlled automatically</div>";
    }

    // Quick actions
    h+="<div class='row'><button class='btn ok' onclick='allLamps(1)'>All ON</button>";
    h+="<button class='btn danger' onclick='allLamps(0)'>All OFF</button></div>";

    // Lamps
    h+="<div class='list'>";
    d.lamps.forEach((l,i)=>{
      const pct=Math.round(l.power*100);
      const isRelay=l.mode==='relay';
      h+="<div class='lamp-card'><div class='head'><b>"+l.id+"</b>"+pill(l.mode==='relay'?'idle':'ok',l.mode)+"</div>";
      if(isRelay){
        h+="<div class='row'><span class='badge'>Power</span>";
        h+="<label class='toggle'><input type='checkbox' "+(pct>0?'checked':'')+" onchange=\"setLamp('"+l.id+"',this.checked?1:0)\"><span class='slider'></span></label></div>";
      }else{
        h+="<div class='slider-row'><input type='range' min='0' max='100' value='"+pct+"' id='sl_"+l.id+"' oninput=\"$('pct_"+l.id+"').textContent=this.value+'%'\" onchange=\"setLamp('"+l.id+"',this.value/100)\">";
        h+="<span class='pct' id='pct_"+l.id+"'>"+pct+"%</span></div>";
      }
      h+="<div class='badge'>Pin "+l.pin+"</div></div>";
    });
    h+="</div>";

    $('content').innerHTML=h;
  }catch(e){$('content').innerHTML="<div class='card alert err'>Load failed</div>";}
}
async function setLamp(id,power){
  await fetch('/api/lamp',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,power})});
}
async function setMaster(on){
  await fetch('/api/master',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({on})});
}
async function allLamps(p){
  const r=await fetch('/api/status');const d=await r.json();
  for(const l of d.lamps) await setLamp(l.id,p);
  refresh();
}
refresh();setInterval(refresh,5000);
</script>
)HTML";
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

/* ========================================================================= */
/*                        SYSTEM PAGE (/system)                              */
/* ========================================================================= */
void WiFiService::handleSystem() {
  String html = pageHead("System");
  html += "<div class='page'>";
  html += "<div id='content'><div class='card'><div class='badge'>Loading...</div></div></div>";
  html += navBar("/system");
  html += R"HTML(
<script>
const $=id=>document.getElementById(id);
function pill(c,t){return "<span class='pill "+c+"'>"+t+"</span>";}
function fmtTime(t){if(!t)return'Not synced';try{const d=new Date(t);return d.toLocaleDateString('id-ID',{day:'numeric',month:'short',year:'numeric'})+' '+d.toLocaleTimeString('id-ID',{hour:'2-digit',minute:'2-digit'});}catch(e){return t;}}
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),m=Math.floor((s%3600)/60);return(d?d+'d ':'')+h+'h '+m+'m';}
async function refresh(){
  try{
    const r=await fetch('/api/status');const d=await r.json();const s=d.system;
    let h="<div class='card'><h2>Device</h2>";
    h+="<div class='sys-row'><span class='k'>Device ID</span><span class='v'>"+s.device_id+"</span></div>";
    h+="<div class='sys-row'><span class='k'>Firmware</span><span class='v'>"+s.fw+"</span></div>";
    h+="<div class='sys-row'><span class='k'>Uptime</span><span class='v'>"+fmtUp(s.uptime_s)+"</span></div>";
    h+="<div class='sys-row'><span class='k'>Time</span><span class='v'>"+fmtTime(s.time)+"</span></div>";
    h+="</div>";

    h+="<div class='card'><h2>Network</h2>";
    h+="<div class='sys-row'><span class='k'>WiFi</span><span class='v'>"+(s.wifi_ssid||'—')+"</span></div>";
    h+="<div class='sys-row'><span class='k'>RSSI</span><span class='v'>"+s.wifi_rssi+" dBm</span></div>";
    h+="<div class='sys-row'><span class='k'>MQTT</span><span class='v'>"+pill(s.mqtt?'ok':'danger',s.mqtt?'Connected':'Offline')+"</span></div>";
    h+="</div>";

    h+="<div class='card'><h2>Memory</h2>";
    h+="<div class='sys-row'><span class='k'>Free Heap</span><span class='v'>"+(s.heap/1024).toFixed(1)+" KB</span></div>";
    h+="</div>";

    h+="<div class='card'><h2>Actions</h2>";
    h+="<div class='row'><button class='btn danger' onclick='doRestart()'>Restart Device</button></div>";
    h+="<div class='row'><button class='btn secondary' onclick='doResetWifi()'>Reset WiFi</button></div>";
    h+="<div id='sysMsg' class='alert'></div></div>";

    $('content').innerHTML=h;
  }catch(e){$('content').innerHTML="<div class='card alert err'>Load failed</div>";}
}
async function doRestart(){
  if(!confirm('Restart device?')) return;
  await fetch('/api/restart',{method:'POST'});
  $('sysMsg').className='alert info';$('sysMsg').textContent='Restarting...';
}
async function doResetWifi(){
  if(!confirm('Reset WiFi and start provisioning?')) return;
  await fetch('/wifi/reset',{method:'POST'});
  $('sysMsg').className='alert info';$('sysMsg').textContent='Resetting WiFi...';
}
refresh();setInterval(refresh,5000);
</script>
)HTML";
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

/* ========================================================================= */
/*                         WIFI PAGE (/wifi)                                 */
/* ========================================================================= */
void WiFiService::handleWifi() {
  String html = pageHead("WiFi Setup");
  html += "<div class='page'><div class='card'>";
  html += R"HTML(
  <h2>Wi-Fi Setup</h2>
  <div class='badge' id='around'>Scan to see networks</div>
  <div class='row'><button id='btnScan' class='btn'>Scan</button></div>
  <div class='list' id='wifiList'></div>
  <h2 style='margin-top:14px'>Connect</h2>
  <div class='row'><select id='ssid' class='input'><option value=''>- Choose -</option></select></div>
  <div class='row'><input id='pass' type='password' class='input' placeholder='Password'></div>
  <div class='row'><button id='btnConnect' class='btn'>Connect</button></div>
  <div class='row'>
    <button id='btnStatus' class='btn secondary'>Status</button>
  </div>
  <div id='msg' class='alert info'>Click Scan to list networks</div>
</div>
)HTML";
  html += navBar("/wifi");
  html += R"HTML(
<script>
const $=id=>document.getElementById(id);
function show(m,t='info'){const el=$('msg');el.textContent=m;el.className='alert '+t;}
async function scan(){
  show('Scanning...','info');
  try{
    const res=await fetch('/wifi/scan');const arr=await res.json();
    $('around').textContent=(arr.length||0)+' networks found';
    const list=$('wifiList'),sel=$('ssid');
    list.innerHTML='';sel.innerHTML='<option value="">- Choose -</option>';
    arr.sort((a,b)=>b.rssi-a.rssi).forEach(x=>{
      const item=document.createElement('div');item.className='item';
      item.innerHTML='<div class="left"><b>'+x.ssid+'</b><div class="badge">'+(x.open?'open':'secured')+' '+x.rssi+'dBm</div></div><button class="btn secondary sm">Select</button>';
      item.querySelector('button').onclick=()=>{sel.value=x.ssid;};
      list.appendChild(item);
      const opt=document.createElement('option');
      opt.value=x.ssid;opt.textContent=x.ssid+' ('+x.rssi+')';sel.appendChild(opt);
    });
    show('Found '+arr.length+' network(s).','ok');
  }catch(e){show('Scan failed.','err');}
}
async function status(){
  try{const r=await fetch('/wifi/status');const j=await r.json();
  if(j.connected) show('Connected. IP '+j.ip,j.internet?'ok':'err');
  else show('Not connected.','err');
  }catch(e){show('Error.','err');}
}
async function doConnect(){
  const ssid=$('ssid').value,pass=$('pass').value;
  if(!ssid){show('Choose SSID.','err');return;}
  show('Connecting...','info');
  const form=new URLSearchParams();form.set('ssid',ssid);if(pass)form.set('pass',pass);
  try{const r=await fetch('/wifi/provision',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form});
  const j=await r.json();
  if(j.ok) show('Connected! IP '+j.ip,'ok'); else show('Failed: '+(j.msg||'unknown'),'err');
  }catch(e){show('Failed.','err');}
}
$('btnScan').onclick=scan;$('btnConnect').onclick=doConnect;$('btnStatus').onclick=status;
scan();
</script>
)HTML";
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

/* ========================================================================= */
/*                    PERIPHERAL CONFIG PAGE (/config)                       */
/* ========================================================================= */
void WiFiService::handleConfigPage() {
  String html = pageHead("Peripherals");
  html += "<div class='page'><div class='card'>";
  html += R"HTML(
  <h1>Peripherals</h1>
  <div class='badge'>Add or remove sensors &amp; lamps without reflashing</div>
  <div id='msg' class='alert'></div>

  <div class='section'>
    <h2>Sensors (DHT)</h2>
    <div id='sensorList' class='list'></div>
    <div class='row'>
      <input id='sId' class='input sm' placeholder='ID'>
      <input id='sPin' type='number' class='input sm' placeholder='Pin' style='max-width:70px'>
      <select id='sType' class='input sm' style='max-width:80px'><option value='22'>DHT22</option><option value='11'>DHT11</option></select>
      <button class='btn sm' onclick='addSensor()'>Add</button>
    </div>
  </div>

  <div class='section'>
    <h2>Lamps</h2>
    <div id='lampList' class='list'></div>
    <div class='row'>
      <input id='lId' class='input sm' placeholder='ID'>
      <input id='lPin' type='number' class='input sm' placeholder='Pin' style='max-width:70px'>
      <select id='lMode' class='input sm' style='max-width:90px'><option value='relay'>Relay</option><option value='pwm'>PWM</option><option value='ac_dimmer'>AC Dim</option></select>
      <button class='btn sm' onclick='addLamp()'>Add</button>
    </div>
    <div class='row' id='zcRow' style='display:none'>
      <span class='badge'>ZC pin:</span>
      <input id='zcPin' type='number' class='input sm' placeholder='ZC' style='max-width:70px'>
    </div>
  </div>

  <div class='section'>
    <h2>Master Relay Pin</h2>
    <div class='row'><input id='masterPin' type='number' class='input sm' value='255' style='max-width:80px'>
    <span class='badge'>255 = disabled</span></div>
  </div>

  <div class='row'><button class='btn' onclick='save()'>Save & Apply</button></div>
</div>
)HTML";
  html += navBar("/config");
  html += R"HTML(
<script>
let cfg={sensors:[],lamps:[],masterRelayPin:255};
const $=id=>document.getElementById(id);
function show(m,t){const el=$('msg');el.textContent=m;el.className='alert '+t;}
function render(){
  let sl=$('sensorList');sl.innerHTML='';
  cfg.sensors.forEach((s,i)=>{
    const d=document.createElement('div');d.className='item';
    d.innerHTML='<div class="left"><b>'+s.id+'</b><div class="badge">Pin '+s.pin+' | DHT'+s.type+'</div></div><button class="btn sm danger" onclick="rmSensor('+i+')">Del</button>';
    sl.appendChild(d);
  });
  let ll=$('lampList');ll.innerHTML='';
  cfg.lamps.forEach((l,i)=>{
    const d=document.createElement('div');d.className='item';
    let x=l.mode;if(l.zcPin&&l.zcPin<255)x+=' ZC:'+l.zcPin;
    d.innerHTML='<div class="left"><b>'+l.id+'</b><div class="badge">Pin '+l.pin+' | '+x+'</div></div><button class="btn sm danger" onclick="rmLamp('+i+')">Del</button>';
    ll.appendChild(d);
  });
  $('masterPin').value=cfg.masterRelayPin;
}
function addSensor(){
  const id=$('sId').value.trim(),pin=parseInt($('sPin').value),type=parseInt($('sType').value);
  if(!id||isNaN(pin)){show('Fill ID and pin','err');return;}
  if(cfg.sensors.length>=8){show('Max 8','err');return;}
  cfg.sensors.push({id,pin,type});$('sId').value='';$('sPin').value='';render();
}
function rmSensor(i){cfg.sensors.splice(i,1);render();}
function addLamp(){
  const id=$('lId').value.trim(),pin=parseInt($('lPin').value),mode=$('lMode').value;
  if(!id||isNaN(pin)){show('Fill ID and pin','err');return;}
  if(cfg.lamps.length>=8){show('Max 8','err');return;}
  let l={id,pin,mode};
  if(mode==='ac_dimmer'){const zc=parseInt($('zcPin').value);if(!isNaN(zc))l.zcPin=zc;}
  cfg.lamps.push(l);$('lId').value='';$('lPin').value='';render();
}
function rmLamp(i){cfg.lamps.splice(i,1);render();}
$('lMode').onchange=function(){$('zcRow').style.display=this.value==='ac_dimmer'?'flex':'none';};
async function save(){
  cfg.masterRelayPin=parseInt($('masterPin').value)||255;
  show('Saving...','info');
  try{
    const r=await fetch('/api/peripherals',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});
    const j=await r.json();
    if(j.ok) show('Applied!','ok'); else show('Error: '+(j.error||'unknown'),'err');
  }catch(e){show('Failed.','err');}
}
async function load(){
  try{const r=await fetch('/api/peripherals');cfg=await r.json();render();}
  catch(e){show('Failed to load.','err');}
}
load();
</script>
)HTML";
  html += "</div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

/* ========================================================================= */
/*                     PERIPHERAL CONFIG API                                 */
/* ========================================================================= */
void WiFiService::handleGetPeripherals() {
  server.send(200, "application/json", PeripheralManager::configToJson());
}

void WiFiService::handlePostPeripherals() {
  if (server.method() != HTTP_POST) { server.send(405,"application/json","{\"error\":\"use POST\"}"); return; }
  String body = server.arg("plain");
  PeripheralConfig newCfg; String err;
  if (!PeripheralManager::parseConfigJson(body, newCfg, err)) {
    server.send(400,"application/json","{\"ok\":false,\"error\":\"" + err + "\"}"); return;
  }
  SensorService::teardown(); LampService::teardown();
  if (!PeripheralManager::applyConfig(newCfg, err)) {
    SensorService::init(PeripheralManager::config()); LampService::init(PeripheralManager::config());
    server.send(500,"application/json","{\"ok\":false,\"error\":\"" + err + "\"}"); return;
  }
  SensorService::init(PeripheralManager::config());
  LampService::init(PeripheralManager::config());
  server.send(200,"application/json","{\"ok\":true}");
  if (MqttService::connected()) MqttService::publishReportedConfig();
}

/* ========================================================================= */
/*                           WI-FI API                                       */
/* ========================================================================= */
void WiFiService::handleScan() {
  WiFi.scanDelete();
  wifi_mode_t m = WiFi.getMode();
  if (m == WIFI_MODE_NULL) { WiFi.mode(WIFI_MODE_STA); delay(100); }
  else if (m == WIFI_MODE_AP) { WiFi.mode(WIFI_MODE_APSTA); delay(100); }

  WiFi.scanNetworks(true, true);
  int n = WIFI_SCAN_RUNNING;
  uint32_t t0 = millis();
  while (n == WIFI_SCAN_RUNNING && millis() - t0 < 10000) { delay(200); n = WiFi.scanComplete(); }
  Logger::info("Scan done: n=%d mode=%d", n, (int)WiFi.getMode());

  if (n < 0) { server.send(500,"application/json","{\"err\":\"scan failed\",\"code\":" + String(n) + "}"); return; }
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
            ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void WiFiService::handleProvision() {
  if (server.method() != HTTP_POST) { server.send(405,"application/json","{\"err\":\"use POST\"}"); return; }
  if (!server.hasArg("ssid")) { server.send(400,"application/json","{\"err\":\"ssid required\"}"); return; }
  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";
  saveCredentials(ssid, pass);
  WiFi.mode(WIFI_STA);
  connect(ssid, pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    server.send(200,"application/json","{\"ok\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}");
    stopProvisioningAP();
  } else {
    server.send(200,"application/json","{\"ok\":false,\"msg\":\"connect failed\"}");
  }
}

void WiFiService::handleWifiStatus() {
  String json = "{\"connected\":"; json += isConnected() ? "true" : "false";
  if (isConnected()) {
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"internet\":"; json += hasInternetPing() ? "true" : "false";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void WiFiService::handleWifiReset() {
  if (server.method() != HTTP_POST) { server.send(405,"application/json","{\"err\":\"use POST\"}"); return; }
  resetCredentials();
  server.send(200,"application/json","{\"ok\":true}");
  delay(400);
  ESP.restart();
}

/* ===== Legacy ===== */
void WiFiService::handleRoot()   { handleHome(); }
void WiFiService::handleStatus() { handleWifiStatus(); }
void WiFiService::handleReset()  { handleWifiReset(); }

/* ===== Utils ===== */
void WiFiService::startCaptiveAP() {
  String apSsid = String(DEVICE_ID) + "-Setup";
  const char* apPass = (PROVISION_AP_PASS && strlen(PROVISION_AP_PASS) >= 8) ? PROVISION_AP_PASS : nullptr;
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  bool ok = apPass ? WiFi.softAP(apSsid.c_str(), apPass, 1, 0, 4) : WiFi.softAP(apSsid.c_str());
  if (!ok) Logger::error("Failed start SoftAP");
  else Logger::info("SoftAP started: %s IP: %s", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
  dns.start(DNS_PORT, "*", WiFi.softAPIP());
  if (!serverActive) { server.begin(); serverActive = true; }
}

bool WiFiService::loadCredentials(String& ssid, String& pass) {
  prefs.begin(NVS_NS, true);
  ssid = prefs.getString(KEY_SSID, "");
  pass = prefs.getString(KEY_PASS, "");
  prefs.end();
  return !ssid.isEmpty();
}

void WiFiService::saveCredentials(const String& ssid, const String& pass) {
  prefs.begin(NVS_NS, false);
  prefs.putString(KEY_SSID, ssid);
  prefs.putString(KEY_PASS, pass);
  prefs.end();
  Logger::info("Credentials saved to NVS");
}
