#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../src/c/map-camera.h"
static void inside(MapCamera c,int px,int py) {
  float x=(px-c.x)*c.zoom/16+90,y=(py-c.y)*c.zoom/16+58;
  assert(x>=9.9f && x<=170.1f && y>=9.9f && y<=106.1f);
}
int main(void) {
  MapCamera far=map_camera_plan(2080,3200,2000,1550,2160,1650,300,1);
  MapCamera approach=map_camera_plan(2080,1920,2000,1550,2160,1650,100,1);
  MapCamera close=map_camera_plan(2090,1680,2000,1550,2160,1650,20,1);
  assert(far.zoom==1); assert(approach.zoom>far.zoom); assert(close.zoom>approach.zoom && close.zoom<=6);
  inside(approach,2080,1920); inside(approach,2000,1550); inside(approach,2160,1650);
  inside(close,2090,1680); inside(close,2000,1550); inside(close,2160,1650);
  MapCamera stale=map_camera_plan(2090,1680,2000,1550,2160,1650,20,0);
  assert(stale.zoom==1);
  MapCamera large=map_camera_plan(2200,3000,1600,1500,2600,2500,10,1);
  assert(large.zoom<2); inside(large,2200,3000); inside(large,1600,1500); inside(large,2600,2500);
  MapCamera on_green=map_camera_plan(2080,1600,2000,1550,2160,1650,0,1);
  assert(on_green.zoom==6); inside(on_green,2080,1600);
  // Hole 8 previously placed the tee below the fixed viewport.
  MapCamera overview=map_camera_hole(2280,3299,2280,3299,2230,1597,2345,1750,400,1);
  assert((3299-overview.y)*overview.zoom/16+75<=137.1f);
  assert((1597-overview.y)*overview.zoom/16+75>=12.9f);
  assert(overview.zoom>1);
  MapCamera offline=map_camera_hole(-1000,-1000,2280,3299,2230,1597,2345,1750,0,0);
  assert(fabsf(offline.zoom-overview.zoom)<0.001f);
  assert(fabsf(offline.y-overview.y)<0.001f);
  MapCamera new_close=map_camera_hole(2280,1800,2280,3299,2230,1597,2345,1750,20,1);
  assert(new_close.zoom>overview.zoom && new_close.zoom<=6);
  puts("Camera tests passed: approach zoom, player/green framing, maximum zoom, large greens and stale fallback.");
}
