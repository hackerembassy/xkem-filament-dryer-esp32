#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"
#include "secrets.h"
#include "sensors.h"
#include "relay.h"
#include "lid.h"
#include "webserver.h"

static WebServer server(80);
static bool wifiConnected = false;

static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Filament Dryer</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#1a1a2e;color:#e0e0e0;font-family:'Courier New',monospace;
  padding:20px;max-width:480px;margin:0 auto}
h1{color:#0ff;font-size:1.4em;text-align:center;margin-bottom:20px;
  border-bottom:1px solid #333;padding-bottom:10px}
.card{background:#16213e;border:1px solid #0f3460;border-radius:8px;
  padding:15px;margin-bottom:12px}
.card h2{font-size:1em;color:#888;margin-bottom:8px}
.val{font-size:1.6em;color:#0ff}
.val.err{color:#f44}
.row{display:flex;justify-content:space-between;align-items:center}
.relay-on{color:#0f0;font-weight:bold}
.relay-off{color:#f44;font-weight:bold}
.ot{color:#f44;font-size:1.2em;text-align:center;padding:10px;
  background:#400;border:1px solid #f44;border-radius:8px;margin-bottom:12px}
.ctrl{display:flex;gap:8px;align-items:center;margin-top:10px}
.ctrl input{width:80px;padding:8px;background:#0a0a1a;color:#0ff;
  border:1px solid #0f3460;border-radius:4px;font-family:inherit;font-size:1em}
.btn{padding:8px 16px;border:none;border-radius:4px;cursor:pointer;
  font-family:inherit;font-size:0.9em;font-weight:bold}
.btn-set{background:#0f3460;color:#0ff}
.btn-toggle{background:#1a4a1a;color:#0f0;width:100%;padding:12px;
  font-size:1.1em;margin-top:8px}
.btn-toggle.off{background:#4a1a1a;color:#f44}
.btn:active{opacity:0.7}
.status{text-align:center;color:#666;font-size:0.8em;margin-top:15px}
</style>
</head>
<body>
<h1>Filament Dryer</h1>
<div id="ot" class="ot" style="display:none">HEATSINK OVERTEMP</div>
<div id="lid" class="card"><h2>Lid</h2>
  <span class="val" id="ls">--</span></div>
<div class="card"><h2>Chamber Temperature</h2>
  <span class="val" id="ct">--</span></div>
<div class="card"><h2>Humidity</h2>
  <span class="val" id="hu">--</span></div>
<div class="card"><h2>Heatsink Temperature</h2>
  <span class="val" id="ht">--</span></div>
<div class="card">
  <div class="row"><h2>Relay</h2>
    <span id="rs" class="relay-off">--</span></div>
  <div class="row" style="margin-top:8px"><h2>Setpoint</h2>
    <span class="val" id="sp" style="font-size:1.2em">--</span></div>
  <div class="ctrl">
    <input type="number" id="si" min="30" max="60" step="0.5" placeholder="50">
    <button class="btn btn-set" onclick="setPoint()">Set</button>
  </div>
</div>
<button class="btn btn-toggle" id="tb" onclick="toggle()">Loading...</button>
<div class="status">Auto-refresh: 60s | <span id="lu">--</span></div>
<script>
function fmt(v,u){return v===null?'--':v.toFixed(1)+u}
function update(d){
  document.getElementById('ct').textContent=d.chamber_valid?fmt(d.chamber_temp,'\u00B0C'):'ERROR';
  document.getElementById('ct').className='val'+(d.chamber_valid?'':' err');
  document.getElementById('hu').textContent=d.chamber_valid?fmt(d.humidity,'%'):'ERROR';
  document.getElementById('hu').className='val'+(d.chamber_valid?'':' err');
  document.getElementById('ht').textContent=d.heatsink_valid?fmt(d.heatsink_temp,'\u00B0C'):'ERROR';
  document.getElementById('ht').className='val'+(d.heatsink_valid?'':' err');
  document.getElementById('rs').textContent=d.relay_on?'ON':'OFF';
  document.getElementById('rs').className=d.relay_on?'relay-on':'relay-off';
  document.getElementById('sp').textContent=fmt(d.setpoint,'\u00B0C');
  document.getElementById('ot').style.display=d.overtemp?'block':'none';
  var ls=document.getElementById('ls');
  ls.textContent=d.lid_open?'OPEN':'CLOSED';
  ls.style.color=d.lid_open?'#f44':'#0f0';
  var tb=document.getElementById('tb');
  tb.textContent=d.enabled?'Stop Dryer':'Start Dryer';
  tb.className='btn btn-toggle'+(d.enabled?'':' off');
  document.getElementById('lu').textContent=new Date().toLocaleTimeString();
}
function refresh(){fetch('/api/status').then(r=>r.json()).then(update).catch(()=>{})}
function setPoint(){
  var v=parseFloat(document.getElementById('si').value);
  if(isNaN(v)||v<30||v>60)return;
  fetch('/api/setpoint',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({setpoint:v})}).then(()=>refresh());
}
function toggle(){
  fetch('/api/toggle',{method:'POST'}).then(()=>refresh());
}
refresh();setInterval(refresh,60000);
</script>
</body>
</html>
)rawliteral";

static void handleRoot(void) {
    server.send(200, "text/html", PAGE_HTML);
}

static void handleStatus(void) {
    char json[320];
    char ct[8], hu[8], ht[8], sp[8];

    // Format floats manually to avoid %f on ESP32
    snprintf(ct, sizeof(ct), "%.1f", (double)sensors_getChamberTemp());
    snprintf(hu, sizeof(hu), "%.1f", (double)sensors_getHumidity());
    snprintf(ht, sizeof(ht), "%.1f", (double)sensors_getHeatsinkTemp());
    snprintf(sp, sizeof(sp), "%.1f", (double)relay_getSetpoint());

    snprintf(json, sizeof(json),
        "{\"chamber_temp\":%s,\"humidity\":%s,\"heatsink_temp\":%s,"
        "\"chamber_valid\":%s,\"heatsink_valid\":%s,"
        "\"relay_on\":%s,\"overtemp\":%s,\"setpoint\":%s,\"enabled\":%s,"
        "\"lid_open\":%s}",
        ct, hu, ht,
        sensors_isChamberValid() ? "true" : "false",
        sensors_isHeatsinkValid() ? "true" : "false",
        relay_isOn() ? "true" : "false",
        relay_isOvertemp() ? "true" : "false",
        sp,
        relay_isEnabled() ? "true" : "false",
        lid_isOpen() ? "true" : "false");

    server.send(200, "application/json", json);
}

static void handleSetpoint(void) {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    String body = server.arg("plain");
    // Simple JSON parsing: find "setpoint" value
    int idx = body.indexOf("\"setpoint\"");
    if (idx < 0) {
        server.send(400, "application/json", "{\"error\":\"missing setpoint\"}");
        return;
    }

    int colon = body.indexOf(':', idx);
    if (colon < 0) {
        server.send(400, "application/json", "{\"error\":\"malformed json\"}");
        return;
    }

    float val = body.substring(colon + 1).toFloat();
    if (val < CHAMBER_SETPOINT_MIN || val > CHAMBER_SETPOINT_MAX) {
        server.send(400, "application/json", "{\"error\":\"out of range\"}");
        return;
    }

    relay_setSetpoint(val);

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"setpoint\":%.1f}", (double)val);
    server.send(200, "application/json", resp);
}

static void handleToggle(void) {
    relay_setEnabled(!relay_isEnabled());
    char resp[32];
    snprintf(resp, sizeof(resp), "{\"enabled\":%s}",
             relay_isEnabled() ? "true" : "false");
    server.send(200, "application/json", resp);
}

void webserver_init(void) {
    Serial.print("Connecting to WiFi");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.print("WiFi connected, IP: ");
        Serial.println(WiFi.localIP());

        server.on("/", HTTP_GET, handleRoot);
        server.on("/api/status", HTTP_GET, handleStatus);
        server.on("/api/setpoint", HTTP_POST, handleSetpoint);
        server.on("/api/toggle", HTTP_POST, handleToggle);
        server.begin();
        Serial.println("HTTP server started");
    } else {
        Serial.println("WiFi connection failed, continuing without web interface");
    }
}

void webserver_loop(void) {
    if (wifiConnected) {
        server.handleClient();
    }
}
