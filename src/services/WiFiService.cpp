#include "WiFiService.h"

void WiFiService::begin() {
  Logger::info("WiFiService begin()");
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(WiFiService::WiFiEvent);

  if (trySavedWifi()) {
    Logger::info("Using saved WiFi credentials");
    return;
  }

  Logger::warn("No saved WiFi or failed to connect, starting provisioning AP");
  startProvisioningAP();
}

bool WiFiService::isConnected() { return WiFi.status() == WL_CONNECTED; }
IPAddress WiFiService::ip() { return WiFi.localIP(); }

bool WiFiService::hasInternetPing() {
  if (!isConnected()) return false;
  return Ping.ping("103.175.220.242", 3);
}

void WiFiService::loop() {
  if (provisioningActive) {
    dns.processNextRequest();
    server.handleClient();
  }
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
  startCaptiveAP();
  provisioningActive = true;
}

void WiFiService::stopProvisioningAP() {
  if (!provisioningActive) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  provisioningActive = false;
  Logger::info("Provisioning AP stopped");
}

/* ================== Private ================== */

bool WiFiService::trySavedWifi(uint32_t timeoutMs) {
  String ssid, pass;
  if (!loadCredentials(ssid, pass) || ssid.isEmpty()) {
    if (strlen(WIFI_SSID) > 0) {
      Logger::info("No saved creds, trying config.h SSID: %s", WIFI_SSID);
      connect(WIFI_SSID, WIFI_PASSWORD);
    } else {
      return false;
    }
  } else {
    Logger::info("Connecting to saved SSID: %s ...", ssid.c_str());
    connect(ssid, pass);
  }

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
      Logger::info("Got IP: %s", WiFi.localIP().toString().c_str());
      stopProvisioningAP();
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Logger::warn("WiFi disconnected, scheduling reconnect...");
      scheduleReconnect(3);
      break;
    default:
      break;
  }
}

void WiFiService::scheduleReconnect(uint32_t sec) {
  wifiReconnectTimer.once(sec, [](){
    String ssid, pass;
    if (loadCredentials(ssid, pass) && !ssid.isEmpty()) {
      connect(ssid, pass);
    } else if (strlen(WIFI_SSID) > 0) {
      connect(WIFI_SSID, WIFI_PASSWORD);
    } else {
      if (!WiFiService::provisioningActive) {
        Logger::warn("No credentials found. Starting provisioning AP.");
        startProvisioningAP();
      }
    }
  });
}

