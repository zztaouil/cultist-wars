"""E3 candidate: Texel base (weights json) + int8 sparse value-correction net; root policy NN removed.
usage: mk_nn.py model.pt texel.json out.cpp"""
import torch,numpy as np,json,sys,base64,subprocess,re
from pathlib import Path
M=torch.load(sys.argv[1]);sd=M['sd'];H1,H2,CMAX=M['H1'],M['H2'],M['CMAX']
E=sd['E.weight'][:455].numpy();F=sd['F.weight'][:455].numpy();SC=sd['sc.weight'].numpy().T  # (2,H1)
W2=sd['l2.weight'].numpy();B2=sd['l2.bias'].numpy();W3=sd['l3.weight'].numpy()[0];B3=float(sd['l3.bias'].numpy()[0])
def q(a):  # per-column (hidden unit) int8
  s=np.abs(a).max(0)/127+1e-12;return np.round(a/s).astype(np.int8),s.astype(np.float32)
Eq,Es=q(E);Fq,Fs=q(F);W2q,W2s=q(W2.T)  # W2.T shape (H1,H2): column=out unit
blob=Eq.tobytes()+Fq.tobytes()+W2q.tobytes()
b64=base64.b64encode(blob).decode()
fl=lambda a:'{'+','.join('%.7gf'%x for x in np.ravel(a))+'}'
code=f'''
#define VN_H1 {H1}
#define VN_H2 {H2}
static const char vn_blob[]="{b64}";
static const float vn_es[VN_H1]={fl(Es)},vn_fs[VN_H1]={fl(Fs)},vn_w2s[VN_H2]={fl(W2s)},vn_sc[2][VN_H1]={{{fl(SC[0])},{fl(SC[1])}}},vn_b2[VN_H2]={fl(B2)},vn_w3[VN_H2]={fl(W3)};
static const float vn_b3={B3:.7g}f;
static float vn_E[455][VN_H1],vn_F[455][VN_H1],vn_W2[VN_H1][VN_H2];
static int vn_b64(char c){{return c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:63;}}
static void vn_init(void){{static int done;if(done)return;done=1;static int8_t raw[455*VN_H1*2+VN_H1*VN_H2];unsigned acc=0;int bits=0,n=0;
for(int i=0;vn_blob[i]&&vn_blob[i]!='=';i++){{acc=(acc<<6)|(unsigned)vn_b64(vn_blob[i]);bits+=6;if(bits>=8){{bits-=8;raw[n++]=(int8_t)((acc>>bits)&255);}}}}
for(int r=0;r<455;r++)for(int h=0;h<VN_H1;h++){{vn_E[r][h]=raw[r*VN_H1+h]*vn_es[h];vn_F[r][h]=raw[455*VN_H1+r*VN_H1+h]*vn_fs[h];}}
for(int h=0;h<VN_H1;h++)for(int o=0;o<VN_H2;o++)vn_W2[h][o]=raw[2*455*VN_H1+h*VN_H2+o]*vn_w2s[o];}}
static int vn_correction(const State *s){{float h[VN_H1];float rm=s->remaining/150.f;
for(int k=0;k<VN_H1;k++)h[k]=vn_sc[0][k]*s->side+vn_sc[1][k]*rm;
for(int i=0;i<UNITS;i++){{const Unit*u=&s->u[i];if(!u->hp)continue;int c=u->owner==2?4:u->owner*2+(1-u->leader);int r=c*91+u->pos;float hv=u->hp*0.1f;
const float*e=vn_E[r],*f=vn_F[r];for(int k=0;k<VN_H1;k++)h[k]+=e[k]+hv*f[k];}}
for(int k=0;k<VN_H1;k++)h[k]=h[k]<0?0:h[k]>1?1:h[k];
float o2[VN_H2];for(int o=0;o<VN_H2;o++)o2[o]=vn_b2[o];
for(int k=0;k<VN_H1;k++){{float x=h[k];if(x==0)continue;for(int o=0;o<VN_H2;o++)o2[o]+=x*vn_W2[k][o];}}
float out=vn_b3;for(int o=0;o<VN_H2;o++){{float x=o2[o]<0?0:o2[o]>1?1:o2[o];out+=x*vn_w3[o];}}
return (int)({CMAX}f*tanhf(out));}}
'''
# base texel candidate
ROOT=Path(__file__).resolve().parents[2]
subprocess.run(['python3',str(Path(__file__).parent/'mk_texel.py'),sys.argv[2],'1','/tmp/_t.cpp'],check=True,stdout=subprocess.DEVNULL)
src=open('/tmp/_t.cpp').read()
# remove root policy blob, disable root NN
a=src.index('static const char nn_blob[]=');b=src.index(';',src.index('"',a+30)+0)
# find end of string literal concatenation: first '";' after a
b=src.index('";',a)+2
src=src[:a]+'static const char nn_blob[]="";'+src[b:]
src=src.replace('#ifndef NN_ENABLE\n#define NN_ENABLE 1\n#endif','#ifndef NN_ENABLE\n#define NN_ENABLE 0\n#endif')
src=src.replace('#include <time.h>','#include <time.h>\n#include <math.h>',1)
src=src.replace('static int evaluate_live(const State *s) {','#ifndef VN_ENABLE\n#define VN_ENABLE 1\n#endif\n'+code+'static int evaluate_live(const State *s) {',1)
src=src.replace('uint64_t key = s->key ^ s->metadata;\nEvalEntry *entry = &eval_tt','uint64_t key = VN_ENABLE ? s->key : (s->key ^ s->metadata);\nEvalEntry *entry = &eval_tt',1)
src=src.replace('int value=(int)(acc/1000);','int value=(int)(acc/1000);\nif(VN_ENABLE){vn_init();value+=vn_correction(s);}',1)
assert 'vn_correction(s)' in src and 's->key : (s->key' in src
Path(sys.argv[3]).write_text(src);print('chars',len(src),'blob',len(b64))
