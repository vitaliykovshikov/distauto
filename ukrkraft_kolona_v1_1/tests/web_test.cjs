const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const source=fs.readFileSync(require('path').join(__dirname,'../WebPage.h'),'utf8');
const script=source.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements={};
function makeElement(){return {style:{},textContent:'',disabled:false,listeners:{},children:[],files:[],clientWidth:800,_value:'',get value(){return this._value;},set value(v){this._value=String(v);},addEventListener(event,fn){this.listeners[event]=fn;},append(...nodes){this.children.push(...nodes);},replaceChildren(...nodes){this.children=nodes;},getContext(){return null;},click(){}};}
for(const match of source.matchAll(/<[a-z]+[^>]*\bid="([^"]+)"[^>]*>/g)){const el=makeElement();el.value=match[0].match(/\bvalue="([^"]*)"/)?.[1]||'';el.style.display=match[0].includes('display:none')?'none':'block';elements[match[1]]=el;}
const el=id=>{assert.ok(elements[id],`Missing element: ${id}`);return elements[id];};
el('newKind').value='auto';
const status={mode:'IDLE',phase:'NONE',temp:78,tSet:78.3,tHyst:.3,tAlarm:82,period:10,open:1,flow:5,recipe:false,manualProfile:0,stageAwaiting:false,stageDecision:0,nextProfile:1,extraHeads:false};
const features={simulator:true,maxRate:4000,minRate:1,manualProfile:0,recipe:false,recipeRates:[4000,400,800],historyReady:false,historyLimit:0,historyUsed:0,manualProfiles:[{id:0,set:78.3,hyst:.3,rate:400},{id:1,set:78.3,hyst:.3,rate:4000},{id:2,set:78.3,hyst:.3,rate:1000}]};
const plan={revision:1,aa:4000,headsMl:416.67,bodyMl:3125,headsRate:400,bodyRate:4000,yieldMl:3541.67,strengthPct:96};
const recipe={revision:2,aaMl:4000,totalPct:20,targetsMl:[83.33,125,625],rates:[4000,400,800],totalMl:833.33};
const calls=[],storage=new Map();let presets=[],startOK=true,delayPlan=null;
const context=vm.createContext({document:{getElementById:el,createElement:makeElement},window:{devicePixelRatio:1,addEventListener(){}},setInterval(){},setTimeout(){},URL,URLSearchParams,Blob,confirm:()=>true,localStorage:{getItem:k=>storage.get(k)||null,setItem:(k,v)=>storage.set(k,v)},fetch:async(url,options={})=>{
 calls.push({url,...options});const values=Object.fromEntries(new URLSearchParams(options.body));
 const ok=data=>({ok:true,json:async()=>structuredClone(data),text:async()=>typeof data==='string'?data:JSON.stringify(data)});
 if(url==='/status')return ok(status);
 if(url==='/features')return ok(features);
 if(url.startsWith('/rates?'))return ok({headsRate:400,bodyRate:4000});
 if(url==='/report')return ok({exists:false});
 if(url==='/presets')return ok(presets);
 if(url==='/preset/save'){const p={slot:presets.length,name:values.name,kind:values.kind,config:values.config};presets=presets.filter(v=>v.name!==p.name);presets.push(p);return ok(presets);}
 if(url==='/preset/delete'){presets=presets.filter(p=>p.slot!==Number(values.slot));return ok(presets);}
 if(url==='/plan'||url==='/recipe/plan'){if(delayPlan)await delayPlan;return ok(url==='/plan'?plan:recipe);}
 if(url==='/auto/start'||url==='/recipe/start'){if(!startOK)return {ok:false,text:async()=>'Невірна швидкість'};Object.assign(status,{mode:'AUTO',phase:'WARMUP',recipe:url==='/recipe/start'});return ok('OK');}
 if(url==='/manual/config'){Object.assign(features.manualProfiles[Number(values.profile)],{set:Number(values.set),hyst:Number(values.hyst),rate:Number(values.rate)});return ok(features.manualProfiles);}
 if(url==='/manual/start'){if(status.mode!=='IDLE')return {ok:false,text:async()=>'Спочатку завершіть етап'};Object.assign(status,{mode:'MANUAL',phase:['HEADS','BODY','TAILS'][Number(values.profile)],manualProfile:Number(values.profile),recipe:false});return ok('OK');}
 if(url==='/stage/end'){status.stageAwaiting=true;status.manualStop=true;status.stageDecision++;status.nextProfile=status.manualProfile<2?status.manualProfile+1:-1;return ok('OK');}
 if(url==='/stage/choice'){status.stageAwaiting=false;status.manualStop=false;if(values.choice==='next'){if(status.nextProfile<0){status.mode='IDLE';status.phase='NONE';}else{status.manualProfile=status.nextProfile;status.mode='MANUAL';status.phase=['HEADS','BODY','TAILS'][status.manualProfile];}}return ok('OK');}
 if(url==='/auto/end'){status.stageAwaiting=false;status.phase=status.phase==='ABORT'?'NONE':'ABORT';if(status.phase==='NONE')status.mode='IDLE';return ok('OK');}
 if(url.startsWith('/manual?on=0')){status.mode='IDLE';status.phase='NONE';return ok('OK');}
 if(url.startsWith('/stop?')){status.manualStop=url.endsWith('1');return ok('OK');}
 return ok('OK');
}});
const run=code=>vm.runInContext(code,context);
(async()=>{
 new vm.Script(script);run(script);await run('boot()');
 assert.equal(el('hh').value,'1');assert.equal(el('dd').value,'4');assert.equal(el('flowHour').value,'18000.00');assert.equal(el('historyCard').style.display,'none');
 run("openMode('auto')");await run('calc()');
 for(const value of ['4000.00','416.67','3125.00','3.542'])assert.ok(el('plan').textContent.includes(value));
 startOK=false;await run('startCalculated()');assert.equal(el('appMessage').textContent,'Невірна швидкість');
 startOK=true;el('bodyRateNumber').value='3500';el('bodyRateNumber').listeners.input();await run('startCalculated()');
 const start=calls.findLast(c=>c.url==='/auto/start');assert.equal(start.body.get('bodyRate'),'3500');assert.ok(start.body.get('date').endsWith('Z'));
 await run('endProcess()');assert.equal(status.phase,'ABORT');await run('endProcess()');assert.equal(status.mode,'IDLE');
 run("openMode('recipe')");await run('recipeDefaults()');assert.equal(el('spitRate').value,'4000');assert.equal(el('dripRate').value,'400');assert.equal(el('streamRate').value,'800');
 assert.equal(el('manual').style.display,'block');assert.equal(el('manualProfile0').style.display,'none');await run('calc()');for(const value of ['83.33','125.00','625.00','833.33','20%'])assert.ok(el('plan').textContent.includes(value));
 await run('startCalculated()');el('dripRateNumber').value='600';el('dripRateNumber').listeners.input();await el('dripRateNumber').listeners.change();assert.equal(el('dripRate').value,'600');
 const rate=calls.findLast(c=>c.url==='/recipe/rate');assert.equal(rate.body.get('stage'),'1');assert.equal(rate.body.get('rate'),'600');
 await run('endProcess()');await run('endProcess()');
 run("openMode('manual')");el('m1rate').value='3000';el('m1rate').listeners.input();await run('startManual(1)');assert.equal(status.phase,'BODY');
 el('m1rate').value='2500';el('m1rate').listeners.input();await el('m1rate').listeners.change();assert.equal(el('m1rateNumber').value,'2500');assert.equal(features.manualProfiles[1].rate,2500);
 await run('finishStage()');assert.equal(el('stageChoice').style.display,'block');assert.equal(el('stageNext').textContent,'Перейти до хвостів');await run("chooseStage('continue')");assert.equal(status.phase,'BODY');await run('finishStage()');await run("chooseStage('next')");assert.equal(status.phase,'TAILS');await run('endProcess()');assert.equal(status.mode,'IDLE');
 el('presetName').value='Колона «4»';await run('savePreset()');assert.equal(presets.length,1);assert.ok(storage.has('distauto.presets.v1'));
 run("openMode('custom')");await run('loadPresets()');assert.ok(el('presetsList').children.length);run('applyPreset(presets[0])');assert.equal(run('view'),'manual');
 const csv='# distauto-run-v1\n#meta,{"schema":1,"label":"Тест","parameters":{}}\nelapsed_s,temperature_c,phase,rate_ml_h,valve,delivered_ml\n0,78.00,HEADS,400,1,0\n5,78.20,HEADS,400,0,1\n';
 context.csv=csv;assert.equal(run('parseReportCsv(csv).summary.deliveredMl'),1);assert.equal(run('parseReportCsv(csv).samples.length'),2);
 context.partial=csv+'10,78.3,HEADS,400\n#summary,{';assert.equal(run('parseReportCsv(partial).summary.deliveredMl'),1);
 run("applyPreset({name:'Авто 3500',kind:'auto',config:'headsRate=350&bodyRate=3500'})");await run('calc()');assert.equal(el('bodyRate').value,'3500');
 run("openMode('auto')");let resolve;delayPlan=new Promise(r=>resolve=r);const pending=run('calc()');el('hh').listeners.input();resolve();await pending;assert.equal(run('planRevision'),null);assert.equal(el('startAuto').style.display,'none');
 el('spitRateNumber').value='4001';el('spitRateNumber').listeners.input();assert.throws(()=>run("readRate('spitRate')"),/4000/);
 console.log('Web: paired numeric inputs, stage choices, recipe body/tails, four modes, staged rates, manual profiles, named presets, stale plans, portable CSV and simulator history exclusion: OK');
})().catch(e=>{console.error(e);process.exitCode=1;});