/* ========= Shared CSS ========= */
static const char APP_CSS[] PROGMEM = R"CSS(
:root{--bg:#f5f7fa;--card:#fff;--muted:#6b7280;--primary:#1f6feb;--border:#e5e7eb;}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:ui-sans-serif,system-ui,Arial}
.container{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.card{width:100%;max-width:460px;background:var(--card);border:1px solid var(--border);border-radius:16px;box-shadow:0 6px 16px rgba(0,0,0,.08);padding:18px}
h1,h2{margin:6px 0 14px}
.row{display:flex;gap:10px;align-items:center;margin:10px 0}
.btn{flex:1;padding:12px 14px;border:0;border-radius:10px;background:var(--primary);color:#fff;cursor:pointer;font-weight:600}
.btn.secondary{background:#e5e7eb;color:#111}
.btn.link{background:transparent;color:var(--primary);padding:0}
.input,select{flex:1;padding:12px;border:1px solid var(--border);border-radius:10px}
.list{display:flex;flex-direction:column;gap:8px;margin:10px 0}
.item{padding:12px;border:1px solid var(--border);border-radius:12px;display:flex;justify-content:space-between;align-items:center;background:#fafafa}
.badge{font-size:12px;color:var(--muted)}
.alert{display:none;margin-top:12px;padding:12px;border-radius:10px;border:1px solid}
.alert.info{display:block;background:#eef5ff;color:#004085;border-color:#cfe2ff}
.alert.ok{display:block;background:#e6ffed;color:#065f46;border-color:#b7ebc6}
.alert.err{display:block;background:#ffe6e6;color:#7f1d1d;border-color:#fecaca}
.linkback{margin-top:10px;text-align:center}
.item {display:flex; justify-content:space-between; align-items:center;}
.item .left {flex:1; display:flex; flex-direction:column;}
.item button {margin-left:12px; white-space:nowrap;}
)CSS";

/* ========= UI Routes ========= */

void WiFiService::handleCss() {
  server.send(200, "text/css", APP_CSS);
}

void WiFiService::handleHome() {
  String html = R"HTML(
<!doctype html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>ESP32 Settings</title>
<link rel="stylesheet" href="/app.css">
</head><body><div class="container"><div class="card">
  <h1>Settings</h1>
  <div class="list">
    <div class="item"><div><b>Connect to Wi-Fi</b><div class="badge">Setup or change Wi-Fi</div></div><a class="btn link" href="/wifi">Open</a></div>
    <div class="item"><div><b>Temperature</b><div class="badge">Read sensors</div></div><a class="btn link" href="/temp">Open</a></div>
    <div class="item"><div><b>Lamp</b><div class="badge">Controls</div></div><a class="btn link" href="/lamp">Open</a></div>
  </div>
</div></div></body></html>
)HTML";
  server.send(200, "text/html; charset=utf-8", html);
}

void WiFiService::handleWifi() {
  String html = R"HTML(
<!doctype html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Connect Wi-Fi</title>
<link rel="stylesheet" href="/app.css">
</head><body><div class="container"><div class="card">
  <h2>Wi-Fi disconnected</h2>
  <div class="badge" id="around">Scan to see networks around you</div>
  <div class="row"><button id="btnScan" class="btn">Rescan</button></div>

  <div class="list" id="wifiList"></div>

  <h2 style="margin-top:18px">Fill the form</h2>
  <div class="badge">The password won't be shared</div>
  <div class="row"><select id="ssid" class="input"><option value="">— Choose Wi-Fi —</option></select></div>
  <div class="row"><input id="pass" type="password" class="input" placeholder="Wi-Fi password"></div>
  <div class="row"><button id="btnConnect" class="btn">Connect</button></div>

  <div class="row">
    <button id="btnStatus" class="btn secondary">Status</button>
    <button id="btnReset"  class="btn secondary">Reset Wi-Fi</button>
  </div>
  <div id="msg" class="alert info">Click Rescan to list networks…</div>
  <div class="linkback"><a class="btn link" href="/">← Back</a></div>
</div></div>
<script>
const $=id=>document.getElementById(id);
function show(m,t="info"){const el=$("msg");el.textContent=m;el.className="alert "+t;}
async function scan(){
  show("Scanning…","info");
  try{
    const res=await fetch("/wifi/scan"); const arr=await res.json();
    $("around").textContent = (arr.length||0)+" Wi-Fi around you";
    const list=$("wifiList"); const sel=$("ssid");
    list.innerHTML=""; sel.innerHTML='<option value="">— Choose Wi-Fi —</option>';
    arr.sort((a,b)=>b.rssi-a.rssi).forEach(x=>{
      const item=document.createElement("div"); item.className="item";
      item.innerHTML=`
        <div class="left">
          <b>${x.ssid||"(hidden)"}</b>
          <div class="badge">${x.open?"open":"secured"} · RSSI ${x.rssi}</div>
        </div>
        <button class="btn secondary">Select</button>`;
      item.querySelector("button").onclick=()=>{ $("ssid").value=x.ssid; };
      list.appendChild(item);
      const opt=document.createElement("option");
      opt.value=x.ssid; opt.textContent=`${x.ssid} (RSSI ${x.rssi})`; sel.appendChild(opt);
    });
    show(`Found ${arr.length} network(s).`,"ok");
  }catch(e){show("Scan failed.","err");}
}
async function status(){
  try{
    const r=await fetch("/wifi/status"); const j=await r.json();
    if(j.connected) show(`Connected. IP ${j.ip}. Internet ${j.internet?"OK":"No"}`, j.internet?"ok":"err");
    else show("Not connected.","err");
  }catch(e){show("Status failed.","err");}
}
async function connect(){
  const ssid=$("ssid").value, pass=$("pass").value;
  if(!ssid){show("Please choose an SSID.","err");return;}
  show("Connecting…","info");
  const form=new URLSearchParams(); form.set("ssid",ssid); if(pass)form.set("pass",pass);
  try{
    const r=await fetch("/wifi/provision",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:form});
    const j=await r.json();
    if(j.ok) show(`Connected. IP ${j.ip}`,"ok"); else show(`Failed: ${j.msg||"unknown"}`,"err");
  }catch(e){show("Provision request failed.","err");}
}
async function resetWifi(){
  if(!confirm("Reset Wi-Fi credentials? Device will restart.")) return;
  try{
    const r=await fetch("/wifi/reset",{method:"POST"});
    const j=await r.json();
    if(j.ok) show("Wi-Fi reset. Device restarting…","ok"); else show("Reset failed.","err");
  }catch(e){show("Reset failed.","err");}
}
$("btnScan").onclick=scan; $("btnConnect").onclick=connect; $("btnStatus").onclick=status; $("btnReset").onclick=resetWifi;
scan();
</script>
</body></html>
)HTML";
  server.send(200,"text/html; charset=utf-8", html);
}


void WiFiService::handleTemp() {
  String html = R"HTML(
<!doctype html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Temperature</title>
<link rel="stylesheet" href="/app.css">
</head><body><div class="container"><div class="card">
  <h2>Temperature</h2>
  <div class="badge">Coming soon: show temp1/temp2 here</div>
  <div class="linkback"><a class="btn link" href="/">← Back</a></div>
</div></div></body></html>
)HTML";
  server.send(200,"text/html; charset=utf-8", html);
}

void WiFiService::handleLamp() {
  String html = R"HTML(
<!doctype html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Lamp</title>
<link rel="stylesheet" href="/app.css">
</head><body><div class="container"><div class="card">
  <h2>Lamp</h2>
  <div class="badge">Coming soon: controls for lamp1/lamp2</div>
  <div class="linkback"><a class="btn link" href="/">← Back</a></div>
</div></div></body></html>
)HTML";
  server.send(200,"text/html; charset=utf-8", html);
}

/* ===== Wi-Fi API ===== */

void WiFiService::handleScan() {
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) { server.send(500,"application/json","{\"err\":\"scan failed\"}"); return; }
  String json="["; 
  for (int i=0;i<n;i++) {
    if (i) json += ",";
    json += "{\"ssid\":\""+WiFi.SSID(i)+"\",\"rssi\":"+String(WiFi.RSSI(i))+
            ",\"open\":"+(WiFi.encryptionType(i)==WIFI_AUTH_OPEN?"true":"false")+"}";
  }
  json += "]";
  server.send(200,"application/json", json);
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
    String resp = String("{\"ok\":true,\"ip\":\"")+WiFi.localIP().toString()+"\"}";
    server.send(200,"application/json",resp);
    stopProvisioningAP();
  } else {
    server.send(200,"application/json","{\"ok\":false,\"msg\":\"connect failed\"}");
  }
}

