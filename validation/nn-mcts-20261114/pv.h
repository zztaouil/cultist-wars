/* Shared state encoder and small legal-action head. Feature version 1. */
#include <math.h>
#define PV_S 208
#define PV_A 32
#define PV_H 64
#define PV_Z 32
#define PV_N 16578
#ifdef PV_WEIGHTS
#include "weights.h"
#else
static float pv_w[PV_N];
static void pv_init(void) {}
#endif
static void pv_state(const State *s, float *x) {
    memset(x, 0, PV_S*sizeof(float));
    for (int i=0;i<CELLS;i++) x[i]=(float)wall[i];
    for (int i=0;i<UNITS;i++) {
        Unit u=s->u[i]; int k=91+8*i;
        x[k+7]=(s->neutral_mask>>i)&1;
        if (!u.hp) continue;
        x[k]=(u.pos%W)/12.f; x[k+1]=(u.pos/W)/6.f;
        x[k+2]=u.hp/10.f; x[k+3]=u.owner==s->side;
        x[k+4]=u.owner==1-s->side; x[k+5]=u.owner==2; x[k+6]=u.leader;
        if(u.owner==s->side)x[206]+=1.f/14;
        if(u.owner==1-s->side)x[207]+=1.f/14;
    }
    x[203]=s->remaining/150.f; x[204]=s->side; x[205]=s->rng_known;
}
static void pv_action(const State *s, Action a, float *x) {
    float old[NF]; features(s,a,old);
    memcpy(x,old+98,30*sizeof(float));
    x[30]=a.kind ? a.unit/13.f : 0;
    x[31]=a.kind==MOVE ? a.target/90.f : a.kind ? a.target/13.f : 0;
}
/* Flatten order: trunk0 W,b; trunk1 W,b; action W,b; policy W,b; value W,b. */
static float pv_encode(const State *s, float *z) {
    float x[PV_S],h[PV_H]; pv_init(); pv_state(s,x);
    int off=0;
    for(int i=0;i<PV_H;i++) {
        float v=pv_w[PV_S*PV_H+i];
        for(int j=0;j<PV_S;j++)v+=pv_w[i*PV_S+j]*x[j];
        h[i]=fmaxf(0,v);
    }
    off=PV_S*PV_H+PV_H;
    for(int i=0;i<PV_Z;i++) {
        float v=pv_w[off+PV_H*PV_Z+i];
        for(int j=0;j<PV_H;j++)v+=pv_w[off+i*PV_H+j]*h[j];
        z[i]=fmaxf(0,v);
    }
    off+=PV_H*PV_Z+PV_Z+PV_A*PV_Z+PV_Z+PV_Z+1;
    float v=pv_w[off+PV_Z];
    for(int i=0;i<PV_Z;i++)v+=pv_w[off+i]*z[i];
    return tanhf(v);
}
static float pv_logit(const State *s, Action a, const float *z) {
    float x[PV_A]; pv_action(s,a,x);
    int off=PV_S*PV_H+PV_H+PV_H*PV_Z+PV_Z;
    int poff=off+PV_A*PV_Z+PV_Z;
    float out=pv_w[poff+PV_Z];
    for(int i=0;i<PV_Z;i++) {
        float v=z[i]+pv_w[off+PV_A*PV_Z+i];
        for(int j=0;j<PV_A;j++)v+=pv_w[off+i*PV_A+j]*x[j];
        out+=pv_w[poff+i]*fmaxf(0,v);
    }
    return out;
}
