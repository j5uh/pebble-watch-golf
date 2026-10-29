var builtIn = require('./course.json');
var geo = require('./distance');
// ponytail: local dev server for courses/; point at the hosted course files once chosen.
var COURSES_URL='https://j5uh.github.io/pebble-watch-golf/';
var courses={}, catalog=[], courseId='', loadError='', infoSent='', mapSent='';
var inflight={}, saveFailed={}, migrated={}, saveQueue=[], queueBusy=false;
courses[builtIn.id]=builtIn;
try {for (var i=0;i<localStorage.length;i++) {var k=localStorage.key(i); if (k.indexOf('saveFailed:')===0 && stored(k)) saveFailed[k.slice(11)]=true;}} catch (e) {}
setCatalog(stored('catalog'));
var hole=1, units=0, target=100, revision=0;
var fix=null, watchId=null, active=false, permissionDenied=false;
var pending=null, sending=false, retryTimer=null, failures=0, lastKey='';
var state=1, refreshing=false, requestToken=0, refreshTimer=null, lastAttempt=0, lastWatchRestart=0;
var options={enableHighAccuracy:true,maximumAge:0,timeout:15000};
var MAX_AGE=30000;
function context() {return {Course:courseId,Hole:hole,Units:units,Revision:revision,LayupTarget:target};}
function stored(key) {try {return JSON.parse(localStorage.getItem(key));} catch (e) {return null;}}
function store(key,value) {try {localStorage.setItem(key,JSON.stringify(value)); return true;} catch (e) {return false;}}
function get(path,ok,fail) {
  var xhr=new XMLHttpRequest();
  xhr.open('GET',COURSES_URL+path); xhr.timeout=15000;
  xhr.onload=function() {
    var data=null;
    try {data=JSON.parse(xhr.responseText);} catch (e) {}
    if (xhr.status===200 && data) ok(data); else fail();
  };
  xhr.onerror=xhr.ontimeout=fail; xhr.send();
}
// The built-in course is always listed; downloaded listings add to it.
function setCatalog(list) {
  var seen={};
  catalog=[];
  [builtIn].concat(list||[]).forEach(function(c) {
    if (c && c.id && c.name && c.center && !seen[c.id]) {seen[c.id]=true; catalog.push({id:c.id,name:c.name,center:c.center,version:c.version});}
  });
}
function usableCourse(c,id) {return c && c.id===id && c.holes && (c.holes.length===9 || c.holes.length===18) && c.tees && c.tees.length>0;}
function failedSave(id,failed) {
  saveFailed[id]=failed;
  if (failed) store('saveFailed:'+id,true); else remove('saveFailed:'+id);
}
function remove(key) {try {localStorage.removeItem(key);} catch (e) {}}
function versionOf(id) {
  var version;
  catalog.forEach(function(c) {if (c.id===id) version=c.version;});
  return version;
}
function offlineState(id) {
  if (id===builtIn.id) return 1;
  var saved=stored('saved:'+id), c;
  if (!saved && !migrated[id]) {
    migrated[id]=true; c=stored('course:'+id);
    if (usableCourse(c,id)) {
      saved=c.version || '1';
      if (!store('saved:'+id,saved)) failedSave(id,true);
    }
  }
  if (saveFailed[id]) return 3;
  return saved?(versionOf(id) && saved!==versionOf(id)?2:1):0;
}
function fetchCourse(id,done) {
  if (inflight[id]) {inflight[id].push(done); return;}
  inflight[id]=[done];
  function finish(c,error) {
    var callbacks=inflight[id]; delete inflight[id];
    callbacks.forEach(function(f) {f(c,error);});
  }
  get(id+'.json',function(c) {
    if (!usableCourse(c,id)) {finish(null,'COURSE DATA INVALID'); return;}
    var saved=store('course:'+id,c);
    if (saved) saved=store('saved:'+id,c.version || '1');
    failedSave(id,!saved);
    if (id===courseId && !courses[id]) courses[id]=c;
    finish(c);
  },function() {finish(null,'OFFLINE - RETRYING');});
}
function loadCourse(id) {
  if (courses[id] || inflight[id]) return;
  var cached=stored('course:'+id), version=versionOf(id);
  if (usableCourse(cached,id) && (!version || cached.version===version)) {courses[id]=cached; return;}
  fetchCourse(id,function(c,error) {
    if (id!==courseId) return;
    if (c) loadError='';
    else if (usableCourse(cached,id)) {courses[id]=cached; loadError='';}
    else loadError=error;
    publish();
  });
}
function saveCourse(id,done) {
  if (id===builtIn.id) {if (done) done(); return;}
  fetchCourse(id,function(c) {
    if (!c) failedSave(id,true);
    if (id===courseId) {if (c) loadError=''; publish();}
    if (done) done();
  });
}
function runQueue() {
  if (queueBusy || !saveQueue.length) return;
  queueBusy=true;
  saveCourse(saveQueue.shift(),function() {queueBusy=false; runQueue();});
}
function away(meters) {
  var v=units?meters/1000:meters/1609.344;
  return (v<10?v.toFixed(1):Math.round(v))+(units?' KM':' MI');
}
// Nearest first once the phone has a position; until then the last course played leads.
function field(text,limit) {
  text=String(text).slice(0,limit).replace(/[\uD800-\uDBFF]$/,'');
  while (text && unescape(encodeURIComponent(text)).length>limit) text=text.slice(0,-1).replace(/[\uD800-\uDBFF]$/,'');
  return text;
}
function courseList() {
  var here=fix?[fix.coords.latitude,fix.coords.longitude]:null, last=stored('last');
  var rows=catalog.map(function(c) {return {c:c,d:here?geo.distance(here,c.center):0};});
  rows.sort(function(a,b) {
    if (here) return a.d-b.d;
    if ((a.c.id===last)!==(b.c.id===last)) return a.c.id===last?-1:1;
    return a.c.name<b.c.name?-1:1;
  });
  var saved=rows.filter(function(r) {return r.c.id!==builtIn.id && stored('saved:'+r.c.id);}).slice(0,6);
  var selected=rows.filter(function(r) {return saved.indexOf(r)>=0 || rows.indexOf(r)<12-saved.length;});
  for (var i=0;selected.length<12 && i<rows.length;i++) if (selected.indexOf(rows[i])<0) selected.push(rows[i]);
  selected.sort(function(a,b) {return rows.indexOf(a)-rows.indexOf(b);});
  function serialize() {
    return selected.map(function(r) {return field(r.c.id,31)+'|'+field(r.c.name,47)+'|'+(here?away(r.d).slice(0,11):'');}).join(';');
  }
  var text=serialize();
  while (unescape(encodeURIComponent(text)).length>1200 && selected.length) {
    var drop=selected.length-1;
    while (drop>=0 && saved.indexOf(selected[drop])>=0) drop--;
    if (drop<0) break;
    selected.splice(drop,1); text=serialize();
  }
  return text;
}
function courseInfo(c) {
  return c.short+'|'+c.holes.map(function(h) {return h.par;}).join('')+'|'+c.tees.map(function(t) {return t.name+':'+t.totalYards;}).join(',');
}
function hexBytes(hex) {
  var out=[];
  for (var i=0;i<hex.length;i+=2) out.push(parseInt(hex.substr(i,2),16));
  return out;
}
function flush() {
  if (sending || !pending || retryTimer!==null) return;
  var payload=pending; pending=null; sending=true;
  Pebble.sendAppMessage(payload,function() {
    sending=false; failures=0;
    // Course info and hole maps go once per course/hole; positions go every update.
    if (payload.CourseInfo && payload.CourseInfo.charAt(0)!=='!') infoSent=payload.Course;
    if (payload.MapData) mapSent=payload.Course+':'+payload.Hole;
    flush();
  },function() {
    sending=false; lastKey='';
    // Never retry a response for a hole/settings revision the user has left.
    if (!pending && (payload.Ready || payload.Revision===revision)) pending=payload;
    failures++;
    retryTimer=setTimeout(function() {retryTimer=null; if (active) publish(); else flush();},Math.min(10000,1000*Math.pow(2,Math.min(3,failures-1))));
  });
}
// Skip repeats of the last queued payload; FixAge alone changes every heartbeat
// and the watch ages fixes locally from FixTime. A delivered map is not a change.
function send(p) {
  var copy={},key;
  for (key in p) if (key!=='FixAge' && key!=='MapData') copy[key]=p[key];
  key=JSON.stringify(copy);
  if (key===lastKey) return;
  lastKey=key; pending=p; flush();
}
function fixAge() {return fix ? Math.max(0,Date.now()-fix.timestamp) : Infinity;}
function mapPoint(position,m) {
  var east=(position[1]-m.origin[1])*m.eastScale,north=(position[0]-m.origin[0])*m.northScale;
  return [Math.round(((east*m.forward[1]-north*m.forward[0])*m.zoom+m.offset[0])*16),
    Math.round((-(east*m.forward[0]+north*m.forward[1])*m.zoom+m.offset[1])*16)];
}
function publish() {
  var p=context(); p.GpsState=state;
  if (!courseId) {p.CourseList=courseList(); send(p); return;}
  p.Offline=offlineState(courseId);
  var course=courses[courseId];
  if (!course) {
    if (loadError) p.CourseInfo='!'+loadError;
    send(p); return;
  }
  var h=course.holes[Math.min(hole,course.holes.length)-1];
  if (infoSent!==courseId) p.CourseInfo=courseInfo(course);
  if (mapSent!==courseId+':'+hole) p.MapData=hexBytes(h.mapHex);
  if (fix && fixAge()<=MAX_AGE) {
    var position=[fix.coords.latitude,fix.coords.longitude];
    var d=geo.yardages(position,h), factor=units?1:1/0.9144;
    var l=geo.layup(position,h,target/factor);
    p.Center=Math.round(d.center*factor);
    p.Front=d.front===null?-1:Math.round(d.front*factor);
    p.Back=d.back===null?-1:Math.round(d.back*factor);
    p.GreenEdges=d.frontPoint && d.backPoint?mapPoint(d.frontPoint,h.map).concat(mapPoint(d.backPoint,h.map)).join(','):'';
    p.Accuracy=Math.round(fix.coords.accuracy); p.FixAge=Math.floor(fixAge()/1000); p.FixTime=Math.floor(fix.timestamp/1000);
    var player=mapPoint(position,h.map); p.MapX=player[0]; p.MapY=player[1];
    p.Par=h.par; p.LayupShot=l?Math.round(l.shot*factor):-1;
    var layupPoint=l?mapPoint(l.target,h.map):[-1000,-1000]; p.LayupX=layupPoint[0]; p.LayupY=layupPoint[1];
    // Passed hazards are left out so the hazard view zooms in as the player advances.
    p.Hazards=geo.hazardDistances(position,h).slice(0,8).map(function(x) {
      var near=mapPoint(x.reachPoint,h.map),far=mapPoint(x.carryPoint,h.map);
      return x.name+','+Math.round(x.reach*factor)+','+Math.round(x.carry*factor)+','+x.osmId+','+near[0]+','+near[1]+','+far[0]+','+far[1];
    }).join('|');
  } else if (state===0) p.GpsState=5;
  send(p);
}
function accept(p) {
  var c=p && p.coords;
  if (!c || !isFinite(c.latitude) || Math.abs(c.latitude)>90 || !isFinite(c.longitude) || Math.abs(c.longitude)>180 || !isFinite(c.accuracy) || c.accuracy<0 || !isFinite(p.timestamp) || p.timestamp>Date.now()+5000 || Date.now()-p.timestamp>MAX_AGE) {
    state=5; publish(); return;
  }
  // A slower one-shot response must not overwrite a newer subscription fix.
  if (fix && p.timestamp<fix.timestamp) return;
  fix=p; state=0; permissionDenied=false; publish();
}
function locationError(e) {
  if (e.code===1) {permissionDenied=true; state=2; fix=null;}
  else state=e.code===3?4:3;
  publish();
}
function startWatch(restart) {
  if (watchId!==null && !restart) return;
  if (watchId!==null) navigator.geolocation.clearWatch(watchId);
  lastWatchRestart=Date.now();
  watchId=navigator.geolocation.watchPosition(accept,locationError,options);
}
function refresh(manual) {
  if (refreshing || (permissionDenied && !manual)) return;
  if (manual) permissionDenied=false;
  refreshing=true; lastAttempt=Date.now(); state=1; publish();
  var token=++requestToken;
  // Recover even if a mobile geolocation implementation never calls either callback.
  refreshTimer=setTimeout(function() {
    if (token!==requestToken) return;
    refreshing=false; refreshTimer=null; requestToken++; state=4; publish();
  },20000);
  navigator.geolocation.getCurrentPosition(function(p) {
    if (token!==requestToken) return;
    clearTimeout(refreshTimer); refreshTimer=null; refreshing=false; accept(p);
  },function(e) {
    if (token!==requestToken) return;
    clearTimeout(refreshTimer); refreshTimer=null; refreshing=false; locationError(e);
  },options);
}
function heartbeat() {
  if (!active) return;
  if (courseId) loadCourse(courseId);
  if (!permissionDenied && fixAge()>12000 && Date.now()-lastAttempt>=15000) {
    if (Date.now()-lastWatchRestart>=30000) startWatch(true);
    refresh(false);
  } else publish();
}
Pebble.addEventListener('ready',function() {
  send({Ready:1});
  get('index.json',function(list) {
    if (list.length) {store('catalog',list); setCatalog(list); publish();}
  },function() {});
});
Pebble.addEventListener('appmessage',function(e) {
  var p=e.payload;
  if (p.Course!==undefined && String(p.Course)!==courseId) {
    if (courseId!==builtIn.id) delete courses[courseId];
    courseId=String(p.Course); infoSent=mapSent=''; loadError='';
    if (courseId) store('last',courseId);
  }
  if (courseId) loadCourse(courseId);
  if (p.Download===1 && p.Course!==undefined && String(p.Course)===courseId && courseId) saveCourse(courseId);
  if (p.Hole!==undefined) {
    var nextHole=Math.max(1,Math.min(18,Math.floor(Number(p.Hole)||1)));
    if (nextHole!==hole) mapSent='';
    hole=nextHole;
  }
  if (p.Units!==undefined) units=Number(p.Units)?1:0;
  if (p.Revision!==undefined) revision=Number(p.Revision)||0;
  if (p.LayupTarget!==undefined) target=Math.max(50,Math.min(200,Number(p.LayupTarget)||100));
  if (!active) {active=true; setInterval(heartbeat,5000);}
  startWatch(false);
  publish();
  // Browsing pages/holes can reuse a recent fix; explicit refresh always asks GPS.
  if (p.Request===1) refresh(true);
  else if (fixAge()>12000 && !permissionDenied) refresh(false);
  else publish();
});

