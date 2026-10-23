#pragma once
// Coordinates use 1/16 of an overview-map pixel to retain close-up detail.
// MAP_W x MAP_H is the watch map layer; tall to suit tee-at-bottom hole maps.
#define MAP_W 160
#define MAP_H 172
typedef struct { float zoom, x, y; } MapCamera;
static inline float camera_min(float a,float b) {return a<b?a:b;}
static inline float camera_max(float a,float b) {return a>b?a:b;}

// A player well outside the hole (walking from the previous green) should not
// shrink the hole into a corner; the map points to them from the edge instead.
static inline int map_player_near(int px,int py,int minx,int miny,int maxx,int maxy) {
  int pad=(int)camera_max(maxx-minx,maxy-miny)/4;
  return px>=minx-pad && px<=maxx+pad && py>=miny-pad && py<=maxy+pad;
}

// Frame the actual hole at the tee; progressively focus on the approach.
static inline MapCamera map_camera_hole(int px,int py,int tx,int ty,int minx,int miny,int maxx,int maxy,float meters,int live) {
  if (!live || meters>=220) {
    minx=(int)camera_min(minx,tx); maxx=(int)camera_max(maxx,tx);
    miny=(int)camera_min(miny,ty); maxy=(int)camera_max(maxy,ty);
  }
  if (live) {
    minx=(int)camera_min(minx,px); maxx=(int)camera_max(maxx,px);
    miny=(int)camera_min(miny,py); maxy=(int)camera_max(maxy,py);
  }
  float fit=camera_min((MAP_W-24)*16/camera_max(1,maxx-minx),(MAP_H-28)*16/camera_max(1,maxy-miny));
  float cap=live && meters<220?camera_min(6,220/camera_max(1,meters)):6;
  MapCamera c={camera_min(cap,fit),(minx+maxx)/2.0f,(miny+maxy)/2.0f};
  return c;
}
