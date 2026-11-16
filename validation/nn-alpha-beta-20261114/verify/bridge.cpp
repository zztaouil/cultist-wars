#define BOT_TEST
#include "/Users/hexagone/Documents/ChatGPT/cultist-wars/bot.cpp"
extern "C" void infer(const float*x,float*y,int n){for(int i=0;i<n;++i)y[i]=value_forward(x+239*i);}
extern "C" void encode(const int*w,const int*p,float*x){memcpy(wall,w,sizeof wall);State s{};for(int i=0;i<14;++i)s.u[i]=Unit{(uint8_t)p[4*i],(uint8_t)p[4*i+1],(uint8_t)p[4*i+2],(uint8_t)p[4*i+3]};s.side=p[56];s.remaining=p[57];s.rng=p[58];s.neutral_mask=p[59];s.rng_known=p[60];value_features(&s,x);}
