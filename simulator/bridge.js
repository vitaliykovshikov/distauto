/* Browser transport and virtual instruments only. All process logic runs in core.cpp/.ino. */
(()=>{
 const el=id=>document.getElementById(id);
 const nativeFetch=window.fetch.bind(window);
 const endpoints=new Set(['/status','/stop','/manual','/set','/plan','/auto/start','/auto/swap','/auto/end','/calib/test','/calib/save','/features','/rates','/recipe/plan','/recipe/start','/recipe/rate','/manual/config','/manual/start','/stage/end','/stage/choice','/presets','/preset/save','/preset/delete','/report','/report.csv','/history','/history/init','/history/download','/history/delete']);
 let api=null;
 const ready=import('./column.mjs').then(async({default:create})=>{
  const module=await create();
  const wrap=(name,result,args=[])=>module.cwrap('sim_'+name,result,args);
  api={init:wrap('init',null),step:wrap('step',null,['number']),temperature:wrap('temperature',null,['number']),
   sensor:wrap('sensor',null,['number']),clear:wrap('clear_args',null),arg:wrap('arg',null,['string','string']),
   request:wrap('request','number',['string','number']),response:wrap('response','string'),
   contentType:wrap('content_type','string'),state:wrap('state','string')};
  api.init();api.step(1000);render();return api;
 });
 ready.catch(error=>{el('simState').textContent='Не вдалося завантажити симулятор. Оновіть сторінку. '+error.message;});
 window.fetch=async(input,options={})=>{
  const url=new URL(input instanceof Request?input.url:input,location.href);
  if(url.origin!==location.origin || !endpoints.has(url.pathname))return nativeFetch(input,options);
  const core=await ready;
  const args=new URLSearchParams(url.search);
  if(options.body)for(const [key,value] of new URLSearchParams(options.body))args.set(key,value);
  core.clear();for(const [key,value] of args)core.arg(key,value);
  const status=core.request(url.pathname,(options.method||'GET').toUpperCase()==='POST'?1:2);
  return new Response(core.response(),{status,headers:{'Content-Type':core.contentType()}});
 };
 function render(){
  if(!api)return;
  const s=JSON.parse(api.state());
  const seconds=Math.floor(s.timeMs/1000);
  const clock=[Math.floor(seconds/3600),Math.floor(seconds/60)%60,seconds%60].map(v=>String(v).padStart(2,'0')).join(':');
  el('simState').textContent=`Час ${clock} · Клапан ${s.valve?'ВІДКРИТИЙ':'ЗАКРИТИЙ'} · Швидкість ${s.rate.toFixed(0)} мл/год · За погін: ${s.deliveredMl.toFixed(1)} мл`;
 }
 el('simTemp').addEventListener('input',()=>{
  const value=Number(el('simTemp').value);
  if(api && el('simTemp').value!=='' && Number.isFinite(value))api.temperature(value);
 });
 el('simSensor').addEventListener('change',()=>{if(api)api.sensor(el('simSensor').checked?1:0);});
 async function advance(milliseconds){
  const core=await ready;core.step(milliseconds);render();if(window.upd)await window.upd();if(window.refreshReport)await window.refreshReport();
 }
 el('simMinute').addEventListener('click',()=>advance(60000));
 el('simTenMinutes').addEventListener('click',()=>advance(600000));
 el('simReset').addEventListener('click',()=>location.reload());
 let last=performance.now(),remainder=0;
 setInterval(()=>{
  const now=performance.now(),elapsed=Math.min(now-last,1000);last=now;
  if(!api)return;
  remainder+=elapsed*Number(el('simSpeed').value);
  const advanceMs=Math.floor(remainder);remainder-=advanceMs;
  if(advanceMs>0)api.step(advanceMs);
  render();
 },100);
})();
