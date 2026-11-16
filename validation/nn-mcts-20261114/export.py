"""FP16 storage, float inference; assemble standalone C submission candidate."""
import argparse,base64,hashlib,json
from pathlib import Path
import numpy as np
import torch
from train import Net
ROOT=Path(__file__).resolve().parent

def main():
    p=argparse.ArgumentParser();p.add_argument('--checkpoint',type=Path,required=True);args=p.parse_args()
    ck=torch.load(args.checkpoint,map_location='cpu',weights_only=True);assert ck['feature_version']==1
    model=Net();model.load_state_dict(ck['state_dict'])
    packed=np.concatenate([v.detach().numpy().ravel() for v in model.state_dict().values()]).astype('<f2');assert len(packed)==16578 and np.isfinite(packed).all()
    b64=base64.b64encode(packed.tobytes()).decode()
    header='static float pv_w[PV_N];\nstatic const char pv_blob[]=\n'+''.join('"'+b64[i:i+1000]+'"\n' for i in range(0,len(b64),1000))+';\n'
    header+='''static void pv_init(void){static int ready;if(ready)return;ready=1;
unsigned acc=0;int bits=0,n=0;unsigned char raw[PV_N*2];
for(int i=0;pv_blob[i]&&pv_blob[i]!='=';i++){unsigned char c=pv_blob[i];unsigned v=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:63;acc=(acc<<6)|v;bits+=6;if(bits>=8){bits-=8;raw[n++]=(acc>>bits)&255;}}
for(int i=0;i<PV_N;i++){unsigned h=raw[2*i]|((unsigned)raw[2*i+1]<<8),e=(h>>10)&31,m=h&1023;
if(!e)pv_w[i]=(h&32768?-1.f:1.f)*(float)m*(1.f/16777216.f);
else {unsigned u=((h&32768)<<16)|((e+112)<<23)|(m<<13);memcpy(pv_w+i,&u,4);}}
}
'''
    (ROOT/'weights.h').write_text(header)
    baseline=(ROOT/'baseline.c').read_text()
    start=baseline.index('static const char nn_blob[]=');end=baseline.index('static int search(',start)
    pv=(ROOT/'pv.h').read_text().replace('#include "weights.h"',header)
    compat='''
static const State *nn_root;
static Action nn_actions[ACTIONS];static float nn_logits[ACTIONS];static int nn_count;
static void nn_begin(const State *s){nn_root=0;nn_count=0;if(!NN_ENABLE)return;
float z[PV_Z];pv_encode(s,z);nn_count=generate(s,nn_actions);
for(int i=0;i<nn_count;i++){if(now_ns()>=deadline){nn_count=0;return;}nn_logits[i]=pv_logit(s,nn_actions[i],z);}nn_root=s;}
static int nn_order(Action a){for(int i=0;i<nn_count;i++)if(same(a,nn_actions[i]))return (int)(1000*nn_logits[i]);return 0;}
#ifndef AB_NEURAL_VALUE
#define AB_NEURAL_VALUE 0
#endif
static int pv_ab_value(const State *s){float z[PV_Z];return (int)(10000*pv_encode(s,z));}
'''
    # Quiescence precedes features/network; forward declaration required.
    text=baseline[:start]+ '#define PV_WEIGHTS\n'+pv+compat+baseline[end:]
    text=text.replace('static int quiescence(', 'static int pv_ab_value(const State *s);\n#ifndef AB_NEURAL_VALUE\n#define AB_NEURAL_VALUE 0\n#endif\nstatic int quiescence(',1)
    text=text.replace('unstable ? -INF : evaluate_live(s)','unstable ? -INF : (AB_NEURAL_VALUE ? pv_ab_value(s) : evaluate_live(s))')
    text=text.replace('int selective = use_futility','int selective = !AB_NEURAL_VALUE && use_futility')
    text=text.replace('static Action choose(', 'static Action ab_choose(',1)
    text=text.replace('#define RNG_CANDIDATES 64',(ROOT/'mcts.h').read_text()+'\n#define RNG_CANDIDATES 64')
    (ROOT/'candidate.c').write_text(text)
    np.save(ROOT/'packed.npy',packed.astype(np.float32))
    report=dict(source_bytes=len(text.encode()),parameters=len(packed),storage='FP16',checkpoint=str(args.checkpoint),checkpoint_sha256=hashlib.sha256(args.checkpoint.read_bytes()).hexdigest(),source_sha256=hashlib.sha256(text.encode()).hexdigest())
    (ROOT/'export.json').write_text(json.dumps(report,indent=2));print(json.dumps(report));assert len(text.encode())<100000
if __name__=='__main__':main()
