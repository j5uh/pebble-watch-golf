const assert = require('node:assert/strict');
const geo = require('../src/pkjs/distance');
const course = require('../src/pkjs/course.json');
// Every imported course must give valid green, hazard and layup geometry on every hole.
const fs = require('node:fs');
const all = fs.readdirSync('courses').filter(f=>f.endsWith('.json')&&f!=='index.json').map(f=>JSON.parse(fs.readFileSync('courses/'+f,'utf8')));
assert.equal(geo.distance([0,0], [0,0]), 0);
assert.ok(Math.abs(geo.distance([0,0], [0,1]) - 111195.08) < 1);
const square = {center:[0,0], green:[[-0.001,-0.001],[-0.001,0.001],[0.001,0.001],[0.001,-0.001],[-0.001,-0.001]]};
const d = geo.yardages([0,-0.003],square);
assert.ok(Math.abs(d.front - 222.39) < 0.1);
assert.ok(Math.abs(d.back - 444.78) < 0.1);
assert.equal(geo.yardages([0,-0.0001],square).front,null);
assert.equal(geo.yardages([0,0],square).back,null);
assert.equal(course.holes.length,18);
assert.equal(course.holes.reduce((sum,h)=>sum+h.par,0),70);
for (const h of all.flatMap(c=>c.holes)) {
  const d = geo.yardages(h.tee,h);
  assert.ok(d.front > 0 && d.front < d.center && d.center < d.back, `hole ${h.number}: valid crossings`);
}
console.log('Distance calculations and every hole on every course passed.');
const hazard={osmId:'test',kind:'WATER',center:[0,0],polygon:square.green};
const ahead={center:[0,0.005],hazards:[hazard]};
const hazardResult=geo.hazardDistances([0,-0.003],ahead)[0];
assert.ok(Math.abs(hazardResult.reach-222.39)<0.1);
assert.ok(Math.abs(hazardResult.carry-444.78)<0.1);
assert.equal(geo.hazardDistances([0,0.003],ahead).length,0,'hide hazard behind player');
assert.equal(geo.hazardDistances([0,-0.0001],ahead)[0].reach,0,'inside hazard');
const dogleg={tee:[0,0],center:[0.003,0.003],path:[[0,0],[0,0.003],[0.003,0.003]]};
const layup=geo.layup([0,0],dogleg,100);
assert.ok(Math.abs(geo.distance(layup.target,dogleg.center)-100)<0.1);
assert.ok(layup.shot>geo.distance([0,0],dogleg.center)-100+20,'dogleg uses target location, not middle minus preferred distance');
assert.equal(geo.layup(dogleg.center,dogleg,100),null,'no layup when already inside approach distance');
for(const hole of all.flatMap(c=>c.holes)) {
  for(const hazard of geo.hazardDistances(hole.tee,hole)) assert.ok(hazard.reach>=0 && hazard.carry>=hazard.reach);
  const shot=geo.layup(hole.tee,hole,91.44);assert.ok(shot && Math.abs(geo.distance(shot.target,hole.center)-91.44)<0.2,`hole ${hole.number} layup`);
}
console.log('Hazard edge distances, behind/inside handling, dogleg layups and all hole targets on every course passed.');

const allHazards=geo.hazardDistances([0,0.003],ahead,true);
assert.equal(allHazards.length,1,'overview includes mapped hazards behind the player');
for (const result of geo.hazardDistances([0,-0.003],ahead,true)) {
  assert.ok(Math.abs(geo.distance([0,-0.003],result.reachPoint)-result.reach)<0.1);
  assert.ok(Math.abs(geo.distance([0,-0.003],result.carryPoint)-result.carry)<0.1);
}
console.log('Overview edge-anchor points match the reach/carry measurements.');

for (const h of all.flatMap(c=>c.holes)) {
  const d=geo.yardages(h.tee,h);
  assert.ok(Math.abs(geo.distance(h.tee,d.frontPoint)-d.front)<0.2);
  assert.ok(Math.abs(geo.distance(h.tee,d.backPoint)-d.back)<0.2);
}
console.log('Green edge-marker coordinates match the front/back yardages on every hole.');
