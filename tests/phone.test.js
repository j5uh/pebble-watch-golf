const assert=require('node:assert/strict');
const fs=require('node:fs'), vm=require('node:vm');
const course=require('../src/pkjs/course.json');
function harness(initial) {
  let now=1000000, next=1, timers=new Map(), watch=[], requests=[], cleared=[], messages=[], handlers={}, xhrs=[], storage={...initial};
  class XHR {open(m,url){this.url=url;} send(){xhrs.push(this);} reply(status,body){this.status=status;this.responseText=JSON.stringify(body);this.onload();}}
  const sandbox={require:p=>require('../src/pkjs/'+p), Date:{now:()=>now},Math,Number,XMLHttpRequest:XHR,
    localStorage:{getItem:k=>k in storage?storage[k]:null,setItem:(k,v)=>{storage[k]=String(v);},removeItem:k=>{delete storage[k];},key:i=>Object.keys(storage)[i],get length(){return Object.keys(storage).length;}},
    setTimeout:(f,ms)=>{const id=next++;timers.set(id,{f,at:now+ms});return id;},
    clearTimeout:id=>timers.delete(id),
    setInterval:(f,ms)=>{const id=next++;timers.set(id,{f,at:now+ms,ms});return id;},
    Pebble:{openURL:url=>{sandbox.opened=url;},addEventListener:(n,f)=>{handlers[n]=f;},sendAppMessage:(p,ok)=>{messages.push(p);ok();}},
    navigator:{geolocation:{watchPosition:(ok,err)=>{watch.push({ok,err});return watch.length;},
      clearWatch:id=>cleared.push(id),getCurrentPosition:(ok,err,opts)=>{requests.push({ok,err,opts});}}}};
  vm.runInNewContext(fs.readFileSync('src/pkjs/index.js','utf8'),sandbox);
  return {messages,watch,requests,cleared,handlers,xhrs,storage,
    request:(p={})=>handlers.appmessage({payload:{Course:'sharpstown-park',Hole:1,Units:0,Revision:2,LayupTarget:100,...p}}),
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
assert.ok(yards.LayupShot>0);assert.ok(yards.LayupX>-1000 && yards.LayupY>-1000);assert.ok(yards.Hazards.includes('BUNKER'));
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
// No course chosen: the phone lists courses, nearest first once it has a position.
const memorial=JSON.parse(fs.readFileSync('courses/memorial-park.json','utf8'));
const c=harness();c.handlers.ready();c.request({Course:'',Request:1});
assert.equal(c.last().CourseList,'sharpstown-park|Sharpstown Park Golf Course|','built-in course listed without distance before GPS');
assert.equal(c.last().Center,undefined);
c.xhrs[0].reply(200,[{id:'memorial-park',name:memorial.name,center:memorial.center}]);
c.requests[0].ok(c.fix(0,memorial.holes[0].tee));
let rows=c.last().CourseList.split(';');assert.equal(rows.length,2);
assert.ok(rows[0].startsWith('memorial-park|Memorial Park Golf Course|0.'),rows[0]);assert.ok(rows[1].endsWith(' MI'));
// Choosing a downloaded course fetches it once; course details and the hole map follow.
c.request({Course:'memorial-park'});assert.equal(c.xhrs.at(-1).url.slice(-18),'memorial-park.json');
assert.equal(c.last().CourseInfo,undefined,'nothing to show until the course arrives');
c.xhrs.at(-1).reply(404,{});assert.equal(c.last().CourseInfo,'!OFFLINE - RETRYING');
c.advance(5000);c.xhrs.at(-1).reply(200,memorial);
let first=c.last();assert.ok(first.CourseInfo.startsWith('MEMORIAL|'+memorial.holes.map(h=>h.par).join('')+'|BLUE:7292,'));
assert.deepEqual(Array.from(first.MapData),[...Buffer.from(memorial.holes[0].mapHex,'hex')]);assert.ok(first.Center>0);
assert.ok(JSON.parse(c.storage['course:memorial-park']).holes.length===18,'course kept for offline play');
c.request({Revision:3,Course:'memorial-park'});assert.equal(c.last().CourseInfo,undefined);assert.equal(c.last().MapData,undefined,'map sent once per hole');
c.request({Revision:4,Hole:2,Course:'memorial-park'});assert.deepEqual(Array.from(c.last().MapData),[...Buffer.from(memorial.holes[1].mapHex,'hex')]);
// A failed delivery of the map is retried rather than marked sent.
const m=harness();let drop;m.sandbox.Pebble.sendAppMessage=(p,ok,err)=>{m.messages.push(p);drop=err;};
m.request({Request:0});assert.ok(m.last().MapData);drop();m.sandbox.Pebble.sendAppMessage=(p,ok)=>{m.messages.push(p);ok();};
m.advance(1000);assert.ok(m.last().MapData);assert.ok(m.last().CourseInfo.startsWith('SHARPSTOWN|'));
// Catalog versions invalidate disk cache, with usable offline fallback.
function cachedHarness(version) {
  const h=harness();
  h.storage['course:memorial-park']=JSON.stringify({...memorial,version});
  h.handlers.ready();
  h.xhrs[0].reply(200,[{id:memorial.id,name:memorial.name,center:memorial.center,version:memorial.version}]);
  h.request({Course:memorial.id});
  return h;
}
const updated=cachedHarness('old');
assert.equal(updated.xhrs.length,2,'mismatched version downloads');
updated.xhrs[1].reply(200,memorial);
assert.equal(JSON.parse(updated.storage['course:memorial-park']).version,memorial.version);
assert.ok(updated.last().CourseInfo.startsWith('MEMORIAL|'));
const offline=cachedHarness('old');offline.xhrs[1].onerror();
assert.ok(offline.last().CourseInfo.startsWith('MEMORIAL|'),'offline uses stale usable cache');
assert.ok(!offline.messages.some(p=>p.CourseInfo==='!OFFLINE - RETRYING'));
const invalid=cachedHarness('old');invalid.xhrs[1].reply(200,{id:memorial.id});
assert.ok(invalid.last().CourseInfo.startsWith('MEMORIAL|'),'invalid 200 uses usable cache');
const matching=cachedHarness(memorial.version);
assert.equal(matching.xhrs.length,1,'matching cache needs no course request');
assert.ok(matching.messages.some(p=>p.CourseInfo && p.CourseInfo.startsWith('MEMORIAL|')));
// A fresh catalog never swaps geometry during an existing round.
matching.handlers.ready();
matching.xhrs.at(-1).reply(200,[{id:memorial.id,name:memorial.name,center:memorial.center,version:'newer'}]);
matching.request({Course:memorial.id,Revision:3});
assert.equal(matching.xhrs.length,2,'in-memory course stays loaded');
// An ACK for A after moving to B must not suppress the map on returning to A.
const race=harness(), acks=[];
race.sandbox.Pebble.sendAppMessage=(p,ok)=>{race.messages.push(p);acks.push(ok);};
race.request({Hole:15,Revision:15});
race.request({Hole:16,Revision:16});
acks.shift()(); // ACK map 15 after the watch has moved to 16; sends pending 16.
race.request({Hole:15,Revision:17});
acks.shift()(); // ACK 16 and send the final request for 15.
assert.equal(race.last().Hole,15);assert.equal(race.last().Revision,17);
assert.deepEqual(Array.from(race.last().MapData),[...Buffer.from(course.holes[14].mapHex,"hex")],"returning to 15 resends its map");
assert.deepEqual(Array.from(race.last().MapData),[...Buffer.from(course.holes[14].mapHex,'hex')]);
// A 9-hole course is usable; its course info carries 9 pars and a stale hole 14 falls back to the last hole.
const nine={...memorial,id:'nine',holes:memorial.holes.slice(0,9)};
const n=harness();n.handlers.ready();n.request({Course:'nine',Hole:14});n.xhrs.at(-1).reply(200,nine);
assert.ok(n.last().CourseInfo.startsWith('MEMORIAL|'+nine.holes.map(h=>h.par).join('')+'|'));
assert.deepEqual(Array.from(n.last().MapData),[...Buffer.from(nine.holes[8].mapHex,'hex')]);
console.log('Phone tests passed: 9-hole courses, course cache versions and fallback, delayed map ACKs, GPS recovery, permissions, revisions and delivery retry.');

// Offline readiness, write failures and shared fetches.
const disk=harness();disk.request({Course:memorial.id});
disk.sandbox.localStorage.setItem=(k,v)=>{if(k.startsWith('course:'))throw Error('quota');disk.storage[k]=String(v);};
disk.xhrs[0].reply(200,memorial);
assert.equal(disk.last().Offline,3);assert.equal(disk.storage['saved:'+memorial.id],undefined);
assert.ok(disk.sandbox.courses[memorial.id],'storage failure still permits this round');
const unsaved=harness();unsaved.sandbox.courses[memorial.id]=memorial;unsaved.request({Course:memorial.id});
assert.equal(unsaved.last().Offline,0,'loaded alone is not saved');
assert.equal(matching.last().Offline,2,'fresh catalog marks loaded saved data outdated');
const before=matching.sandbox.courses[memorial.id], info=matching.sandbox.infoSent, map=matching.sandbox.mapSent;
matching.request({Course:memorial.id,Download:1});const fetching=matching.xhrs.length;
matching.request({Course:memorial.id,Download:1});assert.equal(matching.xhrs.length,fetching,'duplicate joins fetch');
const replacement={...memorial,version:'newer',short:'UPDATED'};
matching.xhrs.at(-1).reply(200,replacement);
assert.equal(matching.sandbox.courses[memorial.id],before);assert.equal(matching.sandbox.infoSent,info);assert.equal(matching.sandbox.mapSent,map);
assert.equal(JSON.parse(matching.storage['saved:'+memorial.id]),'newer');assert.equal(matching.last().Offline,1);
matching.request({Course:memorial.id,Download:1});matching.xhrs.at(-1).onerror();assert.equal(matching.last().Offline,3);
matching.request({Course:memorial.id,Download:1});matching.xhrs.at(-1).reply(200,replacement);assert.equal(matching.last().Offline,1);
matching.request({Course:''});assert.equal(matching.sandbox.courses[memorial.id],undefined);
matching.request({Course:memorial.id});assert.equal(matching.sandbox.courses[memorial.id].short,'UPDATED','next round uses saved update');
const oldCopy=JSON.stringify({...memorial,version:'old'});
const safe=harness({'course:memorial-park':oldCopy,'saved:memorial-park':JSON.stringify('old')});
safe.request({Course:memorial.id});safe.sandbox.localStorage.setItem=()=>{throw Error('quota');};
safe.request({Course:memorial.id,Download:1});safe.xhrs.at(-1).reply(200,memorial);
assert.equal(safe.storage['course:memorial-park'],oldCopy);assert.equal(JSON.parse(safe.storage['saved:memorial-park']),'old');assert.equal(safe.last().Offline,3);
const marker=harness();marker.request({Course:memorial.id});
marker.sandbox.localStorage.setItem=(k,v)=>{if(k.startsWith('saved:'))throw Error('quota');marker.storage[k]=String(v);};
marker.xhrs[0].reply(200,memorial);assert.equal(marker.last().Offline,3,'marker failure stays failed in memory');
const restart=harness({'saveFailed:memorial-park':'true'});restart.request({Course:memorial.id});assert.equal(restart.last().Offline,3,'failure survives restart');
const migrated=harness({'course:memorial-park':JSON.stringify(memorial)});migrated.request({Course:memorial.id});
assert.equal(migrated.last().Offline,1);assert.equal(JSON.parse(migrated.storage['saved:memorial-park']),memorial.version);
// Loading and explicit saving use one fetch; obsolete results never install.
const joined=harness();joined.request({Course:memorial.id,Download:1});assert.equal(joined.xhrs.length,1);
joined.request({Course:''});joined.xhrs[0].reply(200,memorial);assert.equal(joined.sandbox.courses[memorial.id],undefined);
// Configuration queues run sequentially and protected removals are ignored.
const config=harness();config.handlers.ready();
config.xhrs[0].reply(200,[memorial,{...memorial,id:'other'}]);config.request();
config.storage['course:remove']='{}';config.storage['saved:remove']='"1"';config.storage['saved:'+course.id]='"1"';
config.handlers.webviewclosed({response:encodeURIComponent(JSON.stringify({save:[memorial.id,'other'],remove:[course.id,'remove']}))});
assert.equal(config.xhrs.length,2);assert.equal(config.storage['course:remove'],undefined);assert.ok(config.storage['saved:'+course.id]);
config.xhrs[1].reply(200,memorial);assert.equal(config.xhrs.length,3);config.xhrs[2].reply(200,{...memorial,id:'other'});
config.request({Course:memorial.id});config.handlers.webviewclosed({response:JSON.stringify({remove:[memorial.id]})});assert.ok(config.storage['saved:'+memorial.id]);
config.handlers.showConfiguration();const payload=JSON.parse(decodeURIComponent(config.sandbox.opened.split('#')[1]));
assert.equal(payload.builtIn,course.id);assert.equal(payload.current,memorial.id);assert.equal(payload.saved[memorial.id],memorial.version);
// Twelve maximum-length rows fit the wire budget; saved distant courses remain listed.
const budget=harness(), listings=Array.from({length:12},(_,i)=>({id:String(i).padStart(31,'x'),name:'N'.repeat(47),center:[0,i],version:'1'}));
budget.handlers.ready();budget.xhrs[0].reply(200,listings);
listings.slice(6).forEach(c=>budget.storage['saved:'+c.id]='"1"');budget.request({Course:''});budget.requests[0].ok(budget.fix());
const list=budget.last().CourseList;assert.ok(Buffer.byteLength(list)<=1200);assert.equal(list.split(';').length,12);
listings.slice(6).forEach(c=>assert.ok(list.includes(c.id)));
console.log('Offline readiness and configuration tests passed.');
