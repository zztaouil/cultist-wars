from pathlib import Path
import zipfile,json,hashlib
R=Path(__file__).resolve().parent
with zipfile.ZipFile(R/'cultist-nn-results.zip') as z:
 weights=json.loads(z.read('policy_weights.json'));report=json.loads(z.read('report.json'))
assert report['best_epoch']==40 and report['baseline_sha256']==hashlib.sha256((R/'baseline.c').read_bytes()).hexdigest()
(R/'kaggle-report.json').write_text(json.dumps(report,indent=2))
feature=(R/'features.c').read_text();feature=feature[feature.index('static int victim'):feature.index('int extract')]
def array(name,v):
 def fmt(x):
  s=format(x,'.9g');return s+('f' if '.' in s or 'e' in s else '.0f')
 if isinstance(v[0],list):v=[x for row in v for x in row]
 return 'static const float '+name+'[]={'+','.join(map(fmt,v))+'};\n'
code='\n#define NF 128\n#ifndef NN_ENABLE\n#define NN_ENABLE 1\n#endif\n'+feature
for name,key in [('nn_w','0.weight'),('nn_b','0.bias'),('nn_o','2.weight'),('nn_ob','2.bias')]:code+=array(name,weights[key])
code+='''
static float nn_score(const float *f){
 float out=nn_ob[0];for(int h=0;h<32;h++){float v=nn_b[h];for(int j=0;j<128;j++)v+=nn_w[h*128+j]*f[j];if(v>0)out+=nn_o[h]*v;}return out;
}
static const State *nn_root;
static Action nn_actions[ACTIONS];static int nn_ranks[ACTIONS],nn_count;
static void nn_begin(const State *s){
 nn_root=0;nn_count=0;if(!NN_ENABLE)return;
 nn_count=generate(s,nn_actions);float scores[ACTIONS];
 for(int i=0;i<nn_count;i++){float f[NF];features(s,nn_actions[i],f);scores[i]=nn_score(f);}
 for(int i=0;i<nn_count;i++){int rank=0;for(int j=0;j<nn_count;j++)if(scores[i]>scores[j]||(scores[i]==scores[j]&&i<j))rank++;nn_ranks[i]=rank;}
 nn_root=s;
}
static int nn_order(Action a){for(int i=0;i<nn_count;i++)if(same(a,nn_actions[i]))return nn_ranks[i];return -1;}
'''
s=(R/'baseline.c').read_text();pos=s.index('static int search(');s=s[:pos]+code+s[pos:]
s=s.replace('if (same(a, hint)) actions[i].order += 100000000;','if (s == nn_root) actions[i].order = nn_order(a);\n        if (same(a, hint)) actions[i].order += 100000000;')
s=s.replace('Action best = {WAIT, 0, 0, 0};\n    *completed_depth', 'nn_begin(s);\n    Action best = {WAIT, 0, 0, 0};\n    *completed_depth')
s=s.replace('    return best;\n}\n/* Infer hidden RNG', '    nn_root = 0;\n    return best;\n}\n/* Infer hidden RNG')
assert len(s.encode())<100000
(R/'candidate.c').write_text(s);print('Standalone source bytes:',len(s.encode()))
