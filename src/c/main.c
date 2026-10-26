#include <pebble.h>
#include "map-camera.h"
#include "hazard-leaders.h"

#define MAX_HAZARDS 8
#define MAX_FIX_AGE 30
// The phone sends one hole map at a time; scripts/import-course.py checks these limits.
#define MAX_POINTS 800
#define MAX_SHAPES 32
#define MAX_TEES 6
#define MAX_COURSES 5
typedef enum { SCREEN_COURSES, SCREEN_TEES, SCREEN_PLAY, SCREEN_MENU, SCREEN_TARGET } Screen;
typedef struct { uint16_t start, count; uint8_t kind; uint32_t osm_id; } MapShape;
typedef struct { uint16_t start, count; GPoint tee, green; } HoleMap;
typedef struct { char id[32], name[48], away[12]; } CourseChoice;
static GPoint map_points[MAX_POINTS];
static MapShape map_shapes[MAX_SHAPES];
static HoleMap hole_map;
static int map_hole=0;
static CourseChoice choices[MAX_COURSES];
static int choice_count=0, choice_index=0, tee_count=0, tee_yards[MAX_TEES];
static char course_id[32], course_short[12], course_error[32], pars[19], tee_names[MAX_TEES][9];
static bool course_ready=false;
typedef struct { char name[24]; int reach, carry; uint32_t osm_id; GPoint near_edge, far_edge; } Hazard;
static Window *window, *about;
static TextLayer *credits;
static Layer *canvas, *map_canvas, *hazard_canvas;
static const char *main_font=FONT_KEY_GOTHIC_28_BOLD;
static GColor status_color;
static Screen screen=SCREEN_COURSES;
static int hole=1, hole_count=18, units=0, tee=1, target=100, revision=1, page=0, hazard_index=0, menu_index=0;
static int middle=-1, front=-1, back=-1, accuracy=0, age_at_receive=0, gps_state=1, layup_shot=-1;
static Hazard hazards[MAX_HAZARDS];
static int hazard_count=0, map_x=-1000, map_y=-1000, layup_x=-1000, layup_y=-1000;
static GPoint green_front,green_back;
static bool green_edges_valid=false;
static bool ready=false, valid=false, round_started=false, phone_connected=true;
static time_t received=0, last_request=0;
static const char *menu_names[]={"Refresh GPS","Change tee","Layup distance","Yards / meters","New round","Data credits","Back to play"};
static const char *menu_help[]={"Request a fresh reading","Choose your tee box","Preferred approach left","Switch distance units","Pick course, tee, hole 1","OpenStreetMap / ODbL","Return to yardages"};
static char heading_text[48], hole_text[48], context_text[48], main_text[48], caption_text[64];
static char left_text[24], right_text[24], left_cap[32], right_cap[32], status_text[64];

