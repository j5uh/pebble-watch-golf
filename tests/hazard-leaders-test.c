#include <assert.h>
#include <stdio.h>
#include "../src/c/hazard-leaders.h"
int main(void) {
  int count=0;
  for (int side=0;side<2;side++) for (int x=64;x<=160;x+=16) for (int y=8;y<=104;y+=16)
  for (int fx=64;fx<=160;fx+=16) for (int fy=8;fy<=104;fy+=16) for (int top=8;top<=86;top+=26) {
    int label_x=side?174:50;
    LeaderPoint r={x,y},c={fx,fy};LeaderPlan p=hazard_leader_plan(r,c,label_x,top);
    assert(!leaders_cross((LeaderPoint){label_x,p.reach_y},r,(LeaderPoint){label_x,p.carry_y},c));
    count++;
  }
  // The screenshot case: reach is below carry; carry's label must be above reach.
  LeaderPlan p=hazard_leader_plan((LeaderPoint){134,88},(LeaderPoint){146,63},168,40);
  assert(p.carry_y<p.reach_y);
  for (int side=0;side<2;side++) for (int x=64;x<=160;x+=16) for (int y=8;y<=104;y+=16)
  for (int bx=64;bx<=160;bx+=16) for (int by=8;by<=104;by+=16) {
    LeaderPoint f={x,y},b={bx,by},m={(x+bx)/2,(y+by)/2};int lx=side?174:50;
    GreenLeaderPlan g=green_leader_plan(f,m,b,lx,7);
    assert(!leaders_cross((LeaderPoint){lx,g.front_y},f,(LeaderPoint){lx,g.middle_y},m));
    assert(!leaders_cross((LeaderPoint){lx,g.front_y},f,(LeaderPoint){lx,g.back_y},b));
    assert(!leaders_cross((LeaderPoint){lx,g.middle_y},m,(LeaderPoint){lx,g.back_y},b));
  }
  printf("%d non-crossing reach/carry layouts passed.\n",count);
}
