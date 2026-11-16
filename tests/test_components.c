#define BOT_TEST
#include "../bot.c"
#include <assert.h>
int main(void){
initialize();State s={0};s.remaining=100;
s.u[0]=(Unit){39,10,0,1};s.u[1]=(Unit){51,10,1,1};
s.u[2]=(Unit){42,6,0,0};s.u[3]=(Unit){45,10,1,0};prepare(&s);
int d[UNITS];incoming_damage(&s,d);assert(d[2]==4 && d[3]==4);
assert(safety_value(&s)<0);assert(exposed_side(&s));
int v=safety_value(&s),e=evaluate_live(&s);
s.side=1;prepare(&s);assert(evaluate_live(&s)==-e);
for(int i=0;i<UNITS;i++)if(s.u[i].hp && s.u[i].owner<2)s.u[i].owner^=1;
prepare(&s);assert(safety_value(&s)==-v);
wall[43]=1;initialize();prepare(&s);incoming_damage(&s,d);assert(!d[2] && !d[3]);
memset(wall,0,sizeof wall);initialize();memset(&s,0,sizeof s);s.remaining=12;
s.u[0]=(Unit){40,10,0,1};s.u[1]=(Unit){42,10,1,1};s.u[2]=(Unit){41,10,0,0};prepare(&s);
assert(conversion_contest(&s));s.u[2].owner=2;prepare(&s);assert(!conversion_contest(&s));
s.u[2].owner=1;s.u[0].hp=0;prepare(&s);assert(!conversion_contest(&s));
puts("PASS: safety damage, ownership symmetry, cache sign, walls, conversion contest");
}
