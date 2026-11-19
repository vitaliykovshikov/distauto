// ==========================================
// УКРКРАФТ | Версія: v1.1
// Автоматика ректифікації: ESP32 + DS18B20 + Реле (НВ клапан) + OLED + Bluetooth + OTA
// Wi-Fi: мережа "Kolona" (пароль: 12345678) -> http://192.168.4.1
// Bluetooth: "ESP32_Kolona"
// ==========================================

#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <Preferences.h>
#include "OneWire.h"
#include "DallasTemperature.h"
#include <esp_task_wdt.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "BluetoothSerial.h"
#include <stdlib.h>
#include <ctype.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to enable it
#endif

BluetoothSerial SerialBT;

// ---------- Wi-Fi ----------
const char* AP_SSID = "Kolona";
const char* AP_PASS = "12345678";

// ---------- Піни ----------
const int PIN_RELAY   = 23;
const int PIN_ONEWIRE = 4;
const int PIN_SDA     = 21;
const int PIN_SCL     = 22;
const uint8_t OLED_ADDR = 0x3C;

const unsigned long SENSOR_FAIL_MS = 5000;

// ---------- Константи авто-режиму ----------
const float STAB_TOL = 0.15f;                    
const unsigned long STAB_MIN_MS = 10UL * 60000UL; 
const unsigned long REDUCE_HOLD_MS = 60000UL;
const float MIN_RATE_FACTOR = 0.4f;

// ---------- Типи ----------
enum Mode  { M_IDLE, M_MANUAL, M_AUTO, M_CALIB };
enum Phase { PH_NONE, PH_WARMUP, PH_HEADS, PH_STAB, PH_BODY, PH_DONE, PH_ABORT };

struct Plan {
  bool valid;
  float vol, abv, h, d;
  float headsPct, tailsPct, strengthPct, decrementPct, reduceDt;
  unsigned long revision;
  float aaMl, headsMl, bodyMl, headsRate, bodyRate;
  float warmMin;
  String warn;
};

// ---------- Налаштування ----------
float tSet = 78.3, tHyst = 0.3, tAlarm = 82.0;      
unsigned long periodMs = 10000, openMs = 1000;       
float flowMlS = 5.0;                                  
bool autoSwap = false;                                

unsigned long holdMaxMin = 20;                        
unsigned long holdSince = 0;

// ---------- Стан ----------
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);
WebServer server(80);
Preferences prefs;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

bool oledOk = false;
float temperature = NAN;
unsigned long lastGoodRead = 0, lastRequest = 0, cycleStart = 0;
bool conversionPending = false;

Mode mode = M_IDLE;
Phase phase = PH_NONE;
Plan plan;
bool coolingBlock = false;
bool manualStop = false;
bool valveIsOpen = false;
unsigned long openedAt = 0;

float phaseDelivered = 0, totalHeads = 0, totalBody = 0;
float phaseRate = 0, rateFactor = 1.0, tRef = NAN;
unsigned long phaseStart = 0, autoPeriod = 10000, autoOpen = 500;
unsigned long over1Since = 0;
bool idleValveClosed = false;
bool swapConfirmed = false;
String abortReason = "";

String statusText = "ГОТОВО";
const char* statusShort = "IDLE";
char infoText[140] = "";
char oledInfo[24] = "";

const int SBUF_N = 36;
float sbuf[SBUF_N];
int scount = 0, sidx = 0;
unsigned long lastSample = 0;

// ---------- Керування реле (Active-LOW модуль + НВ клапан 220В) ----------
// HIGH на IN -> реле вимкнене -> в розетці 0В -> НВ клапан ВІДКРИТИЙ
// LOW на IN  -> реле увімкнене -> в розетці 220В -> НВ клапан ЗАКРИТИЙ
void valveOpen() {
  if (!valveIsOpen) { valveIsOpen = true; openedAt = millis(); }
  digitalWrite(PIN_RELAY, HIGH);  
}
void valveClose() {
  if (valveIsOpen) {
    phaseDelivered += (millis() - openedAt) / 1000.0f * flowMlS;
    valveIsOpen = false;
  }
  digitalWrite(PIN_RELAY, LOW); 
}

float deliveredNow() {
  return phaseDelivered + (valveIsOpen ? (millis() - openedAt) / 1000.0f * flowMlS : 0.0f);
}

void setStatus(const String& ua, const char* en) { statusText = ua; statusShort = en; }
const char* b(bool v) { return v ? "true" : "false"; }
unsigned long holdMaxMs() { return holdMaxMin * 60000UL; }