Pebble.addEventListener('showConfiguration',function() {
  var saved={}, failed=[];
  catalog.forEach(function(c) {offlineState(c.id);});
  try {
    for (var i=0;i<localStorage.length;i++) {
      var key=localStorage.key(i);
      if (key.indexOf('saved:')===0) {var version=stored(key); if (version) saved[key.slice(6)]=version;}
    }
  } catch (e) {}
  Object.keys(saveFailed).forEach(function(id) {if (saveFailed[id]) failed.push(id);});
  Pebble.openURL(COURSES_URL+'config.html#'+encodeURIComponent(JSON.stringify({saved:saved,failed:failed,current:courseId,builtIn:builtIn.id})));
});
Pebble.addEventListener('webviewclosed',function(e) {
  var data;
  try {data=JSON.parse(decodeURIComponent(e.response));} catch (error) {return;}
  if (!data || typeof data!=='object') return;
  (Array.isArray(data.remove)?data.remove:[]).forEach(function(id) {
    if (id===courseId || id===builtIn.id) return;
    remove('course:'+id); remove('saved:'+id); failedSave(id,false); delete courses[id];
  });
  (Array.isArray(data.save)?data.save:[]).forEach(function(id) {
    if (typeof id==='string' && catalog.some(function(c) {return c.id===id;}) && saveQueue.indexOf(id)<0) saveQueue.push(id);
  });
  runQueue(); publish();
});
