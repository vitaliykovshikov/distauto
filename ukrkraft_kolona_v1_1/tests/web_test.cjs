const fs=require('fs');
const vm=require('vm');
const assert=require('assert/strict');
const source=fs.readFileSync(require('path').join(__dirname,'../ukrkraft_kolona_v1_1.ino'),'utf8');
const script=source.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements={};
for(const match of source.matchAll(/<(?:input|button|div|span)[^>]*\bid="([^"]+)"[^>]*>/g)){
 const tag=match[0];
 elements[match[1]]={style:{},textContent:'',disabled:false,listeners:{},
  _value:tag.match(/\bvalue="([^"]*)"/)?.[1]||'',
  get value(){return this._value;},set value(v){this._value=String(v);},
  addEventListener(event,fn){this.listeners[event]=fn;}};
}
const el=id=>{assert.ok(elements[id],`Missing element: ${id}`);return elements[id];};
const status={mode:'IDLE',phase:'NONE',temp:78,tSet:78.3,tHyst:.3,tAlarm:82,period:10,open:1,flow:5};
const plan={revision:1,aa:4000,headsMl:416.67,bodyMl:3125,headsRate:400,bodyRate:4000,yieldMl:3541.67,strengthPct:96,warn:'Орієнтовно'};
const calls=[];
let startOK=true,delayPlan=null;
const context=vm.createContext({document:{getElementById:el},setInterval:()=>{},URLSearchParams,
 fetch:async(url,options={})=>{
  calls.push({url,...options});
  if(url==='/status')return {ok:true,json:async()=>({...status})};
  if(url==='/plan'){
   if(delayPlan)await delayPlan;
   return {ok:true,json:async()=>({...plan})};
  }
  if(url==='/auto/start'){
   if(startOK){status.mode='AUTO';status.phase='WARMUP';}
   return {ok:startOK,text:async()=>'Невірна швидкість'};
  }
  if(url==='/auto/end'){status.phase='ABORT';return {ok:true};}
  return {ok:true,text:async()=>'OK'};
 }});
const run=code=>vm.runInContext(code,context);
(async()=>{
 run(script);await run('upd()');
 assert.equal(el('hh').value,'1');assert.equal(el('dd').value,'4');
 assert.equal(el('flowHour').value,'18000.00');
 el('flow').value='1.25';run('updateFlowHour()');assert.equal(el('flowHour').value,'4500.00');
 await run('calc()');
 for(const expected of ['4000.00','416.67','3125.00','3.542'])assert.ok(el('plan').textContent.includes(expected));
 assert.equal(el('headsRate').value,'400');assert.equal(el('bodyRate').value,'4000');
 assert.equal(el('startAuto').style.display,'block');
 const body=calls.find(c=>c.url==='/plan').body;
 assert.equal(body.get('headsPct'),'10');assert.equal(body.get('reduceDt'),'0.20');
 startOK=false;await run('startAuto()');assert.equal(el('planWarn').textContent,'Невірна швидкість');
 assert.equal(el('startAuto').disabled,false);
 el('bodyRate').value='3500';startOK=true;await run('startAuto()');
 const starts=calls.filter(c=>c.url==='/auto/start');
 assert.equal(starts.at(-1).body.get('bodyRate'),'3500');assert.equal(starts.at(-1).body.get('revision'),'1');
 assert.equal(el('autoForm').style.display,'none');assert.equal(el('end').style.display,'block');
 await run('endAuto()');assert.equal(calls.find(c=>c.url==='/auto/end').method,'POST');
 assert.equal(el('end').textContent,'Повернутися до налаштувань');
 await run('doSwap()');assert.equal(calls.find(c=>c.url==='/auto/swap').method,'POST');
 status.mode='IDLE';status.phase='NONE';await run('upd()');await run('calc()');
 el('headsPct').listeners.input();assert.equal(el('startAuto').style.display,'none');
 let resolve;delayPlan=new Promise(r=>resolve=r);
 const pending=run('calc()');el('hh').listeners.input();resolve();await pending;
 assert.equal(el('startAuto').style.display,'none');assert.equal(run('planRevision'),null);
 delayPlan=null;el('calSec').value='10';el('calMl').value='25';await run('saveCalib()');
 assert.equal(el('flowHour').value,'9000.00');
 console.log('Web: defaults, six results, editable rates, POST controls, invalidation, stale responses, calibration: OK');
})().catch(e=>{console.error(e);process.exitCode=1;});