const char* phaseName() {
  switch (phase) {
    case PH_WARMUP: return "WARMUP";
    case PH_HEADS:  return "HEADS";
    case PH_STAB:   return "STAB";
    case PH_BODY:   return "BODY";
    case PH_DONE:   return "DONE";
    case PH_ABORT:  return "ABORT";
    default:        return "NONE";
  }
}

void clearBuf() { scount = 0; sidx = 0; }

void sampleBuf(unsigned long now) {
  if (now - lastSample < 5000UL) return;
  if (isnan(temperature) || (now - lastGoodRead) >= SENSOR_FAIL_MS) return;
  lastSample = now;
  sbuf[sidx] = temperature;
  sidx = (sidx + 1) % SBUF_N;
  if (scount < SBUF_N) scount++;
}

float bufAvg() {
  if (scount == 0) return NAN;
  float s = 0;
  for (int i = 0; i < scount; i++) s += sbuf[i];
  return s / scount;
}

bool stableNow() {
  if (scount < SBUF_N) return false;
  float mn = sbuf[0], mx = sbuf[0];
  for (int i = 1; i < SBUF_N; i++) {
    if (sbuf[i] < mn) mn = sbuf[i];
    if (sbuf[i] > mx) mx = sbuf[i];
  }
  return (mx - mn) <= STAB_TOL && bufAvg() > 60.0f;
}

String makePlan(float vol, float abv, float h, float d, float headsPct,
                float tailsPct, float strengthPct, float decrementPct, float reduceDt) {
  plan.valid = false;
  if (!isfinite(vol) || vol < 1 || vol > 200) return "Об'єм має бути 1..200 л";
  if (!isfinite(abv) || abv < 10 || abv > 95) return "Міцність має бути 10..95 %";
  if (!isfinite(h) || h < 0.3 || h > 4) return "Висота колони має бути 0.3..4 м";
  if (!isfinite(d) || d < 0.5 || d > 6) return "Діаметр має бути 0.5..6 дюймів";
  if (!isfinite(headsPct) || headsPct <= 0 || headsPct > 30) return "Голови: більше 0 і до 30 % АС";
  if (!isfinite(tailsPct) || tailsPct < 0 || tailsPct > 50 || headsPct + tailsPct >= 100) return "Невірна частка залишку АС";
  if (!isfinite(strengthPct) || strengthPct < 80 || strengthPct > 96.6f) return "Міцність продукту: 80..96.6 %";
  if (!isfinite(decrementPct) || decrementPct < 1 || decrementPct > 50) return "Декремент: 1..50 %";
  if (!isfinite(reduceDt) || reduceDt < 0.05f || reduceDt > 2) return "Поріг декременту: 0.05..2 °C";

  plan.vol = vol; plan.abv = abv; plan.h = h; plan.d = d;
  plan.aaMl = vol * 1000.0f * abv / 100.0f;

  plan.headsPct = headsPct; plan.tailsPct = tailsPct; plan.strengthPct = strengthPct;
  plan.decrementPct = decrementPct; plan.reduceDt = reduceDt;
  plan.headsMl = plan.aaMl * headsPct / strengthPct;
  plan.bodyMl = plan.aaMl * (100.0f - headsPct - tailsPct) / strengthPct;

  float hf = fminf(1.3f, fmaxf(0.5f, h / 1.2f));          
  plan.bodyRate  = 300.0f * d * d * hf;                    
  plan.headsRate = fminf(400.0f, fmaxf(40.0f, 0.1f * plan.bodyRate));
  plan.warmMin  = 20.0f + 10.0f * h;

  plan.warn = "Швидкості та вихід орієнтовні. Скоригуйте швидкості під вашу колону. Вихід: голови + тіло; залишок АС не включено.";
  plan.revision++;
  plan.valid = true;
  return "";
}

void applyTiming() {
  float rate = phaseRate * rateFactor;                 
  float duty = (rate / 3600.0f) / flowMlS;
  unsigned long per = 10000UL;
  unsigned long op = (unsigned long)lroundf(per * duty);
  if (op < 200UL) {
    float p2 = 200.0f / duty;
    per = (unsigned long)ceilf(p2);
    op = 200UL;
  }
  autoPeriod = per;
  autoOpen = op;
}

void startPhase(Phase p) {
  valveClose();
  phase = p;
  phaseStart = millis();
  phaseDelivered = 0;
  over1Since = 0;
  cycleStart = millis();
  holdSince = 0;
  rateFactor = 1.0f;
  if (p == PH_HEADS) { phaseRate = plan.headsRate; applyTiming(); }
  if (p == PH_BODY)  { phaseRate = plan.bodyRate;  applyTiming(); }
  if (p == PH_STAB)  { clearBuf(); swapConfirmed = autoSwap; }
  if (p == PH_WARMUP) { clearBuf(); }
}

