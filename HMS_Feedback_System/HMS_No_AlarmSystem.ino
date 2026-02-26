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

const float alpha_BPM_step  = 0.20f;   // smaller --> smoother slower
const float alpha_Spo2_step = 0.10f;

const float MAX_BPM_STEP   = 12.0f;   // maximum change 1s
const float MAX_SPO2_STEP  = 3.0f;

// finger lift
uint32_t lastValidPoxMs = 0;
const uint32_t finger_lift_timeMS = 2000;  // 2s lift



/// DS18B20 non_blocking
uint32_t lastBodyReqMs = 0;
const uint32_t BODY_PERIOD_MS = 2000; // update 2s

void onBeatDetected() {
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