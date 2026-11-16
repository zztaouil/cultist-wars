"""Per-output-channel int8 weights, FP16 scales/biases, float32 inference."""
import base64,hashlib,json
from pathlib import Path
import numpy as np
R=Path(__file__).resolve().parent
report=json.loads((R/'fine-wide/report.json').read_text());arch=report['architecture'];assert arch==[128,192,96,1]
w=np.load(R/'fine-wide/policy_weights.npz');payload=bytearray();packed=[];decl=[];off=0;initializers=[]
for i,(a,b) in enumerate(zip(arch,arch[1:])):
 weights=w[f'{2*i}.weight'];bias=w[f'{2*i}.bias'].astype('<f2')
 scales=np.maximum(np.max(abs(weights),axis=1)/127,2**-14).astype('<f2')
 assert np.isfinite(scales).all() and np.isfinite(bias).all()
 q=np.clip(np.rint(weights/scales.astype(np.float32)[:,None]),-127,127).astype('i1')
 start=len(payload);payload.extend(q.tobytes());payload.extend(scales.tobytes());payload.extend(bias.tobytes())
 decl.extend([f'#define nn_weight{i} (nn_params+{off})',f'#define nn_bias{i} (nn_params+{off+a*b})'])
 initializers.append(f'for(int h=0;h<{b};h++){{float z=nn_half(raw+{start+a*b}+2*h);nn_bias{i}[h]=nn_half(raw+{start+a*b+2*b}+2*h);for(int j=0;j<{a};j++)nn_weight{i}[h*{a}+j]=(float)(int8_t)raw[{start}+h*{a}+j]*z;}}')
 packed.extend([q.astype(np.float32)*scales.astype(np.float32)[:,None],bias.astype(np.float32)]);off+=a*b+b
s=(R/'candidate.c').read_text();lo=s.index('static const char nn_blob[]=');hi=s.index('static const State *nn_root;',lo)
e=base64.b64encode(payload).decode();code='static const char nn_blob[]=\n'+'\n'.join('"'+e[i:i+120]+'"' for i in range(0,len(e),120))+';\n'
code+=f'static float nn_params[{off}];\n'+'\n'.join(decl)+'\n'
code+='''static int nn_b64(unsigned char c){if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;return c=='+'?62:63;}
static float nn_half(const unsigned char*p){unsigned h=p[0]|((unsigned)p[1]<<8),e=(h>>10)&31,m=h&1023;if(!e)return(h&32768?-1.f:1.f)*(float)m*(1.f/16777216.f);unsigned u=((h&32768)<<16)|((e+112)<<23)|(m<<13);float v;memcpy(&v,&u,4);return v;}
static void nn_init(void){static int ready;if(ready)return;ready=1;
'''
code+=f'static unsigned char raw[{len(payload)}];unsigned acc=0;int bits=0,n=0;\n'
code+="for(int i=0;nn_blob[i]&&nn_blob[i]!='=';i++){acc=(acc<<6)|(unsigned)nn_b64(nn_blob[i]);bits+=6;if(bits>=8){bits-=8;raw[n++]=(acc>>bits)&255;}}\n"+'\n'.join(initializers)+'\n}\n'
code+='''static float nn_score(const float*f){float h0[192],h1[96];
for(int h=0;h<192;h++){float v=nn_bias0[h];for(int j=0;j<128;j++)v+=nn_weight0[h*128+j]*f[j];h0[h]=v>0?v:0;}
for(int h=0;h<96;h++){float v=nn_bias1[h];for(int j=0;j<192;j++)v+=nn_weight1[h*192+j]*h0[j];h1[h]=v>0?v:0;}
float v=nn_bias2[0];for(int j=0;j<96;j++)v+=nn_weight2[j]*h1[j];return v;}
'''
s=s[:lo]+code+s[hi:];assert len(s.encode())<100000,len(s.encode())
(R/'wide.c').write_text(s);np.save(R/'wide_packed_weights.npy',np.concatenate([a.ravel() for a in packed]))
manifest=dict(architecture=arch,parameters=off,source_bytes=len(s.encode()),source_sha256=hashlib.sha256(s.encode()).hexdigest(),storage='per-channel int8 weights; FP16 scales and biases',inference='float32')
(R/'wide-export.json').write_text(json.dumps(manifest,indent=2));print(manifest)
