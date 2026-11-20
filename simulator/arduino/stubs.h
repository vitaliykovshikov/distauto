#pragma once
#include <string>
#include <map>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <cstdint>
using std::isfinite;
using std::isnan;
#define CONFIG_BT_ENABLED 1
#define DISTAUTO_SIMULATOR 1
#define CONFIG_BLUEDROID_ENABLED 1
#define PROGMEM
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define WIFI_AP 1
#define HTTP_POST 1
#define HTTP_GET 2
#define HTTP_ANY 0
#define SSD1306_WHITE 1
#define SSD1306_SWITCHCAPVCC 1
#define DEVICE_DISCONNECTED_C -127
class String {
 public:
  std::string s;
  String(const char* v=""):s(v){}
  String(std::string v):s(v){}
  String(float v):s(std::to_string(v)){}
  String(unsigned long v):s(std::to_string(v)){}
  const char* c_str() const{return s.c_str();}
  size_t length() const{return s.size();}
  void replace(char a,char b){std::replace(s.begin(),s.end(),a,b);}
  int toInt() const{return std::atoi(s.c_str());}
  bool operator==(const String& v) const{return s==v.s;}
  bool operator!=(const String& v) const{return s!=v.s;}
  String operator+(const String& v) const{return String(s+v.s);}
  String& operator+=(const String& v){s+=v.s;return *this;}
  String& operator+=(char v){s+=v;return *this;}
};
inline uint32_t fakeNow=10000;
inline float fakeTemperature=78.0f;
inline bool fakeSensorConnected=true;
inline int relayLevel=HIGH;
inline unsigned long millis(){return fakeNow;}
inline void delay(unsigned long t){fakeNow+=t;}
inline void pinMode(int,int){}
inline void digitalWrite(int,int level){relayLevel=level;}
struct Console {
  void begin(int){}
  void begin(const char*){}
  void flush(){}
  bool hasClient(){return false;}
  template<class... T> void printf(const char*,T...){}
} inline Serial;
struct BluetoothSerial:Console{bool begin(const char*){return true;}};
struct OneWire {OneWire(int){}};
struct DallasTemperature {
  DallasTemperature(OneWire*){}
  void begin(){} void setWaitForConversion(bool){} void setResolution(int){}
  void requestTemperatures(){} float getTempCByIndex(int){return fakeSensorConnected ? fakeTemperature : DEVICE_DISCONNECTED_C;}
};
struct WebServer {
  std::map<std::string,String> args;
  int code=0;
  String response;
  String contentType;
  std::map<std::string,std::pair<int,void(*)()>> routes;
  WebServer(int){}
  String arg(const char* name){return args[name];}
  void send(int c,const char* type,String body){code=c;response=body;contentType=type;}
  void send_P(int c,const char* type,const char* body){send(c,type,body);}
  void sendHeader(const char*,String){}
  void on(const char* path,void(*fn)()){routes[path]={HTTP_ANY,fn};}
  void on(const char* path,int method,void(*fn)()){routes[path]={method,fn};}
  void dispatch(const char* path,int method){
    auto it=routes.find(path);
    if(it==routes.end()){send(404,"text/plain","Not found");return;}
    if(it->second.first!=HTTP_ANY && it->second.first!=method){send(405,"text/plain","Method not allowed");return;}
    it->second.second();
  }
  void begin(){} void handleClient(){}
};
struct Preferences {
  std::map<std::string,float> floats;
  std::map<std::string,unsigned long> longs;
  std::map<std::string,String> strings;
  bool begin(const char*,bool){return true;}
  float getFloat(const char* key,float v){return floats.count(key)?floats[key]:v;}
  unsigned long getULong(const char* key,unsigned long v){return longs.count(key)?longs[key]:v;}
  void putFloat(const char* key,float v){floats[key]=v;}
  void putULong(const char* key,unsigned long v){longs[key]=v;}
  String getString(const char* key,const char* fallback=""){return strings.count(key)?strings[key]:String(fallback);}
  void putString(const char* key,String v){strings[key]=v;}
  void remove(const char* key){strings.erase(key);floats.erase(key);longs.erase(key);}
};
struct WireStub {bool begin(int,int){return true;} void setClock(int){}} inline Wire;
struct Adafruit_SSD1306 {
  Adafruit_SSD1306(int,int,WireStub*,int){}
  bool begin(int,int){return true;}
  void clearDisplay(){} void setTextColor(int){} void setTextSize(int){}
  void setCursor(int,int){} void display(){}
  template<class T> void print(T){}
  template<class... T> void printf(const char*,T...){}
};
struct WifiStub {bool mode(int){return true;} bool softAP(const char*,const char*){return true;}} inline WiFi;
struct OTAStub {void begin(WebServer*){} void loop(){}} inline ElegantOTA;
