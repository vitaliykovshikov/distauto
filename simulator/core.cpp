// The production sketch is compiled unchanged. Only its hardware/HTTP transport is replaced.
#include "arduino/stubs.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif
#include "../ukrkraft_kolona_v1_1/ukrkraft_kolona_v1_1.ino"

extern "C" {
EXPORT void sim_init(){setup();}
EXPORT void sim_temperature(float value){fakeTemperature=value;}
EXPORT void sim_sensor(int connected){fakeSensorConnected=connected!=0;}
EXPORT void sim_step(unsigned int milliseconds){
  // Small steps preserve pulse widths, samples and all timers at accelerated speed.
  while(milliseconds){
    unsigned int step=std::min(milliseconds,50u);
    fakeNow+=step;milliseconds-=step;loop();
  }
}
EXPORT void sim_clear_args(){server.args.clear();}
EXPORT void sim_arg(const char* key,const char* value){server.args[key]=value;}
EXPORT int sim_request(const char* path,int method){
  server.code=0;server.response="";
  server.dispatch(path,method);
  return server.code;
}
EXPORT const char* sim_response(){return server.response.c_str();}
EXPORT const char* sim_content_type(){return server.contentType.c_str();}
EXPORT const char* sim_state(){
  static char buf[400];
  snprintf(buf,sizeof(buf),R"JSON({"timeMs":%lu,"valve":%s,"phase":"%s","deliveredMl":%.2f,"rate":%.2f,"factor":%.4f,"headsMl":%.2f,"bodyMl":%.2f})JSON",
    millis(),b(valveIsOpen),phaseName(),
    (mode==M_AUTO && (phase==PH_HEADS || phase==PH_BODY || phase==PH_DONE || phase==PH_ABORT))?deliveredNow():0.0f,
    (mode==M_AUTO && (phase==PH_HEADS || phase==PH_BODY) && !manualStop)?phaseRate*rateFactor:0.0f,
    rateFactor,totalHeads,totalBody);
  return buf;
}
}