void startAuto() {
  idleValveClosed = true;
  manualStop = false;
  coolingBlock = false;
  abortReason = "";
  tRef = NAN;
  totalHeads = 0;
  totalBody = 0;
  mode = M_AUTO;
  startPhase(PH_WARMUP);
}

void finishAuto() {
  valveClose();
  totalBody = phaseDelivered;
  phase = PH_DONE;
  setStatus("АВТО: завершено", "AUTO DONE");
}

void abortAuto(const String& reason) {
  valveClose();
  abortReason = reason;
  phase = PH_ABORT;
  setStatus(String("АВТО: ") + reason, "AUTO STOPPED");
}

void endToIdle() {
  valveClose();
  idleValveClosed = true;
  mode = M_IDLE;
  phase = PH_NONE;
  manualStop = false;
  coolingBlock = false;
}

void runPulse(unsigned long period, unsigned long open) {
  unsigned long now = millis();
  unsigned long ph = now - cycleStart;
  if (ph >= period) { cycleStart = now; ph = 0; }
  if (ph < open) valveOpen(); else valveClose();
}

void checkOverTemp(unsigned long now) {
  float dT = temperature - tRef;
  if (dT >= plan.reduceDt) { if (!over1Since) over1Since = now; } else over1Since = 0;

  if (over1Since && (now - over1Since) > REDUCE_HOLD_MS && rateFactor > MIN_RATE_FACTOR) {
    rateFactor = fmaxf(MIN_RATE_FACTOR, rateFactor * (1.0f - plan.decrementPct / 100.0f));
    applyTiming();
    over1Since = now;
  }
}

void autoLoop(unsigned long now) {
  if (manualStop) {
    over1Since = 0;
    valveClose();
    cycleStart = now;
    setStatus("АВТО: ПАУЗА вручну", "AUTO PAUSED");
    strcpy(oledInfo, "paused");
    if (!holdSince) holdSince = now;
    if ((now - holdSince) > holdMaxMs()) abortAuto("пауза довша за ліміт часу");
    return;
  }
  holdSince = 0;
  float elapsedMin = (now - phaseStart) / 60000.0f;

  switch (phase) {
    case PH_WARMUP: {
      valveClose();
      bool st = stableNow();
      setStatus("АВТО: прогрів", "AUTO WARMUP");
      snprintf(infoText, sizeof(infoText), "Прогрів: %.0f/%.0f хв. Стабільно: %s", elapsedMin, plan.warmMin, st ? "так" : "ні");
      if (elapsedMin >= plan.warmMin && st) { tRef = bufAvg(); startPhase(PH_HEADS); }
      break;
    }
    case PH_HEADS: {
      setStatus("АВТО: відбір голів", "AUTO HEADS");
      if (deliveredNow() >= plan.headsMl) { valveClose(); totalHeads = phaseDelivered; startPhase(PH_STAB); break; }
      else runPulse(autoPeriod, autoOpen);
      snprintf(infoText, sizeof(infoText), "Голови: %.0f / %.0f мл; %.0f мл/год", deliveredNow(), plan.headsMl, plan.headsRate);
      break;
    }
    case PH_STAB: {
      valveClose();
      snprintf(infoText, sizeof(infoText), "Відібрано голів: %.0f мл", totalHeads);
      if (!swapConfirmed) {
        setStatus("АВТО: замініть ємність", "SWAP CONTAINER");
      } else {
        bool st = stableNow();
        setStatus("АВТО: стабілізація", "AUTO STABILIZE");
        if ((now - phaseStart) >= STAB_MIN_MS && st) { tRef = bufAvg(); startPhase(PH_BODY); }
      }
      break;
    }
    case PH_BODY: {
      setStatus("АВТО: відбір тіла", "AUTO BODY");
      if (deliveredNow() >= plan.bodyMl) finishAuto();
      else { checkOverTemp(now); runPulse(autoPeriod, autoOpen); }
      snprintf(infoText, sizeof(infoText), "Тіло: %.0f / %.0f мл; %.0f мл/год (%.0f%%)", deliveredNow(), plan.bodyMl, phaseRate * rateFactor, rateFactor * 100.0f);
      break;
    }
    default: valveClose(); break;
  }
}

