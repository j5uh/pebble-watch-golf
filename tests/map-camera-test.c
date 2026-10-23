#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../src/c/map-camera.h"
static void inside(MapCamera c,int px,int py) {
  float x=(px-c.x)*c.zoom/16+MAP_W/2,y=(py-c.y)*c.zoom/16+MAP_H/2;
  assert(x>=11.9f && x<=MAP_W-11.9f && y>=13.9f && y<=MAP_H-13.9f);
}
int main(void) {
  // Hole 8 previously placed the tee below the fixed viewport.
  MapCamera overview=map_camera_hole(2280,3299,2280,3299,2230,1597,2345,1750,400,1);
  inside(overview,2280,3299); inside(overview,2230,1597); inside(overview,2345,1750);
  assert(overview.zoom>1);
  MapCamera offline=map_camera_hole(-1000,-1000,2280,3299,2230,1597,2345,1750,0,0);
  assert(fabsf(offline.zoom-overview.zoom)<0.001f);
  assert(fabsf(offline.y-overview.y)<0.001f);
  MapCamera approach=map_camera_hole(2280,1900,2280,3299,2230,1597,2345,1750,100,1);
  MapCamera close=map_camera_hole(2280,1800,2280,3299,2230,1597,2345,1750,20,1);
  assert(approach.zoom>overview.zoom && close.zoom>approach.zoom && close.zoom<=6);
  inside(approach,2280,1900); inside(approach,2230,1597); inside(approach,2345,1750);
  inside(close,2280,1800);
  // Near the tee or the green counts as on the hole; another hole does not.
  assert(map_player_near(2280,3299,2230,1597,2345,3299));
  assert(map_player_near(2300,1550,2230,1597,2345,3299));
  assert(!map_player_near(4000,3299,2230,1597,2345,3299));
  puts("Camera tests passed: hole overview, approach zoom, maximum zoom, stale fallback and off-hole detection.");
}
