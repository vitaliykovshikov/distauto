#include "stubs.h"
#include <cassert>
#include <iostream>
#include "../../ukrkraft_kolona_v1_1.ino"
void tick(unsigned long ms){while(ms){auto n=std::min(ms,10UL);fakeNow+=n;ms-=n;loop();}}
void call(const char* path,std::map<std::string,String> args={},int method=HTTP_POST){server.args=args;server.dispatch(path,method);}
void temp(float value){fakeTemperature=value;tick(2000);}
void config(bool reduce){call("/simple/config",{{"kind","startstop"},{"close","78.5"},{"open","78.2"},{"rate","2000"},{"decrement","10"},{"reduce",reduce?"1":"0"}});assert(server.code==200);}
int main(){
 setup();tick(2000);assert(manualProfiles[0].hyst==0);
 config(true);temp(78.3);call("/simple/start",{{"kind","startstop"},{"date","2026-11-20T08:00:00.000Z"}});
 assert(server.code==200&&mode==M_STARTSTOP&&run.kind==String("startstop"));auto id=run.id;
 tick(30000);assert(coolingBlock&&!valveIsOpen&&liveRunMl()==0); // Start inside band: wait for lower threshold.
 temp(78.2);tick(20000);assert(!coolingBlock&&liveRunMl()>0&&rateFactor==1);
 temp(78.5);assert(coolingBlock&&!valveIsOpen&&fabsf(rateFactor-.9f)<.0001f);
 float ml=liveRunMl();tick(30000);assert(liveRunMl()==ml&&fabsf(rateFactor-.9f)<.0001f);
 temp(78.2);tick(10000);temp(78.5);assert(fabsf(rateFactor-.81f)<.0001f);
 for(int i=0;i<12;i++){temp(78.2);temp(78.5);}assert(rateFactor==MIN_RATE_FACTOR);
 call("/simple/end");assert(mode==M_IDLE&&!run.active&&!valveIsOpen&&run.id==id);
 call("/report",{},HTTP_GET);std::cout<<server.response.c_str()<<"\n";
 config(false);temp(78);call("/simple/start",{{"kind","startstop"}});tick(10000);
 temp(78.5);assert(coolingBlock&&rateFactor==1);temp(78.2);assert(!coolingBlock&&rateFactor==1);
 call("/stop",{{"on","1"}},HTTP_GET);ml=liveRunMl();tick(10000);assert(!valveIsOpen&&liveRunMl()==ml);
 call("/stop",{{"on","0"}},HTTP_GET);tick(10000);assert(liveRunMl()>ml);
 call("/simple/config",{{"kind","startstop"},{"close","78.5"},{"open","78.2"},{"rate","0"},{"decrement","10"},{"reduce","0"}});
 ml=liveRunMl();tick(10000);assert(server.code==200&&!valveIsOpen&&liveRunMl()==ml);
 fakeSensorConnected=false;tick(7000);assert(!run.active&&mode==M_IDLE&&!valveIsOpen);
 fakeSensorConnected=true;temp(78);config(false);
 server.args["open"]="78.5";handleSimpleConfig();assert(server.code==400);
 call("/simple/config",{{"kind","ss"},{"close","82"},{"rate","4000"}});assert(server.code==400);
 call("/preset/save",{{"name","Старт-стоп"},{"kind","startstop"},{"config","simpleClose=78.5&simpleOpen=78.2"}});assert(server.code==200);
 // SS latches a temperature pause; cooling alone must never reopen it.
 call("/simple/config",{{"kind","ss"},{"close","79"},{"rate","3000"}});assert(server.code==200);
 call("/simple/start",{{"kind","ss"},{"date","2026-11-20T09:00:00.000Z"}});tick(10000);id=run.id;assert(mode==M_SS&&run.active&&liveRunMl()>0);
 temp(79);assert(manualStop&&ssTemperaturePaused&&!valveIsOpen);ml=liveRunMl();
 call("/stop",{{"on","0"}},HTTP_GET);assert(server.code==400&&manualStop);
 temp(78);tick(20000);assert(!valveIsOpen&&liveRunMl()==ml&&run.active);
 call("/stop",{{"on","0"}},HTTP_GET);tick(10000);assert(server.code==200&&!ssTemperaturePaused&&liveRunMl()>ml&&run.id==id);
 temp(79);assert(manualStop&&!valveIsOpen);call("/simple/end");assert(!run.active&&mode==M_IDLE);
 call("/simple/start",{{"kind","ss"}});assert(server.code==400);temp(78);
 call("/simple/start",{{"kind","ss"}});temp(82);assert(!run.active&&mode==M_IDLE&&!valveIsOpen);temp(78);
 // Manual volume stop, continuation beyond target, then an automatic chain in one run.
 for(int i=0;i<3;i++){call("/manual/config",{{"profile",String((unsigned long)i)},{"set","78.5"},{"hyst","0.1"},{"rate","4000"},{"target","5"},{"automatic",i?"1":"0"}});assert(server.code==200);}
 call("/manual/start",{{"profile","0"}});id=run.id;tick(10000);
 assert(stageAwaiting&&phase==PH_HEADS&&!valveIsOpen&&phaseDelivered>=5&&phaseDelivered<5.2f);
 call("/stage/choice",{{"choice","continue"},{"decision",String(stageDecisionId)}});ml=phaseDelivered;tick(10000);
 assert(!stageAwaiting&&phase==PH_HEADS&&liveRunMl()>ml&&manualExtra);
 call("/stage/end");call("/stage/choice",{{"choice","next"},{"decision",String(stageDecisionId)}});
 tick(20000);assert(mode==M_IDLE&&!run.active&&run.id==id&&run.result==String("completed")&&run.volumes[4]>=5&&run.volumes[5]>=5);
 // Invalid next profile leaves the valve closed and asks for a decision.
 manualProfiles[0].automatic=true;manualProfiles[1].rate=0;
 call("/manual/start",{{"profile","0"}});tick(10000);assert(stageAwaiting&&phase==PH_HEADS&&!valveIsOpen);
 call("/manual",{{"on","0"}},HTTP_GET);manualProfiles[1].rate=4000;
 call("/manual/start",{{"profile","0"}});tick(1100);assert(phase==PH_BODY&&!stageAwaiting);
 tick(20000);assert(!run.active&&mode==M_IDLE);
 call("/manual/config",{{"profile","0"},{"set","78.5"},{"rate","400"},{"target","-1"}});assert(server.code==400);
 call("/recipe/plan",{{"vol","1"},{"abv","10"},{"h","1"},{"d","4"},{"strengthPct","96"},{"pct","3"},{"spit","4000"},{"drip","400"},{"stream","800"},{"automatic","1"}});assert(server.code==200);
 call("/recipe/start",{{"revision",String(plan.revision)}});id=run.id;startPhase(PH_STREAM);tick(20000);
 assert(mode==M_IDLE&&!run.active&&run.id==id&&run.kind==String("recipe")&&run.volumes[2]>=recipeTargets[2]&&run.volumes[4]>=5&&run.volumes[5]>=5);
 call("/features",{},HTTP_GET);std::cout<<server.response.c_str()<<"\n";
 std::cerr<<"Manual volume/automatic transitions and SS latched temperature pause; start-stop: thresholds, start in band, repeated decrement/floor, disabled decrement, pause, zero rate, sensor fault and validation: OK\n";
}
