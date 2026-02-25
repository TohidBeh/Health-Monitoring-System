////////////////////////////////////////////////Patient Health Monitoring on ESP32 Web Server////////////////////////////////////////////////////////
////////////////////////////////////////////////////////TOHID BEHESHTI___810100100//////////////////////////////////////////////////////////////////
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "DHT.h"
#include <math.h>


#define DHTTYPE DHT22
#define DHTPIN 18
#define DS18B20 5

#define REPORTING_PERIOD_MS 1000

float temperature = 0, humidity = 0, BPM = 0, SpO2 = 0, bodytemperature = 0;

//// alarm thresholds
enum AlarmLevel : uint8_t { AL_OK=0, AL_WARN=1, AL_CRIT=2, AL_FAULT=3 };

const uint8_t WARN_HOLD_SAMPLES  = 3;   // need 3 consecutive seconds to raise WARNING
const uint8_t CRIT_HOLD_SAMPLES  = 2;   // need 2 consecutive seconds to raise CRITICAL
const uint8_t CLEAR_OK_SAMPLES   = 5;   // need 5 consecutive seconds in-range to clear to OK

const float BPM_WARN_LOW   = 60.0f;
const float BPM_WARN_HIGH  = 100.0f;
const float BPM_CRIT_LOW   = 50.0f;
const float BPM_CRIT_HIGH  = 120.0f;

const float SPO2_WARN_LOW  = 90.0f;   // 90–94 = warning
const float SPO2_OK_LOW    = 94.0f;   // >=95 = OK
const float SPO2_CRIT_LOW  = 90.0f;   // <90 = critical

const float BT_WARN_LOW    = 37.5f;   // 37.5–37.9 = warning
const float BT_CRIT_HIGH   = 38.0f;   // >=38.0 = fever
const float BT_CRIT_LOW    = 35.0f;   // <=35.0 = too low (or bad contact)

const float RT_WARN_LOW    = 18.0f;
const float RT_WARN_HIGH   = 27.0f;
const float RT_CRIT_LOW    = 18.0f;
const float RT_CRIT_HIGH   = 30.0f;

const float HUM_WARN_LOW   = 20.0f;
const float HUM_WARN_HIGH  = 60.0f;
const float HUM_CRIT_LOW   = 20.0f;
const float HUM_CRIT_HIGH  = 70.0f;

AlarmLevel AL_bpm = AL_OK, AL_spo2 = AL_OK, AL_bt = AL_OK, AL_rt = AL_OK, AL_hum = AL_OK;

// Debounce counters
uint8_t bpmWarnCnt=0, bpmCritCnt=0, bpmOkCnt=0;
uint8_t spo2WarnCnt=0, spo2CritCnt=0, spo2OkCnt=0;
uint8_t btWarnCnt=0, btCritCnt=0, btOkCnt=0;
uint8_t rtWarnCnt=0, rtCritCnt=0, rtOkCnt=0;
uint8_t humWarnCnt=0, humCritCnt=0, humOkCnt=0;

// Forward declarations
AlarmLevel computeInstantBpm(float bpm);
AlarmLevel computeInstantSpo2(float spo2);
AlarmLevel computeInstantBodyTemp(float tC);
AlarmLevel computeInstantRoomTemp(float tC);
AlarmLevel computeInstantHumidity(float rh);
void updateAlarms();
AlarmLevel overallAlarmLevel();
String alarmLevelText(AlarmLevel lvl);
String alarmLevelCss(AlarmLevel lvl);

///////// your SSID & pasword 
//const char *ssid = "TP-Link_7F28";
//const char *password = "92487529";
///192.168.1.106
const char *ssid = "iPhone";
const char *password = "tohid1234";
///172.20.10.8


DHT dht(DHTPIN, DHTTYPE);
PulseOximeter pox;
uint32_t tsLastReport = 0;

OneWire oneWire(DS18B20);
DallasTemperature sensors(&oneWire);

