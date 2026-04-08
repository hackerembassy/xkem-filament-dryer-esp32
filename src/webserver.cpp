#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"
#include "secrets.h"
#include "sensors.h"
#include "relay.h"
#include "lid.h"
#include "datalog.h"
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
  background:#400;border:1px solid #f44;border-radius:8px;margin-bottom:12px;display:none}
.btn{padding:8px 16px;border:none;border-radius:4px;cursor:pointer;
  font-family:inherit;font-size:0.9em;font-weight:bold}
.btn-set{background:#0f3460;color:#0ff}
.btn:active{opacity:0.7}
.mode-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}
.mbtn{padding:12px 8px;border:none;border-radius:6px;cursor:pointer;
  font-family:inherit;font-size:0.95em;font-weight:bold;
  background:#0a0a1a;color:#888;border:1px solid #333;text-align:center}
.mbtn.active{background:#0f3460;color:#0ff;border-color:#0ff}
.mbtn:active{opacity:0.7}
.status{text-align:center;color:#666;font-size:0.8em;margin-top:15px}
.chart-wrap{background:#16213e;border:1px solid #0f3460;border-radius:8px;
  padding:12px;margin-top:12px}
.chart-wrap h2{font-size:1em;color:#888;margin-bottom:8px}
.ctrl{display:flex;gap:8px;align-items:center;margin-top:10px}
</style>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
<h1>Filament Dryer</h1>
<div id="ot" class="ot">HEATSINK OVERTEMP</div>
<div id="tf" class="ot">THERMAL FAULT: Chamber not heating</div>
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
  <div class="row" style="margin-top:8px"><h2>Drying Timer</h2>
    <span class="val" id="dt" style="font-size:1.2em">--</span></div>
</div>
<div class="card">
  <h2>Mode</h2>
  <div class="mode-grid">
    <button class="mbtn" id="m_off" onclick="setMode('off')">OFF</button>
    <button class="mbtn" id="m_maintain" onclick="setMode('maintain')">MAINTAIN</button>
    <button class="mbtn" id="m_pla" onclick="setMode('pla')">PLA 45&deg;C 4h</button>
    <button class="mbtn" id="m_petg" onclick="setMode('petg')">PETG 50&deg;C 6h</button>
    <button class="mbtn" id="m_abs" onclick="setMode('abs')">ABS 52&deg;C 8h</button>
    <button class="mbtn" id="m_tpu" onclick="setMode('tpu')">TPU 50&deg;C 5h</button>
    <button class="mbtn" id="m_mix" onclick="setMode('mix')">MIX 45&deg;C 4h</button>
  </div>
</div>
<div class="chart-wrap"><h2>Temperature</h2>
  <canvas id="tempChart" height="150"></canvas></div>
<div class="chart-wrap"><h2>Humidity &amp; Relay</h2>
  <canvas id="envChart" height="150"></canvas></div>
<div class="card" style="margin-top:16px">
  <h2>Data Log</h2>
  <div class="row"><span>Records</span><span id="lr">--</span></div>
  <div class="row"><span>File size</span><span id="lf">--</span></div>
  <div class="row"><span>Disk free</span><span id="ld">--</span></div>
  <div class="row"><span>Logging</span><span id="la">--</span></div>
  <div class="ctrl" style="margin-top:8px">
    <a class="btn btn-set" href="/api/log" download="dryer_log.csv">Download CSV</a>
    <button class="btn btn-set" onclick="clearLog()" style="background:#4a1a1a;color:#f44">Clear</button>
  </div>
</div>
<div class="status">Auto-refresh: 10s | <span id="lu">--</span></div>
<script>
function fmt(v,u){return v===null?'--':v.toFixed(1)+u}
var MODES=['off','maintain','pla','petg','abs','tpu','mix'];
var chartLabels=[],chamberData=[],heatsinkData=[],humidityData=[],relayData=[];
var MAX_PTS=360,tempChart,envChart;
function initCharts(){
  Chart.defaults.color='#888';Chart.defaults.borderColor='#333';
  tempChart=new Chart(document.getElementById('tempChart'),{type:'line',
    data:{labels:chartLabels,datasets:[
      {label:'Chamber',data:chamberData,borderColor:'#0ff',backgroundColor:'rgba(0,255,255,0.1)',tension:0.3,pointRadius:0,borderWidth:1.5},
      {label:'Heatsink',data:heatsinkData,borderColor:'#f44',backgroundColor:'rgba(255,68,68,0.1)',tension:0.3,pointRadius:0,borderWidth:1.5}
    ]},options:{responsive:true,animation:false,
      plugins:{legend:{position:'top',labels:{boxWidth:12}}},
      scales:{x:{ticks:{maxTicksLimit:6,maxRotation:0}},
        y:{title:{display:true,text:'\u00B0C'}}}}});
  envChart=new Chart(document.getElementById('envChart'),{type:'line',
    data:{labels:chartLabels,datasets:[
      {label:'Humidity',data:humidityData,borderColor:'#0f0',backgroundColor:'rgba(0,255,0,0.1)',tension:0.3,pointRadius:0,borderWidth:1.5,yAxisID:'y'},
      {label:'Relay',data:relayData,borderColor:'#ff0',backgroundColor:'rgba(255,255,0,0.05)',stepped:true,pointRadius:0,borderWidth:1.5,yAxisID:'y1'}
    ]},options:{responsive:true,animation:false,
      plugins:{legend:{position:'top',labels:{boxWidth:12}}},
      scales:{x:{ticks:{maxTicksLimit:6,maxRotation:0}},
        y:{title:{display:true,text:'%'},position:'left'},
        y1:{title:{display:true,text:'Relay'},position:'right',min:-0.1,max:1.1,
          ticks:{stepSize:1,callback:function(v){return v===0?'OFF':v===1?'ON':''}},
          grid:{drawOnChartArea:false}}}}});
}
function pushChart(d){
  var t=new Date();var ts=String(t.getHours()).padStart(2,'0')+':'+String(t.getMinutes()).padStart(2,'0')+':'+String(t.getSeconds()).padStart(2,'0');
  chartLabels.push(ts);
  chamberData.push(d.chamber_valid?d.chamber_temp:null);
  heatsinkData.push(d.heatsink_valid?d.heatsink_temp:null);
  humidityData.push(d.chamber_valid?d.humidity:null);
  relayData.push(d.relay_on?1:0);
  while(chartLabels.length>MAX_PTS){chartLabels.shift();chamberData.shift();heatsinkData.shift();humidityData.shift();relayData.shift();}
  tempChart.update();envChart.update();
}
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
  document.getElementById('tf').style.display=d.thermal_fault?'block':'none';
  var ls=document.getElementById('ls');
  ls.textContent=d.lid_open?'OPEN':'CLOSED';
  ls.style.color=d.lid_open?'#f44':'#0f0';
  MODES.forEach(function(m){
    var el=document.getElementById('m_'+m);
    if(el)el.className='mbtn'+(d.mode===m?' active':'');
  });
  var dr=d.drying_remaining;
  if(dr>0){var h=Math.floor(dr/3600),m=Math.floor((dr%3600)/60),s=dr%60;
    document.getElementById('dt').textContent=String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(s).padStart(2,'0');
  }else{document.getElementById('dt').textContent='--';}
  document.getElementById('lu').textContent=new Date().toLocaleTimeString();
  if(tempChart)pushChart(d);
}
function refresh(){fetch('/api/status').then(r=>r.json()).then(update).catch(()=>{})}
function setMode(m){
  fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({mode:m})}).then(()=>refresh());
}
function fmtBytes(b){if(b<1024)return b+'B';return(b/1024).toFixed(1)+'KB'}
function logStats(){
  fetch('/api/log/stats').then(r=>r.json()).then(function(d){
    document.getElementById('lr').textContent=d.record_count;
    document.getElementById('lf').textContent=fmtBytes(d.file_size);
    document.getElementById('ld').textContent=fmtBytes(d.disk_free);
    document.getElementById('la').textContent=d.logging_active?'Active':'Stopped';
    document.getElementById('la').style.color=d.logging_active?'#0f0':'#f44';
  }).catch(function(){});
}
function clearLog(){
  if(!confirm('Clear all log data?'))return;
  fetch('/api/log',{method:'DELETE'}).then(()=>logStats());
}
initCharts();refresh();logStats();setInterval(refresh,10000);setInterval(logStats,60000);
</script>
</body>
</html>
)rawliteral";

