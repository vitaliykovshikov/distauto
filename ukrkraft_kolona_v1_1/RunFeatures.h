#pragma once
#include <vector>
#include <algorithm>
#include <stdint.h>
#ifndef DISTAUTO_SIMULATOR
#include <LittleFS.h>
#endif

float argF(const char* name);
struct ManualProfile { float set, hyst, rate; };
ManualProfile manualProfiles[3]={{78.3f,.3f,400},{78.3f,.3f,4000},{78.3f,.3f,1000}};
int manualProfile=0;
float recipePct=15,recipeRates[3]={18000,400,800},recipeTargets[3]={0,0,0};
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
bool validRate(float rate,bool allowZero=false){
  return isfinite(flowMlS)&&flowMlS>0&&isfinite(rate)&&
    ((allowZero&&rate==0)||rate>=flowMlS*.2f)&&rate<=maximumRate();
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
      ",\"hyst\":"+num(manualProfiles[i].hyst)+",\"rate\":"+num(manualProfiles[i].rate)+"}";
  }
  return out+"]";
}
String configJson(){
  return String("{\"vol\":")+num(plan.vol)+",\"abv\":"+num(plan.abv)+",\"height\":"+num(plan.h)+",\"diameter\":"+num(plan.d)+
    ",\"aaMl\":"+num(plan.aaMl)+",\"headsPct\":"+num(plan.headsPct)+",\"tailsPct\":"+num(plan.tailsPct)+
    ",\"strengthPct\":"+num(plan.strengthPct)+",\"headsRate\":"+num(plan.headsRate)+",\"bodyRate\":"+num(plan.bodyRate)+
    ",\"decrementPct\":"+num(plan.decrementPct)+",\"reduceDt\":"+num(plan.reduceDt)+",\"flowMlS\":"+num(flowMlS,4)+
    ",\"alarm\":"+num(tAlarm)+",\"manualProfile\":"+String((unsigned long)manualProfile)+",\"manualProfiles\":"+profilesJson()+
    ",\"recipePct\":"+num(recipePct)+",\"recipeRates\":["+num(recipeRates[0])+","+num(recipeRates[1])+","+num(recipeRates[2])+
    "],\"recipeTargetsMl\":["+num(recipeTargets[0])+","+num(recipeTargets[1])+","+num(recipeTargets[2])+"]}";
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
  if(manualStop||coolingBlock||!run.active)return 0;
  if(mode==M_MANUAL)return manualProfiles[manualProfile].rate;
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
  run.exists=true;run.active=true;run.truncated=false;run.duration=0;run.lastSample=0;
  run.traceInterval=5000;run.lastTrace=0;run.count=0;run.ml=0;run.result="";
  for(float& ml:run.volumes)ml=0;
  run.start=millis();
  run.kind=strcmp(label,"Ручний")==0?"manual":headsRecipe?"recipe":"auto";
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
void recipeLoop(unsigned long now){
  int i=phase==PH_SPIT?0:phase==PH_DRIP?1:2;
  const char* titles[3]={"2-3-15: плевок","2-3-15: покрапельно","2-3-15: тонка струйка"};
  setStatus(titles[i],phaseCode(phase));
  if(deliveredNow()>=recipeTargets[i]){
    valveClose();totalHeads+=phaseDelivered;
    if(i==2)finishAuto();else startPhase(i==0?PH_DRIP:PH_STREAM);
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
}
void handleManualConfig(){
  float index=argF("profile"),set=argF("set"),hyst=argF("hyst"),rate=argF("rate");
  if((mode!=M_IDLE&&mode!=M_MANUAL)||!isfinite(index)||index<0||index>2||floorf(index)!=index||
      !isfinite(set)||set<0||set>=tAlarm||!isfinite(hyst)||hyst<0||hyst>20||!validRate(rate,true)){
    server.send(400,"text/plain; charset=utf-8","Перевірте профіль, температуру та витрату клапана");return;
  }
  int i=(int)index;
  if(mode==M_MANUAL&&manualProfile==i)valveClose();
  manualProfiles[i]={set,hyst,rate};saveManualProfile(i);
  if(mode==M_MANUAL&&manualProfile==i){tSet=set;tHyst=hyst;phaseRate=rate;rateFactor=1;if(rate>0)applyTiming();cycleStart=millis();logPhaseChange();}
  server.send(200,"application/json; charset=utf-8",profilesJson());
}
void startManualProfile(int i){
  if(mode!=M_MANUAL && !readyToStart())return;
  ManualProfile p=manualProfiles[i];
  if(!validRate(p.rate)||p.set>=tAlarm){server.send(400,"text/plain; charset=utf-8","Перевірте швидкість і температурну уставку профілю");return;}
  valveClose();manualProfile=i;
  if(mode==M_IDLE)beginRun("Ручний");
  mode=M_MANUAL;headsRecipe=false;manualStop=false;coolingBlock=false;idleValveClosed=true;
  tSet=p.set;tHyst=p.hyst;startPhase(manualPhases[i]);phaseRate=p.rate;applyTiming();
  setStatus(String("РУЧНИЙ: ")+manualNames[i],"MANUAL");server.send(200,"text/plain","OK");
}
void handleManualStart(){
  float index=argF("profile");
  if(!isfinite(index)||index<0||index>2||floorf(index)!=index){server.send(400,"text/plain","Invalid profile");return;}
  startManualProfile((int)index);
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
  if(!name.length()||name.length()>64||!config.length()||config.length()>1800||!(kind=="auto"||kind=="manual"||kind=="recipe")){
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
  String out=String("{\"simulator\":")+b(simulatorBuild())+",\"maxRate\":"+num(maximumRate())+",\"minRate\":"+num(flowMlS*.2f)+
    ",\"manualProfile\":"+String((unsigned long)manualProfile)+",\"manualProfiles\":"+profilesJson()+",\"recipe\":"+b(headsRecipe)+
    ",\"recipeRates\":["+num(recipeRates[0])+","+num(recipeRates[1])+","+num(recipeRates[2])+"],\"historyReady\":"+b(historyReady)+
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
  for(int i=0;i<3;i++){
    char key[12];snprintf(key,sizeof(key),"m%dset",i);manualProfiles[i].set=prefs.getFloat(key,tSet);
    snprintf(key,sizeof(key),"m%dhyst",i);manualProfiles[i].hyst=prefs.getFloat(key,tHyst);
    snprintf(key,sizeof(key),"m%drate",i);manualProfiles[i].rate=prefs.getFloat(key,manualProfiles[i].rate);
  }
  recipeRates[0]=maximumRate();
#ifndef DISTAUTO_SIMULATOR
  historyReady=LittleFS.begin(false);if(historyReady)LittleFS.mkdir("/history");
#endif
  server.on("/features",HTTP_GET,handleFeatures);
  server.on("/rates",HTTP_GET,handleSuggestedRates);
  server.on("/recipe/plan",HTTP_POST,handleRecipePlan);server.on("/recipe/start",HTTP_POST,handleRecipeStart);server.on("/recipe/rate",HTTP_POST,handleRecipeRate);
  server.on("/manual/config",HTTP_POST,handleManualConfig);server.on("/manual/start",HTTP_POST,handleManualStart);
  server.on("/presets",HTTP_GET,handlePresets);server.on("/preset/save",HTTP_POST,handlePresetSave);server.on("/preset/delete",HTTP_POST,handlePresetDelete);
  server.on("/report",HTTP_GET,handleReport);server.on("/report.csv",HTTP_GET,handleReportCsv);
  server.on("/history",HTTP_GET,handleHistoryList);server.on("/history/init",HTTP_POST,handleHistoryInit);
  server.on("/history/download",HTTP_GET,handleHistoryDownload);server.on("/history/delete",HTTP_POST,handleHistoryDelete);
}