WebServer server(80);    // port standard Http

/// Max30100 smoothing
float BPM_f = 0, SpO2_f = 0;
float BPM_lastGood = 0, SpO2_lastGood = 0;

const float alpha_BPM_step  = 0.30f;   // smaller --> smoother slower
const float alpha_Spo2_step = 0.15f;

const float MAX_BPM_STEP   = 16.0f;   // maximum change 1s
const float MAX_SPO2_STEP  = 3.0f;

// finger lift
uint32_t lastValidPoxMs = 0;
const uint32_t finger_lift_timeMS = 2000;  // 2s lift
uint32_t lastBeatMs = 0;
const uint32_t beat_timeout_ms = 2500;  // if no beat for 2.5s => finger off / no signal



/// DS18B20 non_blocking
uint32_t lastBodyReqMs = 0;
const uint32_t BODY_PERIOD_MS = 2000; // update 2s

void onBeatDetected() {
  lastBeatMs = millis();
  Serial.println("Beat!");
}

void setup() {
  Serial.begin(115200);
  pinMode(19, OUTPUT);
  delay(100);

  Serial.println(F("DHTxx test!"));
  dht.begin();

  // DS18B20 init
  sensors.begin();
  sensors.setWaitForConversion(false);  // !!!!! dont block CPU
  sensors.requestTemperatures();        // start first conversion
  lastBodyReqMs = millis();

  Serial.print("DS18B20 devices: ");
  Serial.println(sensors.getDeviceCount());    /// confirm it is connected   /// test kon !!

  Serial.println("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected..!");
  Serial.print("Got IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", handle_OnConnect);
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("HTTP server started");

  Serial.print("Initializing pulse oximeter..");

  if (!pox.begin()) {
    Serial.println("FAILED");
    for (;;);
  } else {
    Serial.println("SUCCESS");
    pox.setOnBeatDetectedCallback(onBeatDetected);
  }

  // check different values for best outcome
  pox.setIRLedCurrent(MAX30100_LED_CURR_24MA);

  //filter vars memory
  BPM_f = 0;
  SpO2_f = 0;
  BPM_lastGood = 0;
  SpO2_lastGood = 0;
  lastValidPoxMs = millis();
  lastBeatMs = millis();
}

