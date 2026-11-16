"""Build standalone candidate from immutable baseline and selected trained weights."""
import argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--trained',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
s=(ROOT/'baseline.cpp').read_text().replace('#include <time.h>','#include <time.h>\n#include <math.h>\n#ifndef NEURAL_VALUE\n#define NEURAL_VALUE 1\n#endif')
start=s.index('static int evaluate_live(')
s=s[:start]+(a.trained/'weights.inc').read_text()+(ROOT/'value.h').read_text()+s[start:]
s=s.replace('static int evaluate_live(const State *s) {','static int evaluate_live(const State *s) {\nif(NEURAL_VALUE)return neural_value(s);',1)
start=s.index('#define NF 128');end=s.index('static const State *nn_root;',start)
s=s[:start]+'#ifndef NN_ENABLE\n#define NN_ENABLE 1\n#endif\n'+s[end:]
start=s.index('nn_init();nn_count=generate(s,nn_actions);');end=s.index('for(int i=0;i<nn_count;i++){int rank=',start)
s=s[:start]+'''nn_count=generate(s,nn_actions);float scores[ACTIONS];
for(int i=0;i<nn_count;i++) {
    scores[i]=-INF;
    if(now_ns()>=deadline)continue;
    State child=apply(s,nn_actions[i]);scores[i]=(float)-evaluate(&child);
}
'''+s[end:]
s=s.replace('int selective = use_futility &&','int selective = !NEURAL_VALUE && use_futility &&')
s=s.replace('*completed_depth = 0; *advantage = evaluate(s);', '*completed_depth = 0; *advantage = evaluate(s);\nif(now_ns()>=deadline){nn_root=0;return best;}')
a.output.write_text(s)
