#pragma once
#include <vector>
#include <algorithm>
#include <stdint.h>
#ifndef DISTAUTO_SIMULATOR
#include <LittleFS.h>
#endif

float argF(const char* name);
struct ManualProfile { float set, hyst, rate; bool automatic=false; float target=0; };
ManualProfile manualProfiles[3]={{78.3f,0,400,false,500},{78.3f,.3f,4000,false,3000},{78.3f,.3f,1000,false,500}};
bool manualExtra=false,ssTemperaturePaused=false;
int manualProfile=0;
struct SimpleSettings { float close, open, rate, decrement; bool reduce; };
SimpleSettings startStop={78.5f,78.2f,2000,10,false},ss={80,0,4000,10,false};
String simpleJson(const SimpleSettings& p);
float recipePct=15,recipeRates[3]={4000,400,800},recipeTargets[3]={0,0,0};
bool recipeExtraHeads=false;
bool recipeAutomatic=false;
unsigned long stageDecisionId=0;
const char* manualNames[3]={"Голови","Тіло","Хвости"};
const Phase manualPhases[3]={PH_HEADS,PH_BODY,PH_TAILS};
const unsigned long HISTORY_LIMIT=2UL*1024UL*1024UL;
const int TRACE_MAX=600, PRESET_MAX=8;
struct TracePoint { uint32_t seconds; float temp, rate, ml; uint8_t phase; bool valve; };
struct RunRecord {
  bool exists=false,active=false,truncated=false;
  unsigned long id=0,start=0,duration=0,lastSample=0,traceInterval=5000,lastTrace=0;
  String date,label,kind,result,config;
  float ml=0,volumes[6]={0,0,0,0,0,0};
  TracePoint points[TRACE_MAX]; int count=0;
} run;
bool historyReady=false,historyWriting=false;
String historyBuffer,historyPath;
unsigned long historyFlushedAt=0;

