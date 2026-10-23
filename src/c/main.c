#include <pebble.h>
#include "course.h"
#include "map.h"
#include "map-camera.h"
#include "hazard-leaders.h"

#define MAX_HAZARDS 8
#define MAX_FIX_AGE 30
typedef enum { SCREEN_TEES, SCREEN_PLAY, SCREEN_MENU, SCREEN_TARGET } Screen;
typedef struct { char name[24]; int reach, carry; uint32_t osm_id; GPoint near_edge, far_edge; } Hazard;
static Window *window, *about;
static TextLayer *credits;
static Layer *canvas, *map_canvas, *hazard_canvas;
static const char *main_font=FONT_KEY_GOTHIC_28_BOLD;
static GColor status_color;
static Screen screen=SCREEN_TEES;
static int hole=1, units=0, tee=1, target=100, revision=1, page=0, hazard_index=0, menu_index=0;
static int middle=-1, front=-1, back=-1, accuracy=0, age_at_receive=0, gps_state=1, layup_shot=-1;
static Hazard hazards[MAX_HAZARDS];
static int hazard_count=0, map_x=-1000, map_y=-1000;
static GPoint green_front,green_back;
static bool green_edges_valid=false;
static bool ready=false, valid=false, round_started=false, phone_connected=true;
static time_t received=0, last_request=0;
static const char *tee_names[]={"BLUE","WHITE","GOLD","RED"};
static const char *menu_names[]={"Refresh GPS","Change tee","Layup distance","Yards / meters","New round","Data credits","Back to play"};
static const char *menu_help[]={"Request a fresh reading","Choose your tee box","Preferred approach left","Switch distance units","Choose tee, start hole 1","OpenStreetMap / ODbL","Return to yardages"};
static char heading_text[48], hole_text[48], context_text[48], main_text[32], caption_text[64];
static char left_text[24], right_text[24], left_cap[32], right_cap[32], status_text[64];

