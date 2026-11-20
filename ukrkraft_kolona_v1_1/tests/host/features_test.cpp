#include "stubs.h"
#include <cassert>
#include <iostream>
#include "../../ukrkraft_kolona_v1_1.ino"
void tick(unsigned long duration){while(duration){unsigned long step=std::min(duration,50UL);fakeNow+=step;duration-=step;loop();}}
void request(const char* path,std::map<std::string,String> args={},int method=HTTP_POST){server.args=args;server.dispatch(path,method);}
void recipe(float pct=15){
  request("/recipe/plan",{{"vol","10"},{"abv","40"},{"h","1"},{"d","4"},{"strengthPct","96"},{"pct",String(pct)},{"spit","4000"},{"drip","400"},{"stream","800"}});assert(server.code==200);
}
void choose(const char* choice){request("/stage/choice",{{"choice",choice},{"decision",String(stageDecisionId)}});}
void finish(){request("/stage/end");assert(server.code==200&&stageAwaiting&&!valveIsOpen);}
int main(){
 setup();tick(1000);request("/features",{},HTTP_GET);std::cout<<server.response.c_str()<<"\n";
 assert(controlMaximumRate()==4000);assert(!validRate(4001)&&validRate(4000)&&validRate(0,true));
 recipe(3);assert(fabsf(plan.headsMl-333.3333f)<.01f);assert(fabsf(recipeTargets[0]-83.3333f)<.01f);
 server.args["spit"]="4001";handleRecipePlan();assert(server.code==400&&!plan.valid);
 recipe(3);request("/recipe/start",{{"revision",String(plan.revision)},{"date","2026-11-19T15:00:00.000Z"},{"label","Тест \"2-3-15\""}});
 assert(server.code==200&&run.active&&run.kind==String("recipe"));unsigned long id=run.id;
 startPhase(PH_SPIT);assert(autoOpen<autoPeriod);tick(5000);
 request("/recipe/rate",{{"stage","0"},{"rate","0"}});float before=liveRunMl();tick(3000);assert(!valveIsOpen&&fabsf(liveRunMl()-before)<.01f);
 request("/recipe/rate",{{"stage","0"},{"rate","4000"}});tick(90000);assert(phase==PH_DRIP);
 request("/stop",{{"on","1"}},HTTP_GET);tick(30000);assert(!valveIsOpen);
 request("/stop",{{"on","0"}},HTTP_GET);tick(20UL*60000UL);assert(phase==PH_STREAM);
 tick(10UL*60000UL);assert(stageAwaiting&&phase==PH_STREAM&&run.active&&!valveIsOpen);
 before=liveRunMl();tick(21UL*60000UL);assert(stageAwaiting&&run.active&&fabsf(liveRunMl()-before)<.01f);
 request("/stop",{{"on","0"}},HTTP_GET);assert(server.code==409&&!valveIsOpen);
 request("/manual/start",{{"profile","1"}});assert(server.code==409&&stageAwaiting);
 request("/stage/choice",{{"choice","next"},{"decision",String(stageDecisionId-1)}});assert(server.code==409&&stageAwaiting);
 choose("continue");assert(!stageAwaiting&&recipeExtraHeads);tick(30000);assert(liveRunMl()>before);
 finish();request("/manual/config",{{"profile","1"},{"set","78.3"},{"hyst","0.3"},{"rate","0"}});assert(server.code==200);
 choose("next");assert(server.code==400&&stageAwaiting&&!valveIsOpen);
 request("/manual/config",{{"profile","1"},{"set","78.3"},{"hyst","0.3"},{"rate","2000"}});assert(server.code==200);
 choose("next");assert(server.code==200&&mode==M_MANUAL&&phase==PH_BODY&&headsRecipe&&run.id==id);tick(10000);
 request("/manual/config",{{"profile","1"},{"set","78.3"},{"hyst","0.3"},{"rate","0"}});tick(1000);assert(!valveIsOpen&&!stageAwaiting&&run.active);
 request("/manual/config",{{"profile","1"},{"set","78.3"},{"hyst","0.3"},{"rate","2000"}});tick(10000);assert(currentRate()==2000);
 request("/manual/start",{{"profile","2"}});assert(server.code==409&&phase==PH_BODY);
 finish();float segment=phaseDelivered;choose("continue");assert(!stageAwaiting&&phaseDelivered==segment);tick(10000);
 finish();choose("next");assert(mode==M_MANUAL&&phase==PH_TAILS&&run.id==id);tick(10000);
 finish();choose("next");assert(mode==M_IDLE&&!run.active&&!valveIsOpen&&run.result==String("completed"));
 assert(run.volumes[0]>0&&run.volumes[1]>=125&&run.volumes[2]>=125&&run.volumes[4]>0&&run.volumes[5]>0);
 request("/report",{},HTTP_GET);std::cout<<server.response.c_str()<<"\n";
 request("/report.csv",{},HTTP_GET);assert(server.response.s.find("#summary,")!=std::string::npos);
 request("/manual/config",{{"profile","0"},{"set","78.3"},{"hyst","0.3"},{"rate","5000"}});assert(server.code==400);
 request("/manual/start",{{"profile","0"},{"date","2026-11-19T17:00:00.000Z"}});assert(mode==M_MANUAL&&phase==PH_HEADS&&!headsRecipe&&run.id!=id);
 tick(10000);id=run.id;finish();choose("next");assert(phase==PH_BODY&&run.id==id);tick(10000);
 finish();choose("next");assert(phase==PH_TAILS&&run.id==id);tick(10000);
 finish();choose("continue");assert(phase==PH_TAILS&&!stageAwaiting);finish();choose("next");assert(!run.active&&mode==M_IDLE);
 request("/preset/save",{{"name","Мій режим"},{"kind","manual"},{"config","m0rate=300&vol=10"}});assert(server.code==200);std::cout<<server.response.c_str()<<"\n";
 request("/history",{},HTTP_GET);assert(server.response==String("[]"));request("/history/init",{{"confirm","FORMAT"}});assert(server.code==409);
 request("/manual/start",{{"profile","0"}});finish();fakeSensorConnected=false;tick(7000);assert(!run.active&&!stageAwaiting&&mode==M_IDLE&&!valveIsOpen);
 std::cerr<<"4000 limit, stage decisions, stale-choice rejection, recipe → body → tails, continued stage volume, one dated report and sensor failure: OK\n";
}
