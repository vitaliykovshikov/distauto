#include "stubs.h"
#include <cassert>
#include <iostream>
#include "../../ukrkraft_kolona_v1_1.ino"

bool near(float a,float b){return fabsf(a-b)<0.05f;}
void calculate(float pct=10){assert(makePlan(10,40,1,4,pct,15,96,10,0.2f).length()==0);}
void startArgs(){
  server.args={{"revision",String(plan.revision)},{"headsRate","400"},{"bodyRate","4000"}};
  temperature=78; lastGoodRead=fakeNow; mode=M_IDLE; flowMlS=5;
}
int main(){
  calculate();
  assert(near(plan.aaMl,4000));
  assert(near(plan.headsMl,416.6667f));
  assert(near(plan.bodyMl,3125));
  assert(near((plan.headsMl+plan.bodyMl)*0.96f,3400));
  assert(near(plan.bodyRate,4000));
  calculate(5);assert(near(plan.headsMl,208.3333f));
  assert(near(plan.bodyMl,3333.3333f));
  assert(makePlan(NAN,40,1,4,10,15,96,10,.2f).length()>0 && !plan.valid);
  calculate();startArgs();server.args["revision"]="0";handleAutoStart();assert(server.code==409);
  startArgs();server.args["bodyRate"]="20000";handleAutoStart();assert(server.code==400);
  startArgs();temperature=NAN;handleAutoStart();assert(server.code==400);
  startArgs();handleAutoStart();assert(server.code==200 && mode==M_AUTO && phase==PH_WARMUP);
  handleAutoStart();assert(server.code==409);

  startPhase(PH_BODY);tRef=78;temperature=78.3f;
  checkOverTemp(fakeNow);fakeNow+=60001;checkOverTemp(fakeNow);
  assert(near(rateFactor,.9f));
  fakeNow+=60001;checkOverTemp(fakeNow);assert(near(rateFactor,.81f));
  for(int i=0;i<30;i++){fakeNow+=60001;checkOverTemp(fakeNow);}
  assert(near(rateFactor,.4f));
  rateFactor=1;temperature=78;checkOverTemp(fakeNow);assert(over1Since==0);
  temperature=78.3f;fakeNow+=100;checkOverTemp(fakeNow);assert(near(rateFactor,1));

  valveClose();phaseDelivered=0;valveOpen();fakeNow+=1000;
  assert(near(deliveredNow(),5));valveClose();assert(near(phaseDelivered,5));
  startPhase(PH_HEADS);phaseDelivered=plan.headsMl-2;valveOpen();fakeNow+=500;
  autoLoop(fakeNow);assert(phase==PH_STAB && !valveIsOpen);
  startPhase(PH_BODY);phaseDelivered=plan.bodyMl-2;valveOpen();fakeNow+=500;
  autoLoop(fakeNow);assert(phase==PH_DONE && !valveIsOpen);
  assert(statusText==String("АВТО: завершено"));

  startPhase(PH_BODY);valveOpen();handleAutoEnd();
  assert(phase==PH_ABORT && !valveIsOpen && relayLevel==LOW);
  loop();assert(!valveIsOpen && relayLevel==LOW);
  handleAutoEnd();assert(mode==M_IDLE);loop();assert(!valveIsOpen && relayLevel==LOW);
  calculate();startArgs();handleAutoStart();startPhase(PH_BODY);
  manualStop=true;valveOpen();autoLoop(fakeNow);assert(!valveIsOpen);
  handleCalibTest();assert(server.code==409);
  manualStop=false;lastGoodRead=fakeNow-6000;conversionPending=false;lastRequest=fakeNow;
  loop();assert(phase==PH_ABORT && !valveIsOpen);

  server.args={{"v","12,5"}};assert(near(argF("v"),12.5f));
  for(const char* value:{"","12x","NaN","inf"}){server.args["v"]=value;assert(isnan(argF("v")));}
  handleStatus();std::cout<<server.response.c_str()<<"\n";
  mode=M_IDLE;server.args={{"vol","10"},{"abv","40"},{"h","1"},{"d","4"},{"headsPct","10"},{"tailsPct","15"},{"strengthPct","96"},{"decrementPct","10"},{"reduceDt","0.2"}};
  handlePlan();assert(server.code==200);std::cout<<server.response.c_str()<<"\n";
  std::cerr<<"Plan, validation, start guards, decrement, live volume, completion, stop, pause, sensor fault: OK\n";
}
