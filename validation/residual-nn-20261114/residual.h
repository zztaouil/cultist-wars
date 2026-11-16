/* Board-only features; signed owner differences preserve cache semantics. */
#define RF 16
#define RH 8
#ifndef RESIDUAL_ENABLE
#define RESIDUAL_ENABLE 1
#endif
static unsigned long residual_calls;
static void residual_features(const State *s, float *f) {
    memset(f,0,RF*sizeof(float));
    for(int p=0;p<2;p++) {
        float sign=p?-1.f:1.f;
        unsigned own=s->owned[p];
        while(own) {
            int i=__builtin_ctz(own); own&=own-1;
            const Unit *u=&s->u[i]; int k=u->leader?0:1;
            f[k]+=sign/7.f; f[2+k]+=sign*u->hp/70.f;
            f[4+k]+=sign*(6-abs(u->pos%W-6))/42.f;
            f[6+k]+=sign*(3-abs(u->pos/W-3))/21.f;
            int mobility=0,enemy=CELLS,neutral=CELLS;
            for(int d=0;d<4;d++){int q=adjacent[u->pos][d];if(q>=0&&!wall[q]&&!bit_has(s->occupied,q))mobility++;}
            f[8+k]+=sign*mobility/28.f;
            unsigned targets=s->owned[1-p];
            while(targets){int j=__builtin_ctz(targets);targets&=targets-1;int d=distance_table[u->pos][s->u[j].pos];if(d<enemy)enemy=d;}
            targets=s->owned[2];
            while(targets){int j=__builtin_ctz(targets);targets&=targets-1;int d=distance_table[u->pos][s->u[j].pos];if(d<neutral)neutral=d;}
            if(enemy<CELLS)f[10+k]+=sign/(enemy+1.f);
            if(neutral<CELLS)f[12+k]+=sign/(neutral+1.f);
            f[14+k]+=sign*(u->hp<=4)/7.f;
        }
    }
}
#include "weights.h"
static int residual_correction(const State *s) {
    float f[RF],v=0;residual_features(s,f);residual_calls++;
    /* Odd network: ownership reversal negates correction exactly. */
    for(int h=0;h<RH;h++) {
        float z=0;for(int j=0;j<RF;j++)z+=rw[h][j]*f[j];
        float a=rb[h]+z,b=rb[h]-z;
        v+=ro[h]*((a>0?a:0)-(b>0?b:0))*0.5f;
    }
    if(v>150)v=150;if(v< -150)v=-150;
    int result=(int)v;return s->side?-result:result;
}
static int residual_leaf(const State *s) {
    int base=evaluate_live(s);
    if(!RESIDUAL_ENABLE || material_threat(s))return base;
    return base+residual_correction(s);
}
