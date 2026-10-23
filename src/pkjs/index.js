var course = require('./course.json');
var geo = require('./distance');
var hole=1, units=0, target=100, revision=0;
var fix=null, watchId=null, active=false, permissionDenied=false;
var pending=null, sending=false, retryTimer=null, failures=0;
var state=1, refreshing=false, requestToken=0, refreshTimer=null, lastAttempt=0, lastWatchRestart=0;
var options={enableHighAccuracy:true,maximumAge:0,timeout:15000};
var MAX_AGE=30000;
function context() {return {Hole:hole,Units:units,Revision:revision,LayupTarget:target};}
function flush() {
  if (sending || !pending || retryTimer!==null) return;
  var payload=pending; pending=null; sending=true;
  Pebble.sendAppMessage(payload,function() {
    sending=false; failures=0; flush();
  },function() {
    sending=false;
    // Never retry a response for a hole/settings revision the user has left.
    if (!pending && (payload.Ready || payload.Revision===revision)) pending=payload;
    failures++;
    retryTimer=setTimeout(function() {retryTimer=null; if (active) publish(); else flush();},Math.min(10000,1000*Math.pow(2,Math.min(3,failures-1))));
  });
}
function send(p) {pending=p; flush();}
function fixAge() {return fix ? Math.max(0,Date.now()-fix.timestamp) : Infinity;}
function mapPoint(position,m) {
  var east=(position[1]-m.origin[1])*m.eastScale,north=(position[0]-m.origin[0])*m.northScale;
  return [Math.round(((east*m.forward[1]-north*m.forward[0])*m.zoom+m.offset[0])*16),
    Math.round((-(east*m.forward[0]+north*m.forward[1])*m.zoom+m.offset[1])*16)];
}
function publish() {
  var p=context(); p.GpsState=state;
  if (fix && fixAge()<=MAX_AGE) {
    var position=[fix.coords.latitude,fix.coords.longitude], h=course.holes[hole-1];
    var d=geo.yardages(position,h), factor=units?1:1/0.9144;
    var l=geo.layup(position,h,target/factor);
    p.Center=Math.round(d.center*factor);
    p.Front=d.front===null?-1:Math.round(d.front*factor);
    p.Back=d.back===null?-1:Math.round(d.back*factor);
    p.GreenEdges=d.frontPoint && d.backPoint?mapPoint(d.frontPoint,h.map).concat(mapPoint(d.backPoint,h.map)).join(','):'';
    p.Accuracy=Math.round(fix.coords.accuracy); p.FixAge=Math.floor(fixAge()/1000); p.FixTime=Math.floor(fix.timestamp/1000);
    var m=h.map, east=(position[1]-m.origin[1])*m.eastScale, north=(position[0]-m.origin[0])*m.northScale;
    p.MapX=Math.round((east*m.forward[1]-north*m.forward[0])*m.zoom*16+m.offset[0]*16);
    p.MapY=Math.round(-(east*m.forward[0]+north*m.forward[1])*m.zoom*16+m.offset[1]*16);
    p.Par=h.par; p.LayupShot=l?Math.round(l.shot*factor):-1;
    p.Hazards=geo.hazardDistances(position,h,true).slice(0,8).map(function(x) {
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
  if (!permissionDenied && fixAge()>12000 && Date.now()-lastAttempt>=15000) {
    if (Date.now()-lastWatchRestart>=30000) startWatch(true);
    refresh(false);
  } else publish();
}
Pebble.addEventListener('ready',function() {send({Ready:1});});
Pebble.addEventListener('appmessage',function(e) {
  var p=e.payload;
  if (p.Hole!==undefined) hole=Math.max(1,Math.min(18,Math.floor(Number(p.Hole)||1)));
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