static void handleRoot(void) {
    server.send(200, "text/html", PAGE_HTML);
}

static void handleStatus(void) {
    char json[448];
    char ct[8], hu[8], ht[8], sp[8];

    // Format floats manually to avoid %f on ESP32
    snprintf(ct, sizeof(ct), "%.1f", (double)sensors_getChamberTemp());
    snprintf(hu, sizeof(hu), "%.1f", (double)sensors_getHumidity());
    snprintf(ht, sizeof(ht), "%.1f", (double)sensors_getHeatsinkTemp());
    snprintf(sp, sizeof(sp), "%.1f", (double)relay_getSetpoint());

    snprintf(json, sizeof(json),
        "{\"chamber_temp\":%s,\"humidity\":%s,\"heatsink_temp\":%s,"
        "\"chamber_valid\":%s,\"heatsink_valid\":%s,"
        "\"relay_on\":%s,\"overtemp\":%s,\"setpoint\":%s,"
        "\"enabled\":%s,\"lid_open\":%s,"
        "\"thermal_fault\":%s,\"mode\":\"%s\","
        "\"drying_remaining\":%lu}",
        ct, hu, ht,
        sensors_isChamberValid() ? "true" : "false",
        sensors_isHeatsinkValid() ? "true" : "false",
        relay_isOn() ? "true" : "false",
        relay_isOvertemp() ? "true" : "false",
        sp,
        relay_getMode() != MODE_OFF ? "true" : "false",
        lid_isOpen() ? "true" : "false",
        relay_isThermalFault() ? "true" : "false",
        relay_getModeName(),
        relay_getDryingRemaining());

    server.send(200, "application/json", json);
}