static int fix_age(void) { return age_at_receive+(int)(time(NULL)-received); }
static bool usable(void) { return valid && phone_connected && fix_age()<=MAX_FIX_AGE; }
static void number(char *buffer, size_t size, int value, bool show) {
  if (!show || value<0) snprintf(buffer,size,"--"); else snprintf(buffer,size,"%d",value);
}
static Screen home(void) {return round_started?SCREEN_PLAY:course_id[0]?SCREEN_TEES:SCREEN_COURSES;}
static void render(void) {
  bool live=usable();
  const char *unit=units?"METERS":"YARDS", *name=course_ready?course_short:"PEBBLE GOLF";
  snprintf(heading_text,sizeof(heading_text),"%s",name);
  left_text[0]=right_text[0]=left_cap[0]=right_cap[0]='\0';
  main_font=FONT_KEY_GOTHIC_28_BOLD;
  if (screen==SCREEN_COURSES) {
    snprintf(hole_text,sizeof(hole_text),"SELECT COURSE");
    snprintf(context_text,sizeof(context_text),"%s",choice_count>1?"UP / DOWN TO CHOOSE":"");
    main_font=FONT_KEY_GOTHIC_24_BOLD;
    caption_text[0]='\0';
    if (choice_count) {
      const CourseChoice *c=&choices[choice_index];
      snprintf(main_text,sizeof(main_text),"%s",c->name);
      if (c->away[0]) snprintf(caption_text,sizeof(caption_text),"%s AWAY",c->away);
      snprintf(left_text,sizeof(left_text),"%d",choice_index+1); snprintf(left_cap,sizeof(left_cap),"OF %d",choice_count);
      snprintf(right_text,sizeof(right_text),"SELECT"); snprintf(right_cap,sizeof(right_cap),"TO PLAY");
    } else snprintf(main_text,sizeof(main_text),"Finding courses");
    if (!phone_connected) snprintf(status_text,sizeof(status_text),"PHONE DISCONNECTED");
    else if (!ready) snprintf(status_text,sizeof(status_text),"OPEN PEBBLE PHONE APP");
    else if (choice_count && !choices[0].away[0]) snprintf(status_text,sizeof(status_text),"NO GPS YET - NOT BY DISTANCE");
    else snprintf(status_text,sizeof(status_text),"NEAREST FIRST");
  } else if (screen==SCREEN_TEES && !course_ready) {
    snprintf(hole_text,sizeof(hole_text),"SELECT TEE BOX");
    context_text[0]='\0';
    main_font=FONT_KEY_GOTHIC_24_BOLD;
    snprintf(main_text,sizeof(main_text),"Loading course");
    snprintf(caption_text,sizeof(caption_text),"%s",course_error[0]?course_error:"FROM PHONE");
    snprintf(status_text,sizeof(status_text),"%s",phone_connected?"BACK: CHOOSE COURSE":"PHONE DISCONNECTED");
  } else if (screen==SCREEN_TEES) {
    snprintf(hole_text,sizeof(hole_text),"SELECT TEE BOX");
    snprintf(context_text,sizeof(context_text),"UP / DOWN TO CHOOSE");
    snprintf(main_text,sizeof(main_text),"%s",tee_names[tee]);
    snprintf(caption_text,sizeof(caption_text),round_started?"SELECT TO CONFIRM":"SELECT TO START ROUND");
    int total=units?(tee_yards[tee]*9144+5000)/10000:tee_yards[tee], par=0;
    for (int i=0;i<hole_count;i++) par+=pars[i]-'0';
    snprintf(left_text,sizeof(left_text),"%d,%03d",total/1000,total%1000);
    snprintf(left_cap,sizeof(left_cap),"TOTAL %s",unit);
    snprintf(right_text,sizeof(right_text),"%d",par); snprintf(right_cap,sizeof(right_cap),"PAR / %d HOLES",hole_count);
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
    snprintf(hole_text,sizeof(hole_text),"HOLE %02d  PAR %d",hole,pars[hole-1]-'0');
    context_text[0]='\0';
    if (page==0) {
      snprintf(heading_text,sizeof(heading_text),"%s / GREEN",name);
      main_font=FONT_KEY_ROBOTO_BOLD_SUBSET_49;
      number(main_text,sizeof(main_text),middle,live);
      snprintf(caption_text,sizeof(caption_text),"%s TO MIDDLE",unit);
      number(left_text,sizeof(left_text),front,live); number(right_text,sizeof(right_text),back,live);
      snprintf(left_cap,sizeof(left_cap),"FRONT"); snprintf(right_cap,sizeof(right_cap),"BACK");
    } else if (page==1) {
      snprintf(heading_text,sizeof(heading_text),"%s / HAZARDS",name);
      context_text[0]='\0';
      main_text[0]=caption_text[0]='\0';
    }
    if (page==2) snprintf(heading_text,sizeof(heading_text),"%s / MAP",name);
    if (!phone_connected) snprintf(status_text,sizeof(status_text),"PHONE DISCONNECTED");
    else if (!ready) snprintf(status_text,sizeof(status_text),"OPEN PEBBLE PHONE APP");
    else if (course_error[0] && map_hole!=hole) snprintf(status_text,sizeof(status_text),"%s",course_error);
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
static bool resend=false, resend_fresh=false, inflight_fresh=false;
static AppTimer *retry_timer=NULL;
static void request(bool fresh);
static void retry_request(void *data) {
  retry_timer=NULL;
  if (resend) {
    bool fresh=resend_fresh;
    resend=resend_fresh=false;
    request(fresh);
  }
}
static void schedule_retry(bool fresh) {
  resend=true; resend_fresh|=fresh;
  if (!retry_timer) retry_timer=app_timer_register(2000,retry_request,NULL);
}
static void sent(DictionaryIterator *iter,void *context) {
  if (resend) {
    if (retry_timer) app_timer_cancel(retry_timer);
    retry_request(NULL);
  }
}
static void request(bool fresh) {
  if (!ready || !phone_connected) return;
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter)!=APP_MSG_OK) {schedule_retry(fresh); return;}
  // An empty course asks the phone for the nearby course list.
  dict_write_cstring(iter,MESSAGE_KEY_Course,course_id);
  dict_write_int32(iter,MESSAGE_KEY_Hole,hole); dict_write_int32(iter,MESSAGE_KEY_Units,units);
  dict_write_int32(iter,MESSAGE_KEY_LayupTarget,target); dict_write_int32(iter,MESSAGE_KEY_Revision,revision);
  dict_write_int32(iter,MESSAGE_KEY_Request,fresh?1:0);
  if (app_message_outbox_send()!=APP_MSG_OK) {schedule_retry(fresh); return;}
  inflight_fresh=fresh; last_request=time(NULL);
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
// "SHORT|PARS|TEE:YARDS,TEE:YARDS", e.g. "SHARPSTOWN|534...|BLUE:6602,WHITE:6292".
static bool parse_info(const char *text) {
  char buffer[160]; snprintf(buffer,sizeof(buffer),"%s",text);
  char *bar=strchr(buffer,'|'), *bar2=bar?strchr(bar+1,'|'):NULL;
  if (!bar2 || (bar2-bar-1!=9 && bar2-bar-1!=18)) return false;
  hole_count=bar2-bar-1; *bar=*bar2='\0';
  snprintf(course_short,sizeof(course_short),"%.11s",buffer);
  memcpy(pars,bar+1,hole_count); pars[hole_count]='\0';
  tee_count=0;
  for (char *row=bar2+1;row && *row && tee_count<MAX_TEES;) {
    char *next=strchr(row,','); if (next) *next++='\0';
    char *colon=strchr(row,':');
    if (colon) {*colon='\0'; snprintf(tee_names[tee_count],sizeof(tee_names[0]),"%s",row); tee_yards[tee_count++]=atoi(colon+1);}
    row=next;
  }
  if (tee>=tee_count) tee=0;
  return tee_count>0;
}
// "id|name|distance;..." nearest first; keep the highlighted course if it is still listed.
static void parse_courses(const char *text) {
  char selected[32]; snprintf(selected,sizeof(selected),"%s",choice_count?choices[choice_index].id:"");
  char buffer[512]; snprintf(buffer,sizeof(buffer),"%s",text);
  choice_count=0; choice_index=0;
  for (char *row=buffer;row && *row && choice_count<MAX_COURSES;) {
    char *next=strchr(row,';'); if (next) *next++='\0';
    char *bar=strchr(row,'|'), *bar2=bar?strchr(bar+1,'|'):NULL;
    if (bar2) {
      *bar=*bar2='\0'; CourseChoice *c=&choices[choice_count];
      snprintf(c->id,sizeof(c->id),"%.31s",row); snprintf(c->name,sizeof(c->name),"%.47s",bar+1); snprintf(c->away,sizeof(c->away),"%.11s",bar2+1);
      if (!strcmp(c->id,selected)) choice_index=choice_count;
      choice_count++;
    }
    row=next;
  }
}
static int16_t le16(const uint8_t *p) {return (int16_t)(p[0]|p[1]<<8);}
// Hole map bytes from scripts/import-course.py: tee, green, shapes, then points.
static void parse_map(const uint8_t *d,int length) {
  map_hole=0;
  if (length<9 || d[8]>MAX_SHAPES || length<9+7*d[8]) return;
  int count=d[8], offset=9+7*count, total=0;
  for (int i=0;i<count;i++) {
    const uint8_t *s=d+9+7*i; MapShape *shape=&map_shapes[i];
    shape->kind=s[0]; shape->count=s[1]|s[2]<<8; shape->osm_id=s[3]|s[4]<<8|s[5]<<16|(uint32_t)s[6]<<24;
    shape->start=total; total+=shape->count;
  }
  if (total>MAX_POINTS || length!=offset+4*total) return;
  for (int i=0;i<total;i++) map_points[i]=GPoint(le16(d+offset+4*i),le16(d+offset+4*i+2));
  hole_map=(HoleMap){0,count,GPoint(le16(d),le16(d+2)),GPoint(le16(d+4),le16(d+6))};
  map_hole=hole; course_error[0]='\0';
}
static void inbox(DictionaryIterator *iter,void *context) {
  if (dict_find(iter,MESSAGE_KEY_Ready)) {ready=true; request(true); render(); return;}
  Tuple *list=dict_find(iter,MESSAGE_KEY_CourseList), *course=dict_find(iter,MESSAGE_KEY_Course);
  if (list) {if (screen==SCREEN_COURSES) parse_courses(list->value->cstring); render(); return;}
  if (!course_id[0] || !course || strcmp(course->value->cstring,course_id)) return;
  // Course details and maps apply whatever the revision; distances must match it.
  Tuple *info_tuple=dict_find(iter,MESSAGE_KEY_CourseInfo), *map=dict_find(iter,MESSAGE_KEY_MapData);
  const char *info=info_tuple?info_tuple->value->cstring:NULL;
  if (info && info[0]=='!') snprintf(course_error,sizeof(course_error),"%.31s",info+1);
  else if (info && parse_info(info)) {course_ready=true; course_error[0]='\0'; persist_write_string(7,info);}
  if (map && integer(iter,MESSAGE_KEY_Hole,-1)==hole) parse_map(map->value->data,map->length);
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
    layup_x=integer(iter,MESSAGE_KEY_LayupX,-1000); layup_y=integer(iter,MESSAGE_KEY_LayupY,-1000);
    layup_shot=integer(iter,MESSAGE_KEY_LayupShot,-1);
    Tuple *h=dict_find(iter,MESSAGE_KEY_Hazards); parse_hazards(h?h->value->cstring:"");
    valid=age_at_receive>=0 && age_at_receive<=MAX_FIX_AGE; received=time(NULL);
  } else if (gps_state==2 || gps_state==5) valid=false;
  render();
}
// Retry a failed request in about 3s rather than on every tick.
static void failed(DictionaryIterator *iter,AppMessageResult reason,void *context) {schedule_retry(inflight_fresh);}
static void connection(bool connected) {
  phone_connected=connected;
  if (!connected) valid=false; else request(true);
  render();
}
static void tick(struct tm *now,TimeUnits changed) {
  if (round_started && ready && phone_connected && time(NULL)-last_request>=15 && (!valid || fix_age()>12)) request(false);
  if (screen==SCREEN_PLAY && (now->tm_sec%5==0 || (valid && fix_age()==MAX_FIX_AGE+1))) render();
}
static void invalidate(void) {revision++; valid=false; hazard_count=0; hazard_index=0;}
// Reopening the list starts from the top: nearest first, or last played without GPS.
static void choose_course(void) {course_id[0]='\0'; course_ready=false; map_hole=0; choice_count=0; screen=SCREEN_COURSES; request(true);}
static void change(int delta) {
  if (screen==SCREEN_COURSES) {if (choice_count) choice_index=(choice_index+delta+choice_count)%choice_count;}
  else if (screen==SCREEN_TEES) {if (tee_count) tee=(tee+delta+tee_count)%tee_count;}
  else if (screen==SCREEN_TARGET) {target+=delta*25; if (target<50) target=200; if (target>200) target=50;}
  else if (screen==SCREEN_MENU) menu_index=(menu_index+delta+7)%7;
  else {hole=(hole-1+delta+hole_count)%hole_count+1; persist_write_int(1,hole); invalidate(); request(false);}
  render();
}
static void up(ClickRecognizerRef r,void *c) {change(-1);}
static void down(ClickRecognizerRef r,void *c) {change(1);}
static void select_click(ClickRecognizerRef r,void *c) {
  if (screen==SCREEN_COURSES) {
    if (!choice_count) return;
    snprintf(course_id,sizeof(course_id),"%s",choices[choice_index].id); persist_write_string(6,course_id);
    course_ready=false; course_error[0]='\0'; map_hole=0; screen=SCREEN_TEES; request(true);
  } else if (screen==SCREEN_TEES) {
    if (!course_ready) return;
    persist_write_int(3,tee);
    if (!round_started) {hole=1; persist_write_int(1,hole);}
    round_started=true; persist_write_bool(5,true); screen=SCREEN_PLAY; invalidate(); request(true);
  } else if (screen==SCREEN_TARGET) {
    persist_write_int(4,target); screen=home(); invalidate(); request(false);
  } else if (screen==SCREEN_MENU) {
    switch (menu_index) {
      case 0: screen=home(); request(true); break;
      case 1: screen=SCREEN_TEES; break;
      case 2: screen=SCREEN_TARGET; break;
      case 3: units=!units; persist_write_int(2,units); invalidate(); screen=home(); request(false); break;
      case 4: round_started=false; persist_write_bool(5,false); valid=false; page=0; choose_course(); break;
      case 5: window_stack_push(about,true); break;
      default: screen=home(); break;
    }
  } else if (page==0) {page=1; hazard_index=0;}
  else page=(page+1)%3;
  render();
}
static void settings(ClickRecognizerRef r,void *c) {if (screen==SCREEN_COURSES) return; screen=SCREEN_MENU; menu_index=0; render();}
static void back_click(ClickRecognizerRef r,void *c) {
  if (screen==SCREEN_PLAY || screen==SCREEN_COURSES) window_stack_pop(true);
  else if (screen==SCREEN_TEES && !round_started) {choose_course(); render();}
  else {
    if (screen==SCREEN_TEES) {tee=persist_exists(3)?persist_read_int(3):1;}
    if (screen==SCREEN_TARGET) {target=persist_exists(4)?persist_read_int(4):100;}
    screen=home(); render();
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
  float x=(px-c.x)*c.zoom/16+MAP_W/2, y=(py-c.y)*c.zoom/16+MAP_H/2;
  // Limit distant geometry to the representable range before viewport clipping.
  return GPoint((int)camera_max(-10000,camera_min(10000,x)),(int)camera_max(-10000,camera_min(10000,y)));
}
static GPoint map_point(GPoint p,MapCamera c) {return map_xy(p.x,p.y,c);}
static bool map_visible(GPoint p) {return p.x>=6 && p.x<=MAP_W-6 && p.y>=6 && p.y<=MAP_H-6;}
static void grow(int *box,GPoint p) {
  if (p.x<box[0]) box[0]=p.x;
  if (p.y<box[1]) box[1]=p.y;
  if (p.x>box[2]) box[2]=p.x;
  if (p.y>box[3]) box[3]=p.y;
}
// *near reports whether the live player is on this hole and belongs in the frame.
static MapCamera camera_for_hole(const HoleMap *map,bool *near) {
  int all[4]={map->tee.x,map->tee.y,map->tee.x,map->tee.y}, frame[4]={map->green.x,map->green.y,map->green.x,map->green.y};
  float meters=units?middle:middle*0.9144f;
  for (int i=0;i<map->count;i++) {
    const MapShape *shape=&map_shapes[map->start+i];
    for (int j=0;j<shape->count;j++) grow(all,map_points[shape->start+j]);
  }
  *near=usable() && map_player_near(map_x,map_y,all[0],all[1],all[2],all[3]);
  bool approach=*near && meters<220;
  for (int i=0;i<map->count;i++) {
    const MapShape *shape=&map_shapes[map->start+i];
    if (shape->kind!=1 && (approach || (shape->kind!=0 && shape->kind!=5))) continue;
    for (int j=0;j<shape->count;j++) grow(frame,map_points[shape->start+j]);
  }
  return map_camera_hole(map_x,map_y,map->tee.x,map->tee.y,frame[0],frame[1],frame[2],frame[3],meters,*near);
}
// Clip geometry before rasterizing; distant polygon vertices can otherwise make
// a tiny local view spend time drawing thousands of off-screen scanlines.
static GPoint *map_clip_polygon(const GPoint *input,int count,int left,int top,int right,int bottom,int *out_count) {
  // Each pass grows a concave polygon by at most 1.5x: 1.5^4 < 6.
  *out_count=0; int capacity=count*6+16;
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
// Fill texture one scanline at a time from the clipped polygon's edge crossings.
static void map_texture(GContext *ctx,const GPoint *points,int count,int kind) {
  int miny=points[0].y,maxy=miny;
  for (int i=1;i<count;i++) {miny=points[i].y<miny?points[i].y:miny; maxy=points[i].y>maxy?points[i].y:maxy;}
  int *xs=malloc(count*sizeof(int)); if (!xs) return;
  graphics_context_set_stroke_width(ctx,1);
  graphics_context_set_stroke_color(ctx,kind==2?GColorWindsorTan:kind==3?GColorWhite:GColorIslamicGreen);
  for (int y=miny;y<=maxy;y++) {
    if ((kind==2 && y%5) || (kind==3 && y%7)) continue;
    int n=0;
    for (int i=0,j=count-1;i<count;j=i++) {
      if ((points[i].y>y)!=(points[j].y>y)) xs[n++]=(int32_t)(points[j].x-points[i].x)*(y-points[i].y)/(points[j].y-points[i].y)+points[i].x;
    }
    for (int i=1;i<n;i++) for (int j=i;j>0 && xs[j-1]>xs[j];j--) {int t=xs[j]; xs[j]=xs[j-1]; xs[j-1]=t;}
    for (int i=0;i+1<n;i+=2) for (int x=xs[i];x<xs[i+1];x++) {
      bool pattern=kind==2?x%5==(y%10<5?0:2):kind==3?x%9<6:kind==1?(x+y)%10<3:false;
      if (pattern) graphics_draw_pixel(ctx,GPoint(x,y));
    }
  }
  free(xs);
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
// Where the green-to-player line leaves the padded viewport.
static GPoint map_edge(GPoint from,GPoint to) {
  float dx=to.x-from.x, dy=to.y-from.y, t=1;
  if (to.x<8) t=camera_min(t,(8-from.x)/dx);
  if (to.x>MAP_W-9) t=camera_min(t,(MAP_W-9-from.x)/dx);
  if (to.y<8) t=camera_min(t,(8-from.y)/dy);
  if (to.y>MAP_H-9) t=camera_min(t,(MAP_H-9-from.y)/dy);
  t=camera_max(0,t);
  return GPoint(from.x+(int)(dx*t),from.y+(int)(dy*t));
}
static void draw_map(Layer *layer,GContext *ctx) {
  graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_rect(ctx,layer_get_bounds(layer),0,GCornerNone);
  if (map_hole!=hole) {draw_text(ctx,"Loading map",0,MAP_H/2-14,MAP_W,28,FONT_KEY_GOTHIC_18,GColorDarkGray); return;}
  const HoleMap *map=&hole_map; bool near; MapCamera camera=camera_for_hole(map,&near);
  // Layer fairway, cutouts, tees, dashed route, hazards, then green for a clear visual hierarchy.
  for (int pass=0;pass<6;pass++) {
    for (int i=0;i<map->count;i++) {
      const MapShape *shape=&map_shapes[map->start+i];
      int order=shape->kind==4?0:shape->kind==6?1:shape->kind==5?2:shape->kind==0?3:shape->kind==1?5:4;
      if (order!=pass) continue;
      if (shape->kind==0) {
        for (int j=1;j<shape->count;j++) map_route(ctx,map_point(map_points[shape->start+j-1],camera),map_point(map_points[shape->start+j],camera));
        continue;
      }
      GPoint *points=malloc(shape->count*sizeof(GPoint));
      if (!points) continue;
      for (int j=0;j<shape->count;j++) points[j]=map_point(map_points[shape->start+j],camera);
      int clipped_count=0; GPoint *clipped=map_clip_polygon(points,shape->count,0,0,MAP_W-1,MAP_H-1,&clipped_count);
      free(points); points=clipped;
      if (!points || clipped_count<3) {free(points); continue;}
      GPathInfo info={.num_points=clipped_count,.points=points}; GPath *path=gpath_create(&info);
      if (path) {
        GColor fill=shape->kind==6?GColorWhite:shape->kind==5?GColorIslamicGreen:shape->kind==4?GColorMayGreen:shape->kind==2?GColorPastelYellow:shape->kind==3?GColorVividCerulean:GColorMintGreen;
        GColor outline=shape->kind==2?GColorWindsorTan:shape->kind==3?GColorDukeBlue:GColorDarkGreen;
        graphics_context_set_fill_color(ctx,fill); gpath_draw_filled(ctx,path);
        if (shape->kind<=3) map_texture(ctx,points,clipped_count,shape->kind);
        graphics_context_set_stroke_color(ctx,outline); graphics_context_set_stroke_width(ctx,shape->kind>=4?1:2);
        if (shape->kind!=6) gpath_draw_outline(ctx,path);
        gpath_destroy(path);
      }
      free(points);
    }
  }
  GPoint green=map_point(map->green,camera), player=map_xy(map_x,map_y,camera), end=player;
  bool live=usable(), shown=live && map_visible(player);
  if (live) {
    if (!shown) end=map_edge(green,player);
    graphics_context_set_stroke_color(ctx,GColorCobaltBlue); graphics_context_set_stroke_width(ctx,2); graphics_draw_line(ctx,green,end);
    GPoint layup=map_xy(layup_x,layup_y,camera);
    if (layup_x>-1000 && map_visible(layup)) {
      graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_circle(ctx,layup,4);
      graphics_context_set_fill_color(ctx,GColorCobaltBlue); graphics_fill_circle(ctx,layup,3);
    }
  }
  // A small mark leaves the green and nearby bunkers visible; it is the mapped middle, not today's cup.
  graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_circle(ctx,green,3);
  graphics_context_set_stroke_color(ctx,GColorDarkGreen); graphics_context_set_stroke_width(ctx,1); graphics_draw_circle(ctx,green,3);
  if (!live) return;
  int32_t angle=atan2_lookup(player.y-green.y,player.x-green.x);
  int ux=cos_lookup(angle)*10/TRIG_MAX_RATIO, uy=sin_lookup(angle)*10/TRIG_MAX_RATIO;
  // Label the line midway, or just inside an edge arrow; never over the green mark.
  char distance[8]; snprintf(distance,sizeof(distance),"%d",middle);
  GPoint mid=shown?GPoint((green.x+end.x)/2,(green.y+end.y)/2):GPoint(end.x-ux*44/10,end.y-uy*44/10);
  if (abs(mid.x-green.x)<20 && abs(mid.y-green.y)<14) {
    // Step sideways off the line, toward the middle of the map.
    int side=(MAP_W/2-mid.x)*-uy+(MAP_H/2-mid.y)*ux<0?-1:1;
    mid.x+=side*-uy*22/10; mid.y+=side*ux*22/10;
  }
  GRect box=GRect((int)camera_max(0,camera_min(MAP_W-30,mid.x-15)),(int)camera_max(0,camera_min(MAP_H-15,mid.y-7)),30,15);
  graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_rect(ctx,box,3,GCornersAll);
  graphics_context_set_stroke_color(ctx,GColorCobaltBlue); graphics_context_set_stroke_width(ctx,1); graphics_draw_round_rect(ctx,box,3);
  draw_text(ctx,distance,box.origin.x,box.origin.y-3,30,18,FONT_KEY_GOTHIC_14_BOLD,GColorCobaltBlue);
  if (shown) {
    graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_circle(ctx,player,8);
    graphics_context_set_stroke_color(ctx,GColorBlack); graphics_context_set_stroke_width(ctx,1); graphics_draw_circle(ctx,player,7);
    graphics_context_set_fill_color(ctx,GColorMagenta); graphics_fill_circle(ctx,player,5);
    graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_circle(ctx,player,2);
    return;
  }
  // Off-map player: an arrow at the edge points toward them.
  GPoint arrow[3]={end,GPoint(end.x-ux-uy/2,end.y-uy+ux/2),GPoint(end.x-ux+uy/2,end.y-uy-ux/2)};
  GPathInfo info={.num_points=3,.points=arrow}; GPath *path=gpath_create(&info);
  if (path) {
    graphics_context_set_fill_color(ctx,GColorMagenta); gpath_draw_filled(ctx,path);
    graphics_context_set_stroke_color(ctx,GColorBlack); gpath_draw_outline(ctx,path); gpath_destroy(path);
  }
  // Name the arrow so it reads as the player, not a direction cue.
  GPoint you=GPoint(end.x-ux*18/10,end.y-uy*18/10);
  GRect tag=GRect((int)camera_max(0,camera_min(MAP_W-24,you.x-12)),(int)camera_max(0,camera_min(MAP_H-14,you.y-7)),24,14);
  graphics_context_set_fill_color(ctx,GColorMagenta); graphics_fill_rect(ctx,tag,3,GCornersAll);
  draw_text(ctx,"YOU",tag.origin.x,tag.origin.y-3,24,16,FONT_KEY_GOTHIC_14_BOLD,GColorWhite);
}

static GPoint hazard_preview_point(GPoint p,float scale,float cx,float cy) {
  return GPoint((int)camera_max(-10000,camera_min(10000,(p.x-cx)*scale+112)),(int)camera_max(-10000,camera_min(10000,(p.y-cy)*scale+56)));
}
typedef struct {const MapShape *shape; const Hazard *reading; GPoint center,near_edge,far_edge; int side;} HazardCallout;
static void draw_hazard_image(Layer *layer,GContext *ctx) {
  graphics_context_set_fill_color(ctx,GColorWhite); graphics_fill_rect(ctx,layer_get_bounds(layer),0,GCornerNone);
  if (map_hole!=hole) {draw_text(ctx,"Loading map",0,38,224,28,FONT_KEY_GOTHIC_18,GColorDarkGray); return;}
  const HoleMap *map=&hole_map;
  int minx=32767,miny=32767,maxx=-32768,maxy=-32768, feature_count=0;
  for (int i=0;i<map->count;i++) {
    const MapShape *shape=&map_shapes[map->start+i];
    if (shape->kind!=1 && shape->kind!=2 && shape->kind!=3) continue;
    if (shape->kind!=1) feature_count++;
    for (int j=0;j<shape->count;j++) {
      GPoint p=map_points[shape->start+j];
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
      const MapShape *shape=&map_shapes[map->start+i];
      int order=shape->kind==4?0:shape->kind==1?2:1;
      if (shape->kind==0 || shape->kind>4 || order!=pass) continue;
      GPoint *points=malloc(shape->count*sizeof(GPoint)); if (!points) continue;
      int sumx=0,sumy=0;
      for (int j=0;j<shape->count;j++) {points[j]=hazard_preview_point(map_points[shape->start+j],scale,cx,cy); sumx+=points[j].x; sumy+=points[j].y;}
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
    draw_text(ctx,hole_text,22,36,216,22,FONT_KEY_GOTHIC_18_BOLD,GColorDarkGreen);
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
  map_canvas=layer_create(GRect((260-MAP_W)/2,60,MAP_W,MAP_H)); layer_set_update_proc(map_canvas,draw_map);
  layer_add_child(canvas,map_canvas);
  hazard_canvas=layer_create(GRect(18,98,224,112)); layer_set_update_proc(hazard_canvas,draw_hazard_image);
  layer_add_child(canvas,hazard_canvas); render();
}
static void unload(Window *w) {clear_heading(); cached_heading[0]=0;layer_destroy(hazard_canvas); hazard_canvas=NULL; layer_destroy(map_canvas); map_canvas=NULL; layer_destroy(canvas); canvas=NULL;}

int main(void) {
  hole=persist_exists(1)?persist_read_int(1):1; if (hole<1 || hole>18) hole=1;
  units=persist_exists(2)&&persist_read_int(2)?1:0;
  tee=persist_exists(3)?persist_read_int(3):1; if (tee<0 || tee>=MAX_TEES) tee=1;
  target=persist_exists(4)?persist_read_int(4):100; if (target<50 || target>200 || target%25) target=100;
  // Reopening after an accidental Back resumes the round on the saved hole.
  char info[160];
  if (persist_read_string(6,course_id,sizeof(course_id))>0 && persist_read_string(7,info,sizeof(info))>0) course_ready=parse_info(info);
  if (hole>hole_count) hole=1;
  round_started=persist_read_bool(5) && course_ready;
  if (round_started) screen=SCREEN_PLAY; else {course_id[0]='\0'; course_ready=false;}
  phone_connected=connection_service_peek_pebble_app_connection();
  window=window_create(); window_set_background_color(window,GColorWhite);
  window_set_window_handlers(window,(WindowHandlers){.load=load,.unload=unload}); window_set_click_config_provider(window,clicks);
  app_message_register_inbox_received(inbox); app_message_register_outbox_failed(failed); app_message_register_outbox_sent(sent); app_message_open(5120,256);
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
