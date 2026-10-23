#pragma once
#include <stdint.h>
typedef struct {int x,y;} LeaderPoint;
typedef struct {int reach_y,carry_y;} LeaderPlan;
static inline int64_t leader_orientation(LeaderPoint a,LeaderPoint b,LeaderPoint c) {
  return (int64_t)(b.x-a.x)*(c.y-a.y)-(int64_t)(b.y-a.y)*(c.x-a.x);
}
static inline int leader_opposite(int64_t a,int64_t b) {return (a<0 && b>0)||(a>0 && b<0);}
static inline int leaders_cross(LeaderPoint a,LeaderPoint b,LeaderPoint c,LeaderPoint d) {
  return leader_opposite(leader_orientation(a,b,c),leader_orientation(a,b,d)) && leader_opposite(leader_orientation(c,d,a),leader_orientation(c,d,b));
}
// Swap the labels rather than the measured edge anchors when necessary.
static inline LeaderPlan hazard_leader_plan(LeaderPoint reach,LeaderPoint carry,int label_x,int top_y) {
  LeaderPlan p={reach.y<=carry.y?top_y:top_y+14,reach.y<=carry.y?top_y+14:top_y};
  if (leaders_cross((LeaderPoint){label_x,p.reach_y},reach,(LeaderPoint){label_x,p.carry_y},carry)) {
    int swap=p.reach_y;p.reach_y=p.carry_y;p.carry_y=swap;
  }
  return p;
}

typedef struct {int front_y,middle_y,back_y;} GreenLeaderPlan;
static inline GreenLeaderPlan green_leader_plan(LeaderPoint front,LeaderPoint middle,LeaderPoint back,int x,int top) {
  static const int permutations[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
  LeaderPoint anchors[3]={front,middle,back};
  GreenLeaderPlan best={top,top+14,top+28};int64_t best_score=INT64_MAX;
  for (int p=0;p<6;p++) {
    LeaderPoint labels[3];int64_t score=0;int crosses=0;
    for (int i=0;i<3;i++) {labels[i]=(LeaderPoint){x,top+permutations[p][i]*14};int d=labels[i].y-anchors[i].y;score+=(int64_t)d*d;}
    for (int i=0;i<3;i++) for (int j=i+1;j<3;j++) if (leaders_cross(labels[i],anchors[i],labels[j],anchors[j])) crosses=1;
    if (!crosses && score<best_score) {best_score=score;best=(GreenLeaderPlan){labels[0].y,labels[1].y,labels[2].y};}
  }
  return best;
}
