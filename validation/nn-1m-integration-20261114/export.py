"""FP16 storage, float32 inference; replace only existing root policy payload."""
import base64, hashlib, json
from pathlib import Path
import numpy as np
R=Path(__file__).resolve().parent
report=json.loads((R.parent/'imitation-1m-20261114/model-blocks/report.json').read_text())
w=json.loads((R.parent/'imitation-1m-20261114/model-blocks/policy_weights.json').read_text())
arch=report['architecture']
assert arch == [128, 128, 64, 1], 'C inference dimensions must match model'
arrays=[]; declarations=[]; offset=0
for i in range(len(arch)-1):
    for suffix in ('weight','bias'):
        a=np.asarray(w[f'{i*2}.{suffix}'], dtype='<f2').flatten()
        assert np.isfinite(a).all()
        declarations.append(f'#define nn_{suffix}{i} (nn_params+{offset})\n')
        offset+=len(a);arrays.append(a)
packed=np.concatenate(arrays); encoded=base64.b64encode(packed.tobytes()).decode()
s=(R/'baseline.c').read_text(); lo=s.index('\n#define NF 128'); hi=s.index('static int search(',lo)
old=s[lo:hi]; features=old[old.index('static int victim'):old.index('static const float nn_w')]
code='\n#define NF 128\n#ifndef NN_ENABLE\n#define NN_ENABLE 1\n#endif\n'+features
code+='static const char nn_blob[]=\n'+'\n'.join('"'+encoded[i:i+120]+'"' for i in range(0,len(encoded),120))+';\n'
code+=f'static float nn_params[{offset}];\n'+''.join(declarations)
code+='''
static int nn_b64(unsigned char c){if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;return c=='+'?62:63;}
static void nn_init(void){
 static int ready;if(ready)return;ready=1;
 unsigned acc=0, low=0;int bits=0,bytes=0,n=0;
 for(int i=0;nn_blob[i]&&nn_blob[i]!='=';i++){
  acc=(acc<<6)|(unsigned)nn_b64(nn_blob[i]);bits+=6;
  if(bits>=8){bits-=8;unsigned b=(acc>>bits)&255;
   if(!(bytes++&1))low=b;else{
    unsigned h=low|(b<<8),e=(h>>10)&31,m=h&1023;float v;
    if(e){unsigned u=((h&32768)<<16)|((e+112)<<23)|(m<<13);memcpy(&v,&u,4);}
    else v=(h&32768?-1.f:1.f)*(float)m*(1.f/16777216.f);
    nn_params[n++]=v;
   }
  }
 }
}
static float nn_score(const float *f){
 float h0[128],h1[64];
 for(int h=0;h<128;h++){float v=nn_bias0[h];for(int j=0;j<128;j++)v+=nn_weight0[h*128+j]*f[j];h0[h]=v>0?v:0;}
 for(int h=0;h<64;h++){float v=nn_bias1[h];for(int j=0;j<128;j++)v+=nn_weight1[h*128+j]*h0[j];h1[h]=v>0?v:0;}
 float out=nn_bias2[0];for(int j=0;j<64;j++)out+=nn_weight2[j]*h1[j];return out;
}
'''
root=old[old.index('static const State *nn_root;'):]
root=root.replace('nn_count=generate(s,nn_actions);','nn_init();nn_count=generate(s,nn_actions);')
code+=root
candidate=s[:lo]+code+s[hi:]
assert len(candidate.encode())<100000, len(candidate.encode())
(R/'candidate.c').write_text(candidate)
np.save(R/'packed_weights.npy',packed.astype(np.float32))
manifest=dict(parameters=offset,source_bytes=len(candidate.encode()),storage='IEEE float16 little endian, base64',inference='float32',
    architecture=arch,source_sha256=hashlib.sha256(candidate.encode()).hexdigest(),baseline_sha256=hashlib.sha256(s.encode()).hexdigest())
(R/'export.json').write_text(json.dumps(manifest,indent=2));print(manifest)
