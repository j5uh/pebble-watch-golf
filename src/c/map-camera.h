#pragma once
// Coordinates use 1/16 of an overview-map pixel to retain close-up detail.
typedef struct { float zoom, x, y; } MapCamera;
static inline float camera_min(float a,float b) {return a<b?a:b;}
static inline float camera_max(float a,float b) {return a>b?a:b;}
static inline MapCamera map_camera_plan(int px,int py,int minx,int miny,int maxx,int maxy,float meters,int live) {
  MapCamera c={1,130*16,152*16};
  if (!live || meters>=220) return c;
  minx=(int)camera_min(minx,px); maxx=(int)camera_max(maxx,px);
  miny=(int)camera_min(miny,py); maxy=(int)camera_max(maxy,py);
  float cap=camera_min(6,220/camera_max(1,meters));
  float fit=camera_min(160*16/camera_max(1,maxx-minx),96*16/camera_max(1,maxy-miny));
  c.zoom=camera_max(1,camera_min(cap,fit));
  if (c.zoom<=1.01f) {c.zoom=1; return c;}
  float blend=camera_min(1,(c.zoom-1)/0.4f);
  c.x+=((minx+maxx)/2.0f-c.x)*blend;
  c.y+=((miny+maxy)/2.0f-c.y)*blend;
  // Keep the full green and the player inside a padded viewport while panning.
  float hx=80*16/c.zoom, hy=48*16/c.zoom;
  c.x=camera_max(maxx-hx,camera_min(c.x,minx+hx));
  c.y=camera_max(maxy-hy,camera_min(c.y,miny+hy));
  return c;
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
  float fit=camera_min(176*16/camera_max(1,maxx-minx),122*16/camera_max(1,maxy-miny));
  float cap=live && meters<220?camera_min(6,220/camera_max(1,meters)):6;
  MapCamera c={camera_min(cap,fit),(minx+maxx)/2.0f,(miny+maxy)/2.0f};
  return c;
}
