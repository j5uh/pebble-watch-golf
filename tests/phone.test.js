const assert=require('node:assert/strict');
const fs=require('node:fs'), vm=require('node:vm');
const course=require('../src/pkjs/course.json');
function harness() {
  let now=1000000, next=1, timers=new Map(), watch=[], requests=[], cleared=[], messages=[], handlers={};
  const sandbox={require:p=>require('../src/pkjs/'+p), Date:{now:()=>now},Math,Number,
    setTimeout:(f,ms)=>{const id=next++;timers.set(id,{f,at:now+ms});return id;},
    clearTimeout:id=>timers.delete(id),
    setInterval:(f,ms)=>{const id=next++;timers.set(id,{f,at:now+ms,ms});return id;},
    Pebble:{addEventListener:(n,f)=>{handlers[n]=f;},sendAppMessage:(p,ok)=>{messages.push(p);ok();}},
    navigator:{geolocation:{watchPosition:(ok,err)=>{watch.push({ok,err});return watch.length;},
      clearWatch:id=>cleared.push(id),getCurrentPosition:(ok,err,opts)=>{requests.push({ok,err,opts});}}}};
  vm.runInNewContext(fs.readFileSync('src/pkjs/index.js','utf8'),sandbox);
  return {messages,watch,requests,cleared,handlers,
    request:(p={})=>handlers.appmessage({payload:{Hole:1,Units:0,Revision:2,LayupTarget:100,...p}}),
    fix:(age=0,point=course.holes[0].tee)=>({timestamp:now-age,coords:{latitude:point[0],longitude:point[1],accuracy:5}}),
    last:()=>messages[messages.length-1],
    advance:ms=>{const end=now+ms; for(;;) {let entry=[...timers].filter(([,t])=>t.at<=end).sort((a,b)=>a[1].at-b[1].at)[0]; if(!entry)break; const [id,t]=entry;now=t.at;if(t.ms)t.at+=t.ms;else timers.delete(id);t.f();}now=end;}, sandbox};
}
const h=harness();h.handlers.ready();assert.equal(h.last().Ready,1);
h.request({Request:1});assert.equal(h.requests.length,1);assert.equal(h.last().GpsState,1);
assert.equal(h.requests[0].opts.maximumAge,0);
h.request({Request:1});assert.equal(h.requests.length,1,'no overlapping refreshes');
h.requests[0].ok(h.fix());let yards=h.last();assert.equal(yards.GreenEdges.split(',').length,4);assert.ok(yards.Front<yards.Center && yards.Center<yards.Back);
assert.equal(yards.MapX,Math.round(course.holes[0].map.offset[0]*16));
assert.equal(yards.MapY,Math.round(course.holes[0].map.offset[1]*16));
assert.ok(yards.LayupShot>0);assert.ok(yards.Hazards.includes('BUNKER'));
for (const row of yards.Hazards.split('|')) {const fields=row.split(',');assert.equal(fields.length,8); assert.ok(fields.slice(4).every(v=>Number.isFinite(Number(v))));assert.ok(course.holes[0].hazards.some(h=>h.osmId===fields[3]), 'hazard card carries its exact mapped feature ID');}
h.request({Units:1,Revision:3});let meters=h.last();assert.ok(Math.abs(meters.Center-yards.Center*0.9144)<1);
assert.equal(meters.Revision,3);assert.equal(meters.LayupTarget,100);
h.request({Hole:18,Revision:4});assert.equal(h.last().Hole,18);
let sent=h.messages.length;h.advance(5000);assert.equal(h.messages.length,sent,'heartbeat skips unchanged fix');
h.request({Revision:5});assert.equal(h.last().FixAge,5,'reply preserves actual fix age');assert.equal(h.messages.length,sent+1,'one reply per request');
h.request({Request:1,Revision:5});h.requests.at(-1).err({code:1});assert.equal(h.last().GpsState,2);assert.equal(h.last().Center,undefined);
let count=h.requests.length;h.advance(60000);h.request({Request:0,Revision:5});assert.equal(h.requests.length,count,'no automatic permission retry');
h.request({Request:1,Revision:6});assert.equal(h.requests.length,count+1);h.requests.at(-1).ok(h.fix());assert.ok(h.last().Center>0);
// A silent geolocation provider times out and recovers automatically.
const t=harness();t.request({Request:1});t.advance(20000);assert.equal(t.last().GpsState,4);
t.advance(20000);assert.ok(t.requests.length>=2);assert.ok(t.cleared.length>=1);
t.requests.at(-1).ok(t.fix());assert.equal(t.last().GpsState,0);
// An expired one-shot callback cannot replace a new request's result.
const recovered=t.last().Center;t.requests[0].ok(t.fix(1000,course.holes[4].tee));assert.equal(t.last().Center,recovered);
// Expired fixes must never be sent as live data, even with periodic heartbeats.
const s=harness();s.request({Request:1});s.requests[0].ok(s.fix());s.advance(35000);assert.equal(s.last().Center,undefined);
// A delayed older response cannot overwrite a more recent watchPosition fix.
const o=harness();o.request({Request:1});o.advance(1000);o.watch[0].ok(o.fix());const recent=o.last().Center;
o.requests[0].ok(o.fix(1000,course.holes[5].tee));assert.equal(o.last().Center,recent);
// Invalid GPS data is not accepted.
const v=harness();v.request({Request:1});let bad=v.fix();bad.coords.latitude=NaN;v.requests[0].ok(bad);assert.equal(v.last().Center,undefined);
// AppMessage backoff retains the newest revision when delivery fails.
const q=harness();let fail;
q.sandbox.Pebble.sendAppMessage=(p,ok,err)=>{q.messages.push(p);fail=err;};
q.request({Request:1,Revision:2});q.request({Request:0,Revision:9,Hole:9});fail();
q.sandbox.Pebble.sendAppMessage=(p,ok)=>{q.messages.push(p);ok();};q.advance(1000);
assert.equal(q.last().Revision,9);assert.equal(q.last().Hole,9);
console.log('Phone tests passed: GPS recovery, silent timeout, permissions, fix age, revisions, units, stale callbacks and delivery retry.');
