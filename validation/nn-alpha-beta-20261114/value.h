// Full-state value network, feature contract v2: 239 -> 32 -> 16 -> 1.
// Output is side-to-move-relative, bounded strictly below terminal scores.
static constexpr int VALUE_FEATURES=239, VALUE_PARAMS=8225;
static float value_params[VALUE_PARAMS];
static int value_b64(unsigned char c) {
    if(c>='A'&&c<='Z')return c-'A';
    if(c>='a'&&c<='z')return c-'a'+26;
    if(c>='0'&&c<='9')return c-'0'+52;
    return c=='+'?62:63;
}
static void value_init() {
    static bool ready=false;if(ready)return;
    unsigned acc=0;int bits=0,n=0;unsigned char raw[VALUE_PARAMS*2];
    for(int i=0;value_blob[i]&&value_blob[i]!='=';++i) {
        acc=(acc<<6)|(unsigned)value_b64(value_blob[i]);bits+=6;
        if(bits>=8){bits-=8;raw[n++]=(acc>>bits)&255;}
    }
    for(int i=0;i<VALUE_PARAMS;++i) {
        unsigned h=raw[2*i]|((unsigned)raw[2*i+1]<<8),e=(h>>10)&31,m=h&1023;
        if(!e)value_params[i]=(h&32768?-1.f:1.f)*m*(1.f/16777216.f);
        else {unsigned u=((h&32768)<<16)|((e+112)<<23)|(m<<13);memcpy(value_params+i,&u,4);}
    }
    ready=true;
}
static void value_features(const State *s,float *x) {
    memset(x,0,VALUE_FEATURES*sizeof(float));
    for(int p=0;p<CELLS;++p)x[p]=(float)wall[p];
    for(int i=0;i<UNITS;++i) {
        Unit u=s->u[i];int k=91+8*i;
        x[k+7]=(s->neutral_mask>>i)&1;
        if(!u.hp)continue;
        x[k]=(u.pos%W)/12.f;x[k+1]=(u.pos/W)/6.f;x[k+2]=u.hp/10.f;
        x[k+3]=u.owner==s->side;x[k+4]=u.owner==1-s->side;
        x[k+5]=u.owner==2;x[k+6]=u.leader;
        if(u.owner==s->side)x[206]+=1.f/14;
        if(u.owner==1-s->side)x[207]+=1.f/14;
    }
    x[203]=s->remaining/150.f;x[204]=s->side;x[205]=s->rng_known;
    for(int b=0;b<31;++b)x[208+b]=s->rng_known?((s->rng>>b)&1):0;
}
static float value_forward(const float *x) {
    value_init();float h[32],z[16];
    for(int i=0;i<32;++i) {
        float v=value_params[7648+i];
        for(int j=0;j<239;++j)v+=value_params[239*i+j]*x[j];
        h[i]=v>0?v:0;
    }
    for(int i=0;i<16;++i) {
        float v=value_params[8192+i];
        for(int j=0;j<32;++j)v+=value_params[7680+32*i+j]*h[j];
        z[i]=v>0?v:0;
    }
    float v=value_params[8224];for(int i=0;i<16;++i)v+=value_params[8208+i]*z[i];
    return tanhf(v);
}
static int neural_value(const State *s) {
    // Side, remaining actions, RNG and neutral list all affect the learned value.
    uint64_t key=s->key;EvalEntry *entry=&eval_tt[key&(TT_SIZE-1)];
    if(entry->valid && entry->key==key)return entry->value;
    float x[VALUE_FEATURES];value_features(s,x);
    int value=(int)lroundf(10000.f*value_forward(x));
    *entry=EvalEntry{key,value,1};return value;
}
