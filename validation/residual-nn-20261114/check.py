from experiment import *
def main():
    candidate();off=library('check-off');on=library('check-on',1)
    src=ROOT/'build/frozen.c';src.write_text(h.CODE.replace('SOURCE',str(ROOT/'baseline.c')))
    so=src.with_suffix('.so');subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(src),'-o',str(so)],check=True);baseline=C.CDLL(str(so))
    rows=json.loads((ROOT/'records.json').read_text())[::70]
    for r in rows:
        for lib in (off,baseline):h.setup(lib,r['walls'])
        a=h.run(off,r['state'],2);b=h.run(baseline,r['state'],2)
        assert all(a[k]==b[k] for k in ('value','action','nodes','depth')),(a,b)
        h.setup(on,r['walls']);s=r['state'];v=on.correction(h.Array(*s));assert abs(v)<=150
        reverse=list(s)
        for i in range(14):
            if reverse[4*i+2]<2:reverse[4*i+2]=1-reverse[4*i+2]
        assert on.correction(h.Array(*reverse))==-v
        reverse=list(s);reverse[56]=1-s[56];assert on.correction(h.Array(*reverse))==-v
    results={}
    for test in ('test_bot','test_search','test_recruitment','test_components'):
        text=(REPO/'tests'/f'{test}.c').read_text().replace('#include "../bot.c"',f'#include "{ROOT}/candidate.c"')
        src=ROOT/'build'/f'{test}.c';src.write_text(text);exe=src.with_suffix('')
        subprocess.run(['cc','-O1','-g','-std=c11','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True,capture_output=True)
        p=subprocess.run([str(exe)],capture_output=True,text=True);results[test]=dict(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr);assert p.returncode==0,results[test]
    source='''#define BOT_TEST
#include "../candidate.c"
#include <assert.h>
int main(void){initialize();State s={0};s.remaining=10;s.u[0]=(Unit){39,10,0,1};s.u[1]=(Unit){51,10,1,1};prepare(&s);assert(!material_threat(&s));assert(abs(residual_leaf(&s)-evaluate_live(&s))<=150);s.u[2]=(Unit){40,1,0,0};s.u[3]=(Unit){41,10,1,0};prepare(&s);assert(material_threat(&s));unsigned long before=residual_calls;assert(residual_leaf(&s)==evaluate_live(&s));assert(residual_calls==before);s.u[1].hp=s.u[3].hp=0;prepare(&s);deadline=now_ns()+1000000000ULL;assert(quiescence(&s,-INF,INF,4)==WIN);assert(residual_calls==before);return 0;}
'''
    src=ROOT/'build/test_residual.c';src.write_text(source);exe=src.with_suffix('');subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True,capture_output=True);subprocess.run([str(exe)],check=True)
    save('checks.json',dict(disabled_exact_parity_positions=len(rows),checks=results,antisymmetry=True,bounded=True,tactical_bypass=True,terminal_bypass=True))
    print('PASS: frozen-search parity, symmetry, bounds, tactical/terminal bypass, four sanitizer suites')
if __name__=='__main__':main()