void loop() {
  // !!!!! call it too frequently
  pox.update();

  server.handleClient();  // did someone called me

  /////DS18B20 non_blocking update/////
  if (millis() - lastBodyReqMs >= BODY_PERIOD_MS) {
    bodytemperature = sensors.getTempCByIndex(0);  // read last completed conversion
    sensors.requestTemperatures();                  // start next conversion
    lastBodyReqMs = millis();
  }

  // every second: read DHT &  MAX30100 filtered process
  if (millis() - tsLastReport > REPORTING_PERIOD_MS) {

    // dht22
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    if (!isnan(t)) temperature = t;
    if (!isnan(h)) humidity = h;

    // max30100 
    float bpmRaw  = pox.getHeartRate();
    float spo2Raw = pox.getSpO2();
    bool beatRecent = (millis() - lastBeatMs) < beat_timeout_ms;
    if (!beatRecent) {
      // Force invalid when there hasn't been a beat recently
      bpmRaw = 0;
      spo2Raw = 0;
    }
  
    bool bpmOk  = (bpmRaw  >= 40.0f && bpmRaw  <= 200.0f);
    bool spo2Ok = (spo2Raw >= 70.0f && spo2Raw <= 100.0f);
    if (bpmRaw == 0)  bpmOk = false;
    if (spo2Raw == 0) spo2Ok = false;

    // spike
    bool bpmSpike = false;
    bool spo2Spike = false;

    if (BPM_lastGood > 0 && bpmOk) {
      if (fabs(bpmRaw - BPM_lastGood) > MAX_BPM_STEP) bpmSpike = true;
    }
    if (SpO2_lastGood > 0 && spo2Ok) {
      if (fabs(spo2Raw - SpO2_lastGood) > MAX_SPO2_STEP) spo2Spike = true;
    }

    ///time of the last valid pox 
    bool anyValid = (bpmOk && !bpmSpike) || (spo2Ok && !spo2Spike);
    if (anyValid) {
      lastValidPoxMs = millis();
    }

    ////Update BPMf if BPM is good 
    if (bpmOk && !bpmSpike) {
      if (BPM_f <= 0) BPM_f = bpmRaw;
      else BPM_f = (alpha_BPM_step * bpmRaw) + ((1.0f - alpha_BPM_step) * BPM_f);
      BPM_lastGood = bpmRaw;
    }

    ///Update Spo2f if SpO2 is good
    if (spo2Ok && !spo2Spike) {
      if (SpO2_f <= 0) SpO2_f = spo2Raw;
      else SpO2_f = (alpha_Spo2_step * spo2Raw) + ((1.0f - alpha_Spo2_step) * SpO2_f);
      SpO2_lastGood = spo2Raw;
    }

    //Finger removed
    if (millis() - lastValidPoxMs > finger_lift_timeMS) {
      if (true) {
        BPM_f  *= 0.55f; 
        SpO2_f *= 0.55f;
        if (BPM_f < 1.0f)  BPM_f = 0;
        if (SpO2_f < 1.0f) SpO2_f = 0;
      } else {
        BPM_f = 0;
        SpO2_f = 0;
      }

      BPM_lastGood = 0;
      SpO2_lastGood = 0;
    }

    ////values that we send for html
    BPM  = BPM_f;
    SpO2 = SpO2_f;

    // Update debounced alarms once per second (based on latest values)
    updateAlarms();

    ///serial monitor
    Serial.print("Room Temperature: ");
    Serial.println(temperature);

    Serial.print("Room Humidity: ");
    Serial.println(humidity);

    Serial.print("BPM raw: ");
    Serial.print(bpmRaw);
    Serial.print(" | BPM filtered: ");
    Serial.println(BPM);

    Serial.print("SpO2 raw: ");
    Serial.print(spo2Raw);
    Serial.print(" | SpO2 filtered: ");
    Serial.println(SpO2);

    Serial.print("Body Temperature: ");
    Serial.println(bodytemperature);

    Serial.println("*********************************");

    tsLastReport = millis();
  }
}

// ======================= Alarm logic =======================
static inline void debounceUpdate(AlarmLevel instant, AlarmLevel &current,
                                 uint8_t &warnCnt, uint8_t &critCnt, uint8_t &okCnt) {
  if (instant == AL_FAULT) {
    current = AL_FAULT;
    warnCnt = critCnt = okCnt = 0;
    return;
  }

  // Update counters
  if (instant == AL_CRIT) { critCnt++; } else { critCnt = 0; }
  if (instant == AL_WARN) { warnCnt++; } else { warnCnt = 0; }
  if (instant == AL_OK)   { okCnt++;   } else { okCnt   = 0; }

  // Promote
  if (instant == AL_CRIT && critCnt >= CRIT_HOLD_SAMPLES) current = AL_CRIT;
  else if (instant == AL_WARN && warnCnt >= WARN_HOLD_SAMPLES && current < AL_CRIT) current = AL_WARN;

  // Clear back to OK only after being OK for a while
  if (instant == AL_OK && okCnt >= CLEAR_OK_SAMPLES) current = AL_OK;
}

AlarmLevel computeInstantBpm(float bpm) {
  if (bpm <= 0.5f) return AL_FAULT; // finger lifted / no reading
  if (bpm < BPM_CRIT_LOW || bpm > BPM_CRIT_HIGH) return AL_CRIT;
  if (bpm < BPM_WARN_LOW || bpm > BPM_WARN_HIGH) return AL_WARN;
  return AL_OK;
}

