var R = 6371008.8;
function distance(a, b) {
  var rad = Math.PI / 180;
  var dlat = (b[0] - a[0]) * rad, dlon = (b[1] - a[1]) * rad;
  var h = Math.pow(Math.sin(dlat / 2), 2) + Math.cos(a[0] * rad) * Math.cos(b[0] * rad) * Math.pow(Math.sin(dlon / 2), 2);
  return 2 * R * Math.asin(Math.sqrt(Math.min(1, h)));
}
// Intersect the player's ray through the green centroid with the green boundary.
// Return no front/back value on the green or when there is no forward crossing.
function yardages(position, hole) {
  var scale = Math.PI / 180 * R, cos = Math.cos(position[0] * Math.PI / 180);
  function project(p) { return [(p[1] - position[1]) * scale * cos, (p[0] - position[0]) * scale]; }
  var c = project(hole.center), len = Math.sqrt(c[0]*c[0] + c[1]*c[1]);
  var result = { center: distance(position, hole.center), front: null, back: null, frontPoint:null, backPoint:null };
  if (len < 0.01) return result;
  var dx = c[0] / len, dy = c[1] / len, hits = [];
  for (var i = 0; i < hole.green.length - 1; i++) {
    var a = project(hole.green[i]), b = project(hole.green[i + 1]);
    var ex = b[0] - a[0], ey = b[1] - a[1], den = dx * ey - dy * ex;
    if (Math.abs(den) < 1e-9) continue;
    var t = (a[0] * ey - a[1] * ex) / den;
    var u = (a[0] * dy - a[1] * dx) / den;
    if (u >= 0 && u <= 1) hits.push(t);
  }
  hits.sort(function(a,b) { return a-b; });
  // For concave greens use the boundary crossings enclosing the centroid.
  var before = hits.filter(function(t) { return t <= len; });
  var after = hits.filter(function(t) { return t >= len; });
  if (before.length && after.length && before[before.length-1] > 0) {
    result.front = before[before.length-1]; result.back = after[0];
    var inverse=projection(position);
    result.frontPoint=inverse.point([dx*result.front,dy*result.front]);
    result.backPoint=inverse.point([dx*result.back,dy*result.back]);
  }
  return result;
}
module.exports = { distance: distance, yardages: yardages };

function projection(origin) {
  var scale = Math.PI / 180 * R, cos = Math.cos(origin[0] * Math.PI / 180);
  return {xy: function(p) {return [(p[1]-origin[1])*scale*cos,(p[0]-origin[0])*scale];},
    point: function(p) {return [origin[0]+p[1]/scale,origin[1]+p[0]/(scale*cos)];}};
}
function pointInside(p, ring) {
  var inside = false;
  for (var i=0,j=ring.length-1;i<ring.length;j=i++) {
    var a=ring[i], b=ring[j];
    if ((a[0]>p[0]) !== (b[0]>p[0]) && p[1]<(b[1]-a[1])*(p[0]-a[0])/(b[0]-a[0])+a[1]) inside=!inside;
  }
  return inside;
}
function hazardDistances(position, hole, includeBehind) {
  var project=projection(position), green=project.xy(hole.center);
  return (hole.hazards || []).map(function(h) {
    var center=project.xy(h.center), length=Math.sqrt(center[0]*center[0]+center[1]*center[1]);
    if (!length) {center=green[0] || green[1]?green:[1,0]; length=Math.sqrt(center[0]*center[0]+center[1]*center[1]);}
    // Hide hazards behind the player relative to the selected green.
    if (!includeBehind && center[0]*green[0]+center[1]*green[1] <= 0) return null;
    var dx=center[0]/length, dy=center[1]/length, hits=[];
    for (var i=0;i<h.polygon.length-1;i++) {
      var a=project.xy(h.polygon[i]), b=project.xy(h.polygon[i+1]);
      var ex=b[0]-a[0], ey=b[1]-a[1], den=dx*ey-dy*ex;
      if (Math.abs(den)<1e-9) continue;
      var t=(a[0]*ey-a[1]*ex)/den, u=(a[0]*dy-a[1]*dx)/den;
      if (t>=0 && u>=0 && u<=1) hits.push(t);
    }
    if (!hits.length) return null;
    hits.sort(function(a,b) {return a-b;});
    // Carry includes the last forward crossing of a concave polygon on this bearing.
    var reach=pointInside(position,h.polygon)?0:hits[0], carry=hits[hits.length-1];
    var cross=green[0]*center[1]-green[1]*center[0];
    var side=Math.abs(cross)/Math.max(1,Math.sqrt(green[0]*green[0]+green[1]*green[1]))<8?'AHEAD':cross>0?'LEFT':'RIGHT';
    return {name:h.kind+' '+side,reach:reach,carry:carry,osmId:h.osmId,reachPoint:project.point([dx*reach,dy*reach]),carryPoint:project.point([dx*carry,dy*carry])};
  }).filter(function(h) {return h!==null;}).sort(function(a,b) {return a.reach-b.reach;});
}
// Find the point on the mapped playing line whose straight-line approach to the
// middle equals the preferred remaining distance. This handles doglegs without
// presenting middle-minus-target as the distance of the layup shot.
function layup(position, hole, remaining) {
  if (!(remaining>0) || distance(position,hole.center)<=remaining) return null;
  var project=projection(hole.center), path=(hole.path || [hole.tee,hole.center]).slice();
  path[path.length-1]=hole.center;
  for (var i=path.length-2;i>=0;i--) {
    var a=project.xy(path[i]), b=project.xy(path[i+1]);
    var dx=a[0]-b[0], dy=a[1]-b[1], aa=dx*dx+dy*dy;
    if (aa<1e-6) continue;
    var bb=2*(b[0]*dx+b[1]*dy), cc=b[0]*b[0]+b[1]*b[1]-remaining*remaining;
    var disc=bb*bb-4*aa*cc;
    if (disc<0) continue;
    var roots=[(-bb-Math.sqrt(disc))/(2*aa),(-bb+Math.sqrt(disc))/(2*aa)];
    for (var j=0;j<roots.length;j++) {
      var t=roots[j];
      if (t<0 || t>1) continue;
      var target=project.point([b[0]+t*dx,b[1]+t*dy]);
      // A target should advance the player toward the green, not send them back.
      var pg=projection(position), toward=pg.xy(hole.center), shot=pg.xy(target);
      if (shot[0]*toward[0]+shot[1]*toward[1]<=0) return null;
      return {shot:distance(position,target),target:target,remaining:remaining};
    }
  }
  return null;
}
module.exports.hazardDistances=hazardDistances;
module.exports.layup=layup;