static int fix_age(void) { return age_at_receive+(int)(time(NULL)-received); }
static bool usable(void) { return valid && phone_connected && fix_age()<=MAX_FIX_AGE; }
static void number(char *buffer, size_t size, int value, bool show) {
  if (!show || value<0) snprintf(buffer,size,"--"); else snprintf(buffer,size,"%d",value);
}
static void render(void) {
  bool live=usable();
  const char *unit=units?"METERS":"YARDS";
  snprintf(heading_text,sizeof(heading_text),"SHARPSTOWN");
  left_text[0]=right_text[0]=left_cap[0]=right_cap[0]='\0';
  main_font=FONT_KEY_GOTHIC_28_BOLD;
  if (screen==SCREEN_TEES) {
    snprintf(hole_text,sizeof(hole_text),"SELECT TEE BOX");
    snprintf(context_text,sizeof(context_text),"UP / DOWN TO CHOOSE");
    snprintf(main_text,sizeof(main_text),"%s",tee_names[tee]);
    snprintf(caption_text,sizeof(caption_text),round_started?"SELECT TO CONFIRM":"SELECT TO START ROUND");
    int total=units?(COURSE_TEE_YARDS[tee]*9144+5000)/10000:COURSE_TEE_YARDS[tee];
    snprintf(left_text,sizeof(left_text),"%d,%03d",total/1000,total%1000);
    snprintf(left_cap,sizeof(left_cap),"TOTAL %s",unit);
    snprintf(right_text,sizeof(right_text),"70"); snprintf(right_cap,sizeof(right_cap),"PAR / 18 HOLES");
    snprintf(status_text,sizeof(status_text),"HOLD SELECT: SETTINGS");
  } else if (screen==SCREEN_TARGET) {
    snprintf(hole_text,sizeof(hole_text),"LAYUP PREFERENCE");
    snprintf(context_text,sizeof(context_text),"UP / DOWN: 25 %s",unit);
    snprintf(main_text,sizeof(main_text),"%d",target);
    main_font=FONT_KEY_ROBOTO_BOLD_SUBSET_49;
    snprintf(caption_text,sizeof(caption_text),"%s LEFT TO MIDDLE",unit);
    snprintf(status_text,sizeof(status_text),"SELECT TO SAVE");
  } else if (screen==SCREEN_MENU) {
    snprintf(hole_text,sizeof(hole_text),"ROUND SETTINGS");
    snprintf(context_text,sizeof(context_text),"UP / DOWN TO BROWSE");
    snprintf(main_text,sizeof(main_text),"%s",menu_names[menu_index]);
    // Settings names need room for letters; use 24 rather than the numeric font.
    main_font=FONT_KEY_GOTHIC_24_BOLD;
    snprintf(caption_text,sizeof(caption_text),"%s",menu_help[menu_index]);
    snprintf(left_text,sizeof(left_text),"%d",menu_index+1);
    snprintf(left_cap,sizeof(left_cap),"OF 7");
    snprintf(right_text,sizeof(right_text),"SELECT");
    snprintf(right_cap,sizeof(right_cap),"TO OPEN");
    snprintf(status_text,sizeof(status_text),"BACK TO RETURN");
  } else {
    snprintf(hole_text,sizeof(hole_text),"HOLE %02d  PAR %d",hole,COURSE_PAR[hole-1]);
    context_text[0]='\0';
    if (page==0) {
      snprintf(heading_text,sizeof(heading_text),"SHARPSTOWN / GREEN");
      main_font=FONT_KEY_ROBOTO_BOLD_SUBSET_49;
      number(main_text,sizeof(main_text),middle,live);
      snprintf(caption_text,sizeof(caption_text),"%s TO MIDDLE",unit);
      number(left_text,sizeof(left_text),front,live); number(right_text,sizeof(right_text),back,live);
      snprintf(left_cap,sizeof(left_cap),"FRONT"); snprintf(right_cap,sizeof(right_cap),"BACK");
    } else if (page==1) {
      snprintf(heading_text,sizeof(heading_text),"SHARPSTOWN / HAZARDS");
      context_text[0]='\0';
      main_text[0]=caption_text[0]='\0';
    }
    if (page==2) snprintf(heading_text,sizeof(heading_text),"SHARPSTOWN / MAP");
    if (!phone_connected) snprintf(status_text,sizeof(status_text),"PHONE DISCONNECTED");
    else if (!ready) snprintf(status_text,sizeof(status_text),"OPEN PEBBLE PHONE APP");
    else if (live && gps_state==6) snprintf(status_text,sizeof(status_text),"SIMULATED GPS / %ds",fix_age());
    else if (live) snprintf(status_text,sizeof(status_text),"%s +/- %dm / %ds",accuracy>30?"WEAK GPS":"GPS",accuracy,fix_age());
    else {
      const char *states[]={"WAITING FOR GPS","GETTING PHONE GPS","ALLOW PHONE LOCATION","GPS RETRYING","GPS TIMEOUT - RETRYING","GPS STALE - RETRYING"};
      snprintf(status_text,sizeof(status_text),"%s",states[gps_state>=0 && gps_state<=5?gps_state:1]);
    }
  }
  status_color=screen==SCREEN_PLAY && !live?GColorWindsorTan:GColorDarkGray;
  if (canvas) layer_mark_dirty(canvas);
  if (hazard_canvas) {layer_set_hidden(hazard_canvas,!(screen==SCREEN_PLAY && page==1)); layer_mark_dirty(hazard_canvas);}
  if (map_canvas) {layer_set_hidden(map_canvas,!(screen==SCREEN_PLAY && page==2)); layer_mark_dirty(map_canvas);}
}
static void request(bool fresh) {
  if (!ready || !round_started || !phone_connected) return;
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter)!=APP_MSG_OK) return;
  dict_write_int32(iter,MESSAGE_KEY_Hole,hole); dict_write_int32(iter,MESSAGE_KEY_Units,units);
  dict_write_int32(iter,MESSAGE_KEY_LayupTarget,target); dict_write_int32(iter,MESSAGE_KEY_Revision,revision);
  dict_write_int32(iter,MESSAGE_KEY_Request,fresh?1:0);
  if (app_message_outbox_send()==APP_MSG_OK) last_request=time(NULL);
}
static int integer(DictionaryIterator *iter,uint32_t key,int fallback) {
  Tuple *t=dict_find(iter,key); return t?t->value->int32:fallback;
}
static void parse_hazards(const char *text) {
  char buffer[768]; snprintf(buffer,sizeof(buffer),"%s",text);
  hazard_count=0; char *row=buffer;
  while (row && *row && hazard_count<MAX_HAZARDS) {
    char *next=strchr(row,'|'); if (next) *next++='\0';
    char *first=strchr(row,','), *second=first?strchr(first+1,','):NULL;
    if (first && second) {
      *first='\0'; *second='\0';
      char *id_text=strchr(second+1,',');
      if (id_text) *id_text++='\0';
      int reach=atoi(first+1), carry=atoi(second+1);
      if (reach>=0 && carry>=reach) {
        Hazard *h=&hazards[hazard_count++]; snprintf(h->name,sizeof(h->name),"%.23s",row); h->reach=reach; h->carry=carry; h->osm_id=0;
        h->near_edge=GPoint(0,0); h->far_edge=GPoint(0,0);
        if (id_text) {
          for (const char *digit=id_text;*digit>='0' && *digit<='9';digit++) h->osm_id=h->osm_id*10+(*digit-'0');
          int values[4]={0,0,0,0}; char *next=strchr(id_text,',');
          for (int i=0;i<4 && next;i++) {values[i]=atoi(next+1); next=strchr(next+1,',');}
          h->near_edge=GPoint(values[0],values[1]); h->far_edge=GPoint(values[2],values[3]);
        }
      }
    }
    row=next;
  }
}
static void inbox(DictionaryIterator *iter,void *context) {
  if (dict_find(iter,MESSAGE_KEY_Ready)) {ready=true; request(true); render(); return;}
  if (integer(iter,MESSAGE_KEY_Hole,-1)!=hole || integer(iter,MESSAGE_KEY_Units,-1)!=units || integer(iter,MESSAGE_KEY_Revision,-1)!=revision) return;
  gps_state=integer(iter,MESSAGE_KEY_GpsState,1);
  if (dict_find(iter,MESSAGE_KEY_Center)) {
    middle=integer(iter,MESSAGE_KEY_Center,-1); front=integer(iter,MESSAGE_KEY_Front,-1); back=integer(iter,MESSAGE_KEY_Back,-1);
    accuracy=integer(iter,MESSAGE_KEY_Accuracy,0); age_at_receive=integer(iter,MESSAGE_KEY_FixAge,MAX_FIX_AGE+1);
    int fix_time=integer(iter,MESSAGE_KEY_FixTime,0);
    if (fix_time>0 && time(NULL)-fix_time>age_at_receive) age_at_receive=(int)(time(NULL)-fix_time);
    Tuple *green_edge_data=dict_find(iter,MESSAGE_KEY_GreenEdges);green_edges_valid=false;
    if (green_edge_data && green_edge_data->length>1) {
      char edge_buffer[96];snprintf(edge_buffer,sizeof(edge_buffer),"%s",green_edge_data->value->cstring);
      int values[4]={0,0,0,0},count=0;char *next=edge_buffer;
      while (next && count<4) {values[count++]=atoi(next);next=strchr(next,',');if(next)next++;}
      if (count==4) {green_front=GPoint(values[0],values[1]);green_back=GPoint(values[2],values[3]);green_edges_valid=true;}
    }
    map_x=integer(iter,MESSAGE_KEY_MapX,-1000); map_y=integer(iter,MESSAGE_KEY_MapY,-1000);
    layup_shot=integer(iter,MESSAGE_KEY_LayupShot,-1);
    Tuple *h=dict_find(iter,MESSAGE_KEY_Hazards); parse_hazards(h?h->value->cstring:"");
    valid=age_at_receive>=0 && age_at_receive<=MAX_FIX_AGE; received=time(NULL);
  } else if (gps_state==2 || gps_state==5) valid=false;
  render();
}
static void failed(DictionaryIterator *iter,AppMessageResult reason,void *context) {last_request=0;}
static void connection(bool connected) {
  phone_connected=connected;
  if (!connected) valid=false; else request(true);
  render();
}
static void tick(struct tm *now,TimeUnits changed) {
  if (round_started && ready && phone_connected && time(NULL)-last_request>=15 && (!valid || fix_age()>12)) request(false);
  if (screen==SCREEN_PLAY && (now->tm_sec%5==0 || (valid && fix_age()==MAX_FIX_AGE+1))) render();
}
static void invalidate(void) {revision++; valid=false; hazard_count=0; page=0; hazard_index=0;}
static void change(int delta) {
  if (screen==SCREEN_TEES) tee=(tee+delta+4)%4;
  else if (screen==SCREEN_TARGET) {target+=delta*25; if (target<50) target=200; if (target>200) target=50;}
  else if (screen==SCREEN_MENU) menu_index=(menu_index+delta+7)%7;
  else {hole=(hole-1+delta+18)%18+1; persist_write_int(1,hole); invalidate(); request(false);}
  render();
}
static void up(ClickRecognizerRef r,void *c) {change(-1);}
static void down(ClickRecognizerRef r,void *c) {change(1);}
static void select_click(ClickRecognizerRef r,void *c) {
  if (screen==SCREEN_TEES) {
    persist_write_int(3,tee);
    if (!round_started) {hole=1; persist_write_int(1,hole);}
    round_started=true; screen=SCREEN_PLAY; invalidate(); request(true);
  } else if (screen==SCREEN_TARGET) {
    persist_write_int(4,target); screen=round_started?SCREEN_PLAY:SCREEN_TEES; invalidate(); request(false);
  } else if (screen==SCREEN_MENU) {
    switch (menu_index) {
      case 0: screen=round_started?SCREEN_PLAY:SCREEN_TEES; request(true); break;
      case 1: screen=SCREEN_TEES; break;
      case 2: screen=SCREEN_TARGET; break;
      case 3: units=!units; persist_write_int(2,units); invalidate(); screen=round_started?SCREEN_PLAY:SCREEN_TEES; request(false); break;
      case 4: round_started=false; screen=SCREEN_TEES; valid=false; page=0; break;
      case 5: window_stack_push(about,true); break;
      default: screen=round_started?SCREEN_PLAY:SCREEN_TEES; break;
    }
  } else if (page==0) {page=1; hazard_index=0;}
  else page=(page+1)%3;
  render();
}
static void settings(ClickRecognizerRef r,void *c) {screen=SCREEN_MENU; menu_index=0; render();}
static void back_click(ClickRecognizerRef r,void *c) {
  if (screen==SCREEN_PLAY || (screen==SCREEN_TEES && !round_started)) window_stack_pop(true);
  else {
    if (screen==SCREEN_TEES) {tee=persist_exists(3)?persist_read_int(3):1;}
    if (screen==SCREEN_TARGET) {target=persist_exists(4)?persist_read_int(4):100;}
    screen=round_started?SCREEN_PLAY:SCREEN_TEES; render();
  }
}
static void clicks(void *c) {
  window_single_click_subscribe(BUTTON_ID_UP,up); window_single_click_subscribe(BUTTON_ID_DOWN,down);
  window_single_click_subscribe(BUTTON_ID_SELECT,select_click); window_single_click_subscribe(BUTTON_ID_BACK,back_click);
  window_long_click_subscribe(BUTTON_ID_SELECT,700,settings,NULL);
}
static void draw_text(GContext *ctx,const char *value,int x,int y,int width,int height,const char *font,GColor color) {
  graphics_context_set_text_color(ctx,color);
  graphics_draw_text(ctx,value,fonts_get_system_font(font),GRect(x,y,width,height),GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
}
// Cache system-font glyphs once per heading, then place their centers on the rim.
static GBitmap *heading_glyphs[48];
static int heading_widths[48], heading_count, heading_width;
static char cached_heading[48];
static void clear_heading(void) {
  for (int i=0;i<heading_count;i++) gbitmap_destroy(heading_glyphs[i]);
  heading_count=0;
}
static void draw_heading(GContext *ctx) {
  if (strcmp(cached_heading,heading_text)!=0) {
    clear_heading(); heading_width=0;
    GFont font=fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    for (int i=0;heading_text[i] && i<47;i++) {
      char glyph[2]={heading_text[i],0};
      int width=heading_text[i]==' '?4:graphics_text_layout_get_content_size(glyph,font,GRect(0,0,40,24),GTextOverflowModeFill,GTextAlignmentLeft).w;
      width+=1;
      GBitmap *bitmap=gbitmap_create_blank(GSize(width,24),GBitmapFormat8Bit);
      if (!bitmap) break;
      graphics_context_set_fill_color(ctx,GColorWhite);
      graphics_fill_rect(ctx,GRect(100,110,40,24),0,GCornerNone);
      graphics_context_set_text_color(ctx,GColorDarkGreen);
      graphics_draw_text(ctx,glyph,font,GRect(100,110,width,24),GTextOverflowModeFill,GTextAlignmentLeft,NULL);
      GBitmap *frame=graphics_capture_frame_buffer(ctx);
      uint8_t *pixels=gbitmap_get_data(bitmap);
      int stride=gbitmap_get_bytes_per_row(bitmap);
      for (int y=0;y<24;y++) {
        GBitmapDataRowInfo row=gbitmap_get_data_row_info(frame,110+y);
        for (int x=0;x<width;x++) {
          uint8_t pixel=row.data[100+x];
          pixels[y*stride+x]=pixel==GColorWhite.argb?GColorClear.argb:pixel;
        }
      }
      graphics_release_frame_buffer(ctx,frame);
      heading_glyphs[heading_count]=bitmap;
      heading_widths[heading_count++]=width; heading_width+=width;
    }
    snprintf(cached_heading,sizeof(cached_heading),"%s",heading_text);
    graphics_context_set_fill_color(ctx,GColorWhite);
    graphics_fill_rect(ctx,GRect(100,110,40,24),0,GCornerNone);
  }
  // Center the entire measured string about twelve o'clock, including spaces.
  int offset=0;
  const int radius=112;
  graphics_context_set_compositing_mode(ctx,GCompOpSet);
  for (int i=0;i<heading_count;i++) {
    int width=heading_widths[i];
    int angle=(2*offset+width-heading_width)*TRIG_MAX_ANGLE/(2*radius*6283/1000);
    GPoint center=GPoint(130+(int)(sin_lookup(angle)*radius/TRIG_MAX_RATIO),
                        130-(int)(cos_lookup(angle)*radius/TRIG_MAX_RATIO));
    graphics_draw_rotated_bitmap(ctx,heading_glyphs[i],GPoint(width/2,12),angle,center);
    offset+=width;
  }
}
static GPoint map_xy(int px,int py,MapCamera c) {
  float x=(px-c.x)*c.zoom/16+100, y=(py-c.y)*c.zoom/16+75;
  // Limit distant geometry to the representable range before viewport clipping.
  return GPoint((int)camera_max(-10000,camera_min(10000,x)),(int)camera_max(-10000,camera_min(10000,y)));
}
static GPoint map_point(GPoint p,MapCamera c) {return map_xy(p.x,p.y,c);}
static bool map_visible(GPoint p) {return p.x>=6 && p.x<=194 && p.y>=6 && p.y<=144;}
static MapCamera camera_for_hole(const HoleMap *map) {
  int minx=map->green.x,maxx=minx,miny=map->green.y,maxy=miny;
  for (int i=0;i<map->count;i++) {
    const MapShape *shape=&MAP_SHAPES[map->start+i];
    if (shape->kind!=1 && !(shape->kind==0 && (!usable() || (units?middle:middle*0.9144f)>=220))) continue;
    for (int j=0;j<shape->count;j++) {
      GPoint p=MAP_POINTS[shape->start+j];
      if (p.x<minx) minx=p.x;
      if (p.x>maxx) maxx=p.x;
      if (p.y<miny) miny=p.y;
      if (p.y>maxy) maxy=p.y;
    }
  }
  return map_camera_hole(map_x,map_y,map->tee.x,map->tee.y,minx,miny,maxx,maxy,units?middle:middle*0.9144f,usable());
}
// Clip geometry before rasterizing; distant polygon vertices can otherwise make
// a tiny local view spend time drawing thousands of off-screen scanlines.
static GPoint *map_clip_polygon(const GPoint *input,int count,int left,int top,int right,int bottom,int *out_count) {
  *out_count=0; int capacity=count*16+16;
  GPoint *a=malloc(capacity*sizeof(GPoint)),*b=malloc(capacity*sizeof(GPoint));
  if (!a || !b) {free(a); free(b); return NULL;}
  memcpy(a,input,count*sizeof(GPoint)); int n=count;
  for (int edge=0;edge<4 && n;edge++) {
    int out=0, bound=edge==0?left:edge==1?right:edge==2?top:bottom;
    GPoint previous=a[n-1];
    bool previous_inside=edge==0?previous.x>=bound:edge==1?previous.x<=bound:edge==2?previous.y>=bound:previous.y<=bound;
    for (int i=0;i<n;i++) {
      GPoint current=a[i];
      bool inside=edge==0?current.x>=bound:edge==1?current.x<=bound:edge==2?current.y>=bound:current.y<=bound;
      if (inside!=previous_inside) {
        float dx=current.x-previous.x,dy=current.y-previous.y;
        GPoint crossing;
        if (edge<2) {crossing.x=bound; crossing.y=(int)(previous.y+dy*(bound-previous.x)/dx);}
        else {crossing.y=bound; crossing.x=(int)(previous.x+dx*(bound-previous.y)/dy);}
        if (out<capacity) b[out++]=crossing;
      }
      if (inside && out<capacity) b[out++]=current;
      previous=current; previous_inside=inside;
    }
    GPoint *swap=a; a=b; b=swap; n=out;
  }
  free(b); *out_count=n; return a;
}
static bool map_inside(int x,int y,const GPoint *p,int count) {
  bool inside=false;
  for (int i=0,j=count-1;i<count;j=i++) {
    if ((p[i].y>y)!=(p[j].y>y) && x<(int32_t)(p[j].x-p[i].x)*(y-p[i].y)/(p[j].y-p[i].y)+p[i].x) inside=!inside;
  }
  return inside;
}
static void map_texture(GContext *ctx,const GPoint *points,int count,int kind) {
  int minx=200,miny=150,maxx=0,maxy=0;
  for (int i=0;i<count;i++) {
    if (points[i].x<minx) minx=points[i].x;
    if (points[i].y<miny) miny=points[i].y;
    if (points[i].x>maxx) maxx=points[i].x;
    if (points[i].y>maxy) maxy=points[i].y;
  }
  minx=(int)camera_max(0,minx); miny=(int)camera_max(0,miny);
  maxx=(int)camera_min(199,maxx); maxy=(int)camera_min(149,maxy);
  graphics_context_set_stroke_width(ctx,1);
  graphics_context_set_stroke_color(ctx,kind==2?GColorWindsorTan:kind==3?GColorWhite:GColorIslamicGreen);
  for (int y=miny;y<=maxy;y++) {
    for (int x=minx;x<=maxx;x++) {
      bool pattern=kind==2?(x%5==(y%10<5?0:2) && y%5==0):kind==3?(y%7==0 && x%9<6):kind==1?((x+y)%10<3):false;
      if (pattern && map_inside(x,y,points,count)) graphics_draw_pixel(ctx,GPoint(x,y));
    }
  }
}
static void map_route(GContext *ctx,GPoint a,GPoint b) {
  int dx=b.x-a.x,dy=b.y-a.y,steps=abs(dx)>abs(dy)?abs(dx):abs(dy);
  if (!steps) return;
  graphics_context_set_stroke_color(ctx,GColorDarkGray); graphics_context_set_stroke_width(ctx,2);
  for (int i=0;i<steps;i+=9) {
    int end=i+4<steps?i+4:steps;
    graphics_draw_line(ctx,GPoint(a.x+dx*i/steps,a.y+dy*i/steps),GPoint(a.x+dx*end/steps,a.y+dy*end/steps));
  }
}
static void draw_map(Layer *layer,GContext *ctx) {
  const HoleMap *map=&HOLE_MAPS[hole-1]; MapCamera camera=camera_for_hole(map);
  graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_rect(ctx,layer_get_bounds(layer),0,GCornerNone);
  // Layer fairway, dashed route, hazards, then green for a clear visual hierarchy.
  for (int pass=0;pass<7;pass++) {
    for (int i=0;i<map->count;i++) {
      const MapShape *shape=&MAP_SHAPES[map->start+i];
      int order=shape->kind==5?0:shape->kind==7?1:shape->kind==4?2:shape->kind==6?3:shape->kind==0?4:shape->kind==1?6:5;
      if (order!=pass) continue;
      if (shape->kind==0) {
        for (int j=1;j<shape->count;j++) map_route(ctx,map_point(MAP_POINTS[shape->start+j-1],camera),map_point(MAP_POINTS[shape->start+j],camera));
        continue;
      }
      GPoint *points=malloc(shape->count*sizeof(GPoint));
      if (!points) continue;
      for (int j=0;j<shape->count;j++) points[j]=map_point(MAP_POINTS[shape->start+j],camera);
      int clipped_count=0; GPoint *clipped=map_clip_polygon(points,shape->count,0,0,199,149,&clipped_count);
      free(points); points=clipped;
      if (!points || clipped_count<3) {free(points); continue;}
      GPathInfo info={.num_points=clipped_count,.points=points}; GPath *path=gpath_create(&info);
      if (path) {
        GColor fill=shape->kind==7?GColorWhite:shape->kind==5 || shape->kind==6?GColorMintGreen:shape->kind==4?GColorMayGreen:shape->kind==2?GColorPastelYellow:shape->kind==3?GColorVividCerulean:GColorMintGreen;
        GColor outline=shape->kind==2?GColorWindsorTan:shape->kind==3?GColorDukeBlue:GColorDarkGreen;
        // A small neutral edge shadow separates features from the white ground.
        if (shape->kind>=1 && shape->kind<=3) {
          gpath_move_to(path,GPoint(2,2)); graphics_context_set_fill_color(ctx,GColorLightGray); gpath_draw_filled(ctx,path);
          gpath_move_to(path,GPoint(0,0));
        }
        graphics_context_set_fill_color(ctx,fill); gpath_draw_filled(ctx,path);
        if (shape->kind<=3) map_texture(ctx,points,clipped_count,shape->kind);
        graphics_context_set_stroke_color(ctx,outline); graphics_context_set_stroke_width(ctx,shape->kind==4?1:2);
        if (shape->kind<=4) gpath_draw_outline(ctx,path);
        gpath_destroy(path);
      }
      free(points);
    }
  }
  GPoint green=map_point(map->green,camera), tee_point=map_point(map->tee,camera);
  // Labels identify targets without implying that the mapped middle is today's cup.
  graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_circle(ctx,green,7);
  graphics_context_set_stroke_color(ctx,GColorDarkGreen); graphics_context_set_stroke_width(ctx,2); graphics_draw_circle(ctx,green,7);
  draw_text(ctx,"G",green.x-7,green.y-9,14,18,FONT_KEY_GOTHIC_14_BOLD,GColorDarkGreen);
  if (map_visible(tee_point)) {
    graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_rect(ctx,GRect(tee_point.x-7,tee_point.y-8,14,16),2,GCornersAll);
    graphics_context_set_stroke_color(ctx,GColorBlack); graphics_context_set_stroke_width(ctx,1); graphics_draw_round_rect(ctx,GRect(tee_point.x-7,tee_point.y-8,14,16),2);
    draw_text(ctx,"T",tee_point.x-7,tee_point.y-9,14,18,FONT_KEY_GOTHIC_14_BOLD,GColorBlack);
  }
  GPoint player=map_xy(map_x,map_y,camera);
  if (usable() && map_visible(player)) {
    graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_circle(ctx,player,8);
    graphics_context_set_stroke_color(ctx,GColorBlack); graphics_context_set_stroke_width(ctx,1); graphics_draw_circle(ctx,player,7);
    graphics_context_set_fill_color(ctx,GColorMagenta); graphics_fill_circle(ctx,player,5);
    graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_circle(ctx,player,2);
  }
}

static GPoint hazard_preview_point(GPoint p,float scale,float cx,float cy) {
  return GPoint((int)camera_max(-10000,camera_min(10000,(p.x-cx)*scale+112)),(int)camera_max(-10000,camera_min(10000,(p.y-cy)*scale+56)));
}
typedef struct {const MapShape *shape; const Hazard *reading; GPoint center,near_edge,far_edge; int side;} HazardCallout;
static void draw_hazard_image(Layer *layer,GContext *ctx) {
  graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_rect(ctx,layer_get_bounds(layer),0,GCornerNone);
  const HoleMap *map=&HOLE_MAPS[hole-1];
  int minx=32767,miny=32767,maxx=-32768,maxy=-32768, feature_count=0;
  for (int i=0;i<map->count;i++) {
    const MapShape *shape=&MAP_SHAPES[map->start+i];
    if (shape->kind!=1 && shape->kind!=2 && shape->kind!=3) continue;
    if (shape->kind!=1) feature_count++;
    for (int j=0;j<shape->count;j++) {
      GPoint p=MAP_POINTS[shape->start+j];
      if (p.x<minx) minx=p.x;
      if (p.x>maxx) maxx=p.x;
      if (p.y<miny) miny=p.y;
      if (p.y>maxy) maxy=p.y;
    }
  }
  if (!feature_count) {draw_text(ctx,"No mapped hazards",0,38,224,28,FONT_KEY_GOTHIC_18,GColorDarkGray); return;}
  float scale=camera_min(96/camera_max(1,maxx-minx),88/camera_max(1,maxy-miny));
  float cx=(minx+maxx)/2.0f,cy=(miny+maxy)/2.0f;
  HazardCallout callouts[MAX_HAZARDS]; int count=0;
  GPoint green=hazard_preview_point(map->green,scale,cx,cy);
  for (int pass=0;pass<3;pass++) {
    for (int i=0;i<map->count;i++) {
      const MapShape *shape=&MAP_SHAPES[map->start+i];
      int order=shape->kind==4?0:shape->kind==1?2:1;
      if (shape->kind==0 || shape->kind>4 || order!=pass) continue;
      GPoint *points=malloc(shape->count*sizeof(GPoint)); if (!points) continue;
      int sumx=0,sumy=0;
      for (int j=0;j<shape->count;j++) {points[j]=hazard_preview_point(MAP_POINTS[shape->start+j],scale,cx,cy); sumx+=points[j].x; sumy+=points[j].y;}
      if ((shape->kind==2 || shape->kind==3) && count<MAX_HAZARDS) {
        HazardCallout *c=&callouts[count++]; c->shape=shape; c->reading=NULL;
        c->center=GPoint(sumx/shape->count,sumy/shape->count); c->side=c->center.x<green.x?0:1;
        for (int h=0;h<hazard_count;h++) if (hazards[h].osm_id==shape->osm_id) {c->reading=&hazards[h]; break;}
        c->near_edge=c->reading?hazard_preview_point(c->reading->near_edge,scale,cx,cy):c->center;
        c->far_edge=c->reading?hazard_preview_point(c->reading->far_edge,scale,cx,cy):c->center;
      }
      int clipped_count=0; GPoint *clipped=map_clip_polygon(points,shape->count,60,0,164,111,&clipped_count);
      free(points); points=clipped;
      if (!points || clipped_count<3) {free(points); continue;}
      GPathInfo info={.num_points=clipped_count,.points=points}; GPath *path=gpath_create(&info);
      if (path) {
        GColor fill=shape->kind==2?GColorPastelYellow:shape->kind==3?GColorVividCerulean:GColorMintGreen;
        GColor outline=shape->kind==2?GColorWindsorTan:shape->kind==3?GColorDukeBlue:GColorDarkGreen;
        graphics_context_set_fill_color(ctx,fill); gpath_draw_filled(ctx,path); map_texture(ctx,points,clipped_count,shape->kind);
        graphics_context_set_stroke_color(ctx,outline); graphics_context_set_stroke_width(ctx,shape->kind==4?1:2);
        gpath_draw_outline(ctx,path); gpath_destroy(path);
      }
      free(points);
    }
  }
  // Order labels vertically on each side to avoid overlapping callouts.
  for (int i=0;i<count;i++) for (int j=i+1;j<count;j++) if (callouts[j].center.y<callouts[i].center.y) {HazardCallout swap=callouts[i]; callouts[i]=callouts[j]; callouts[j]=swap;}
  int side_count[2]={0,0},side_index[2]={0,0};
  for (int i=0;i<count;i++) side_count[callouts[i].side]++;
  int green_side=side_count[0]<=side_count[1]?0:1;
  for (int i=0;i<count;i++) {
    HazardCallout *c=&callouts[i]; int side=c->side,slot=side_index[side]++;
    bool has_reading=usable() && c->reading;
    GPoint near=has_reading?c->near_edge:c->center,far=has_reading?c->far_edge:c->center;
    int offset=side==green_side?44:0,available=112-offset;
    int slot_start=offset+slot*available/side_count[side],slot_end=offset+(slot+1)*available/side_count[side];
    int top=(near.y+far.y)/2-14;
    if (top<slot_start) top=slot_start;
    if (top>slot_end-28) top=slot_end-28;
    int leader_x=side?(int)camera_max(162,camera_min(180,(near.x>far.x?near.x:far.x)+7)):
      (int)camera_min(62,camera_max(44,(near.x<far.x?near.x:far.x)-7));
    LeaderPlan labels=hazard_leader_plan((LeaderPoint){near.x,near.y},(LeaderPoint){far.x,far.y},leader_x,top+7);
    graphics_context_set_stroke_width(ctx,1);
    if (has_reading) {
      graphics_context_set_stroke_color(ctx,GColorDarkGray); graphics_draw_line(ctx,GPoint(leader_x,labels.reach_y),near);
      graphics_context_set_stroke_color(ctx,GColorCobaltBlue); graphics_draw_line(ctx,GPoint(leader_x,labels.carry_y),far);
    }
    char reach[24],carry[24];
    if (has_reading) {snprintf(reach,sizeof(reach),"R %d",c->reading->reach); snprintf(carry,sizeof(carry),"C %d",c->reading->carry);}
    else {snprintf(reach,sizeof(reach),"R --"); snprintf(carry,sizeof(carry),"C --");}
    int text_x=side?leader_x+2:leader_x-42;
    GTextAlignment alignment=side?GTextAlignmentLeft:GTextAlignmentRight;
    graphics_context_set_fill_color(ctx,GColorWhite);
    graphics_fill_rect(ctx,GRect(text_x,labels.reach_y-7,40,14),0,GCornerNone);
    graphics_fill_rect(ctx,GRect(text_x,labels.carry_y-7,40,14),0,GCornerNone);
    graphics_context_set_text_color(ctx,GColorDarkGray);
    graphics_draw_text(ctx,reach,fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),GRect(text_x,labels.reach_y-7,40,16),GTextOverflowModeTrailingEllipsis,alignment,NULL);
    graphics_context_set_text_color(ctx,GColorCobaltBlue);
    graphics_draw_text(ctx,carry,fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),GRect(text_x,labels.carry_y-7,40,16),GTextOverflowModeTrailingEllipsis,alignment,NULL);
    graphics_context_set_fill_color(ctx,GColorDarkGray); graphics_fill_circle(ctx,near,1);
    graphics_context_set_fill_color(ctx,GColorCobaltBlue); graphics_fill_circle(ctx,far,1);
  }
  GPoint f=green_edges_valid?hazard_preview_point(green_front,scale,cx,cy):green;
  GPoint b=green_edges_valid?hazard_preview_point(green_back,scale,cx,cy):green;
  int min_green_x=f.x<green.x?f.x:green.x;if(b.x<min_green_x)min_green_x=b.x;
  int max_green_x=f.x>green.x?f.x:green.x;if(b.x>max_green_x)max_green_x=b.x;
  int label_x=green_side?(int)camera_max(162,camera_min(180,max_green_x+7)):(int)camera_min(62,camera_max(44,min_green_x-7));
  GreenLeaderPlan plan=green_leader_plan((LeaderPoint){f.x,f.y},(LeaderPoint){green.x,green.y},(LeaderPoint){b.x,b.y},label_x,7);
  GPoint anchors[3]={f,green,b};int label_y[3]={plan.front_y,plan.middle_y,plan.back_y};int readings[3]={front,middle,back};
  const char *prefix[3]={"F","M","B"};
  for (int i=0;i<3;i++) {
    bool measured=usable() && readings[i]>=0 && (i==1 || green_edges_valid);
    graphics_context_set_stroke_width(ctx,1);graphics_context_set_stroke_color(ctx,GColorDarkGreen);
    if (measured) {graphics_draw_line(ctx,GPoint(label_x,label_y[i]),anchors[i]);graphics_context_set_fill_color(ctx,GColorDarkGreen);graphics_fill_circle(ctx,anchors[i],2);}
    char text[24];if(measured)snprintf(text,sizeof(text),"%s %d",prefix[i],readings[i]);else snprintf(text,sizeof(text),"%s --",prefix[i]);
    int x=green_side?label_x+2:label_x-42;
    graphics_context_set_fill_color(ctx,GColorWhite);graphics_fill_rect(ctx,GRect(x,label_y[i]-7,40,14),0,GCornerNone);
    graphics_context_set_text_color(ctx,GColorDarkGreen);
    graphics_draw_text(ctx,text,fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),GRect(x,label_y[i]-7,40,16),GTextOverflowModeTrailingEllipsis,green_side?GTextAlignmentLeft:GTextAlignmentRight,NULL);
  }
}