AlarmLevel computeInstantSpo2(float spo2) {
  if (spo2 <= 0.5f) return AL_FAULT; // finger lifted / no reading
  if (spo2 < SPO2_CRIT_LOW) return AL_CRIT;
  if (spo2 < SPO2_OK_LOW)   return AL_WARN;  // 90–94
  return AL_OK;
}

AlarmLevel computeInstantBodyTemp(float tC) {
  // DS18B20 common fault codes: -127 (no sensor). Also flag 85 as "not ready" / startup.
  if (tC <= -100.0f || fabs(tC + 127.0f) < 0.01f || fabs(tC - 85.0f) < 0.01f) return AL_FAULT;
  if (tC <= BT_CRIT_LOW || tC >= BT_CRIT_HIGH) return AL_CRIT;
  if (tC >= BT_WARN_LOW) return AL_WARN;
  return AL_OK;
}

AlarmLevel computeInstantRoomTemp(float tC) {
  if (isnan(tC) || tC < -40.0f || tC > 80.0f) return AL_FAULT; // DHT read error or impossible indoor
  if (tC < RT_CRIT_LOW || tC > RT_CRIT_HIGH) return AL_CRIT;
  if (tC < RT_WARN_LOW || tC > RT_WARN_HIGH) return AL_WARN;
  return AL_OK;
}

AlarmLevel computeInstantHumidity(float rh) {
  if (isnan(rh) || rh < 0.0f || rh > 100.0f) return AL_FAULT;
  if (rh < HUM_CRIT_LOW || rh > HUM_CRIT_HIGH) return AL_CRIT;
  if (rh < HUM_WARN_LOW || rh > HUM_WARN_HIGH) return AL_WARN;
  return AL_OK;
}

void updateAlarms() {
  debounceUpdate(computeInstantBpm(BPM),  AL_bpm,  bpmWarnCnt,  bpmCritCnt,  bpmOkCnt);
  debounceUpdate(computeInstantSpo2(SpO2),AL_spo2, spo2WarnCnt, spo2CritCnt, spo2OkCnt);
  debounceUpdate(computeInstantBodyTemp(bodytemperature), AL_bt, btWarnCnt, btCritCnt, btOkCnt);
  debounceUpdate(computeInstantRoomTemp(temperature),     AL_rt, rtWarnCnt, rtCritCnt, rtOkCnt);
  debounceUpdate(computeInstantHumidity(humidity),        AL_hum,humWarnCnt,humCritCnt,humOkCnt);
}

AlarmLevel overallAlarmLevel() {
  AlarmLevel lvl = AL_OK;
  if (AL_rt   > lvl) lvl = AL_rt;
  if (AL_hum  > lvl) lvl = AL_hum;
  if (AL_bpm  > lvl) lvl = AL_bpm;
  if (AL_spo2 > lvl) lvl = AL_spo2;
  if (AL_bt   > lvl) lvl = AL_bt;
  return lvl;
}

String alarmLevelText(AlarmLevel lvl) {
  switch (lvl) {
    case AL_OK:    return "OK";
    case AL_WARN:  return "WARNING";
    case AL_CRIT:  return "CRITICAL";
    case AL_FAULT: return "SENSOR FAULT";
    default:       return "OK";
  }
}

String alarmLevelCss(AlarmLevel lvl) {
  switch (lvl) {
    case AL_OK:    return "alarmOK";
    case AL_WARN:  return "alarmWARN";
    case AL_CRIT:  return "alarmCRIT";
    case AL_FAULT: return "alarmFAULT";
    default:       return "alarmOK";
  }
}