String num(float value,int precision=2){
  if(!isfinite(value))return "null";
  char buffer[48];snprintf(buffer,sizeof(buffer),"%.*f",precision,value);return buffer;
}
String quote(const String& value){
  String out="\"";const char* text=value.c_str();
  for(size_t i=0;i<value.length();i++){
    unsigned char c=text[i];
    if(c=='"'||c=='\\'){out+='\\';out+=(char)c;}
    else if(c<32){char escaped[7];snprintf(escaped,sizeof(escaped),"\\u%04x",c);out+=escaped;}
    else out+=(char)c;
  }
  out+='"';return out;
}
const char* phaseCode(Phase p){
  switch(p){
    case PH_WARMUP:return "WARMUP";case PH_HEADS:return "HEADS";case PH_STAB:return "STAB";
    case PH_BODY:return "BODY";case PH_DONE:return "DONE";case PH_ABORT:return "ABORT";
    case PH_SPIT:return "SPIT";case PH_DRIP:return "DRIP";case PH_STREAM:return "STREAM";
    case PH_TAILS:return "TAILS";default:return "NONE";
  }
}
bool simulatorBuild(){
#ifdef DISTAUTO_SIMULATOR
  return true;
#else
  return false;
#endif
}
float maximumRate(){return flowMlS*3600.0f;}
float controlMaximumRate(){return fminf(4000.0f,maximumRate());}
bool validRate(float rate,bool allowZero=false){
  return isfinite(flowMlS)&&flowMlS>0&&isfinite(rate)&&
    ((allowZero&&rate==0)||rate>=flowMlS*.2f)&&rate<=controlMaximumRate();
}
bool readyToStart(){
  if(mode!=M_IDLE){server.send(409,"text/plain; charset=utf-8","Спочатку зупиніть поточний процес");return false;}
  if(!isfinite(temperature)||millis()-lastGoodRead>=SENSOR_FAIL_MS||!isfinite(tAlarm)||temperature>=tAlarm){
    server.send(400,"text/plain; charset=utf-8","Перевірте датчик та аварійну температуру");return false;
  }
  return true;
}
String profilesJson(){
  String out="[";
  for(int i=0;i<3;i++){
    if(i)out+=',';
    out+=String("{\"id\":")+String((unsigned long)i)+",\"name\":"+quote(manualNames[i])+",\"set\":"+num(manualProfiles[i].set)+
      ",\"hyst\":"+num(manualProfiles[i].hyst)+",\"rate\":"+num(manualProfiles[i].rate)+",\"automatic\":"+b(manualProfiles[i].automatic)+",\"target\":"+num(manualProfiles[i].target)+"}";
  }
  return out+"]";
}
String simpleJson(const SimpleSettings& p){
  return String("{\"close\":")+num(p.close)+",\"open\":"+num(p.open)+",\"rate\":"+num(p.rate)+",\"decrement\":"+num(p.decrement)+",\"reduce\":"+b(p.reduce)+"}";
}
String configJson(){
  return String("{\"vol\":")+num(plan.vol)+",\"abv\":"+num(plan.abv)+",\"height\":"+num(plan.h)+",\"diameter\":"+num(plan.d)+
    ",\"aaMl\":"+num(plan.aaMl)+",\"headsPct\":"+num(plan.headsPct)+",\"tailsPct\":"+num(plan.tailsPct)+
    ",\"strengthPct\":"+num(plan.strengthPct)+",\"headsRate\":"+num(plan.headsRate)+",\"bodyRate\":"+num(plan.bodyRate)+
    ",\"decrementPct\":"+num(plan.decrementPct)+",\"reduceDt\":"+num(plan.reduceDt)+",\"flowMlS\":"+num(flowMlS,4)+
    ",\"alarm\":"+num(tAlarm)+",\"manualProfile\":"+String((unsigned long)manualProfile)+",\"manualProfiles\":"+profilesJson()+
    ",\"recipePct\":"+num(recipePct)+",\"recipeAutomatic\":"+b(recipeAutomatic)+",\"recipeRates\":["+num(recipeRates[0])+","+num(recipeRates[1])+","+num(recipeRates[2])+
    "],\"recipeTargetsMl\":["+num(recipeTargets[0])+","+num(recipeTargets[1])+","+num(recipeTargets[2])+"],\"startStop\":"+simpleJson(startStop)+",\"ss\":"+simpleJson(ss)+"}";
}
int volumeIndex(){
  switch(phase){case PH_SPIT:return 0;case PH_DRIP:return 1;case PH_STREAM:return 2;
    case PH_HEADS:return 3;case PH_BODY:return 4;case PH_TAILS:return 5;default:return -1;}
}
float liveRunMl(){return run.ml+(run.active&&valveIsOpen?(millis()-openedAt)/1000.0f*flowMlS:0);}
void accountRun(float ml){
  if(!run.active)return;
  run.ml+=ml;int i=volumeIndex();if(i>=0)run.volumes[i]+=ml;
}
float currentRate(){
  if(stageAwaiting||manualStop||coolingBlock||!run.active)return 0;
  if(mode==M_MANUAL)return manualProfiles[manualProfile].rate;
  if(mode==M_STARTSTOP||mode==M_SS)return phaseRate*rateFactor;
  return phase==PH_HEADS||phase==PH_BODY||phase==PH_SPIT||phase==PH_DRIP||phase==PH_STREAM?phaseRate*rateFactor:0;
}
String runMeta(){
  return String("{\"schema\":1,\"id\":")+String(run.id)+",\"startedAt\":"+quote(run.date)+",\"label\":"+quote(run.label)+",\"kind\":"+quote(run.kind)+",\"parameters\":"+run.config+"}";
}
unsigned long historyBudget(){
#ifdef DISTAUTO_SIMULATOR
  return 0;
#else
  unsigned long total=historyReady?LittleFS.totalBytes():0;
  return total>32768UL?std::min(HISTORY_LIMIT,total-32768UL):0;
#endif
}
void flushHistory(){
#ifndef DISTAUTO_SIMULATOR
  if(!historyWriting||!historyBuffer.length())return;
  if(LittleFS.usedBytes()+historyBuffer.length()>historyBudget()){
    run.truncated=true;historyWriting=false;historyBuffer="";return;
  }
  File file=LittleFS.open(historyPath.c_str(),"a");
  if(!file || file.print(historyBuffer)!=historyBuffer.length()){run.truncated=true;historyWriting=false;}
  file.close();historyBuffer="";
  historyFlushedAt=millis();
#endif
}
void queueHistory(const String& line){
  if(!historyWriting)return;
  historyBuffer+=line;
  if(historyBuffer.length()>=1024)flushHistory();
}
void recordRunSample(unsigned long now,bool force=false){
  if(!run.active)return;
  if(!force && now-run.lastSample<5000UL)return;
  run.lastSample=now;
  unsigned long seconds=(now-run.start)/1000UL;
  TracePoint p={(uint32_t)seconds,temperature,currentRate(),liveRunMl(),(uint8_t)phase,valveIsOpen};
  if(force || !run.count || now-run.lastTrace>=run.traceInterval){
    if(run.count>=TRACE_MAX){
      int kept=0;for(int i=0;i<run.count;i+=2)run.points[kept++]=run.points[i];
      run.count=kept;run.traceInterval*=2;
    }
    run.points[run.count++]=p;run.lastTrace=now;
  }
  queueHistory(String(seconds)+","+num(p.temp)+","+phaseCode(phase)+","+num(p.rate)+","+(p.valve?"1":"0")+","+num(p.ml)+"\n");
  if(now-historyFlushedAt>=30000UL)flushHistory();
}
void logPhaseChange(){recordRunSample(millis(),true);}
void beginRun(const char* label){
  stageAwaiting=false;recipeExtraHeads=false;manualExtra=false;ssTemperaturePaused=false;
  run.exists=true;run.active=true;run.truncated=false;run.duration=0;run.lastSample=0;
  run.traceInterval=5000;run.lastTrace=0;run.count=0;run.ml=0;run.result="";
  for(float& ml:run.volumes)ml=0;
  run.start=millis();
  run.kind=strcmp(label,"Старт-стоп")==0?"startstop":strcmp(label,"SS")==0?"ss":strcmp(label,"Ручний")==0?"manual":headsRecipe?"recipe":"auto";
  run.id=prefs.getULong("runId",0)+1;prefs.putULong("runId",run.id);
  run.label=server.arg("label");if(!run.label.length()||run.label.length()>96)run.label=label;
  run.date=server.arg("date");if(run.date.length()>32)run.date="";
  run.config=configJson();historyWriting=false;historyBuffer="";
#ifndef DISTAUTO_SIMULATOR
  if(historyReady){
    historyPath=String("/history/run-")+String(run.id)+".csv";
    while(LittleFS.exists(historyPath.c_str())){run.id++;historyPath=String("/history/run-")+String(run.id)+".csv";}
    prefs.putULong("runId",run.id);
    File file=LittleFS.open(historyPath.c_str(),"w");
    historyWriting=(bool)file;file.close();
    if(historyWriting){queueHistory(String("# distauto-run-v1\n#meta,")+runMeta()+"\nelapsed_s,temperature_c,phase,rate_ml_h,valve,delivered_ml\n");flushHistory();}
    else run.truncated=true;
  }else run.truncated=true;
#endif
}
String runSummary(){
  String out=String("{\"durationSeconds\":")+String((run.active?millis()-run.start:run.duration)/1000UL)+
    ",\"deliveredMl\":"+num(liveRunMl())+",\"result\":"+quote(run.result)+",\"active\":"+b(run.active)+
    ",\"historyIncomplete\":"+b(run.truncated)+",\"volumesMl\":[";
  for(int i=0;i<6;i++){if(i)out+=',';float v=run.volumes[i];if(run.active&&valveIsOpen&&volumeIndex()==i)v+=(millis()-openedAt)/1000.0f*flowMlS;out+=num(v);}
  return out+"]}";
}
void endRun(const char* result){
  stageAwaiting=false;
  if(!run.active)return;
  recordRunSample(millis(),true);run.duration=millis()-run.start;run.result=result;run.active=false;
  queueHistory(String("#summary,")+runSummary()+"\n");flushHistory();historyWriting=false;
}
void handleReport(){
  if(!run.exists){server.send(200,"application/json","{\"exists\":false}");return;}
  String out=String("{\"exists\":true,\"meta\":")+runMeta()+",\"summary\":"+runSummary()+",\"sampleIntervalSeconds\":"+
    String(run.traceInterval/1000UL)+",\"samples\":[";
  for(int i=0;i<run.count;i++){
    if(i)out+=',';TracePoint &p=run.points[i];
    out+=String("[")+String((unsigned long)p.seconds)+","+num(p.temp)+","+quote(phaseCode((Phase)p.phase))+","+num(p.rate)+","+(p.valve?"true":"false")+","+num(p.ml)+"]";
  }
  out+="]}";server.send(200,"application/json; charset=utf-8",out);
}
void handleReportCsv(){
  if(!run.exists){server.send(404,"text/plain","No run");return;}
  String out=String("# distauto-run-v1\n#meta,")+runMeta()+"\nelapsed_s,temperature_c,phase,rate_ml_h,valve,delivered_ml\n";
  for(int i=0;i<run.count;i++){
    TracePoint &p=run.points[i];out+=String((unsigned long)p.seconds)+","+num(p.temp)+","+phaseCode((Phase)p.phase)+","+num(p.rate)+","+(p.valve?"1":"0")+","+num(p.ml)+"\n";
  }
  out+=String("#summary,")+runSummary()+"\n";
  server.sendHeader("Content-Disposition","attachment; filename=distauto-current.csv");
  server.send(200,"text/csv; charset=utf-8",out);
}
void applyRecipePhase(Phase p){
  int i=p==PH_SPIT?0:p==PH_DRIP?1:2;phaseRate=recipeRates[i];
  if(phaseRate>0)applyTiming();else {autoPeriod=10000;autoOpen=0;}
}
int nextStageProfile(){return mode==M_AUTO&&headsRecipe?1:manualProfile<2?manualProfile+1:-1;}
bool activateManualProfile(int i,bool newRun,bool respond=true);
bool advanceStage(bool respond){
  int next=nextStageProfile();
  if(next<0){endRun("completed");endToIdle();return true;}
  return activateManualProfile(next,false,respond);
}
void showStageDecisionStatus(){
  setStatus("Етап завершено: оберіть наступну дію","STAGE WAIT");
  snprintf(infoText,sizeof(infoText),"Клапан закритий. %.1f мл за етап",deliveredNow());
}
void awaitStageDecision(){
  valveClose();stageAwaiting=true;manualStop=true;stageDecisionId++;
  if(mode==M_AUTO&&headsRecipe)totalHeads=run.volumes[0]+run.volumes[1]+run.volumes[2];
  showStageDecisionStatus();logPhaseChange();
  if((mode==M_AUTO&&headsRecipe?recipeAutomatic:manualProfiles[manualProfile].automatic)&&!advanceStage(false)){
    setStatus("Перехід зупинено: перевірте наступний етап","STAGE WAIT");
  }
}
void recipeLoop(unsigned long now){
  int i=phase==PH_SPIT?0:phase==PH_DRIP?1:2;
  const char* titles[3]={"2-3-15: плевок","2-3-15: покрапельно","2-3-15: тонка струйка"};
  setStatus(recipeExtraHeads?"2-3-15: додатковий відбір голів":titles[i],phaseCode(phase));
  if(!recipeExtraHeads&&deliveredNow()>=recipeTargets[i]){
    valveClose();
    if(i==2)awaitStageDecision();else {totalHeads+=phaseDelivered;startPhase(i==0?PH_DRIP:PH_STREAM);}
    return;
  }
  if(phaseRate==0){valveClose();cycleStart=now;}else runPulse(autoPeriod,autoOpen);
  snprintf(infoText,sizeof(infoText),"Етап %d/3: %.1f / %.1f мл; %.0f мл/год",i+1,deliveredNow(),recipeTargets[i],phaseRate);
}
void handleRecipePlan(){
  if(mode!=M_IDLE){server.send(409,"text/plain","IDLE only");return;}
  float pct=argF("pct"),spit=argF("spit"),drip=argF("drip"),stream=argF("stream");
  if(!isfinite(pct)||pct<3||pct>15||!validRate(spit)||!validRate(drip)||!validRate(stream)){
    plan.valid=false;server.send(400,"text/plain; charset=utf-8","Частка струйки: 3–15%; швидкості — в межах каліброваної витрати");return;
  }
  String err=makePlan(argF("vol"),argF("abv"),argF("h"),argF("d"),5+pct,0,argF("strengthPct"),10,.2f);
  if(err.length()){server.send(400,"text/plain; charset=utf-8",err);return;}
  recipePct=pct;recipeRates[0]=spit;recipeRates[1]=drip;recipeRates[2]=stream;
  recipeTargets[0]=plan.aaMl*2/plan.strengthPct;recipeTargets[1]=plan.aaMl*3/plan.strengthPct;recipeTargets[2]=plan.aaMl*pct/plan.strengthPct;
  headsRecipe=true;
  recipeAutomatic=server.arg("automatic")=="1";prefs.putULong("recipeAuto",recipeAutomatic?1:0);
  String out=String("{\"revision\":")+String(plan.revision)+",\"aaMl\":"+num(plan.aaMl)+",\"totalPct\":"+num(5+pct)+
    ",\"targetsMl\":["+num(recipeTargets[0])+","+num(recipeTargets[1])+","+num(recipeTargets[2])+"],\"rates\":["+
    num(spit)+","+num(drip)+","+num(stream)+"],\"totalMl\":"+num(plan.headsMl)+"}";
  server.send(200,"application/json; charset=utf-8",out);
}
void handleRecipeStart(){
  if(!readyToStart())return;
  if(!headsRecipe||!plan.valid||server.arg("revision")!=String(plan.revision)){
    server.send(409,"text/plain; charset=utf-8","Повторіть розрахунок 2-3-15");return;
  }
  for(int i=0;i<3;i++)if(!validRate(recipeRates[i])){server.send(400,"text/plain","Invalid rate");return;}
  startAuto();server.send(200,"text/plain","OK");
}
void handleRecipeRate(){
  float index=argF("stage"),rate=argF("rate");
  if(mode!=M_AUTO||!headsRecipe||phase==PH_DONE||phase==PH_ABORT||!isfinite(index)||index<0||index>2||floorf(index)!=index||!validRate(rate,true)){
    server.send(400,"text/plain; charset=utf-8","Невірний етап або швидкість");return;
  }
  valveClose();recipeRates[(int)index]=rate;
  if((phase==PH_SPIT&&(int)index==0)||(phase==PH_DRIP&&(int)index==1)||(phase==PH_STREAM&&(int)index==2)){
    applyRecipePhase(phase);cycleStart=millis();
  }
  logPhaseChange();server.send(200,"text/plain","OK");
}
void saveManualProfile(int i){
  char key[12];snprintf(key,sizeof(key),"m%dset",i);prefs.putFloat(key,manualProfiles[i].set);
  snprintf(key,sizeof(key),"m%dhyst",i);prefs.putFloat(key,manualProfiles[i].hyst);
  snprintf(key,sizeof(key),"m%drate",i);prefs.putFloat(key,manualProfiles[i].rate);
  snprintf(key,sizeof(key),"m%dauto",i);prefs.putULong(key,manualProfiles[i].automatic?1:0);
  snprintf(key,sizeof(key),"m%dtarget",i);prefs.putFloat(key,manualProfiles[i].target);
}
void handleManualConfig(){
  float index=argF("profile"),set=argF("set"),hyst=argF("hyst"),rate=argF("rate");
  if(index==0)hyst=0;
  float target=server.arg("target").length()?argF("target"):0;
  if(!isfinite(index)||index<0||index>2||floorf(index)!=index||
      (mode!=M_IDLE&&mode!=M_MANUAL&&!(mode==M_AUTO&&headsRecipe&&index>0))||
      !isfinite(set)||set<0||set>=tAlarm||!isfinite(hyst)||hyst<0||hyst>20||!validRate(rate,true)||!isfinite(target)||target<0||target>200000){
    server.send(400,"text/plain; charset=utf-8","Перевірте профіль, температуру та витрату клапана");return;
  }
  int i=(int)index;
  if(mode==M_MANUAL&&manualProfile==i)valveClose();
  bool changedTarget=target!=manualProfiles[i].target;
  manualProfiles[i]={set,hyst,rate,server.arg("automatic")=="1",target};saveManualProfile(i);
  if(mode==M_MANUAL&&manualProfile==i&&changedTarget)manualExtra=false;
  if(mode==M_MANUAL&&manualProfile==i){tSet=set;tHyst=hyst;phaseRate=rate;rateFactor=1;if(rate>0)applyTiming();cycleStart=millis();logPhaseChange();}
  server.send(200,"application/json; charset=utf-8",profilesJson());
}
bool activateManualProfile(int i,bool newRun,bool respond){
  ManualProfile p=manualProfiles[i];
  if(!validRate(p.rate)||!isfinite(p.set)||p.set>=tAlarm||!isfinite(temperature)||millis()-lastGoodRead>=SENSOR_FAIL_MS||temperature>=tAlarm){if(respond)server.send(400,"text/plain; charset=utf-8","Перевірте датчик, швидкість і температурну уставку профілю");return false;}
  valveClose();manualProfile=i;
  if(newRun){headsRecipe=false;beginRun("Ручний");}
  stageAwaiting=false;
  manualExtra=false;
  mode=M_MANUAL;manualStop=false;coolingBlock=false;idleValveClosed=true;
  tSet=p.set;tHyst=p.hyst;startPhase(manualPhases[i]);phaseRate=p.rate;applyTiming();
  setStatus(String("РУЧНИЙ: ")+manualNames[i],"MANUAL");return true;
}
void startManualProfile(int i){
  if(!readyToStart())return;
  if(activateManualProfile(i,true))server.send(200,"text/plain","OK");
}
void handleStageEnd(){
  if(!run.active||stageAwaiting||!(mode==M_MANUAL||(mode==M_AUTO&&headsRecipe&&recipeExtraHeads))){server.send(409,"text/plain","No manual stage to finish");return;}
  awaitStageDecision();server.send(200,"text/plain","OK");
}
void handleStageChoice(){
  if(!run.active||!stageAwaiting||server.arg("decision")!=String(stageDecisionId)){
    server.send(409,"text/plain; charset=utf-8","Вибір етапу застарів. Оновіть стан");return;
  }
  String choice=server.arg("choice");
  if(choice=="continue"){
    if(!isfinite(temperature)||millis()-lastGoodRead>=SENSOR_FAIL_MS||temperature>=tAlarm){server.send(400,"text/plain","Check sensor/temperature");return;}
    stageAwaiting=false;manualStop=false;cycleStart=millis();
    if(mode==M_AUTO&&headsRecipe)recipeExtraHeads=true;
    if(mode==M_MANUAL)manualExtra=true;
    if(mode==M_MANUAL)setStatus(String("РУЧНИЙ: ")+manualNames[manualProfile],"MANUAL");
    else setStatus("2-3-15: додатковий відбір голів","EXTRA HEADS");
    logPhaseChange();server.send(200,"text/plain","OK");return;
  }
  if(choice!="next"){server.send(400,"text/plain","Invalid choice");return;}
  if(advanceStage(true))server.send(200,"text/plain","OK");
}
void handleManualStart(){
  float index=argF("profile");
  if(!isfinite(index)||index<0||index>2||floorf(index)!=index){server.send(400,"text/plain","Invalid profile");return;}
  startManualProfile((int)index);
}
bool simpleKind(const String& kind){return kind=="startstop"||kind=="ss";}
bool validSimple(const SimpleSettings& p,bool isSS){
  return isfinite(p.close)&&p.close>=0&&p.close<tAlarm&&validRate(p.rate,true)&&
    (isSS||(isfinite(p.open)&&p.open>=0&&p.open<p.close&&isfinite(p.decrement)&&p.decrement>=1&&p.decrement<=50));
}
void saveSimpleSettings(const char* prefix,const SimpleSettings& p){
  String key=String(prefix)+"c";prefs.putFloat(key.c_str(),p.close);
  key=String(prefix)+"o";prefs.putFloat(key.c_str(),p.open);
  key=String(prefix)+"r";prefs.putFloat(key.c_str(),p.rate);
  key=String(prefix)+"d";prefs.putFloat(key.c_str(),p.decrement);
  key=String(prefix)+"e";prefs.putULong(key.c_str(),p.reduce?1:0);
}
void handleSimpleConfig(){
  String kind=server.arg("kind");bool isSS=kind=="ss";
  SimpleSettings p={argF("close"),isSS?0:argF("open"),argF("rate"),isSS?10:argF("decrement"),!isSS&&server.arg("reduce")=="1"};
  if(!simpleKind(kind)||!validSimple(p,isSS)||(mode!=M_IDLE&&mode!=(isSS?M_SS:M_STARTSTOP))){server.send(400,"text/plain; charset=utf-8","Перевірте температури та швидкість; відкриття має бути нижче закриття і аварійного порога");return;}
  if(mode!=M_IDLE)valveClose();
  (isSS?ss:startStop)=p;saveSimpleSettings(isSS?"ss":"st",p);
  if(mode!=M_IDLE){phaseRate=p.rate;if(p.rate>0)applyTiming();cycleStart=millis();logPhaseChange();}
  server.send(200,"application/json",simpleJson(p));
}
void handleSimpleStart(){
  if(!readyToStart())return;
  String kind=server.arg("kind");bool isSS=kind=="ss";SimpleSettings p=isSS?ss:startStop;
  if(!simpleKind(kind)||!validSimple(p,isSS)||!validRate(p.rate)|| (isSS&&temperature>=p.close)){
    server.send(400,"text/plain; charset=utf-8","Перевірте швидкість, датчик та температури режиму");return;
  }
  valveClose();headsRecipe=false;beginRun(isSS?"SS":"Старт-стоп");
  mode=isSS?M_SS:M_STARTSTOP;manualStop=false;idleValveClosed=true;
  startPhase(PH_BODY);phaseRate=p.rate;rateFactor=1;applyTiming();
  coolingBlock=!isSS&&temperature>p.open;
  setStatus(isSS?"SS: відбір":"СТАРТ-СТОП: відбір",isSS?"SS":"START STOP");
  server.send(200,"text/plain","OK");
}
void handleSimpleEnd(){
  if(mode!=M_STARTSTOP&&mode!=M_SS){server.send(409,"text/plain","Simple mode only");return;}
  endToIdle();server.send(200,"text/plain","OK");
}
void simpleLoop(unsigned long now){
  if(mode==M_SS&&temperature>=ss.close&&!ssTemperaturePaused){
    valveClose();ssTemperaturePaused=true;manualStop=true;logPhaseChange();
  }
  if(mode==M_STARTSTOP){
    if(!coolingBlock&&temperature>=startStop.close){
      valveClose();coolingBlock=true;
      if(startStop.reduce&&!manualStop&&phaseRate>0){rateFactor=fmaxf(MIN_RATE_FACTOR,rateFactor*(1-startStop.decrement/100));applyTiming();}
      logPhaseChange();
    }else if(coolingBlock&&temperature<=startStop.open){coolingBlock=false;cycleStart=now;logPhaseChange();}
  }
  if(manualStop||coolingBlock){valveClose();cycleStart=now;}
  else if(phaseRate>0)runPulse(autoPeriod,autoOpen);else valveClose();
  setStatus(ssTemperaturePaused?"SS: температурна пауза, продовжіть вручну":manualStop?"ПАУЗА вручну":coolingBlock?"СТАРТ-СТОП: очікування відкриття":mode==M_SS?"SS: відбір":"СТАРТ-СТОП: відбір",mode==M_SS?"SS":"START STOP");
  snprintf(infoText,sizeof(infoText),"%.0f мл/год (%.0f%%); %.1f мл",currentRate(),rateFactor*100,deliveredNow());
}
void handlePresets(){
  String out="[";
  bool first=true;
  for(int i=0;i<PRESET_MAX;i++){
    char nk[8],vk[8],kk[8];snprintf(nk,sizeof(nk),"p%dn",i);snprintf(vk,sizeof(vk),"p%dv",i);snprintf(kk,sizeof(kk),"p%dk",i);
    String name=prefs.getString(nk,"");if(!name.length())continue;
    if(!first)out+=',';first=false;
    out+=String("{\"slot\":")+String((unsigned long)i)+",\"name\":"+quote(name)+",\"kind\":"+quote(prefs.getString(kk,"auto"))+",\"config\":"+quote(prefs.getString(vk,""))+"}";
  }
  server.send(200,"application/json; charset=utf-8",out+"]");
}
void handlePresetSave(){
  if(mode!=M_IDLE){server.send(409,"text/plain","IDLE only");return;}
  String name=server.arg("name"),kind=server.arg("kind"),config=server.arg("config");
  if(!name.length()||name.length()>64||!config.length()||config.length()>2400||!(kind=="auto"||kind=="manual"||kind=="recipe"||simpleKind(kind))){
    server.send(400,"text/plain; charset=utf-8","Назва: 1–64 байти; оберіть тип і параметри режиму");return;
  }
  int slot=-1;
  for(int i=0;i<PRESET_MAX;i++){char key[8];snprintf(key,sizeof(key),"p%dn",i);String existing=prefs.getString(key,"");if(existing==name){slot=i;break;}if(slot<0&&!existing.length())slot=i;}
  if(slot<0){server.send(400,"text/plain; charset=utf-8","Збережено 8 режимів. Видаліть непотрібний");return;}
  char key[8];snprintf(key,sizeof(key),"p%dn",slot);prefs.putString(key,name);
  snprintf(key,sizeof(key),"p%dv",slot);prefs.putString(key,config);
  snprintf(key,sizeof(key),"p%dk",slot);prefs.putString(key,kind);handlePresets();
}
void handlePresetDelete(){
  float slot=argF("slot");
  if(mode!=M_IDLE||!isfinite(slot)||slot<0||slot>=PRESET_MAX||floorf(slot)!=slot){server.send(400,"text/plain","Invalid slot");return;}
  char key[8];for(char suffix:{'n','v','k'}){snprintf(key,sizeof(key),"p%d%c",(int)slot,suffix);prefs.remove(key);}
  handlePresets();
}
void handleFeatures(){
  String out=String("{\"simulator\":")+b(simulatorBuild())+",\"maxRate\":"+num(controlMaximumRate())+",\"minRate\":"+num(flowMlS*.2f)+
    ",\"manualProfile\":"+String((unsigned long)manualProfile)+",\"manualProfiles\":"+profilesJson()+",\"recipe\":"+b(headsRecipe)+
    ",\"recipeRates\":["+num(recipeRates[0])+","+num(recipeRates[1])+","+num(recipeRates[2])+"],\"recipeAutomatic\":"+b(recipeAutomatic)+",\"startStop\":"+simpleJson(startStop)+",\"ss\":"+simpleJson(ss)+",\"historyReady\":"+b(historyReady)+
    ",\"historyLimit\":"+String(historyBudget());
#ifndef DISTAUTO_SIMULATOR
  out+=String(",\"historyUsed\":")+String((unsigned long)(historyReady?LittleFS.usedBytes():0));
#else
  out+=",\"historyUsed\":0";
#endif
  server.send(200,"application/json; charset=utf-8",out+"}");
}
void handleSuggestedRates(){
  float h=argF("h"),d=argF("d");
  if(!isfinite(h)||h<.3f||h>4||!isfinite(d)||d<.5f||d>6){server.send(400,"text/plain","Invalid geometry");return;}
  server.send(200,"application/json",String("{\"headsRate\":")+num(estimatedHeadsRate(h,d))+",\"bodyRate\":"+num(estimatedBodyRate(h,d))+"}");
}
unsigned long historyId(){
  String text=server.arg("id");if(!text.length()||text.length()>9)return 0;
  for(size_t i=0;i<text.length();i++)if(text.c_str()[i]<'0'||text.c_str()[i]>'9')return 0;
  return strtoul(text.c_str(),nullptr,10);
}
void handleHistoryInit(){
#ifndef DISTAUTO_SIMULATOR
  if(mode!=M_IDLE||server.arg("confirm")!="FORMAT"){server.send(400,"text/plain","Confirmation required");return;}
  if(historyReady){server.send(409,"text/plain","Storage already initialized");return;}
  historyReady=LittleFS.format()&&LittleFS.begin(false);
  if(historyReady)LittleFS.mkdir("/history");
  server.send(historyReady?200:500,"text/plain; charset=utf-8",historyReady?"Сховище готове":"Немає доступного розділу LittleFS. Перевірте Partition Scheme");
#else
  server.send(409,"text/plain","History is disabled in the simulator");
#endif
}
void handleHistoryList(){
#ifndef DISTAUTO_SIMULATOR
  std::vector<unsigned long> ids;
  if(historyReady){
    File dir=LittleFS.open("/history");File file=dir.openNextFile();
    while(file){
      unsigned long id=0;char tail=0;
      const char* name=strrchr(file.name(),'/');name=name?name+1:file.name();
      if(sscanf(name,"run-%lu.csv%c",&id,&tail)==1)ids.push_back(id);
      file.close();file=dir.openNextFile();
    }
  }
  std::sort(ids.rbegin(),ids.rend());String out="[";bool first=true;
  unsigned long before=historyId();int shown=0;
  for(unsigned long id:ids){
    if(before&&id>=before)continue;if(shown++>=50)break;
    String path=String("/history/run-")+String(id)+".csv";File file=LittleFS.open(path.c_str());
    if(!file)continue;
    unsigned long size=file.size();file.readStringUntil('\n');String meta=file.readStringUntil('\n');file.close();
    if(!meta.startsWith("#meta,"))continue;
    if(!first)out+=',';first=false;
    out+=String("{\"id\":")+String(id)+",\"bytes\":"+String(size)+",\"meta\":"+meta.substring(6)+"}";
  }
  server.send(200,"application/json; charset=utf-8",out+"]");
#else
  server.send(200,"application/json","[]");
#endif
}
void handleHistoryDownload(){
#ifndef DISTAUTO_SIMULATOR
  if(mode!=M_IDLE){server.send(409,"text/plain; charset=utf-8","Зупиніть процес перед завантаженням історії");return;}
  unsigned long id=historyId();
  if(!id||!historyReady){server.send(404,"text/plain","Not found");return;}
  if(run.active&&id==run.id)flushHistory();
  String path=String("/history/run-")+String(id)+".csv";File file=LittleFS.open(path.c_str());
  if(!file){server.send(404,"text/plain","Not found");return;}
  server.sendHeader("Content-Disposition",String("attachment; filename=run-")+String(id)+".csv");
  server.streamFile(file,"text/csv; charset=utf-8");file.close();
#else
  server.send(404,"text/plain","No historical files in the simulator");
#endif
}
void handleHistoryDelete(){
#ifndef DISTAUTO_SIMULATOR
  unsigned long id=historyId();
  if(mode!=M_IDLE||!id||!historyReady){server.send(409,"text/plain","IDLE only");return;}
  String path=String("/history/run-")+String(id)+".csv";
  server.send(LittleFS.remove(path.c_str())?200:404,"text/plain","OK");
#else
  server.send(409,"text/plain","History disabled");
#endif
}
void initRunFeatures(){
  for(int i=0;i<2;i++){
    const char* prefix=i?"ss":"st";SimpleSettings &p=i?ss:startStop;
    String key=String(prefix)+"c";p.close=prefs.getFloat(key.c_str(),p.close);
    key=String(prefix)+"o";p.open=prefs.getFloat(key.c_str(),p.open);
    key=String(prefix)+"r";p.rate=fminf(controlMaximumRate(),prefs.getFloat(key.c_str(),p.rate));
    key=String(prefix)+"d";p.decrement=prefs.getFloat(key.c_str(),p.decrement);
    key=String(prefix)+"e";p.reduce=prefs.getULong(key.c_str(),0)!=0;
  }
  for(int i=0;i<3;i++){
    char key[12];snprintf(key,sizeof(key),"m%dset",i);manualProfiles[i].set=prefs.getFloat(key,tSet);
    snprintf(key,sizeof(key),"m%dhyst",i);manualProfiles[i].hyst=prefs.getFloat(key,tHyst);
    if(i==0)manualProfiles[i].hyst=0;
    snprintf(key,sizeof(key),"m%drate",i);manualProfiles[i].rate=fminf(controlMaximumRate(),prefs.getFloat(key,manualProfiles[i].rate));
    snprintf(key,sizeof(key),"m%dauto",i);manualProfiles[i].automatic=prefs.getULong(key,0)!=0;
    snprintf(key,sizeof(key),"m%dtarget",i);manualProfiles[i].target=prefs.getFloat(key,manualProfiles[i].target);
  }
  recipeRates[0]=controlMaximumRate();
  recipeAutomatic=prefs.getULong("recipeAuto",0)!=0;
#ifndef DISTAUTO_SIMULATOR
  historyReady=LittleFS.begin(false);if(historyReady)LittleFS.mkdir("/history");
#endif
  server.on("/features",HTTP_GET,handleFeatures);
  server.on("/rates",HTTP_GET,handleSuggestedRates);
  server.on("/recipe/plan",HTTP_POST,handleRecipePlan);server.on("/recipe/start",HTTP_POST,handleRecipeStart);server.on("/recipe/rate",HTTP_POST,handleRecipeRate);
  server.on("/manual/config",HTTP_POST,handleManualConfig);server.on("/manual/start",HTTP_POST,handleManualStart);
  server.on("/stage/end",HTTP_POST,handleStageEnd);server.on("/stage/choice",HTTP_POST,handleStageChoice);
  server.on("/simple/config",HTTP_POST,handleSimpleConfig);server.on("/simple/start",HTTP_POST,handleSimpleStart);server.on("/simple/end",HTTP_POST,handleSimpleEnd);
  server.on("/presets",HTTP_GET,handlePresets);server.on("/preset/save",HTTP_POST,handlePresetSave);server.on("/preset/delete",HTTP_POST,handlePresetDelete);
  server.on("/report",HTTP_GET,handleReport);server.on("/report.csv",HTTP_GET,handleReportCsv);
  server.on("/history",HTTP_GET,handleHistoryList);server.on("/history/init",HTTP_POST,handleHistoryInit);
  server.on("/history/download",HTTP_GET,handleHistoryDownload);server.on("/history/delete",HTTP_POST,handleHistoryDelete);
}