static void handleMode(void) {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }

    String body = server.arg("plain");
    // Simple JSON parsing: find "mode" value
    int idx = body.indexOf("\"mode\"");
    if (idx < 0) {
        server.send(400, "application/json", "{\"error\":\"missing mode\"}");
        return;
    }

    // Extract mode string between quotes after colon
    int colon = body.indexOf(':', idx);
    int q1 = body.indexOf('"', colon);
    int q2 = body.indexOf('"', q1 + 1);
    if (colon < 0 || q1 < 0 || q2 < 0) {
        server.send(400, "application/json", "{\"error\":\"malformed json\"}");
        return;
    }

    String mode = body.substring(q1 + 1, q2);

    struct { const char* name; DryerMode val; } modes[] = {
        {"off", MODE_OFF}, {"maintain", MODE_MAINTAIN},
        {"pla", MODE_DRY_PLA}, {"petg", MODE_DRY_PETG},
        {"abs", MODE_DRY_ABS}, {"tpu", MODE_DRY_TPU},
        {"mix", MODE_DRY_MIX}
    };

    for (auto &m : modes) {
        if (mode == m.name) {
            relay_setMode(m.val);
            char resp[64];
            snprintf(resp, sizeof(resp), "{\"mode\":\"%s\"}", relay_getModeName());
            server.send(200, "application/json", resp);
            return;
        }
    }

    server.send(400, "application/json", "{\"error\":\"unknown mode\"}");
}

static void handleToggle(void) {
    // Backward compat: toggle between OFF and MAINTAIN
    if (relay_getMode() == MODE_OFF) {
        relay_setMode(MODE_MAINTAIN);
    } else {
        relay_setMode(MODE_OFF);
    }
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"mode\":\"%s\"}", relay_getModeName());
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
        server.on("/api/mode", HTTP_POST, handleMode);
        server.on("/api/toggle", HTTP_POST, handleToggle);
        datalog_registerEndpoints(server);
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