void updateDisplay() {
  static unsigned long last = 0;
  if (!oledOk || millis() - last < 500) return;
  last = millis();
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  if (isnan(temperature)) display.print("--.--");
  else display.printf("%.2f", temperature);
  display.print(" C");

  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print(statusShort);
  display.setCursor(0, 32);
  display.print(oledInfo);
  display.setCursor(0, 44);
  display.print("Valve: ");
  display.print(valveIsOpen ? "OPEN" : "CLOSED");
  display.setCursor(0, 56);
  display.print("UKRKRAFT v1.1");
  display.display();
}

const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="uk"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>УКРКРАФТ - Колона v1.1</title>
<style>
body{font-family:sans-serif;margin:0;padding:16px;background:#111;color:#eee}
.card{background:#1e1e1e;border-radius:12px;padding:16px;margin-bottom:14px}
.big{font-size:44px;font-weight:bold}
.st{font-size:20px;margin-top:6px}
.sub{font-size:15px;color:#aaa;margin-top:6px}
label{display:block;margin-top:10px;font-size:14px;color:#aaa}
input[type=number]{width:100%;box-sizing:border-box;padding:10px;font-size:18px;border-radius:8px;border:1px solid #444;background:#111;color:#eee}
.flow-fields{display:flex;gap:12px}
.flow-fields>div{flex:1;min-width:0}
button{width:100%;padding:14px;margin-top:14px;font-size:18px;border:0;border-radius:10px;color:#fff;background:#2a6fdb}
button.red{background:#c0392b}
button.green{background:#27ae60}
button.gray{background:#555}
#plan{white-space:pre-line;margin-top:12px;font-size:15px}
.msg{margin-top:10px;min-height:20px}
</style></head><body>
<div class="card">
 <div style="text-align:right;font-size:12px;color:#888">УКРКРАФТ v1.1</div>
 <div class="big"><span id="t">--</span> °C</div>
 <div class="st" id="s">...</div>
 <div class="sub" id="info"></div>
 <button id="pause" class="red" onclick="togglePause()" style="display:none">Пауза</button>
 <button id="swap" onclick="doSwap()" style="display:none">Ємність замінено, продовжити</button>
 <button id="end" class="red" onclick="endAuto()" style="display:none">Зупинити авто</button>
</div>
<div class="card" id="autoForm">
 <b>Авто ректифікація (УКРКРАФТ)</b>
 <label>Об'єм сирцю, л</label><input id="vol" type="number" step="0.1" value="10">
 <label>Міцність сирцю, %</label><input id="abv" type="number" step="0.1" value="40">
 <label>Висота колони, м</label><input id="hh" type="number" min="0.3" max="4" step="0.01" value="1">
 <label>Діаметр колони, дюйми</label><input id="dd" type="number" min="0.5" max="6" step="0.1" value="4">
 <label>Голови, % від абсолютного спирту</label><input id="headsPct" type="number" min="0.1" max="30" step="0.1" value="10">
 <label>Декремент: зростання температури від початку тіла, °C</label><input id="reduceDt" type="number" min="0.05" max="2" step="0.05" value="0.20">
 <label>Декремент: зменшення поточної швидкості, %</label><input id="decrementPct" type="number" min="1" max="50" step="1" value="10">
 <div class="sub">При перевищенні порога протягом 60 с швидкість знижується. Якщо температура залишається підвищеною — повтор кожні 60 с, до 40% початкової швидкості. Завершення — за розрахованим об’ємом тіла або вручну.</div>
 <details><summary>Припущення для розрахунку виходу</summary>
  <label>Очікувана міцність голів і тіла, % об.</label><input id="strengthPct" type="number" min="80" max="96.6" step="0.1" value="96">
  <label>АС, що залишається у хвостах та втратах, % від початкового АС</label><input id="tailsPct" type="number" min="0" max="50" step="0.1" value="15">
 </details>
 <button onclick="calc()">Розрахувати режим</button>
 <div id="plan"></div>
 <div id="rates" style="display:none">
  <label>Швидкість відбору голів, мл/год</label><input id="headsRate" type="number" min="0.1" step="0.01">
  <label>Початкова швидкість відбору тіла, мл/год</label><input id="bodyRate" type="number" min="0.1" step="0.01">
  <div class="sub" id="planWarn"></div>
 </div>
 <button id="startAuto" style="display:none" onclick="startAuto()">Запустити авто</button>
</div>
<div class="card">
 <b>Ручний режим і налаштування</b>
 <button id="manBtn" onclick="toggleManual()">Запустити ручний відбір</button>
 <label>Аварійна температура, °C</label><input id="talarm" type="number" step="0.1">
 <div class="flow-fields">
  <div><label for="flow">Витрата клапана (мл/с)</label><input id="flow" type="number" step="0.01" oninput="updateFlowHour()"></div>
  <div><label for="flowHour">Витрата (мл/год)</label><input id="flowHour" type="number" step="0.01" readonly></div>
 </div>
 <label>Ручний: уставка, °C</label><input id="tset" type="number" step="0.1">
 <label>Ручний: гістерезис, °C</label><input id="thyst" type="number" step="0.1">
 <label>Ручний: період, с</label><input id="per" type="number" step="0.1">
 <label>Ручний: час відкриття, с</label><input id="opn" type="number" step="0.1">
 <button onclick="save()">Зберегти</button>
 <div class="msg" id="msg"></div>
</div>
<div class="card">
 <b>Калібрування клапана</b>
 <label>Час тесту відкриття (секунд)</label><input id="calSec" type="number" value="10">
 <label>Вилито рідини у мірний посуд (мл)</label><input id="calMl" type="number" value="50">
 <button class="green" onclick="runCalibTest()">1. Відкрити клапан на тестовий час</button>
 <button onclick="saveCalib()">2. Зберегти калібрування (мл/с)</button>
 <div class="msg" id="calMsg"></div>
</div>
<script>
const $=id=>document.getElementById(id);
const show=(id,v)=>$(id).style.display=v?'block':'none';
let filled=false,stopped=false,mode='IDLE',phase='NONE';
let planRevision=null,calcGeneration=0;
const planInputs=['vol','abv','hh','dd','headsPct','tailsPct','strengthPct','decrementPct','reduceDt'];
function invalidatePlan(){planRevision=null;calcGeneration++;show('startAuto',false);show('rates',false);$('plan').textContent='Параметри змінено. Розрахуйте режим.';}
planInputs.forEach(id=>$(id).addEventListener('input',invalidatePlan));
function updateFlowHour(){
 const value=$('flow').value;
 const flow=Number(value);
 $('flowHour').value=value.trim()!==''&&Number.isFinite(flow)?(flow*3600).toFixed(2):'';
}
async function upd(){
 try{
  const d=await(await fetch('/status')).json();
  mode=d.mode;phase=d.phase;stopped=d.manualStop;
  $('t').textContent=d.temp===null?'--':d.temp.toFixed(2);
  $('s').textContent=d.status;
  $('info').textContent=d.info;
  const fin=(phase==='DONE'||phase==='ABORT');
  show('pause',mode!=='IDLE'&&!fin);
  $('pause').textContent=stopped?'Продовжити':'Пауза';
  show('swap',d.needSwap);
  show('end',mode==='AUTO');
  $('end').textContent=fin?'Повернутися до налаштувань':'Зупинити авто (закрити клапан)';
  show('autoForm',mode==='IDLE');
  show('manBtn',mode!=='AUTO');
  $('manBtn').textContent=mode==='MANUAL'?'Зупинити ручний відбір':'Запустити ручний відбір';
  if(!filled){
   $('tset').value=d.tSet;$('thyst').value=d.tHyst;$('talarm').value=d.tAlarm;
   $('per').value=d.period;$('opn').value=d.open;$('flow').value=d.flow;
   updateFlowHour();
   filled=true;
  }
 }catch(e){}
}
async function togglePause(){await fetch('/stop?on='+(stopped?0:1));upd();}
async function doSwap(){
 try{const r=await fetch('/auto/swap',{method:'POST'});if(!r.ok){$('info').textContent=await r.text();return;}await upd();}
 catch(e){$('info').textContent='Помилка зв’язку. Заміну ємності не підтверджено.';}
}
async function endAuto(){
 try{const r=await fetch('/auto/end',{method:'POST'});if(!r.ok){$('info').textContent=await r.text();return;}invalidatePlan();await upd();}
 catch(e){$('info').textContent='Помилка зв’язку. Зупинку не підтверджено.';}
}
async function toggleManual(){await fetch('/manual?on='+(mode==='MANUAL'?0:1));upd();}
async function calc(){
 invalidatePlan();
 const generation=calcGeneration;
 $('plan').textContent='Розрахунок…';
 const p=new URLSearchParams({vol:$('vol').value,abv:$('abv').value,h:$('hh').value,d:$('dd').value,
  headsPct:$('headsPct').value,tailsPct:$('tailsPct').value,strengthPct:$('strengthPct').value,
  decrementPct:$('decrementPct').value,reduceDt:$('reduceDt').value});
 try{
 const r=await fetch('/plan',{method:'POST',body:p});
 if(!r.ok){const message=await r.text();if(generation===calcGeneration)$('plan').textContent=message;return;}
 const d=await r.json();
 if(generation!==calcGeneration)return;
 planRevision=d.revision;
 $('plan').textContent='Абсолютний спирт: '+d.aa.toFixed(2)+' мл ('+(d.aa/1000).toFixed(3)+' л)\n'+
  'Кількість голів: '+d.headsMl.toFixed(2)+' мл\nКількість тіла: '+d.bodyMl.toFixed(2)+' мл\n'+
  'Розрахунковий вихід (голови + тіло): '+(d.yieldMl/1000).toFixed(3)+' л при '+d.strengthPct+'%';
 $('headsRate').value=d.headsRate;$('bodyRate').value=d.bodyRate;
 $('planWarn').textContent=d.warn;
 show('rates',true);
 show('startAuto',true);
 }catch(e){if(generation===calcGeneration)$('plan').textContent='Помилка зв’язку. Повторіть розрахунок.';}
}
async function startAuto(){
 if(planRevision===null)return;
 const p=new URLSearchParams({revision:planRevision,headsRate:$('headsRate').value,bodyRate:$('bodyRate').value});
 $('startAuto').disabled=true;
 try{
  const r=await fetch('/auto/start',{method:'POST',body:p});
  if(!r.ok){$('planWarn').textContent=await r.text();return;}
  invalidatePlan();await upd();
 }catch(e){$('planWarn').textContent='Помилка зв’язку. Перевірте стан процесу.';}
 finally{$('startAuto').disabled=false;}
}
async function save(){
 const p=new URLSearchParams({tset:$('tset').value,thyst:$('thyst').value,talarm:$('talarm').value,
  per:$('per').value,opn:$('opn').value,flow:$('flow').value});
 const r=await fetch('/set',{method:'POST',body:p});
 $('msg').textContent=await r.text();
}
async function runCalibTest(){
 const sec=$('calSec').value;
 const r=await fetch('/calib/test?sec='+sec);
 $('calMsg').textContent=await r.text();
}
async function saveCalib(){
 const sec=$('calSec').value, ml=$('calMl').value;
 const r=await fetch('/calib/save?sec='+sec+'&ml='+ml);
 $('calMsg').textContent=await r.text();
 if(r.ok){$('flow').value=Number(ml)/Number(sec);updateFlowHour();}
 upd();
}
upd();setInterval(upd,2000);
</script></body></html>
)HTML";

float argF(const char* n) {
  String s = server.arg(n);
  s.replace(',', '.');
  const char* start = s.c_str();
  char* end;
  float value = strtof(start, &end);
  if (end == start) return NAN;
  while (*end && isspace((unsigned char)*end)) end++;
  return (*end || !isfinite(value)) ? NAN : value;
}

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", PAGE); }

void handleStatus() {
  char buf[1800];
  char tbuf[16], rbuf[16];
  if (isnan(temperature)) strcpy(tbuf, "null"); else snprintf(tbuf, sizeof(tbuf), "%.2f", temperature);
  if (isnan(tRef)) strcpy(rbuf, "null"); else snprintf(rbuf, sizeof(rbuf), "%.2f", tRef);
  const char* modeS = (mode == M_IDLE) ? "IDLE" : (mode == M_MANUAL ? "MANUAL" : (mode == M_AUTO ? "AUTO" : "CALIB"));
  bool needSwap = (mode == M_AUTO && phase == PH_STAB && !swapConfirmed);
  snprintf(buf, sizeof(buf),
    R"JSON({"temp":%s,"status":"%s","info":"%s","mode":"%s","phase":"%s","valve":%s,"manualStop":%s,"needSwap":%s,"planValid":%s,"tRef":%s,"tSet":%.1f,"tHyst":%.1f,"tAlarm":%.1f,"period":%.1f,"open":%.1f,"flow":%.4f})JSON",
    tbuf, statusText.c_str(), infoText, modeS, phaseName(),
    b(valveIsOpen), b(manualStop), b(needSwap), b(plan.valid), rbuf,
    tSet, tHyst, tAlarm, periodMs / 1000.0, openMs / 1000.0, flowMlS);
  server.send(200, "application/json; charset=utf-8", buf);
}

void handleStop() {
  manualStop = server.arg("on") == "1";
  if (manualStop) valveClose();
  server.send(200, "text/plain", "OK");
}

void handleManual() {
  bool on = server.arg("on") == "1";
  if (on && mode == M_IDLE) {
    mode = M_MANUAL; coolingBlock = false; manualStop = false; cycleStart = millis();
  } else if (!on && mode == M_MANUAL) {
    endToIdle();
  }
  server.send(200, "text/plain", "OK");
}

void handleSet() {
  if (mode == M_AUTO) { server.send(400, "text/plain", "Недоступно під час авто"); return; }
  float ts = argF("tset"), th = argF("thyst"), ta = argF("talarm");
  float per = argF("per"), op = argF("opn"), flow = argF("flow");
  if (!isfinite(ts) || !isfinite(th) || !isfinite(ta) || !isfinite(per) || !isfinite(op) || !isfinite(flow) ||
      ts < 0 || ts > 125 || th < 0 || th > 20 || ta <= ts || ta > 125 || per < 0.2f || per > 3600 || op < 0 || op > per || flow <= 0 || flow > 1000) {
    server.send(400, "text/plain; charset=utf-8", "Перевірте температуру, період, час відкриття та витрату клапана"); return;
  }
  valveClose();
  tSet = ts; tHyst = th; tAlarm = ta;
  periodMs = (unsigned long)(per * 1000);
  openMs = (unsigned long)(op * 1000);
  flowMlS = flow;
  
  prefs.putFloat("tSet", tSet);  prefs.putFloat("tHyst", tHyst);
  prefs.putFloat("tAlarm", tAlarm);
  prefs.putULong("period", periodMs); prefs.putULong("open", openMs);
  prefs.putFloat("flow", flowMlS);

  cycleStart = millis();
  server.send(200, "text/plain", "Збережено");
}

void handlePlan() {
  if (mode != M_IDLE) { server.send(400, "text/plain", "Лише в режимі очікування"); return; }
  String err = makePlan(argF("vol"), argF("abv"), argF("h"), argF("d"),
    argF("headsPct"), argF("tailsPct"), argF("strengthPct"), argF("decrementPct"), argF("reduceDt"));
  if (err.length()) { server.send(400, "text/plain", err); return; }
  char buf[1400];
  snprintf(buf, sizeof(buf),
    R"JSON({"revision":%lu,"aa":%.2f,"headsMl":%.2f,"bodyMl":%.2f,"headsRate":%.2f,"bodyRate":%.2f,"yieldMl":%.2f,"strengthPct":%.2f,"warn":"%s"})JSON",
    plan.revision, plan.aaMl, plan.headsMl, plan.bodyMl, plan.headsRate, plan.bodyRate,
    plan.headsMl + plan.bodyMl, plan.strengthPct, plan.warn.c_str());
  server.send(200, "application/json; charset=utf-8", buf);
}

void handleAutoStart() {
  if (mode != M_IDLE || !plan.valid) { server.send(409, "text/plain; charset=utf-8", "Спочатку розрахуйте режим у стані очікування"); return; }
  if (server.arg("revision") != String(plan.revision)) { server.send(409, "text/plain; charset=utf-8", "Розрахунок змінився. Розрахуйте повторно"); return; }
  float headsRate = argF("headsRate"), bodyRate = argF("bodyRate");
  float minRate = flowMlS * 0.2f / MIN_RATE_FACTOR;
  float maxRate = flowMlS * 3600.0f * 0.8f;
  if (!isfinite(flowMlS) || flowMlS <= 0 || !isfinite(headsRate) || !isfinite(bodyRate) ||
      headsRate < minRate || bodyRate < minRate || headsRate > maxRate || bodyRate > maxRate) {
    server.send(400, "text/plain; charset=utf-8", "Швидкості поза можливостями клапана. Перевірте калібрування та зменшіть швидкість"); return;
  }
  if (!isfinite(temperature) || millis() - lastGoodRead >= SENSOR_FAIL_MS || temperature >= tAlarm) {
    server.send(400, "text/plain; charset=utf-8", "Перевірте датчик та аварійну температуру"); return;
  }
  plan.headsRate = headsRate; plan.bodyRate = bodyRate;
  startAuto(); server.send(200, "text/plain", "OK");
}
void handleAutoSwap() { if (mode == M_AUTO && phase == PH_STAB) swapConfirmed = true; server.send(200, "text/plain", "OK"); }
void handleAutoEnd() {
  if (mode != M_AUTO) { server.send(409, "text/plain", "AUTO only"); return; }
  if (phase == PH_DONE || phase == PH_ABORT) endToIdle();
  else abortAuto("зупинено вручну");
  server.send(200, "text/plain", "OK");
}

void handleCalibTest() {
  if (mode != M_IDLE) { server.send(409, "text/plain", "IDLE only"); return; }
  int sec = server.arg("sec").toInt();
  if (sec < 1) sec = 10;
  mode = M_CALIB;
  valveOpen();
  delay(sec * 1000UL);
  valveClose();
  mode = M_IDLE;
  idleValveClosed = true;
  server.send(200, "text/plain", "Тест завершено! Виміряйте об'єм у циліндрі.");
}

void handleCalibSave() {
  if (mode != M_IDLE) { server.send(409, "text/plain", "IDLE only"); return; }
  float sec = argF("sec");
  float ml = argF("ml");
  if (isfinite(sec) && isfinite(ml) && sec > 0 && ml > 0 && ml / sec <= 1000) {
    valveClose();
    flowMlS = ml / sec;
    prefs.putFloat("flow", flowMlS);
    server.send(200, "text/plain", String("Збережено! Нова швидкість: ") + flowMlS + " мл/с");
  } else {
    server.send(400, "text/plain", "Невірні дані");
  }
}

void readTemperature() {
  unsigned long now = millis();
  if (!conversionPending && now - lastRequest >= 1000) {
    sensors.requestTemperatures();
    lastRequest = now;
    conversionPending = true;
  } else if (conversionPending && now - lastRequest >= 800) {   
    float t = sensors.getTempCByIndex(0);
    conversionPending = false;
    if (t != DEVICE_DISCONNECTED_C && t > -50 && t < 125) {
      temperature = t;
      lastGoodRead = now;
    }
  }
}

void setup() {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, HIGH); // Старт в режимі очікування: реле вимкнене (0В), клапан відкритий
  valveIsOpen = true;

  Serial.begin(115200);
  SerialBT.begin("ESP32_Kolona");

  prefs.begin("col", false);
  tSet     = prefs.getFloat("tSet", tSet);
  tHyst    = prefs.getFloat("tHyst", tHyst);
  tAlarm   = prefs.getFloat("tAlarm", tAlarm);
  periodMs = prefs.getULong("period", periodMs);
  openMs   = prefs.getULong("open", openMs);
  flowMlS  = prefs.getFloat("flow", flowMlS);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);

  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.setResolution(12);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/stop", handleStop);
  server.on("/manual", handleManual);
  server.on("/set", HTTP_POST, handleSet);
  server.on("/plan", HTTP_POST, handlePlan);
  server.on("/auto/start", HTTP_POST, handleAutoStart);
  server.on("/auto/swap", HTTP_POST, handleAutoSwap);
  server.on("/auto/end", HTTP_POST, handleAutoEnd);
  server.on("/calib/test", handleCalibTest);
  server.on("/calib/save", handleCalibSave);

  ElegantOTA.begin(&server);
  server.begin();

  lastGoodRead = millis();
  cycleStart = millis();
}

void loop() {
  server.handleClient();
  ElegantOTA.loop();

  readTemperature();
  unsigned long now = millis();
  sampleBuf(now);
  updateDisplay();

  static unsigned long lastBt = 0;
  if (SerialBT.hasClient() && now - lastBt > 2000) {
    lastBt = now;
    SerialBT.printf("Temp: %.2f C | Status: %s | Valve: %s\n", temperature, statusShort, valveIsOpen ? "OPEN" : "CLOSED");
  }

  if (mode == M_IDLE || mode == M_CALIB) {
    if (mode == M_IDLE) { if (idleValveClosed) valveClose(); else valveOpen(); }
    setStatus("ГОТОВО: очікування", "IDLE");
    return;
  }

  if (mode == M_AUTO && (phase == PH_DONE || phase == PH_ABORT)) {
    valveClose();
    return;
  }

  bool sensorOk = isfinite(temperature) && (now - lastGoodRead) < SENSOR_FAIL_MS;
  if (!sensorOk) {
    valveClose();
    if (mode == M_AUTO) abortAuto("помилка датчика");
    setStatus("ПОМИЛКА ДАТЧИКА", "SENSOR ERROR");
    return;
  }

  if (temperature >= tAlarm) {
    valveClose();
    if (mode == M_AUTO) abortAuto("перегрів");
    return;
  }

  if (mode == M_AUTO) {
    autoLoop(now);
    return;
  }

  if (temperature > tSet + tHyst) coolingBlock = true;
  else if (temperature < tSet - tHyst) coolingBlock = false;

  if (manualStop || coolingBlock) {
    valveClose();
    return;
  }
  
  runPulse(periodMs, openMs);
}