void handle_OnConnect() {
  server.send(200, "text/html", SendHTML(temperature, humidity, BPM, SpO2, bodytemperature));
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

//////////////////////////// Dashboard __ dark mode ////////////////////////////////

String SendHTML(float temperature, float humidity, float BPM, float SpO2, float bodytemperature) {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Patient Health Monitoring</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/css/all.min.css'>";

  html += "<style>";
  html += ":root{--bg:#0b1220;--panel:#0f1a2e;--card:#111f38;--stroke:rgba(255,255,255,.10);"
          "--text:#e7ecff;--muted:rgba(231,236,255,.70);--shadow:0 18px 40px rgba(0,0,0,.45);"
          "--teal:#28d7c4;--blue:#4aa3ff;--aqua:#39c7ff;--red:#ff4d6d;--pink:#ff4fd8;--amber:#ffc857;}";
  html += "*{box-sizing:border-box} body{margin:0;font-family:ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,Arial;"
          "background:radial-gradient(900px 450px at 20% 0%, rgba(40,215,196,.18), transparent 55%),"
          "radial-gradient(900px 450px at 80% 10%, rgba(74,163,255,.18), transparent 55%),"
          "var(--bg);color:var(--text)}";
  html += "#page{max-width:980px;margin:0 auto;padding:26px 18px 34px}";
  html += ".header{display:flex;flex-direction:column;gap:8px;align-items:center;padding:16px 0 10px}";
  html += ".title{margin:0;font-weight:800;letter-spacing:.4px;font-size:clamp(28px,4vw,44px);"
          "color:var(--teal);text-align:center}";
  html += ".subtitle{margin:0;font-weight:600;color:var(--muted);font-size:clamp(13px,2vw,16px);text-align:center}";
  html += ".shell{margin-top:18px;background:linear-gradient(180deg, rgba(255,255,255,.06), rgba(255,255,255,.03));"
          "border:1px solid var(--stroke);border-radius:18px;box-shadow:var(--shadow);overflow:hidden}";
  html += ".shellHead{display:flex;justify-content:space-between;align-items:center;padding:16px 18px;"
          "background:rgba(0,0,0,.18);border-bottom:1px solid var(--stroke)}";
  html += ".shellHead h2{margin:0;font-size:18px;letter-spacing:.2px}";
  html += ".badge{font-size:12px;color:var(--muted);padding:6px 10px;border:1px solid var(--stroke);border-radius:999px}";
  html += ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;padding:14px}";
  html += "@media (max-width:620px){.grid{grid-template-columns:1fr}}";
  html += ".card{background:linear-gradient(180deg, rgba(255,255,255,.05), rgba(255,255,255,.03));"
          "border:1px solid var(--stroke);border-radius:16px;padding:14px 14px 12px;position:relative;overflow:hidden}";
  html += ".card:before{content:'';position:absolute;inset:-80px -120px auto auto;width:220px;height:220px;"
          "background:radial-gradient(circle at 30% 30%, rgba(255,255,255,.16), transparent 60%);transform:rotate(18deg)}";
  html += ".row{display:flex;align-items:center;gap:12px;position:relative}";
  html += ".icon{width:42px;height:42px;border-radius:14px;display:flex;align-items:center;justify-content:center;"
          "border:1px solid var(--stroke);background:rgba(0,0,0,.16)}";
  html += ".label{font-size:13px;color:var(--muted);margin:0 0 2px}";
  html += ".value{margin:0;font-size:34px;font-weight:800;line-height:1.05;letter-spacing:.2px}";
  html += ".unit{font-size:14px;font-weight:700;color:var(--muted);margin-left:6px}";
  html += ".foot{margin-top:10px;height:8px;border-radius:999px;background:rgba(255,255,255,.06);overflow:hidden}";
  html += ".bar{height:100%;border-radius:999px;opacity:.95}";

  html += ".tBlue .value{color:var(--blue)} .tBlue .bar{background:linear-gradient(90deg,var(--blue),rgba(74,163,255,.2))}";
  html += ".tAqua .value{color:var(--aqua)} .tAqua .bar{background:linear-gradient(90deg,var(--aqua),rgba(57,199,255,.2))}";
  html += ".tRed  .value{color:var(--red)}  .tRed  .bar{background:linear-gradient(90deg,var(--red),rgba(255,77,109,.2))}";
  html += ".tPink .value{color:var(--pink)} .tPink .bar{background:linear-gradient(90deg,var(--pink),rgba(255,79,216,.2))}";
  html += ".tAmber .value{color:var(--amber)} .tAmber .bar{background:linear-gradient(90deg,var(--amber),rgba(255,200,87,.2))}";
  html += ".icoBlue{color:var(--blue)} .icoAqua{color:var(--aqua)} .icoRed{color:var(--red)} .icoPink{color:var(--pink)} .icoAmber{color:var(--amber)}";
    // Alarm banner styles
  html += ".alarm{margin:16px 0 6px;padding:14px 14px;border-radius:16px;border:1px solid var(--stroke);"
          "box-shadow:var(--shadow);display:flex;gap:12px;align-items:flex-start}";
  html += ".alarm .badge{font-weight:800;font-size:12px;letter-spacing:.35px;padding:6px 10px;border-radius:999px;"
          "border:1px solid var(--stroke);white-space:nowrap}";
  html += ".alarm .txt{display:flex;flex-direction:column;gap:6px}";
  html += ".alarm .txt .h{font-weight:800;font-size:15px}";
  html += ".alarm .txt ul{margin:0;padding-left:18px;color:var(--muted)}";
  html += ".alarmOK{background:linear-gradient(180deg,rgba(40,215,196,.12),rgba(17,31,56,.85))}";
  html += ".alarmOK .badge{color:var(--teal);background:rgba(40,215,196,.10)}";
  html += ".alarmWARN{background:linear-gradient(180deg,rgba(255,200,87,.18),rgba(17,31,56,.88))}";
  html += ".alarmWARN .badge{color:var(--amber);background:rgba(255,200,87,.10)}";
  html += ".alarmCRIT{background:linear-gradient(180deg,rgba(255,77,109,.18),rgba(17,31,56,.88))}";
  html += ".alarmCRIT .badge{color:var(--red);background:rgba(255,77,109,.10)}";
  html += ".alarmFAULT{background:linear-gradient(180deg,rgba(255,79,216,.16),rgba(17,31,56,.88))}";
  html += ".alarmFAULT .badge{color:var(--pink);background:rgba(255,79,216,.10)}";
  html += "</style>";

  // Auto refresh (kept)
  html += "<script>";
  html += "setInterval(loadDoc,1000);";
  html += "function loadDoc(){var xhttp=new XMLHttpRequest();";
  html += "xhttp.onreadystatechange=function(){if(this.readyState==4&&this.status==200){document.documentElement.innerHTML=this.responseText;}};";
  html += "xhttp.open('GET','/',true);xhttp.send();}";
  html += "</script>";

  html += "</head><body><div id='page'>";

  html += "<div class='header'>";
  html += "<h1 class='title'>Health Monitoring System</h1>";
  html += "<p class='subtitle'>Bachelor Project Tohid Beheshti</p>";
  html += "</div>";

  html += "<div class='shell'>";
  html += "<div class='shellHead'><h2>Sensors Dashboard</h2>";
  html += "<span class='badge'><i class='fas fa-wifi'></i>&nbsp;Live · 1s</span></div>";

    // ======================= Alarm banner (server-side) =======================
  AlarmLevel ov = overallAlarmLevel();
  html += "<div class='alarm " + alarmLevelCss(ov) + "'>";
  html += "<div class='badge'>" + alarmLevelText(ov) + "</div>";
  html += "<div class='txt'><div class='h'>System Status</div>";

  // Details list (only show items that are not OK)
  html += "<ul>";
  if (AL_bpm == AL_FAULT)      html += "<li>Heart rate sensor: no valid reading (finger lifted / contact lost).</li>";
  else if (AL_bpm == AL_CRIT)  html += "<li>Heart rate CRITICAL: " + String(BPM,0) + " bpm.</li>";
  else if (AL_bpm == AL_WARN)  html += "<li>Heart rate warning: " + String(BPM,0) + " bpm.</li>";

  if (AL_spo2 == AL_FAULT)     html += "<li>SpO₂ sensor: no valid reading (finger lifted / contact lost).</li>";
  else if (AL_spo2 == AL_CRIT) html += "<li>SpO₂ CRITICAL: " + String(SpO2,0) + "%.</li>";
  else if (AL_spo2 == AL_WARN) html += "<li>SpO₂ warning: " + String(SpO2,0) + "%.</li>";

  if (AL_bt == AL_FAULT)       html += "<li>Body temperature sensor: fault / disconnected.</li>";
  else if (AL_bt == AL_CRIT)   html += "<li>Body temperature CRITICAL: " + String(bodytemperature,1) + "°C.</li>";
  else if (AL_bt == AL_WARN)   html += "<li>Body temperature warning: " + String(bodytemperature,1) + "°C.</li>";

  if (AL_rt == AL_FAULT)       html += "<li>Room temperature sensor: fault / invalid reading.</li>";
  else if (AL_rt == AL_CRIT)   html += "<li>Room temperature CRITICAL: " + String(temperature,1) + "°C.</li>";
  else if (AL_rt == AL_WARN)   html += "<li>Room temperature warning: " + String(temperature,1) + "°C.</li>";

  if (AL_hum == AL_FAULT)      html += "<li>Humidity sensor: fault / invalid reading.</li>";
  else if (AL_hum == AL_CRIT)  html += "<li>Humidity CRITICAL: " + String(humidity,0) + "%.</li>";
  else if (AL_hum == AL_WARN)  html += "<li>Humidity warning: " + String(humidity,0) + "%.</li>";

  if (ov == AL_OK) html += "<li>All signals are within normal ranges.</li>";
  html += "</ul>";

  html += "</div></div>";

  html += "<div class='grid'>";

  // Room Temp
  html += "<div class='card tBlue'><div class='row'>";
  html += "<div class='icon'><i class='fas fa-thermometer-half icoBlue'></i></div>";
  html += "<div><p class='label'>Room Temperature</p><p class='value'>";
  html += String((int)temperature);
  html += "<span class='unit'>°C</span></p></div></div>";
  html += "<div class='foot'><div class='bar' style='width:70%'></div></div></div>";

  // Humidity
  html += "<div class='card tAqua'><div class='row'>";
  html += "<div class='icon'><i class='fas fa-tint icoAqua'></i></div>";
  html += "<div><p class='label'>Room Humidity</p><p class='value'>";
  html += String((int)humidity);
  html += "<span class='unit'>%</span></p></div></div>";
  html += "<div class='foot'><div class='bar' style='width:55%'></div></div></div>";

  // Heart Rate
  html += "<div class='card tRed'><div class='row'>";
  html += "<div class='icon'><i class='fas fa-heartbeat icoRed'></i></div>";
  html += "<div><p class='label'>Heart Rate</p><p class='value'>";
  html += String((int)BPM);
  html += "<span class='unit'>BPM</span></p></div></div>";
  html += "<div class='foot'><div class='bar' style='width:60%'></div></div></div>";

  // SpO2
  html += "<div class='card tPink'><div class='row'>";
  html += "<div class='icon'><i class='fas fa-burn icoPink'></i></div>";
  html += "<div><p class='label'>SpO2</p><p class='value'>";
  html += String((int)SpO2);
  html += "<span class='unit'>%</span></p></div></div>";
  html += "<div class='foot'><div class='bar' style='width:75%'></div></div></div>";

  // Body Temp
  html += "<div class='card tAmber'><div class='row'>";
  html += "<div class='icon'><i class='fas fa-thermometer-full icoAmber'></i></div>";
  html += "<div><p class='label'>Body Temperature</p><p class='value'>";
  html += String((int)bodytemperature);
  html += "<span class='unit'>°C</span></p></div></div>";
  html += "<div class='foot'><div class='bar' style='width:68%'></div></div></div>";

  html += "</div></div></div></div></body></html>";
  return html;
}