static void draw(Layer *layer,GContext *ctx) {
  graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_rect(ctx,layer_get_bounds(layer),0,GCornerNone);
  if (screen==SCREEN_PLAY && page==2) {
    draw_heading(ctx);
    draw_text(ctx,hole_text,22,34,216,30,FONT_KEY_GOTHIC_24_BOLD,GColorDarkGreen);
    char distance[32];
    if (usable()) snprintf(distance,sizeof(distance),"%d %s TO GREEN",middle,units?"m":"yd");
    else snprintf(distance,sizeof(distance),"HOLE OVERVIEW");
    draw_text(ctx,distance,35,219,190,18,FONT_KEY_GOTHIC_14_BOLD,GColorDarkGreen);
    draw_text(ctx,status_text,32,237,196,18,FONT_KEY_GOTHIC_14,status_color);
    return;
  }
  draw_heading(ctx);
  draw_text(ctx,hole_text,22,47,216,30,FONT_KEY_GOTHIC_24_BOLD,GColorDarkGreen);
  if (!(screen==SCREEN_PLAY && page==1)) draw_text(ctx,context_text,20,77,220,18,FONT_KEY_GOTHIC_14,GColorDarkGray);
  if (screen==SCREEN_PLAY && page==1) {
    draw_text(ctx,units?"R: REACH / C: CARRY (M)":"R: REACH / C: CARRY (YD)",24,211,212,17,FONT_KEY_GOTHIC_14,GColorDarkGreen);
    draw_text(ctx,status_text,32,231,196,18,FONT_KEY_GOTHIC_14,status_color); return;
  }
  if (screen==SCREEN_PLAY && page==0) {
    draw_text(ctx,main_text,15,77,230,59,main_font,GColorCobaltBlue);
    draw_text(ctx,caption_text,15,134,230,20,FONT_KEY_GOTHIC_14,GColorDarkGreen);
    draw_text(ctx,left_text,32,153,94,32,FONT_KEY_GOTHIC_28_BOLD,GColorCobaltBlue);
    draw_text(ctx,right_text,134,153,94,32,FONT_KEY_GOTHIC_28_BOLD,GColorCobaltBlue);
    draw_text(ctx,left_cap,28,183,102,18,FONT_KEY_GOTHIC_14,GColorDarkGray);
    draw_text(ctx,right_cap,130,183,102,18,FONT_KEY_GOTHIC_14,GColorDarkGray);
    char layup[48],shot[16]; number(shot,sizeof(shot),layup_shot,usable());
    if (usable() && layup_shot<0) snprintf(layup,sizeof(layup),"NO FORWARD LAYUP TARGET");
    else snprintf(layup,sizeof(layup),"LAYUP: HIT %s / LEAVE %d",shot,target);
    draw_text(ctx,layup,22,207,216,20,FONT_KEY_GOTHIC_14_BOLD,GColorDarkGreen);
    draw_text(ctx,status_text,32,231,196,18,FONT_KEY_GOTHIC_14,status_color);
    return;
  }
  draw_text(ctx,main_text,15,94,230,59,main_font,GColorCobaltBlue);
  draw_text(ctx,caption_text,15,151,230,22,FONT_KEY_GOTHIC_14,GColorDarkGreen);
  draw_text(ctx,left_text,32,172,94,34,FONT_KEY_GOTHIC_28_BOLD,GColorCobaltBlue);
  draw_text(ctx,right_text,134,172,94,34,FONT_KEY_GOTHIC_28_BOLD,GColorCobaltBlue);
  draw_text(ctx,left_cap,28,203,102,18,FONT_KEY_GOTHIC_14,GColorDarkGray);
  draw_text(ctx,right_cap,130,203,102,18,FONT_KEY_GOTHIC_14,GColorDarkGray);
  draw_text(ctx,status_text,32,225,196,18,FONT_KEY_GOTHIC_14,status_color);
}
static void load(Window *w) {
  canvas=layer_create(layer_get_bounds(window_get_root_layer(w)));
  layer_set_update_proc(canvas,draw); layer_add_child(window_get_root_layer(w),canvas);
  map_canvas=layer_create(GRect(30,70,200,150)); layer_set_update_proc(map_canvas,draw_map);
  layer_add_child(canvas,map_canvas);
  hazard_canvas=layer_create(GRect(18,98,224,112)); layer_set_update_proc(hazard_canvas,draw_hazard_image);
  layer_add_child(canvas,hazard_canvas); render();
}
static void unload(Window *w) {clear_heading(); cached_heading[0]=0;layer_destroy(hazard_canvas); hazard_canvas=NULL; layer_destroy(map_canvas); map_canvas=NULL; layer_destroy(canvas); canvas=NULL;}

