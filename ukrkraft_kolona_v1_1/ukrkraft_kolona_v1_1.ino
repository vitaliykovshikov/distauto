// ==========================================
// УКРКРАФТ | Версія: v1.3
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
enum Phase { PH_NONE, PH_WARMUP, PH_HEADS, PH_STAB, PH_BODY, PH_DONE, PH_ABORT, PH_SPIT, PH_DRIP, PH_STREAM, PH_TAILS };

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
bool headsRecipe = false;
bool stageAwaiting = false;
void showStageDecisionStatus();
void beginRun(const char* label);
void endRun(const char* result);
void accountRun(float ml);
void recipeLoop(unsigned long now);
void applyRecipePhase(Phase p);
void logPhaseChange();

// ---------- Керування реле (Active-LOW модуль + НВ клапан 220В) ----------
// HIGH на IN -> реле вимкнене -> в розетці 0В -> НВ клапан ВІДКРИТИЙ
// LOW на IN  -> реле увімкнене -> в розетці 220В -> НВ клапан ЗАКРИТИЙ
void valveOpen() {
  if (!valveIsOpen) { valveIsOpen = true; openedAt = millis(); }
  digitalWrite(PIN_RELAY, HIGH);  
}
void valveClose() {
  if (valveIsOpen) {
    float ml = (millis() - openedAt) / 1000.0f * flowMlS;
    phaseDelivered += ml;
    accountRun(ml);
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
    case PH_SPIT:   return "SPIT";
    case PH_DRIP:   return "DRIP";
    case PH_STREAM: return "STREAM";
    case PH_TAILS:  return "TAILS";
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

float estimatedBodyRate(float h,float d){return 300.0f*d*d*fminf(1.3f,fmaxf(.5f,h/1.2f));}
float estimatedHeadsRate(float h,float d){return fminf(400.0f,fmaxf(40.0f,.1f*estimatedBodyRate(h,d)));}
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

  plan.bodyRate=estimatedBodyRate(h,d);plan.headsRate=estimatedHeadsRate(h,d);
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
  if (p == PH_SPIT || p == PH_DRIP || p == PH_STREAM) applyRecipePhase(p);
  logPhaseChange();
}

void startAuto() {
  valveClose();
  beginRun(headsRecipe ? "2-3-15" : "Авто");
  idleValveClosed = true;
  manualStop = false;
  coolingBlock = false;
  abortReason = "";
  tRef = NAN;
  totalHeads = 0;
  totalBody = 0;
  mode = M_AUTO;
  startPhase(PH_WARMUP);
  infoText[0]=0;
  setStatus("АВТО: прогрів", "AUTO WARMUP");
}

void finishAuto() {
  valveClose();
  if (!headsRecipe && phase == PH_BODY) totalBody = phaseDelivered;
  endRun("completed");
  phase = PH_DONE;
  setStatus("АВТО: завершено", "AUTO DONE");
}

void abortAuto(const String& reason) {
  valveClose();
  abortReason = reason;
  endRun(reason.c_str());
  phase = PH_ABORT;
  setStatus(String("АВТО: ") + reason, "AUTO STOPPED");
}

void endToIdle() {
  valveClose();
  endRun("зупинено вручну");
  idleValveClosed = true;
  mode = M_IDLE;
  phase = PH_NONE;
  manualStop = false;
  coolingBlock = false;
  setStatus("ГОТОВО: очікування","IDLE");infoText[0]=0;
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
  if(stageAwaiting){valveClose();showStageDecisionStatus();return;}
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
  if (headsRecipe && (phase == PH_SPIT || phase == PH_DRIP || phase == PH_STREAM)) { recipeLoop(now); return; }
  float elapsedMin = (now - phaseStart) / 60000.0f;

  switch (phase) {
    case PH_WARMUP: {
      valveClose();
      bool st = stableNow();
      setStatus("АВТО: прогрів", "AUTO WARMUP");
      snprintf(infoText, sizeof(infoText), "Прогрів: %.0f/%.0f хв. Стабільно: %s", elapsedMin, plan.warmMin, st ? "так" : "ні");
      if (elapsedMin >= plan.warmMin && st) { tRef = bufAvg(); startPhase(headsRecipe ? PH_SPIT : PH_HEADS); }
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
  display.print("UKRKRAFT v1.3");
  display.display();
}

#include "RunFeatures.h"

#include "WebPage.h"

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
    R"JSON({"temp":%s,"status":"%s","info":"%s","mode":"%s","phase":"%s","valve":%s,"manualStop":%s,"needSwap":%s,"planValid":%s,"tRef":%s,"tSet":%.1f,"tHyst":%.1f,"tAlarm":%.1f,"period":%.1f,"open":%.1f,"flow":%.4f,"recipe":%s,"manualProfile":%d,"rate":%.2f,"stageAwaiting":%s,"stageDecision":%lu,"nextProfile":%d,"extraHeads":%s})JSON",
    tbuf, statusText.c_str(), infoText, modeS, phaseName(),
    b(valveIsOpen), b(manualStop), b(needSwap), b(plan.valid), rbuf,
    tSet, tHyst, tAlarm, periodMs / 1000.0, openMs / 1000.0, flowMlS,b(headsRecipe),manualProfile,currentRate(),b(stageAwaiting),stageDecisionId,nextStageProfile(),b(recipeExtraHeads));
  server.send(200, "application/json; charset=utf-8", buf);
}

void handleStop() {
  if(stageAwaiting){server.send(409,"text/plain; charset=utf-8","Оберіть продовження або наступний етап");return;}
  manualStop = server.arg("on") == "1";
  if (manualStop) valveClose();
  server.send(200, "text/plain", "OK");
}

void handleManual() {
  bool on = server.arg("on") == "1";
  if (on && mode == M_IDLE) {
    startManualProfile(manualProfile);return;
  } else if (!on && mode == M_MANUAL) {
    endToIdle();
  }
  server.send(200, "text/plain", "OK");
}

void handleSet() {
  if (mode != M_IDLE) { server.send(400, "text/plain", "Недоступно під час відбору"); return; }
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
  headsRecipe = false;
  char buf[1400];
  snprintf(buf, sizeof(buf),
    R"JSON({"revision":%lu,"aa":%.2f,"headsMl":%.2f,"bodyMl":%.2f,"headsRate":%.2f,"bodyRate":%.2f,"yieldMl":%.2f,"strengthPct":%.2f,"warn":"%s"})JSON",
    plan.revision, plan.aaMl, plan.headsMl, plan.bodyMl, plan.headsRate, plan.bodyRate,
    plan.headsMl + plan.bodyMl, plan.strengthPct, plan.warn.c_str());
  server.send(200, "application/json; charset=utf-8", buf);
}

void handleAutoStart() {
  if (mode != M_IDLE || !plan.valid || headsRecipe) { server.send(409, "text/plain; charset=utf-8", "Спочатку розрахуйте режим у стані очікування"); return; }
  if (server.arg("revision") != String(plan.revision)) { server.send(409, "text/plain; charset=utf-8", "Розрахунок змінився. Розрахуйте повторно"); return; }
  float headsRate = argF("headsRate"), bodyRate = argF("bodyRate");
  float minRate = flowMlS * 0.2f / MIN_RATE_FACTOR;
  float maxRate = fminf(4000.0f,flowMlS * 3600.0f * 0.8f);
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
  float requested=argF("sec");
  if(!isfinite(requested)||requested<1||requested>60||floorf(requested)!=requested){server.send(400,"text/plain","Use 1..60 seconds");return;}
  int sec=(int)requested;
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
  initRunFeatures();

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
  recordRunSample(now);
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
    else { endRun("помилка датчика");mode=M_IDLE;idleValveClosed=true; }
    setStatus("ПОМИЛКА ДАТЧИКА", "SENSOR ERROR");
    return;
  }

  if (temperature >= tAlarm) {
    valveClose();
    if (mode == M_AUTO) abortAuto("перегрів");
    else {endRun("перегрів");mode=M_IDLE;idleValveClosed=true;}
    return;
  }

  if (mode == M_AUTO) {
    autoLoop(now);
    return;
  }

  if(stageAwaiting){valveClose();showStageDecisionStatus();return;}
  if (temperature > tSet + tHyst) coolingBlock = true;
  else if (temperature < tSet - tHyst) coolingBlock = false;
  snprintf(infoText,sizeof(infoText),"%s: %.1f мл; %.0f мл/год; уставка %.2f C",manualNames[manualProfile],deliveredNow(),manualProfiles[manualProfile].rate,tSet);

  if (manualStop || coolingBlock) {
    valveClose();
    setStatus(manualStop?"РУЧНИЙ: пауза":"РУЧНИЙ: температурна блокировка","MANUAL HOLD");
    return;
  }
  
  setStatus(String("РУЧНИЙ: ")+manualNames[manualProfile],"MANUAL");
  if(manualProfiles[manualProfile].rate>0)runPulse(autoPeriod,autoOpen);else valveClose();
}