void WiFiService::handleWifiStatus() {
  String json="{\"connected\":"; json += isConnected()?"true":"false";
  if (isConnected()) {
    json += ",\"ip\":\""+WiFi.localIP().toString()+"\"";
    json += ",\"internet\":"; json += hasInternetPing()?"true":"false";
  }
  json += "}";
  server.send(200,"application/json",json);
}

void WiFiService::handleWifiReset() {
  if (server.method() != HTTP_POST) { server.send(405,"application/json","{\"err\":\"use POST\"}"); return; }
  resetCredentials();
  server.send(200,"application/json","{\"ok\":true}");
  delay(400);
  ESP.restart();
}

/* ===== Legacy alias (opsional) ===== */
void WiFiService::handleRoot()      { handleHome(); }
void WiFiService::handleStatus()    { handleWifiStatus(); }
void WiFiService::handleReset()     { handleWifiReset(); }

/* ===== Utils ===== */
void WiFiService::startCaptiveAP() {
  String apSsid = String(DEVICE_ID) + "-Setup";
  const char* apPass = (PROVISION_AP_PASS && strlen(PROVISION_AP_PASS) >= 8) ? PROVISION_AP_PASS : nullptr;

  WiFi.persistent(false);
  WiFi.disconnect(true,true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));

  bool ok = apPass ? WiFi.softAP(apSsid.c_str(), apPass, 1, 0, 4)
                   : WiFi.softAP(apSsid.c_str());
  if (!ok) Logger::error("Failed start SoftAP");
  else     Logger::info("SoftAP started: %s IP: %s", apSsid.c_str(), WiFi.softAPIP().toString().c_str());

  // DNS hijack (captive)
  dns.start(DNS_PORT, "*", WiFi.softAPIP());

  // UI
  server.on("/",        HTTP_GET, handleHome);
  server.on("/wifi",    HTTP_GET, handleWifi);
  server.on("/temp",    HTTP_GET, handleTemp);
  server.on("/lamp",    HTTP_GET, handleLamp);
  server.on("/app.css", HTTP_GET, handleCss);

  // API
  server.on("/wifi/scan",      HTTP_GET,  handleScan);
  server.on("/wifi/provision", HTTP_POST, handleProvision);
  server.on("/wifi/status",    HTTP_GET,  handleWifiStatus);
  server.on("/wifi/reset",     HTTP_POST, handleWifiReset);

  // (kompatibel dgn versi lama)
  server.on("/status", HTTP_GET, handleWifiStatus);
  server.on("/reset",  HTTP_POST, handleWifiReset);

  server.begin();
}

bool WiFiService::loadCredentials(String& ssid, String& pass) {
  prefs.begin(NVS_NS,true);
  ssid = prefs.getString(KEY_SSID,"");
  pass = prefs.getString(KEY_PASS,"");
  prefs.end();
  return !ssid.isEmpty();
}

void WiFiService::saveCredentials(const String& ssid, const String& pass) {
  prefs.begin(NVS_NS,false);
  prefs.putString(KEY_SSID, ssid);
  prefs.putString(KEY_PASS, pass);
  prefs.end();
  Logger::info("Credentials saved to NVS");
}