int main(void) {
  hole=persist_exists(1)?persist_read_int(1):1; if (hole<1 || hole>18) hole=1;
  units=persist_exists(2)&&persist_read_int(2)?1:0;
  tee=persist_exists(3)?persist_read_int(3):1; if (tee<0 || tee>3) tee=1;
  target=persist_exists(4)?persist_read_int(4):100; if (target<50 || target>200 || target%25) target=100;
  phone_connected=connection_service_peek_pebble_app_connection();
  window=window_create(); window_set_background_color(window,GColorWhite);
  window_set_window_handlers(window,(WindowHandlers){.load=load,.unload=unload}); window_set_click_config_provider(window,clicks);
  app_message_register_inbox_received(inbox); app_message_register_outbox_failed(failed); app_message_open(2048,256);
  tick_timer_service_subscribe(SECOND_UNIT,tick);
  connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler=connection});
  about=window_create(); credits=text_layer_create(GRect(30,45,200,180));
  text_layer_set_font(credits,fonts_get_system_font(FONT_KEY_GOTHIC_18)); text_layer_set_text_alignment(credits,GTextAlignmentCenter);
  text_layer_set_text(credits,"Course data\n© OpenStreetMap\ncontributors\nODbL 1.0\nopenstreetmap.org/\ncopyright");
  layer_add_child(window_get_root_layer(about),text_layer_get_layer(credits));
  window_stack_push(window,true); app_event_loop();
  connection_service_unsubscribe(); tick_timer_service_unsubscribe(); app_message_deregister_callbacks();
  text_layer_destroy(credits); window_destroy(about); window_destroy(window);
}
