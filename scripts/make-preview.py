"""Create a separate emulator-only PBW with simulated GPS; never modifies the release."""
import json
import argparse
import math
import zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
course=json.loads((root/'src/pkjs/course.json').read_text())
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--distance-yards',type=float,help='Fixed approach distance along hole 1 playing line')
parser.add_argument('--fixed-hole',type=int,choices=range(1,19),help='Hold GPS at the mapped starting tee of this hole')
parser.add_argument('--white-first-tee',action='store_true',help='Hold GPS at an approximate hole 1 white tee position')
parser.add_argument('--walk',action='store_true',help='Cycle fake GPS from the tee to 10 yards from the selected green')
parser.add_argument('--step-seconds',type=int,default=12,help='Seconds spent at each fake position')
args=parser.parse_args()
def approach(hole,yards):
    remaining=max(0,yards)*0.9144
    path=hole['path'][:-1]+[hole['center']]
    for a,b in reversed(list(zip(path,path[1:]))):
        length=math.hypot((a[0]-b[0])*111195,(a[1]-b[1])*111195*math.cos(math.radians(b[0])))
        if remaining<=length:
            f=remaining/length if length else 0
            return [b[0]+(a[0]-b[0])*f,b[1]+(a[1]-b[1])*f]
        remaining-=length
    return hole['tee']
routes=[[hole['tee']]+[approach(hole,d) for d in [300,200,150,100,75,50,25,10]] if args.walk else [approach(hole,args.distance_yards)] if args.distance_yards is not None else [hole['tee']] for hole in course['holes']]
if args.white_first_tee:
    if args.walk or args.distance_yards is not None: parser.error('--white-first-tee cannot be combined with --walk or --distance-yards')
    # White hole 1 is published as 559 yards. This is a demo coordinate, not a surveyed marker.
    h=course['holes'][0]; g=h['center']; t=h['tee']
    distance=math.hypot((t[0]-g[0])*111195,(t[1]-g[1])*111195*math.cos(math.radians(g[0])))
    ratio=559*0.9144/distance
    white=[g[0]+(t[0]-g[0])*ratio,g[1]+(t[1]-g[1])*ratio]
    routes=[[white] for _ in course['holes']]
if args.fixed_hole is not None:
    if args.walk or args.distance_yards is not None or args.white_first_tee: parser.error('--fixed-hole cannot be combined with other position modes')
    routes=[[course['holes'][args.fixed_hole-1]['tee']] for _ in course['holes']]
prefix='''// EMULATOR PREVIEW ONLY: simulated positions follow the manually selected hole.
(function() {
  var originalSend=Pebble.sendAppMessage;
  Pebble.sendAppMessage=function(data,ok,error) {
    if (data.Center!==undefined) data.GpsState=6;
    return originalSend.call(Pebble,data,ok,error);
  };
  var routes=ROUTES, holeIndex=0, started=Date.now(), watchers={};
  Pebble.addEventListener('appmessage',function(event) {
    if (event.payload.Hole!==undefined) {
      var next=Math.max(0,Math.min(17,Number(event.payload.Hole)-1));
      if (next!==holeIndex) {
        holeIndex=next; started=Date.now();
        setTimeout(function() { for (var id in watchers) watchers[id](sample()); },100);
      }
    }
  });
  function sample() {
    var positions=routes[holeIndex];
    var p=positions[Math.floor((Date.now()-started)/STEP_MS)%positions.length];
    return {timestamp:Date.now(),coords:{latitude:p[0],longitude:p[1],accuracy:5}};
  }
  navigator.geolocation.getCurrentPosition=function(ok) { setTimeout(function(){ok(sample());},100); };
  navigator.geolocation.watchPosition=function(ok) {
    var id=setInterval(function(){ok(sample());},5000); watchers[id]=ok; return id;
  };
  navigator.geolocation.clearWatch=function(id) { clearInterval(id); delete watchers[id]; };
})();
'''.replace('ROUTES',json.dumps(routes)).replace('STEP_MS',str(max(5,args.step_seconds)*1000)).encode()
out=Path('/tmp/pebble-golf-preview.pbw')
with zipfile.ZipFile(root/'build/pebble-golf.pbw') as src, zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as dst:
    for name in src.namelist():
        data=src.read(name)
        if name=='pebble-js-app.js': data=prefix+data
        dst.writestr(name,data)
print(out)
