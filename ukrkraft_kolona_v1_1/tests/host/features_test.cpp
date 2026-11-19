#include "stubs.h"
#include <cassert>
#include <iostream>
#include "../../ukrkraft_kolona_v1_1.ino"
void tick(unsigned long duration){while(duration){unsigned long step=std::min(duration,50UL);fakeNow+=step;duration-=step;loop();}}
void request(const char* path,std::map<std::string,String> args={},int method=HTTP_POST){server.args=args;server.dispatch(path,method);}
void recipe(float pct=15){
  request("/recipe/plan",{{"vol","10"},{"abv","40"},{"h","1"},{"d","4"},{"strengthPct","96"},{"pct",String(pct)},{"spit","18000"},{"drip","400"},{"stream","800"}});
  assert(server.code==200);
}
int main(){
  setup();tick(1000);
  request("/features",{},HTTP_GET);assert(server.code==200);std::cout<<server.response.c_str()<<"\n";
  recipe();assert(fabsf(recipeTargets[0]-83.3333f)<.01f);assert(fabsf(recipeTargets[1]-125)<.01f);assert(fabsf(recipeTargets[2]-625)<.01f);
  assert(fabsf(plan.headsMl-833.3333f)<.01f);
  request("/auto/start",{{"revision",String(plan.revision)},{"headsRate","400"},{"bodyRate","4000"}});assert(server.code==409);
  recipe(5);assert(fabsf(plan.headsMl-416.6667f)<.01f);
  recipe(3);assert(fabsf(plan.headsMl-333.3333f)<.01f);
  server.args["pct"]="2";handleRecipePlan();assert(server.code==400 && !plan.valid);
  recipe();server.args["drip"]="999999";handleRecipePlan();assert(server.code==400);
  recipe();request("/recipe/start",{{"revision",String(plan.revision)},{"date","2026-11-19T15:00:00.000Z"},{"label","Тест \"2-3-15\""}});
  assert(server.code==200 && run.active && run.kind==String("recipe"));
  startPhase(PH_SPIT);assert(autoOpen==autoPeriod);tick(5000);
  float before=liveRunMl();request("/recipe/rate",{{"stage","0"},{"rate","0"}});tick(3000);
  assert(!valveIsOpen && fabsf(liveRunMl()-before)<.01f);
  request("/recipe/rate",{{"stage","0"},{"rate","18000"}});tick(15000);assert(phase==PH_DRIP);
  request("/stop",{{"on","1"}},HTTP_GET);tick(30000);assert(!valveIsOpen && phase==PH_DRIP);
  request("/stop",{{"on","0"}},HTTP_GET);tick(20UL*60000UL);assert(phase==PH_STREAM);
  tick(50UL*60000UL);assert(phase==PH_DONE && !run.active && !valveIsOpen);
  assert(run.ml>=833.333f && run.ml<835);
  assert(run.volumes[0]>=83.333f && run.volumes[1]>=125 && run.volumes[2]>=625);
  request("/report",{},HTTP_GET);assert(server.code==200);std::cout<<server.response.c_str()<<"\n";
  request("/report.csv",{},HTTP_GET);assert(server.response.s.find("#meta,")!=std::string::npos && server.response.s.find("#summary,")!=std::string::npos);
  request("/auto/end");assert(mode==M_IDLE);
  request("/manual/config",{{"profile","0"},{"set","78.3"},{"hyst","0.3"},{"rate","300"}});assert(server.code==200);
  request("/manual/config",{{"profile","1"},{"set","78.4"},{"hyst","0.2"},{"rate","2000"}});assert(server.code==200);
  request("/manual/config",{{"profile","2"},{"set","79"},{"hyst","0.4"},{"rate","1200"}});assert(server.code==200);
  request("/manual/start",{{"profile","0"},{"date","2026-11-19T17:00:00.000Z"}});assert(mode==M_MANUAL && phase==PH_HEADS && run.active);
  unsigned long id=run.id;tick(10000);assert(run.ml>0);
  request("/manual/start",{{"profile","1"}});tick(10000);assert(run.id==id && phase==PH_BODY && run.volumes[3]>0);
  request("/manual/config",{{"profile","1"},{"set","78.4"},{"hyst","0.2"},{"rate","0"}});tick(1000);assert(!valveIsOpen);
  request("/manual/start",{{"profile","2"}});tick(10000);assert(phase==PH_TAILS && run.id==id && run.volumes[4]>0);
  request("/preset/save",{{"name","Не під час погону"},{"kind","manual"},{"config","m0rate=300"}});assert(server.code==409);
  request("/manual",{{"on","0"}},HTTP_GET);assert(!run.active && mode==M_IDLE && !valveIsOpen && run.volumes[5]>0);
  request("/preset/save",{{"name","Мій \"режим\""},{"kind","manual"},{"config","m0rate=300&vol=10"}});assert(server.code==200);std::cout<<server.response.c_str()<<"\n";
  request("/preset/save",{{"name","Мій \"режим\""},{"kind","manual"},{"config","m0rate=600"}});assert(server.code==200);
  assert(prefs.getString("p0v","")==String("m0rate=600"));
  request("/history",{},HTTP_GET);assert(server.response==String("[]"));
  request("/history/init",{{"confirm","FORMAT"}});assert(server.code==409);
  request("/manual/config",{{"profile","0"},{"set","78.3"},{"hyst","0.3"},{"rate","300"}});
  request("/manual/start",{{"profile","0"}});fakeSensorConnected=false;tick(7000);
  assert(!run.active && mode==M_IDLE && !valveIsOpen);
  std::cerr<<"2-3-15 volumes/transitions, full opening, live zero rate, pause, manual profiles, named presets, reports and simulator storage isolation: OK\n";
}
